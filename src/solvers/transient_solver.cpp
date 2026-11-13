// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/transient_solver.cpp
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/fourier_analyzer.hpp"

// Step 2.4: Reduced device includes - only include what's directly used
// Temperature-dependent device headers moved to temperature_utils.cpp
#include "kirimaspice/devices/device.hpp"                     // Base class
#include "kirimaspice/devices/time_varying_voltage_source.hpp" // set_time()
#include "kirimaspice/solvers/temperature_utils.hpp"
#include "kirimaspice/solvers/parallel_stamper.hpp"
#include "kirimaspice/core/constants.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <cmath>
#include <chrono>
#include <limits>

#ifdef TRAPEZOIDAL
#undef TRAPEZOIDAL
#endif

// OpenMP for parallel device stamping
#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice::solvers {

TransientSolver::TransientSolver(const circuit::CircuitGraph& circuit,
                                 const SimulationConfig& config)
    : circuit_(circuit)
    , config_(config)
    , dc_solver_(std::make_unique<DCSolver>(circuit, config)) {

    // Calculate MNA system size
    std::uint32_t num_nodes = circuit_.num_nodes() - 1;  // Exclude GND
    std::uint32_t num_branches = 0;
    std::uint32_t extra_nodes = 0;

    // One branch current variable per device to which CircuitGraph gave a
    // branch id: voltage sources, VCVS, CCVS and voltage-mode B sources.
    num_branches = static_cast<std::uint32_t>(circuit_.get_voltage_sources().size());

    // Check circuit size before allocation
    const std::uint32_t total_nodes = num_nodes + extra_nodes;
    if (total_nodes > core::MAX_CIRCUIT_NODES) {
        throw std::runtime_error(fmt::format(
            "Circuit exceeds maximum node limit: {} > {}",
            total_nodes, core::MAX_CIRCUIT_NODES));
    }
    if (num_branches > core::MAX_CIRCUIT_BRANCHES) {
        throw std::runtime_error(fmt::format(
            "Circuit exceeds maximum branch limit: {} > {}",
            num_branches, core::MAX_CIRCUIT_BRANCHES));
    }

    mna_.resize(total_nodes, num_branches);


    spdlog::info("Created TransientSolver (nodes={}, branches={}, system_size={})",
                 total_nodes, num_branches, mna_.size());

    // Create linear solver backend (ENV or default)
    lin_solver_ = make_solver(choose_backend());

    // Auto-correction: TR-BDF2 + PREDICTOR_CORRECTOR is inefficient (237x overhead)
    // Automatically switch to PHYSICAL LTE for optimal performance
    if (config_.transient.method == IntegrationMethod::TR_BDF2) {
        if (config_.transient.error_est == ErrorEstimation::PREDICTOR_CORRECTOR) {
            spdlog::warn("TR-BDF2 with PREDICTOR_CORRECTOR LTE is inefficient (237x overhead)");
            spdlog::warn("Automatically switching to PHYSICAL LTE for optimal performance");
            config_.transient.error_est = ErrorEstimation::PHYSICAL;
            spdlog::info("LTE estimation method changed: PREDICTOR_CORRECTOR -> PHYSICAL");
        } else if (config_.transient.error_est == ErrorEstimation::COMBINED) {
            spdlog::warn("TR-BDF2 with COMBINED LTE includes inefficient PREDICTOR_CORRECTOR");
            spdlog::warn("Automatically switching to PHYSICAL LTE for optimal performance");
            config_.transient.error_est = ErrorEstimation::PHYSICAL;
            spdlog::info("LTE estimation method changed: COMBINED -> PHYSICAL");
        }
    }

    // Reset state of all dynamic devices (Capacitor, Inductor)
    // This is critical for temperature sweep where multiple TransientSolver instances
    // are created for the same circuit. Without reset, charge/flux history accumulates.
    for (const auto& device : circuit_.get_devices()) {
        if (device->is_dynamic()) {
            device->reset_state();
        }
    }
}

TransientSolver::~TransientSolver() = default;

TransientResult TransientSolver::solve(Real t_start, Real t_stop, Real timestep) {
    auto start_time = std::chrono::high_resolution_clock::now();

    spdlog::info("Starting transient analysis: t=[{}, {}], dt={}",
                 t_start, t_stop, timestep);

    // Apply global temperature to all devices before analysis starts (only if explicitly set)
    if (config_.temperature_explicitly_set) {
        apply_temperature_to_devices(circuit_, config_.temperature);
    }

    // Step 1: Initialize simulation (DC operating point / UIC mode / Pure DC)
    auto result = initialize_simulation(t_start, timestep);
    if (result.status != ConvergenceStatus::SUCCESS) {
        return result;
    }

    // Step 2: Run time-stepping loop (adaptive or fixed)
    bool success = run_timestepping_loop(result, t_start, t_stop, timestep);
    if (!success) {
        return result;
    }

    // Step 3: Finalize and report
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<Real> elapsed = end_time - start_time;
    result.stats.total_time_seconds = elapsed.count();

    spdlog::info("Transient analysis complete: {} timesteps, {} iterations, {:.3f}s",
                 result.timepoints.size() - 1, result.stats.total_iterations,
                 result.stats.total_time_seconds);

    // Step 4: Perform Fourier analysis if requested
    if (config_.fourier.enabled) {
        spdlog::info("Performing Fourier analysis on signal: {}", config_.fourier.signal);

        // Extract time and signal data from timepoints
        std::vector<Real> time;
        std::vector<Real> signal;

        // Parse signal name to determine type (voltage or current)
        std::string signal_name = config_.fourier.signal;
        bool is_voltage = false;
        bool is_current = false;
        std::string node_or_source;

        // Parse v(node) or i(source) format
        if (signal_name.size() >= 4 && signal_name[1] == '(' && signal_name.back() == ')') {
            char type = signal_name[0];
            node_or_source = signal_name.substr(2, signal_name.size() - 3);

            if (type == 'v' || type == 'V') {
                is_voltage = true;
            } else if (type == 'i' || type == 'I') {
                is_current = true;
            }
        }

        if (!is_voltage && !is_current) {
            spdlog::error("Invalid Fourier signal format: {}", signal_name);
        } else if (is_voltage) {
            // Extract voltage signal: v(node)
            // Find node ID from node name
            try {
                std::uint32_t node_id = circuit_.get_node_id(node_or_source);
                std::uint32_t node_index = node_id - 1;  // Convert to 0-based index (skip GND)

                for (const auto& tp : result.timepoints) {
                    time.push_back(tp.time);
                    if (node_index < tp.node_voltages.size()) {
                        signal.push_back(tp.node_voltages[node_index]);
                    } else {
                        spdlog::warn("Node index {} out of range at t={}", node_index, tp.time);
                        signal.push_back(0.0);
                    }
                }
            } catch (const std::exception& e) {
                spdlog::error("Node not found for Fourier analysis: {} ({})", node_or_source, e.what());
            }
        } else if (is_current) {
            // Extract current signal: i(source)
            // Find branch current index for voltage source
            std::uint32_t branch_index = 0;
            bool found = false;

            for (const auto& device : circuit_.get_devices()) {
                if (device->type() == DeviceType::VSOURCE) {
                    // Check if device name matches
                    if (device->name() == node_or_source) {
                        found = true;
                        break;
                    }
                    branch_index++;
                }
            }

            if (!found) {
                spdlog::error("Voltage source not found for Fourier analysis: {}", node_or_source);
            } else {
                for (const auto& tp : result.timepoints) {
                    time.push_back(tp.time);
                    if (branch_index < tp.branch_currents.size()) {
                        signal.push_back(tp.branch_currents[branch_index]);
                    } else {
                        spdlog::warn("Branch index {} out of range at t={}", branch_index, tp.time);
                        signal.push_back(0.0);
                    }
                }
            }
        }

        // Perform Fourier analysis if data was extracted
        if (!time.empty() && !signal.empty()) {
            FourierResult fourier_result = FourierAnalyzer::analyze(
                time, signal,
                config_.fourier.fundamental_freq,
                config_.fourier.signal,
                config_.fourier.num_harmonics
            );

            // Print results
            FourierAnalyzer::print_result(fourier_result);
        } else {
            spdlog::error("Failed to extract signal data for Fourier analysis");
        }
    }

    return result;
}

bool TransientSolver::initialize_dc_operating_point(DenseVector& solution) {
    SolverStats stats;
    ConvergenceStatus status = dc_solver_->solve(solution, stats);

    if (status != ConvergenceStatus::SUCCESS) {
        return false;
    }

    // Initialize device states with DC solution
    accept_timestep_solution(solution);

    return true;
}

void TransientSolver::build_mna_transient(const DenseVector& node_voltages,
                                          Real timestep,
                                          IntegrationMethod method,
                                          const BDFCoefficients* bdf_coef,
                                          const TR_BDF2_Coefficients* tr_bdf2_coef) {
    // Clear system
    mna_.G.clear();
    mna_.b.assign(mna_.b.size(), 0.0);

    // Stamp all devices
    stamp_devices_transient(node_voltages, timestep, method, bdf_coef, tr_bdf2_coef);
}

void TransientSolver::stamp_devices_transient(const DenseVector& node_voltages,
                                              Real timestep,
                                              IntegrationMethod method,
                                              const BDFCoefficients* bdf_coef_in,
                                              const TR_BDF2_Coefficients* tr_bdf2_coef) {
    using namespace devices;

    // Compute variable-timestep BDF coefficients if needed
    const BDFCoefficients* bdf_coef = bdf_coef_in;
    BDFCoefficients coef;  // Local storage for computed coefficients

    if (bdf_coef == nullptr && (method == IntegrationMethod::GEAR2 || method == IntegrationMethod::GEAR3)) {
        coef = get_bdf_coefficients(method, timestep);
        bdf_coef = &coef;

        spdlog::trace("stamp_devices_transient: method={}, dt={:.3e}, using variable-timestep BDF coefficients",
                     (method == IntegrationMethod::GEAR2) ? "GEAR2" : "GEAR3", timestep);
    }

    // Update time-varying voltage sources with current time
    // NOTE: This loop must remain sequential as it modifies device state
    for (const auto& device : circuit_.get_devices()) {
        if (device->type() == DeviceType::VSOURCE) {
            // Try to cast to TimeVaryingVoltageSource
            auto* tv_source = dynamic_cast<TimeVaryingVoltageSource*>(device.get());
            if (tv_source) {
                tv_source->set_time(current_time_);
            }
        }
    }

    const auto& devices = circuit_.get_devices();

    // Step 2.3: Use unified ParallelDeviceStamper
    // benchmark results: threshold=1000000 (effectively disabled for transient)
    ParallelDeviceStamper::execute(
        devices, mna_, thread_local_mna_mgr_,
        [&](const Device& dev, MNASystem& mna) {
            dev.stamp_transient(mna, node_voltages, timestep, method, bdf_coef, tr_bdf2_coef);
        },
        {.threshold = 1000000, .enable_profiling = true, .name = "Transient"}
    );
}

// CRITICAL FIX: Changed return type to include both accepted_dt and next_dt
// Return: {status, accepted_dt, next_dt, accepted}
std::tuple<ConvergenceStatus, Real, Real, bool>
TransientSolver::solve_timestep_adaptive(DenseVector& solution,
                                        Real current_dt,
                                        std::uint32_t& iterations,
                                        std::uint32_t& rejects) {
    // Embedded method: solve with both low and high order methods
    // Use LTE estimation to accept/reject and adjust timestep
    // With adaptive order control, select appropriate methods based on history

    constexpr std::uint32_t max_timestep_rejects = 20;  // Allow more rejects for sharp transients
    std::uint32_t reject_count = 0;

    while (reject_count < max_timestep_rejects) {
        // Save device states before attempting this timestep
        save_device_states();

        // Select integration methods with adaptive order control
        IntegrationMethod method_high = config_.transient.adaptive_order ?
            select_integration_order(0.5) :  // Provisional call with moderate LTE
            config_.transient.method;

        Real lte_voltage = 0.0;
        DenseVector solution_high;
        std::uint32_t iter_high = 0;
        ConvergenceStatus status_high;

        // Choose error estimation method based on configuration
        const bool use_predictor_corrector =
            (config_.transient.error_est == ErrorEstimation::PREDICTOR_CORRECTOR) ||
            (config_.transient.error_est == ErrorEstimation::COMBINED);

        const bool use_embedded =
            (config_.transient.error_est == ErrorEstimation::PHYSICAL) ||
            (config_.transient.error_est == ErrorEstimation::COMBINED);

        // Warm-up mode for P-C method: need at least 2 solutions in history for reliable prediction
        // During warm-up (first 1-2 steps), use PHYSICAL LTE instead to build history
        const bool pc_warmup_active = use_predictor_corrector && (solution_history_.size() < 2);

        if (use_predictor_corrector && !use_embedded && !pc_warmup_active) {
            // Pure Predictor-Corrector difference-based LTE estimation
            // This is the Spectre/HSPICE DVDT approach

            // For P-C method, we need at least 2 solutions in history for good prediction
            const bool has_history = !solution_history_.empty();

            DenseVector solution_predicted;
            if (has_history) {
                // Predictor: Extrapolate from past solutions
                solution_predicted = predict_solution(current_dt);
            } else {
                // No history: use current solution as prediction
                solution_predicted = solution;
                spdlog::trace("P-C method: No history, using current solution as prediction");
            }

            // Corrector: Solve with Newton-Raphson (uses prediction as initial guess)
            solution_high = solution_predicted;  // Use prediction as initial guess

            if (method_high == IntegrationMethod::TR_BDF2) {
                status_high = solve_timestep_tr_bdf2(solution_high, current_dt, iter_high);
            } else {
                status_high = solve_timestep(solution_high, current_dt, iter_high, method_high);
            }

            if (status_high != ConvergenceStatus::SUCCESS) {
                restore_device_states();
                return {status_high, current_dt, current_dt, false};
            }

            iterations += iter_high;

            // Estimate LTE from P-C difference
            lte_voltage = estimate_lte_predictor_corrector(solution_predicted, solution_high);

            spdlog::trace("P-C method: predictor-corrector difference LTE={:.3e}", lte_voltage);

        } else if (use_embedded || pc_warmup_active) {
            // Traditional embedded method (low-order vs high-order)
            // This is the Berkeley SPICE approach
            // Also used during P-C warm-up to build solution history

            if (pc_warmup_active) {
                spdlog::trace("P-C warm-up mode (step {}): using PHYSICAL LTE to build history",
                             solution_history_.size());
            }

            // Determine low order method (one level below for LTE estimation)
            IntegrationMethod method_low = IntegrationMethod::BACKWARD_EULER;
            if (method_high == IntegrationMethod::GEAR2 || method_high == IntegrationMethod::TR_BDF2) {
                method_low = IntegrationMethod::BACKWARD_EULER;
            } else if (method_high == IntegrationMethod::GEAR3) {
                method_low = IntegrationMethod::GEAR2;
            } else if (method_high == IntegrationMethod::TRAPEZOIDAL) {
                method_low = IntegrationMethod::BACKWARD_EULER;
            }

            // Solve with low-order method
            DenseVector solution_low = solution;
            std::uint32_t iter_low = 0;
            ConvergenceStatus status_low = solve_timestep(solution_low, current_dt,
                                                          iter_low, method_low);

            if (status_low != ConvergenceStatus::SUCCESS) {
                restore_device_states();
                return {status_low, current_dt, current_dt, false};
            }

            iterations += iter_low;

            // Restore device states for second solve
            restore_device_states();

            // Solve with high-order method
            solution_high = solution;

            if (method_high == IntegrationMethod::TR_BDF2) {
                status_high = solve_timestep_tr_bdf2(solution_high, current_dt, iter_high);
            } else {
                status_high = solve_timestep(solution_high, current_dt, iter_high, method_high);
            }

            if (status_high != ConvergenceStatus::SUCCESS) {
                restore_device_states();
                return {status_high, current_dt, current_dt, false};
            }

            iterations += iter_high;

            // Estimate LTE from embedded method
            lte_voltage = estimate_lte(solution_low, solution_high);

            // If COMBINED mode, also calculate P-C difference for comparison
            if (use_predictor_corrector && !solution_history_.empty()) {
                DenseVector solution_predicted = predict_solution(current_dt);
                Real lte_pc = estimate_lte_predictor_corrector(solution_predicted, solution_high);
                lte_voltage = std::max(lte_voltage, lte_pc);
                spdlog::trace("COMBINED: embedded LTE + P-C LTE = {:.3e}", lte_voltage);
            }
        } else {
            // Fallback: solve once with high-order method
            solution_high = solution;

            if (method_high == IntegrationMethod::TR_BDF2) {
                status_high = solve_timestep_tr_bdf2(solution_high, current_dt, iter_high);
            } else {
                status_high = solve_timestep(solution_high, current_dt, iter_high, method_high);
            }

            if (status_high != ConvergenceStatus::SUCCESS) {
                restore_device_states();
                return {status_high, current_dt, current_dt, false};
            }

            iterations += iter_high;
            lte_voltage = 0.1;  // Assume moderate error
        }

        // Physical quantity-based LTE estimation
        // Calculate maximum physical LTE across all dynamic devices (capacitors, inductors)
        // Final: Extended warm-up period for Q_scale stability
        // With improved Q_scale (additive), 20 steps provides sufficient charge/flux accumulation
        Real lte_physical = 0.0;
        Real lte = lte_voltage;  // Default: use voltage-based LTE

        if (history_count_ >= 20) {
            // Sufficient warm-up: use physical quantity-based error estimation
            Real chgtol = config_.transient.chgtol;

            // CRITICAL FIX: Convert solution_high to node_voltages for get_physical_lte()
            // This is needed because get_physical_lte() calculates charge/flux from the new solution
            DenseVector node_voltages_high(mna_.num_nodes + 1);
            node_voltages_high[0] = 0.0;  // GND
            for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
                node_voltages_high[i + 1] = solution_high[i];
            }

            for (const auto& device : circuit_.get_devices()) {
                if (device->is_dynamic()) {
                    Real device_lte = device->get_physical_lte(chgtol, node_voltages_high);
                    lte_physical = std::max(lte_physical, device_lte);
                }
            }

            // Combined LTE: use maximum of voltage-based and physical-based estimates
            // This ensures we respect both voltage accuracy and charge/flux conservation
            lte = std::max(lte_voltage, lte_physical);

            spdlog::trace("LTE estimation: voltage-based={:.3e}, physical-based={:.3e}, combined={:.3e}",
                         lte_voltage, lte_physical, lte);
        } else {
            // Warm-up period (steps 0-19): use only voltage-based LTE
            // This allows charge/flux to accumulate sufficiently for stable Q_scale calculation
            spdlog::trace("LTE estimation (warm-up step {}): voltage-based={:.3e} (physical LTE skipped)",
                         history_count_, lte_voltage);
        }

        // Update order selection based on actual LTE (if adaptive order enabled)
        // Final: Track order changes for timestep adjustment
        std::uint32_t previous_order = current_order_;
        if (config_.transient.adaptive_order) {
            method_high = select_integration_order(lte);
        }

        // Adjust timestep and check acceptance
        auto [new_dt, accept] = adjust_timestep(current_dt, lte, current_order_);

        // Final: Reduce timestep when order changes
        // Order changes require recalibration of timestep for stability
        if (current_order_ != previous_order) {
            Real order_change_factor = 0.8;  // Reduce to 80% when order changes
            new_dt *= order_change_factor;
            spdlog::debug("Order change detected ({} -> {}): reducing timestep by {:.1f}%",
                         previous_order, current_order_, (1.0 - order_change_factor) * 100);
        }

        if (accept) {
            // Accept high-order solution
            solution = solution_high;

            // Update history count for next timestep
            update_history_count();

            spdlog::debug("Timestep ACCEPTED: dt={:.3e}, LTE={:.3e}, next_dt={:.3e}, order={}",
                         current_dt, lte, new_dt, current_order_);
            // CRITICAL FIX: Return both accepted timestep (current_dt) and next timestep (new_dt)
            // This fixes the time management bug where t was incremented by the initial
            // trial timestep instead of the actual accepted timestep after rejections
            return {ConvergenceStatus::SUCCESS, current_dt, new_dt, true};
        } else {
            // Reject and retry with smaller timestep
            restore_device_states();
            current_dt = new_dt;
            reject_count++;
            rejects++;

            spdlog::debug("Timestep REJECTED ({}): LTE={:.3e} too large, retrying with dt={:.3e}",
                         reject_count, lte, current_dt);

            if (current_dt < config_.transient.min_step) {
                spdlog::error("Timestep below minimum: dt={:.3e} < min={:.3e}",
                             current_dt, config_.transient.min_step);
                return {ConvergenceStatus::TIMESTEP_TOO_SMALL, current_dt, current_dt, false};
            }
        }
    }

    // Too many rejects
    spdlog::error("Too many timestep rejects: {}", reject_count);
    restore_device_states();
    return {ConvergenceStatus::MAX_ITERATIONS, current_dt, current_dt, false};
}

ConvergenceStatus TransientSolver::solve_timestep(DenseVector& solution,
                                                  Real timestep,
                                                  std::uint32_t& iterations,
                                                  IntegrationMethod method,
                                                  const BDFCoefficients* bdf_coef,
                                                  const TR_BDF2_Coefficients* tr_bdf2_coef) {
    // TR-BDF2 uses two-stage method
    if (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef == nullptr) {
        // Call dedicated TR-BDF2 solver (two-stage method)
        return solve_timestep_tr_bdf2(solution, timestep, iterations);
    }

    iterations = 0;
    DenseVector x_old = solution;

    // Newton-Raphson iteration
    for (std::uint32_t iter = 0; iter < config_.transient.max_iterations; ++iter) {
        iterations++;

        // Build MNA system at current operating point
        build_mna_transient(x_old, timestep, method, bdf_coef, tr_bdf2_coef);

        // Factorize and solve
        bool factorize_ok;
        if (iter == 0) {
            factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
        } else {
            factorize_ok = lin_solver_->refactorize(mna_.G);
        }

        if (!factorize_ok) {
            spdlog::error("Linear solver factorization failed: {}",
                         lin_solver_->last_error());
            return ConvergenceStatus::SINGULAR_MATRIX;
        }

        // Solve G*x_new = b
        DenseVector x_new;
        bool solve_ok = lin_solver_->solve(mna_.b, x_new);

        if (!solve_ok) {
            spdlog::error("Linear solver failed: {}", lin_solver_->last_error());
            return ConvergenceStatus::NUMERICAL_ERROR;
        }

        // Check convergence
        if (check_convergence(x_new, x_old)) {
            solution = x_new;
            return ConvergenceStatus::SUCCESS;
        }

        x_old = x_new;
    }

    return ConvergenceStatus::MAX_ITERATIONS;
}

ConvergenceStatus TransientSolver::solve_timestep_tr_bdf2(DenseVector& solution,
                                                          Real timestep,
                                                          std::uint32_t& iterations) {
    // TR-BDF2 two-stage method (Bank & Rose, 1981)
    // γ = 2 - √2 ≈ 0.585786437626905
    // Stage 1: Trapezoidal rule from t_n to t_n + γΔt
    // Stage 2: Modified BDF2 from t_n + γΔt to t_n + Δt
    //
    // KEY OPTIMIZATION: With γ = 2 - √2, both stages have the same coefficient α
    // α_stage1 = 2/(γh) = α_stage2 = (2-γ)/((1-γ)h) = (2+√2)/h
    // This allows us to reuse the LU factorization of (G + αC) for both stages

    constexpr Real gamma = 2.0 - 1.41421356237309504880;  // 2 - √2

    iterations = 0;

    // Save device states before TR-BDF2 (for potential rejection at higher level)
    save_device_states();

    // Stage 1: Trapezoidal rule with reduced timestep γΔt
    Real gamma_step = gamma * timestep;

    // Create TR-BDF2 Stage 1 coefficients
    TR_BDF2_Coefficients stage1_coef;
    stage1_coef.stage = 1;
    stage1_coef.gamma = gamma;
    stage1_coef.stage_dt = gamma_step;
    stage1_coef.full_dt = timestep;
    stage1_coef.alpha0 = 0.0;  // Not used in Stage 1
    stage1_coef.alpha1 = 0.0;
    stage1_coef.alpha2 = 0.0;
    stage1_coef.beta0 = 0.0;

    spdlog::trace("TR-BDF2 Stage 1: Trapezoidal rule, γΔt={:.3e} (gamma={:.6f})",
                 gamma_step, gamma);

    // Newton-Raphson iteration for Stage 1
    DenseVector x_old = solution;
    DenseVector solution_gamma = solution;
    std::uint32_t iter_stage1 = 0;
    bool symbolic_analyzed = false;  // Track if symbolic analysis is done

    for (std::uint32_t iter = 0; iter < config_.transient.max_iterations; ++iter) {
        iter_stage1++;

        // Build MNA system at current operating point
        build_mna_transient(x_old, gamma_step, IntegrationMethod::TR_BDF2, nullptr, &stage1_coef);

        // Factorize: analyze once, then numeric factorize each iteration
        bool factorize_ok;
        if (!symbolic_analyzed) {
            // First iteration: perform symbolic + numeric factorization
            factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
            symbolic_analyzed = true;
            spdlog::trace("TR-BDF2 Stage 1: Symbolic analysis + numeric factorization (iter={})", iter);
        } else {
            // Subsequent iterations: only numeric factorization (reuse symbolic)
            factorize_ok = lin_solver_->refactorize(mna_.G);
            spdlog::trace("TR-BDF2 Stage 1: Numeric factorization only (iter={})", iter);
        }

        if (!factorize_ok) {
            spdlog::error("Linear solver factorization failed: {}", lin_solver_->last_error());
            restore_device_states();
            return ConvergenceStatus::SINGULAR_MATRIX;
        }

        // Solve G*x_new = b
        DenseVector x_new;
        bool solve_ok = lin_solver_->solve(mna_.b, x_new);

        if (!solve_ok) {
            spdlog::error("Linear solver failed: {}", lin_solver_->last_error());
            restore_device_states();
            return ConvergenceStatus::NUMERICAL_ERROR;
        }

        // Check convergence
        if (check_convergence(x_new, x_old)) {
            solution_gamma = x_new;
            spdlog::trace("TR-BDF2 Stage 1 converged in {} iterations", iter_stage1);
            break;
        }

        x_old = x_new;

        if (iter == config_.transient.max_iterations - 1) {
            spdlog::warn("TR-BDF2 Stage 1 failed to converge in {} iterations", iter_stage1);
            restore_device_states();
            return ConvergenceStatus::MAX_ITERATIONS;
        }
    }

    iterations += iter_stage1;

    // Temporarily accept Stage 1 solution to update device histories
    // This allows Stage 2 to use solution_gamma as previous state
    accept_timestep_solution(solution_gamma);

    // Stage 2: Modified BDF2 from t_n to t_n + Δt (using V_n and V_{n+γ})
    // Modified BDF2 coefficients derived from Lagrange interpolation:
    // For 3-point BDF using (0,V_n), (γΔt,V_{n+γ}), (Δt,V_{n+1}):
    // dV/dt|_{t=Δt} = [(1-γ)²V_n - V_{n+γ} + γ(2-γ)V_{n+1}] / [(1-γ)γΔt]
    // BDF form: [α₀V_{n+1} + α₁V_{n+γ} + α₂V_n] / (β₀Δt) = dV/dt
    // With β₀ = (1-γ)γ:
    //   α₀ = γ(2-γ)
    //   α₁ = -1
    //   α₂ = (1-γ)²
    //   β₀ = (1-γ)γ
    Real one_minus_gamma = 1.0 - gamma;
    Real two_minus_gamma = 2.0 - gamma;
    Real alpha0 = gamma * two_minus_gamma;
    Real alpha1 = -1.0;
    Real alpha2 = one_minus_gamma * one_minus_gamma;
    Real beta0 = one_minus_gamma * gamma;

    TR_BDF2_Coefficients stage2_coef;
    stage2_coef.stage = 2;
    stage2_coef.gamma = gamma;
    stage2_coef.stage_dt = timestep - gamma_step;  // (1-γ)Δt
    stage2_coef.full_dt = timestep;
    stage2_coef.alpha0 = alpha0;
    stage2_coef.alpha1 = alpha1;
    stage2_coef.alpha2 = alpha2;
    stage2_coef.beta0 = beta0;

    spdlog::trace("TR-BDF2 Stage 2: Modified BDF2, Δt={:.3e}, α₀={:.6f}, α₁={:.6f}, α₂={:.6f}",
                 timestep, alpha0, alpha1, alpha2);

    // Newton-Raphson iteration for Stage 2
    // CRITICAL: Reuse symbolic factorization from Stage 1
    // Because γ = 2 - √2, both stages have the same α coefficient
    x_old = solution_gamma;
    DenseVector solution_final = solution_gamma;
    std::uint32_t iter_stage2 = 0;

    for (std::uint32_t iter = 0; iter < config_.transient.max_iterations; ++iter) {
        iter_stage2++;

        // Build MNA system at current operating point
        build_mna_transient(x_old, timestep, IntegrationMethod::TR_BDF2, nullptr, &stage2_coef);

        // OPTIMIZATION: Only numeric factorization (reuse symbolic from Stage 1)
        // This is the key benefit of γ = 2 - √2: both stages have the same matrix structure
        bool factorize_ok = lin_solver_->refactorize(mna_.G);
        spdlog::trace("TR-BDF2 Stage 2: Numeric factorization only (reusing symbolic, iter={})", iter);

        if (!factorize_ok) {
            spdlog::error("Linear solver factorization failed: {}", lin_solver_->last_error());
            return ConvergenceStatus::SINGULAR_MATRIX;
        }

        // Solve G*x_new = b
        DenseVector x_new;
        bool solve_ok = lin_solver_->solve(mna_.b, x_new);

        if (!solve_ok) {
            spdlog::error("Linear solver failed: {}", lin_solver_->last_error());
            return ConvergenceStatus::NUMERICAL_ERROR;
        }

        // Check convergence
        if (check_convergence(x_new, x_old)) {
            solution_final = x_new;
            spdlog::trace("TR-BDF2 Stage 2 converged in {} iterations", iter_stage2);
            break;
        }

        x_old = x_new;

        if (iter == config_.transient.max_iterations - 1) {
            spdlog::warn("TR-BDF2 Stage 2 failed to converge in {} iterations", iter_stage2);
            return ConvergenceStatus::MAX_ITERATIONS;
        }
    }

    iterations += iter_stage2;

    // Update solution with Stage 2 result
    solution = solution_final;

    spdlog::trace("TR-BDF2 complete: total_iterations={} (Stage1={}, Stage2={})",
                 iterations, iter_stage1, iter_stage2);
    spdlog::debug("TR-BDF2 LU factorization reuse: 1 symbolic analysis + {} numeric factorizations",
                 iter_stage1 + iter_stage2);

    return ConvergenceStatus::SUCCESS;
}

bool TransientSolver::check_convergence(const DenseVector& x_new,
                                       const DenseVector& x_old) const {
    // Step 2.1: Delegate to ConvergenceChecker
    const auto& tran_config = config_.transient;

    // Build ConvergenceConfig from SimulationConfig (Transient mode)
    ConvergenceConfig conv_config;
    conv_config.abstol = tran_config.abstol;
    conv_config.reltol = tran_config.reltol;
    conv_config.vntol = tran_config.vntol;
    conv_config.check_residual = false;  // Transient mode: no residual check

    // Create checker with current num_nodes
    ConvergenceChecker checker(mna_.num_nodes);

    return checker.check(x_new, x_old, conv_config, nullptr);
}

void TransientSolver::save_device_states() {
    for (const auto& device : circuit_.get_devices()) {
        device->save_state();
    }
}

void TransientSolver::restore_device_states() {
    for (const auto& device : circuit_.get_devices()) {
        device->restore_state();
    }
}

void TransientSolver::accept_timestep_solution(const DenseVector& solution) {
    // Convert solution to node voltages (including GND=0)
    DenseVector node_voltages(mna_.num_nodes + 1);
    node_voltages[0] = 0.0;  // GND

    for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
        node_voltages[i + 1] = solution[i];
    }

    // Update each device's state
    for (const auto& device : circuit_.get_devices()) {
        device->accept_solution(node_voltages);
    }

    // Save solution history for predictor-corrector error estimation
    // Keep last 3 solutions for quadratic extrapolation
    solution_history_.push_back(solution);
    if (solution_history_.size() > 3) {
        solution_history_.erase(solution_history_.begin());
    }
}

Real TransientSolver::estimate_lte(const DenseVector& solution_low_order,
                                   const DenseVector& solution_high_order) const {
    // Embedded method: LTE ≈ |solution_high - solution_low|
    // Use normalized error: |error| / (reltol * |value| + abstol)

    Real lte_abstol = config_.transient.lte_abstol;
    Real lte_reltol = config_.transient.lte_reltol;
    Real max_lte = 0.0;

    // Check LTE for node voltages only (not branch currents)
    for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
        Real v_low = solution_low_order[i];
        Real v_high = solution_high_order[i];
        Real error = std::abs(v_high - v_low);
        Real scale = lte_reltol * std::max(std::abs(v_high), std::abs(v_low)) + lte_abstol;
        Real normalized_lte = error / scale;

        max_lte = std::max(max_lte, normalized_lte);

        spdlog::trace("LTE estimation: node[{}] V_low={:.6e}, V_high={:.6e}, "
                     "error={:.6e}, normalized_lte={:.6e}",
                     i, v_low, v_high, error, normalized_lte);
    }

    return max_lte;
}

DenseVector TransientSolver::predict_solution(Real current_dt) const {
    // Predictor using extrapolation from past solutions
    // Order depends on available history:
    // - 0 history: use current solution (0th order hold)
    // - 1 history: linear extrapolation
    // - 2+ history: quadratic extrapolation (Adams-Bashforth 2nd order)

    const std::size_t system_size = mna_.size();
    DenseVector predicted(system_size, 0.0);

    if (solution_history_.empty()) {
        // No history: predict zero (will be corrected by Newton-Raphson)
        spdlog::trace("Predictor: No history, predicting zero vector");
        return predicted;
    }

    const auto& x_n = solution_history_.back();  // Most recent solution

    if (solution_history_.size() == 1 || timestep_history_.empty()) {
        // Only 1 solution in history: 0th order hold (predict same as last)
        predicted = x_n;
        spdlog::trace("Predictor: 0th order hold (predict = x_n)");
        return predicted;
    }

    // Linear or quadratic extrapolation
    const auto& x_n_minus_1 = solution_history_[solution_history_.size() - 2];
    const Real dt_prev = timestep_history_.back();

    if (solution_history_.size() == 2) {
        // Linear extrapolation: x_pred = x_n + (x_n - x_{n-1}) * dt_current / dt_prev
        Real ratio = current_dt / dt_prev;
        for (std::size_t i = 0; i < system_size; ++i) {
            predicted[i] = x_n[i] + ratio * (x_n[i] - x_n_minus_1[i]);
        }
        spdlog::trace("Predictor: Linear extrapolation (ratio={:.3f})", ratio);
    } else {
        // Quadratic extrapolation (Adams-Bashforth 2nd order)
        // x_pred = x_n + dt * (1.5 * f_n - 0.5 * f_{n-1})
        // Approximation: f ≈ (x_n - x_{n-1}) / dt_prev
        const auto& x_n_minus_2 = solution_history_[solution_history_.size() - 3];
        const Real dt_prev_2 = timestep_history_[timestep_history_.size() - 2];

        for (std::size_t i = 0; i < system_size; ++i) {
            Real f_n = (x_n[i] - x_n_minus_1[i]) / dt_prev;
            Real f_n_minus_1 = (x_n_minus_1[i] - x_n_minus_2[i]) / dt_prev_2;
            predicted[i] = x_n[i] + current_dt * (1.5 * f_n - 0.5 * f_n_minus_1);
        }
        spdlog::trace("Predictor: Quadratic extrapolation (Adams-Bashforth 2)");
    }

    return predicted;
}

Real TransientSolver::estimate_lte_predictor_corrector(const DenseVector& predicted,
                                                        const DenseVector& corrected) const {
    // Predictor-Corrector difference-based LTE estimation
    // LTE ≈ |x_corrected - x_predicted|
    // This is the method used by Spectre and HSPICE DVDT mode
    //
    // Theoretical basis: The difference between predictor and corrector
    // is proportional to the local truncation error of the integration method

    Real lte_abstol = config_.transient.lte_abstol;
    Real lte_reltol = config_.transient.lte_reltol;
    Real max_lte = 0.0;

    // Check LTE for node voltages only (not branch currents)
    for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
        Real v_pred = predicted[i];
        Real v_corr = corrected[i];
        Real error = std::abs(v_corr - v_pred);
        Real scale = lte_reltol * std::max(std::abs(v_corr), std::abs(v_pred)) + lte_abstol;
        Real normalized_lte = error / scale;

        max_lte = std::max(max_lte, normalized_lte);

        spdlog::trace("P-C LTE estimation: node[{}] V_pred={:.6e}, V_corr={:.6e}, "
                     "error={:.6e}, normalized_lte={:.6e}",
                     i, v_pred, v_corr, error, normalized_lte);
    }

    return max_lte;
}

std::pair<Real, bool> TransientSolver::adjust_timestep(Real current_dt,
                                                       Real lte_estimate,
                                                       std::uint32_t order) const {
    // Target: LTE ≈ 1.0 (normalized)
    // Safety factor: 0.9
    // Timestep adjustment: dt_new = dt_old * (1.0 / LTE)^(1/(order+1))

    Real target_lte = 1.0;
    Real safety = 0.9;

    // Accept step if LTE < TRTOL (ngspice-compatible truncation tolerance)
    // Changed from hardcoded 10.0 to configurable trtol (default 2.0)
    bool accept = (lte_estimate <= config_.transient.trtol);

    // Calculate timestep multiplier
    Real exponent = 1.0 / (order + 1.0);
    Real multiplier = safety * std::pow(target_lte / std::max(lte_estimate, 1e-10), exponent);

    // Clamp multiplier with conditional maximum increase
    // Conservative timestep growth for accuracy
    // ngspice uses very conservative timestep growth (~1.2×)
    // 1.2 achieves best accuracy (1.99% error vs theory) despite more rejects
    //
    // Final: Initial timestep control improvement
    // During initial period (first 10 steps), use even more conservative growth
    // to prevent large errors when LTE estimate is artificially small
    Real max_increase;
    if (history_count_ < 10) {
        // Initial period: very conservative growth (1.1x max)
        max_increase = 1.1;
        spdlog::trace("Initial timestep control active (history_count={}): max_increase={:.2f}",
                     history_count_, max_increase);
    } else if (lte_estimate < 0.1) {
        // Normal operation with good accuracy: allow 1.2x
        max_increase = 1.2;
    } else {
        // Normal operation: conservative 1.1x
        max_increase = 1.1;
    }

    Real min_decrease = 0.5;
    multiplier = std::max(min_decrease, std::min(max_increase, multiplier));

    Real new_dt = current_dt * multiplier;

    // Apply min/max timestep limits
    new_dt = std::max(config_.transient.min_step,
                     std::min(config_.transient.max_step, new_dt));

    spdlog::debug("Timestep adjustment: LTE={:.3e}, multiplier={:.3f}, "
                 "dt: {:.3e} -> {:.3e}, accept={}",
                 lte_estimate, multiplier, current_dt, new_dt, accept);

    return {new_dt, accept};
}

IntegrationMethod TransientSolver::select_integration_order(Real lte_estimate) {
    // If adaptive order control is disabled, use fixed method
    if (!config_.transient.adaptive_order) {
        return config_.transient.method;
    }

    // Determine maximum available order based on history
    // CRITICAL FIX: BDF2 needs 2 history points (V_n and V_{n-1}), not 1
    // BDF3 needs 3 history points (V_n, V_{n-1}, V_{n-2}), not 2
    std::uint32_t max_available_order = 1;
    if (history_count_ >= 2) max_available_order = 2;  // BDF2 needs 2 previous steps
    if (history_count_ >= 3) max_available_order = 3;  // BDF3 needs 3 previous steps
    // NOTE: Now using variable-timestep BDF formulas, so BDF3 is re-enabled

    // Clamp by configured maximum order
    max_available_order = std::min(max_available_order, config_.transient.max_order);

    // Order selection logic based on LTE estimate
    // Adjusted thresholds from 0.1/2.0 to 0.2/1.5 for better responsiveness
    std::uint32_t new_order = current_order_;

    if (lte_estimate < 0.2 && current_order_ < max_available_order) {
        // Error small: try increasing order
        new_order = current_order_ + 1;
        spdlog::debug("Increasing order: {} -> {} (LTE={:.3e})",
                     current_order_, new_order, lte_estimate);
    } else if (lte_estimate > 1.5 && current_order_ > 1) {
        // Error large: decrease order for stability
        new_order = current_order_ - 1;
        spdlog::debug("Decreasing order: {} -> {} (LTE={:.3e})",
                     current_order_, new_order, lte_estimate);
    }

    // Update current order
    current_order_ = new_order;

    // Map order to IntegrationMethod
    switch (current_order_) {
        case 1:
            return IntegrationMethod::BACKWARD_EULER;
        case 2:
            // Order 2: Choose between GEAR2 (BDF2) and TRAPEZOIDAL based on user preference
            // BDF2 (GEAR2): L-stable, better for stiff problems (default)
            // TRAPEZOIDAL: A-stable, better accuracy for smooth problems
            return config_.transient.prefer_trapezoidal ?
                   IntegrationMethod::TRAPEZOIDAL :
                   IntegrationMethod::GEAR2;
        case 3:
            return IntegrationMethod::GEAR3;
        default:
            return IntegrationMethod::BACKWARD_EULER;
    }
}

void TransientSolver::update_history_count() {
    // Increment history counter, capped at max_order
    if (history_count_ != std::numeric_limits<std::uint32_t>::max()) {
        history_count_++;
    }
    spdlog::trace("History count updated: {}", history_count_);
}

BDFCoefficients TransientSolver::compute_bdf2_coefficients(Real dt_n, Real dt_prev) const {
    // Variable-timestep BDF2 formula
    // Based on Lagrange interpolation with non-uniform timesteps
    //
    // Timestep ratio:
    //   ρ = dt_n / dt_prev
    //
    // Coefficients (corrected formula):
    //   α₀ = (1 + 2ρ) / (1 + ρ)
    //   α₁ = -(1 + ρ)
    //   α₂ = ρ² / (1 + ρ)
    //   β₀ = dt_n  ← CRITICAL FIX: was dt_n/(1+ρ), causing 2x error in g_eq
    //
    // Formula: dV/dt ≈ (α₀*V_{n+1} + α₁*V_n + α₂*V_{n-1}) / β₀
    //
    // Special case (uniform timestep, ρ = 1):
    //   α₀ = 3/2, α₁ = -2, α₂ = 1/2, β₀ = dt
    //   → (3/2*V_{n+1} - 2*V_n + 1/2*V_{n-1}) / dt = (3V_{n+1} - 4V_n + V_{n-1}) / (2dt)  ✓

    Real rho = dt_n / dt_prev;
    Real denom = 1.0 + rho;

    BDFCoefficients coef;
    coef.alpha0 = (1.0 + 2.0 * rho) / denom;
    coef.alpha1 = -(1.0 + rho);
    coef.alpha2 = rho * rho / denom;
    coef.alpha3 = 0.0;  // Not used in BDF2
    coef.beta0 = dt_n;  // CRITICAL FIX: was dt_n / denom

    spdlog::trace("BDF2 coefficients: ρ={:.3f}, α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.3e}",
                 rho, coef.alpha0, coef.alpha1, coef.alpha2, coef.beta0);

    return coef;
}

BDFCoefficients TransientSolver::compute_bdf3_coefficients(Real dt_n, Real dt_prev1, Real dt_prev2) const {
    // Variable-timestep BDF3 formula using Lagrange interpolation
    //
    // BDF3 fits a cubic polynomial through 4 points:
    //   (t_{n+1}, y_{n+1}), (t_n, y_n), (t_{n-1}, y_{n-1}), (t_{n-2}, y_{n-2})
    //
    // Then: y'_{n+1} = dP/dt|_{t=t_{n+1}}
    //
    // Using shifted coordinate s measured from t_n:
    //   s = 0:       t_n       (y_n)
    //   s = dt_n:    t_{n+1}   (y_{n+1})
    //   s = -dt_prev1:          t_{n-1}   (y_{n-1})
    //   s = -(dt_prev1+dt_prev2): t_{n-2}   (y_{n-2})
    //
    // Lagrange interpolation gives:
    //   P(s) = Σ y_i * L_i(s)
    //   P'(s) = Σ y_i * L_i'(s)
    //
    // where L_i(s) = Π_{j≠i} (s - s_j) / (s_i - s_j)
    //
    // Coefficients: α_i = L_i'(dt_n) * dt_n  (normalized by dt_n)
    //
    // For uniform timestep (dt_n = dt_prev1 = dt_prev2 = h):
    //   α₀ = 11/6, α₁ = -3, α₂ = 3/2, α₃ = -1/3, β₀ = h

    Real h0 = dt_n;
    Real h1 = dt_prev1;
    Real h2 = dt_prev2;

    // Time points in shifted coordinate (s measured from t_n)
    Real s0 = h0;                  // t_{n+1}
    Real s1 = 0.0;                 // t_n
    Real s2 = -h1;                 // t_{n-1}
    Real s3 = -(h1 + h2);          // t_{n-2}

    // Evaluate point for derivative: s_eval = s0 = h0
    Real s = h0;

    // L_0'(s0) for y_{n+1}
    // L_0(s) = [(s-s1)(s-s2)(s-s3)] / [(s0-s1)(s0-s2)(s0-s3)]
    // L_0'(s) = [(s-s2)(s-s3) + (s-s1)(s-s3) + (s-s1)(s-s2)] / denom
    Real denom0 = (s0 - s1) * (s0 - s2) * (s0 - s3);
    Real term0 = (s - s2) * (s - s3) + (s - s1) * (s - s3) + (s - s1) * (s - s2);
    Real L0_prime = term0 / denom0;

    // L_1'(s0) for y_n
    // L_1(s) = [(s-s0)(s-s2)(s-s3)] / [(s1-s0)(s1-s2)(s1-s3)]
    Real denom1 = (s1 - s0) * (s1 - s2) * (s1 - s3);
    Real term1 = (s - s2) * (s - s3) + (s - s0) * (s - s3) + (s - s0) * (s - s2);
    Real L1_prime = term1 / denom1;

    // L_2'(s0) for y_{n-1}
    // L_2(s) = [(s-s0)(s-s1)(s-s3)] / [(s2-s0)(s2-s1)(s2-s3)]
    Real denom2 = (s2 - s0) * (s2 - s1) * (s2 - s3);
    Real term2 = (s - s1) * (s - s3) + (s - s0) * (s - s3) + (s - s0) * (s - s1);
    Real L2_prime = term2 / denom2;

    // L_3'(s0) for y_{n-2}
    // L_3(s) = [(s-s0)(s-s1)(s-s2)] / [(s3-s0)(s3-s1)(s3-s2)]
    Real denom3 = (s3 - s0) * (s3 - s1) * (s3 - s2);
    Real term3 = (s - s1) * (s - s2) + (s - s0) * (s - s2) + (s - s0) * (s - s1);
    Real L3_prime = term3 / denom3;

    // BDF coefficients (normalized by h0 = dt_n)
    BDFCoefficients coef;
    coef.alpha0 = L0_prime * h0;
    coef.alpha1 = L1_prime * h0;
    coef.alpha2 = L2_prime * h0;
    coef.alpha3 = L3_prime * h0;
    coef.beta0 = h0;

    // Log for debugging (optional)
    Real rho1 = dt_n / dt_prev1;
    Real rho2 = dt_n / dt_prev2;
    spdlog::trace("BDF3 Lagrange: h_n={:.3e}, h_{{n-1}}={:.3e}, h_{{n-2}}={:.3e}, ρ₁={:.3f}, ρ₂={:.3f}",
                 dt_n, dt_prev1, dt_prev2, rho1, rho2);
    spdlog::trace("BDF3 coefficients: α₀={:.10f}, α₁={:.10f}, α₂={:.10f}, α₃={:.10f}, β₀={:.3e}",
                 coef.alpha0, coef.alpha1, coef.alpha2, coef.alpha3, coef.beta0);

    // Verify for uniform timestep (optional debug check)
    if (std::abs(rho1 - 1.0) < 1e-10 && std::abs(rho2 - 1.0) < 1e-10) {
        Real expected_a0 = 11.0 / 6.0;
        Real expected_a1 = -3.0;
        Real expected_a2 = 1.5;
        Real expected_a3 = -1.0 / 3.0;
        Real err0 = std::abs(coef.alpha0 - expected_a0);
        Real err1 = std::abs(coef.alpha1 - expected_a1);
        Real err2 = std::abs(coef.alpha2 - expected_a2);
        Real err3 = std::abs(coef.alpha3 - expected_a3);
        Real max_err = std::max({err0, err1, err2, err3});
        spdlog::debug("BDF3 uniform timestep verification: max_error={:.2e} (expected < 1e-10)", max_err);
        if (max_err > 1e-6) {
            spdlog::warn("BDF3 uniform timestep coefficients deviate from expected values!");
        }
    }

    return coef;
}

BDFCoefficients TransientSolver::get_bdf_coefficients(IntegrationMethod method, Real dt_n) const {
    if (method == IntegrationMethod::GEAR2 && timestep_history_.size() >= 1) {
        // Variable-timestep BDF2
        return compute_bdf2_coefficients(dt_n, timestep_history_.back());
    } else if (method == IntegrationMethod::GEAR3 && timestep_history_.size() >= 2) {
        // Variable-timestep BDF3
        Real dt_prev1 = timestep_history_[timestep_history_.size() - 1];
        Real dt_prev2 = timestep_history_[timestep_history_.size() - 2];
        return compute_bdf3_coefficients(dt_n, dt_prev1, dt_prev2);
    } else {
        // Not enough history: return uniform-timestep coefficients
        BDFCoefficients coef;

        if (method == IntegrationMethod::GEAR2) {
            // Uniform BDF2: (3V_{n+1} - 4V_n + V_{n-1}) / (2dt)
            coef.alpha0 = 1.5;
            coef.alpha1 = -2.0;
            coef.alpha2 = 0.5;
            coef.alpha3 = 0.0;
            coef.beta0 = dt_n;
        } else if (method == IntegrationMethod::GEAR3) {
            // Uniform BDF3: (11V_{n+1} - 18V_n + 9V_{n-1} - 2V_{n-2}) / (6dt)
            coef.alpha0 = 11.0 / 6.0;
            coef.alpha1 = -3.0;
            coef.alpha2 = 1.5;
            coef.alpha3 = -1.0 / 3.0;
            coef.beta0 = dt_n;
        } else {
            // Fallback: Backward Euler
            coef.alpha0 = 1.0;
            coef.alpha1 = -1.0;
            coef.alpha2 = 0.0;
            coef.alpha3 = 0.0;
            coef.beta0 = dt_n;
        }

        spdlog::trace("Using uniform BDF coefficients (insufficient history)");
        return coef;
    }
}

// ====== Phase A: Initialization helper methods (refactoring) ======

bool TransientSolver::check_has_dynamic_devices() const {
    // Check if circuit has dynamic devices (C, L)
    for (const auto& device : circuit_.get_devices()) {
        if (device->type() == DeviceType::CAPACITOR ||
            device->type() == DeviceType::INDUCTOR) {
            return true;
        }
    }
    return false;
}

void TransientSolver::initialize_uic_mode(TransientResult& result, Real t_start,
                                          DenseVector& solution) {
    // UIC mode: Skip DC operating point, start from zero (for step response analysis)
    // If .IC is specified, use those initial conditions instead of zero
    spdlog::debug("UIC mode: initializing voltages and currents");

    // Reset all device states to initial values
    for (const auto& device : circuit_.get_devices()) {
        device->reset_state();
    }

    // Initialize with zeros first
    std::fill(solution.begin(), solution.end(), 0.0);

    DenseVector initial_voltages(circuit_.num_nodes(), 0.0);

    // Apply .IC initial conditions
    if (!config_.ic.empty()) {
        spdlog::info("Applying .IC initial conditions for {} nodes", config_.ic.size());

        for (const auto& [node_name, voltage] : config_.ic) {
            try {
                NodeId node_id = circuit_.get_node_id(node_name);

                // Skip ground node (node 0)
                if (node_id == NODE_GND) {
                    spdlog::warn(".IC V({})={} ignored (ground node must be 0V)",
                                node_name, voltage);
                    continue;
                }

                // Set initial voltage in full voltage vector (includes GND)
                if (node_id < initial_voltages.size()) {
                    initial_voltages[node_id] = voltage;
                    spdlog::debug(".IC V({})={:.3f}V applied to node {}",
                                 node_name, voltage, node_id);
                }

                // Set in solution vector (excludes GND)
                // MNA system: solution[0..num_nodes-1] are node voltages (excluding GND)
                // NodeId 1 -> solution[0], NodeId 2 -> solution[1], etc.
                std::uint32_t num_nodes = circuit_.num_nodes() - 1;
                if (node_id - 1 < num_nodes) {
                    solution[node_id - 1] = voltage;
                }
            } catch (const std::exception& e) {
                spdlog::warn(".IC V({})={} failed: {}", node_name, voltage, e.what());
            }
        }
    } else {
        spdlog::debug("No .IC specified, using zero initial conditions");
    }

    // Let devices accept initial solution
    for (const auto& device : circuit_.get_devices()) {
        device->accept_solution(initial_voltages);
    }

    // Save initial state (t=t_start)
    TimePoint initial_tp;
    initial_tp.time = t_start;
    initial_tp.node_voltages.resize(mna_.num_nodes);
    initial_tp.branch_currents.resize(mna_.num_branches);

    // Copy initial voltages (excluding GND)
    for (std::uint32_t i = 0; i < mna_.num_nodes; ++i) {
        initial_tp.node_voltages[i] = initial_voltages[i + 1];  // Skip node 0 (GND)
    }
    std::fill(initial_tp.branch_currents.begin(), initial_tp.branch_currents.end(), 0.0);

    result.timepoints.push_back(initial_tp);

    if (!config_.ic.empty()) {
        spdlog::debug("Initial conditions (t={}) set from .IC directive", t_start);
    } else {
        spdlog::debug("Initial conditions (t={}) set to zero (UIC mode)", t_start);
    }
}

bool TransientSolver::initialize_dc_mode(TransientResult& result, Real t_start,
                                         DenseVector& solution) {
    // Standard mode: Solve DC operating point for initial conditions
    // (Inductors treated as short circuits, capacitors as open circuits)
    spdlog::debug("Dynamic devices detected: solving DC operating point for initial conditions");

    // Reset all device states before DC solve
    for (const auto& device : circuit_.get_devices()) {
        device->reset_state();
    }

    SolverStats dc_stats;
    ConvergenceStatus dc_status = dc_solver_->solve(solution, dc_stats);

    if (dc_status != ConvergenceStatus::SUCCESS) {
        spdlog::error("Failed to solve DC operating point for initial conditions");
        result.status = dc_status;
        return false;
    }

    result.stats.total_iterations += dc_stats.total_iterations;

    // Extract node voltages from DC solution
    // Map MNA indices to node IDs: MNA index i → Node ID (i+1), GND → 0
    DenseVector node_voltages(mna_.num_nodes + 1);
    node_voltages[0] = 0.0;  // GND is always 0V
    for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
        node_voltages[i + 1] = solution[i];  // MNA index → Node ID
    }

    // Initialize device states with DC solution
    for (const auto& device : circuit_.get_devices()) {
        device->accept_solution(node_voltages);
    }

    // Save initial state (t=t_start) with DC operating point
    TimePoint initial_tp;
    initial_tp.time = t_start;
    initial_tp.node_voltages.resize(mna_.num_nodes);
    for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
        initial_tp.node_voltages[i] = solution[i];  // Exclude GND
    }
    initial_tp.branch_currents.resize(mna_.num_branches);

    // Extract branch currents from DC solution
    for (std::size_t i = 0; i < mna_.num_branches; ++i) {
        initial_tp.branch_currents[i] = solution[mna_.num_nodes + i];
    }

    result.timepoints.push_back(initial_tp);

    spdlog::debug("Initial conditions (t={}) set from DC operating point", t_start);
    return true;
}

bool TransientSolver::initialize_pure_dc_mode(TransientResult& result, Real t_start,
                                              DenseVector& solution, Real timestep) {
    // Pure DC circuit: solve DC operating point for t=0
    spdlog::debug("No dynamic devices: solving DC operating point for t=0");

    std::uint32_t init_iterations = 0;
    ConvergenceStatus init_status = solve_timestep(solution, timestep, init_iterations,
                                                   IntegrationMethod::BACKWARD_EULER);

    if (init_status != ConvergenceStatus::SUCCESS) {
        spdlog::error("Failed to solve initial DC operating point");
        result.status = init_status;
        return false;
    }

    result.stats.total_iterations += init_iterations;

    // Accept solution to update device states
    accept_timestep_solution(solution);

    // Save initial state (t=t_start) with DC operating point
    TimePoint initial_tp;
    initial_tp.time = t_start;
    initial_tp.node_voltages.resize(mna_.num_nodes);
    initial_tp.branch_currents.resize(mna_.num_branches);

    for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
        initial_tp.node_voltages[i] = solution[i];
    }
    for (std::size_t i = 0; i < mna_.num_branches; ++i) {
        initial_tp.branch_currents[i] = solution[mna_.num_nodes + i];
    }

    result.timepoints.push_back(initial_tp);

    spdlog::debug("Initial conditions (t={}) solved with DC operating point ({} iterations)",
                 t_start, init_iterations);
    return true;
}

TransientResult TransientSolver::initialize_simulation(Real t_start, Real timestep) {
    TransientResult result;
    result.status = ConvergenceStatus::SUCCESS;
    result.stats = SolverStats{};

    DenseVector solution;
    solution.resize(mna_.size());

    bool has_dynamic = check_has_dynamic_devices();

    if (has_dynamic) {
        // Check if UIC (Use Initial Conditions) mode is enabled
        if (config_.transient.use_initial_conditions) {
            initialize_uic_mode(result, t_start, solution);
        } else {
            if (!initialize_dc_mode(result, t_start, solution)) {
                return result;  // Failed, status already set
            }
        }
    } else {
        if (!initialize_pure_dc_mode(result, t_start, solution, timestep)) {
            return result;  // Failed, status already set
        }
    }

    return result;
}

// ============================================================================
// Phase B: Timestepping loop methods
// ============================================================================

void TransientSolver::run_adaptive_timestepping(TransientResult& result, DenseVector& solution,
                                                Real& t, Real t_stop, Real& dt, std::uint32_t& step_count) {
    while (t < t_stop) {
        // Check ITL5 limit (max timesteps)
        if (step_count >= config_.transient.max_timesteps) {
            spdlog::error("Transient analysis exceeded ITL5 limit (max_timesteps={})",
                         config_.transient.max_timesteps);
            result.status = ConvergenceStatus::MAX_ITERATIONS;
            return;
        }

        Real dt_attempt = std::min(dt, t_stop - t);
        std::uint32_t attempt = 0;

        while (true) {
            // Set current time for this timestep (solving for t + dt_attempt)
            current_time_ = t + dt_attempt;

            spdlog::trace("Timestep {} (attempt {}): t={}, dt_attempt={}",
                          step_count + 1, attempt + 1, t, dt_attempt);

            // Adaptive timestep control with LTE estimation
            std::uint32_t iter_count = 0;
            auto [status, accepted_dt, next_dt, accepted] =
                solve_timestep_adaptive(solution, dt_attempt, iter_count,
                                       result.stats.timestep_rejects);

            result.stats.total_iterations += iter_count;

            if (status != ConvergenceStatus::SUCCESS) {
                // NR failed: try a smaller timestep before bailing out
                if (dt_attempt <= config_.transient.min_step || attempt >= 10) {
                    spdlog::error("Timestep {} failed (t={}, dt={:.3e}) after {} attempts: status={}",
                                 step_count + 1, t, dt_attempt, attempt + 1, static_cast<int>(status));
                    result.status = status;
                    return;
                }

                dt_attempt = std::max(config_.transient.min_step, dt_attempt * 0.5);
                attempt++;
                continue;
            }

            if (accepted) {
                // Timestep accepted
                t += accepted_dt;
                step_count++;

                // Accept solution (already updated in solve_timestep_adaptive)
                accept_timestep_solution(solution);

                // Update timestep history for variable-timestep BDF
                if (timestep_history_.size() >= 3) {
                    timestep_history_.erase(timestep_history_.begin());
                }
                timestep_history_.push_back(accepted_dt);
                spdlog::trace("Timestep history updated: size={}, last={:.3e}",
                             timestep_history_.size(), accepted_dt);

                // Save timepoint
                TimePoint tp;
                tp.time = t;
                tp.node_voltages.resize(mna_.num_nodes);
                tp.branch_currents.resize(mna_.num_branches);

                for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
                    tp.node_voltages[i] = solution[i];
                }
                for (std::size_t i = 0; i < mna_.num_branches; ++i) {
                    tp.branch_currents[i] = solution[mna_.num_nodes + i];
                }

                result.timepoints.push_back(tp);

                // Update timestep for next iteration
                dt = next_dt;

                if (step_count % 100 == 0) {
                    spdlog::debug("Completed {} timesteps (t={}, rejects={})",
                                 step_count, t, result.stats.timestep_rejects);
                }
                break; // exit attempt loop
            } else {
                // Rejected based on LTE: continue with updated dt_attempt (=next_dt)
                dt_attempt = std::max(config_.transient.min_step, next_dt);
                attempt++;
            }
        }
    }
}

void TransientSolver::run_fixed_timestepping(TransientResult& result, DenseVector& solution,
                                             Real& t, Real t_stop, Real dt, std::uint32_t& step_count) {
    while (t < t_stop) {
        // Check ITL5 limit (max timesteps)
        if (step_count >= config_.transient.max_timesteps) {
            spdlog::error("Transient analysis exceeded ITL5 limit (max_timesteps={})",
                         config_.transient.max_timesteps);
            result.status = ConvergenceStatus::MAX_ITERATIONS;
            return;
        }

        Real dt_target = std::min(dt, t_stop - t);
        Real dt_attempt = dt_target;
        std::uint32_t attempt = 0;
        ConvergenceStatus status = ConvergenceStatus::SUCCESS;
        std::uint32_t iter_count = 0;
        bool tried_backwards = false;  // try a lower-order fallback before shrinking dt

        while (true) {
            // Set current time for this timestep (solving for t + dt_attempt)
            current_time_ = t + dt_attempt;

            spdlog::trace("Timestep {} (attempt {}): t={}, dt_attempt={}",
                          step_count + 1, attempt + 1, t, dt_attempt);

            // Save device states before attempting this timestep
            save_device_states();

            // Use configured integration method (with warm-up fallback)
            IntegrationMethod method = config_.transient.method;
            if (tried_backwards) {
                method = IntegrationMethod::BACKWARD_EULER; // conservative fallback
            } else if (method == IntegrationMethod::TRAPEZOIDAL ||
                       method == IntegrationMethod::TR_BDF2) {
                // The trapezoidal rule uses the device currents of the previous
                // point. At the first point they come only from the initial
                // conditions (a capacitor started with UIC has current 0), so
                // the first step is a backward-Euler step, as for BDF2 below.
                if (history_count_ < 1) method = IntegrationMethod::BACKWARD_EULER;
            } else if (method == IntegrationMethod::GEAR2) {
                if (history_count_ < 2) method = IntegrationMethod::BACKWARD_EULER;
            } else if (method == IntegrationMethod::GEAR3) {
                if (history_count_ < 1) {
                    method = IntegrationMethod::BACKWARD_EULER;
                } else if (history_count_ < 3) {
                    method = IntegrationMethod::GEAR2;
                }
            }

            // Solve for this timestep
            status = solve_timestep(solution, dt_attempt, iter_count, method);
            result.stats.total_iterations += iter_count;

            if (status == ConvergenceStatus::SUCCESS) {
                break; // accept
            }

            // Retry with fallback or smaller timestep if allowed
            restore_device_states();

            // First, try a lower-order BACKWARD_EULER solve at the same dt before shrinking
            if (!tried_backwards && method != IntegrationMethod::BACKWARD_EULER) {
                tried_backwards = true;
                continue;
            }

            if (dt_attempt <= config_.transient.min_step || attempt >= 15) {
                spdlog::error("Timestep {} failed to converge (t={}, dt={:.3e}) after {} attempts",
                              step_count + 1, t, dt_attempt, attempt + 1);
                result.status = status;
                return;
            }

            dt_attempt = std::max(config_.transient.min_step, dt_attempt * 0.5);
            attempt++;
            tried_backwards = false; // reset fallback for new smaller dt
        }

        // Accept solution and update device histories
        accept_timestep_solution(solution);
        step_count++;

        // Maintain timestep history for variable-step BDF support
        if (timestep_history_.size() >= 3) {
            timestep_history_.erase(timestep_history_.begin());
        }
        timestep_history_.push_back(dt_attempt);

        // Advance time
        t += dt_attempt;

        // Save timepoint
        TimePoint tp;
        tp.time = t;
        tp.node_voltages.resize(mna_.num_nodes);
        tp.branch_currents.resize(mna_.num_branches);

        for (std::size_t i = 0; i < mna_.num_nodes; ++i) {
            tp.node_voltages[i] = solution[i];
        }
        for (std::size_t i = 0; i < mna_.num_branches; ++i) {
            tp.branch_currents[i] = solution[mna_.num_nodes + i];
        }

        result.timepoints.push_back(tp);

        // Track accepted step count for LTE warm-up/order control
        update_history_count();

        if (step_count % 100 == 0) {
            spdlog::debug("Completed {} timesteps (t={})", step_count, t);
        }
    }
}

bool TransientSolver::run_timestepping_loop(TransientResult& result, Real t_start,
                                             Real t_stop, Real timestep) {
    // Get solution from initial timepoint
    DenseVector solution(result.timepoints[0].node_voltages.size() +
                        result.timepoints[0].branch_currents.size());
    for (std::size_t i = 0; i < result.timepoints[0].node_voltages.size(); ++i) {
        solution[i] = result.timepoints[0].node_voltages[i];
    }
    for (std::size_t i = 0; i < result.timepoints[0].branch_currents.size(); ++i) {
        solution[result.timepoints[0].node_voltages.size() + i] =
            result.timepoints[0].branch_currents[i];
    }

    // Time-stepping loop initialization
    Real t = t_start;
    current_time_ = t_start;  // Initialize current time
    std::uint32_t step_count = 0;

    // Determine timestep strategy
    bool use_adaptive = config_.transient.use_adaptive_timestep;
    Real dt = use_adaptive ? config_.transient.initial_step : timestep;

    spdlog::info("Time-stepping: {} (dt={})",
                 use_adaptive ? "adaptive" : "fixed", dt);

    // Run appropriate timestepping loop
    if (use_adaptive) {
        run_adaptive_timestepping(result, solution, t, t_stop, dt, step_count);
    } else {
        run_fixed_timestepping(result, solution, t, t_stop, dt, step_count);
    }

    // Check if loop completed successfully
    return result.status == ConvergenceStatus::SUCCESS;
}

// Transient temperature sweep implementation
ConvergenceStatus TransientSolver::solve_temp_sweep(Real t_start, Real t_stop, Real timestep,
                                                      TransientTemperatureSweepResult& result) {
    using namespace std::chrono;
    auto start_time = high_resolution_clock::now();

    // Preserve original temperature to restore after sweep
    const Real original_temp = config_.temperature;

    spdlog::info("Starting Transient Temperature Sweep: {} temperature points, t=[{}, {}], dt={}",
                config_.temp_sweep.temperatures.size(), t_start, t_stop, timestep);

    // Save original temperature to restore after sweep
    const Real original_temperature = config_.temperature;

    // Initialize result
    result.points.clear();
    result.status = ConvergenceStatus::SUCCESS;

    // Temperature sweep loop
    for (std::size_t i = 0; i < config_.temp_sweep.temperatures.size(); ++i) {
        Real temp = config_.temp_sweep.temperatures[i];

        // Create a new configuration with the current temperature
        SimulationConfig temp_config = config_;
        temp_config.temperature = temp;

        // Update temperature for all temperature-dependent devices
        apply_temperature_to_devices(circuit_, temp);

        // Reset state of all dynamic devices (Capacitor, Inductor)
        // This clears charge/flux history to prevent accumulation across temperature points
        for (const auto& device : circuit_.get_devices()) {
            if (device->is_dynamic()) {
                device->reset_state();
            }
        }

        spdlog::debug("Temperature Sweep: Point {}/{} T={:.2f}K ({:.2f}°C)",
                     i + 1, config_.temp_sweep.temperatures.size(),
                     temp, temp - 273.15);

        // Create a new TransientSolver for this temperature
        // (This ensures clean state for each temperature point)
        TransientSolver temp_solver(circuit_, temp_config);

        // CRITICAL: Reset device states again after TransientSolver construction
        // The constructor calls reset_state(), but we need to ensure a clean state
        // before solve() is called, since solve() may modify device states during
        // DC operating point calculation
        for (const auto& device : circuit_.get_devices()) {
            if (device->is_dynamic()) {
                device->reset_state();
            }
        }

        // Run transient analysis at this temperature
        TransientResult tran_result = temp_solver.solve(t_start, t_stop, timestep);

        // Store results
        TransientTemperatureSweepPoint point;
        point.temperature = temp;
        point.result = std::move(tran_result);

        result.points.push_back(std::move(point));

        // Check convergence
        if (tran_result.status != ConvergenceStatus::SUCCESS) {
            spdlog::warn("Temperature Sweep: Transient analysis failed at T={:.2f}K ({:.2f}°C)",
                        temp, temp - 273.15);
            result.status = tran_result.status;
            // Continue to next temperature (don't fail entire sweep)
        }

        spdlog::debug("Temperature Sweep: Point {}/{} T={:.2f}K completed",
                     i + 1, config_.temp_sweep.temperatures.size(), temp);
    }

    // Restore original temperature to avoid polluting subsequent analyses
    apply_temperature_to_devices(circuit_, original_temperature);
    spdlog::debug("Temperature Sweep: Restored original temperature {:.2f}K ({:.2f}°C)",
                 original_temperature, original_temperature - 273.15);

    auto end_time = high_resolution_clock::now();
    std::chrono::duration<Real> elapsed = end_time - start_time;

    spdlog::info("Transient Temperature Sweep completed: {} points ({:.3f} ms)",
                result.points.size(), elapsed.count() * 1000.0);

    // Restore device temperatures to original value to avoid contaminating subsequent analyses
    apply_temperature_to_devices(circuit_, original_temp);

    return result.all_converged() ? ConvergenceStatus::SUCCESS : result.status;
}

} // namespace kirimaspice::solvers
