// src/solvers/dc_solver.cpp
#include "kirimaspice/solvers/dc_solver.hpp"

// Step 2.4: Reduced device includes - only include what's directly used
#include "kirimaspice/devices/device.hpp"          // Base class for polymorphism
#include "kirimaspice/devices/voltage_source.hpp"  // Source scaling/sweep
#include "kirimaspice/devices/current_source.hpp"  // Source scaling
#include "kirimaspice/devices/vcvs.hpp"            // Branch current extraction
#include "kirimaspice/devices/ccvs.hpp"            // Branch current extraction
#include "kirimaspice/devices/bjt_npn.hpp"         // Darlington detection
#include "kirimaspice/solvers/temperature_utils.hpp"
#include "kirimaspice/solvers/parallel_stamper.hpp"
#include "kirimaspice/core/constants.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <set>
#include <limits>

// OpenMP for parallel device stamping
#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice::solvers {

DCSolver::DCSolver(const circuit::CircuitGraph& circuit,
                   const SimulationConfig& config)
    : circuit_(circuit)
    , config_(config) {

    // Detect presence of nonlinear devices (Voltage limiting can be relaxed for purely linear circuits)
    has_nonlinear_devices_ = false;
    for (const auto& device : circuit_.get_devices()) {
        if (!device->is_linear()) {
            has_nonlinear_devices_ = true;
            break;
        }
    }

    // Calculate MNA system size
    std::uint32_t num_nodes = circuit_.num_nodes() - 1;  // Exclude GND
    std::uint32_t num_branches = 0;
    std::uint32_t extra_nodes = 0;

    // Count devices that need branch current variables
    // - Voltage sources (independent and time-varying)
    // - Voltage-controlled voltage sources (VCVS)
    // - Current-controlled voltage sources (CCVS)
    for (const auto& device : circuit_.get_devices()) {
        const auto type = device->type();
        if (type == DeviceType::VSOURCE ||
            type == DeviceType::VCVS ||
            type == DeviceType::CCVS) {
            num_branches++;
        }
    }

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

    // Resize MNA system (append internal nodes after external nodes)
    mna_.resize(total_nodes, num_branches);


    spdlog::info("DCSolver created: {} nodes, {} branches, total size {}",
                 total_nodes, num_branches, mna_.size());

    // Create linear solver backend (ENV or default)
    lin_solver_ = make_solver(choose_backend());
}

DCSolver::~DCSolver() = default;

void DCSolver::initialize_solution(DenseVector& x) const {
    if (use_initial_guess_override_ && initial_guess_override_.size() == mna_.size()) {
        x = initial_guess_override_;
        spdlog::debug("Using override initial guess ({} variables)", x.size());
        return;
    }
    // Initialize with zeros first
    x.resize(mna_.size());
    std::fill(x.begin(), x.end(), 0.0);

    // Apply .NODESET initial guesses
    // NODESET provides user-specified initial guesses to improve DC convergence
    if (!config_.nodeset.empty()) {
        spdlog::info("Applying .NODESET initial guesses for {} nodes", config_.nodeset.size());

        for (const auto& [node_name, voltage] : config_.nodeset) {
            try {
                NodeId node_id = circuit_.get_node_id(node_name);

                // Skip ground node (node 0)
                if (node_id == NODE_GND) {
                    spdlog::warn(".NODESET V({})={} ignored (ground node must be 0V)",
                                node_name, voltage);
                    continue;
                }

                // MNA system: x[0..num_nodes-1] are node voltages (excluding GND)
                // NodeId 1 -> x[0], NodeId 2 -> x[1], etc.
                std::uint32_t num_nodes = circuit_.num_nodes() - 1;
                if (node_id - 1 < num_nodes) {
                    x[node_id - 1] = voltage;
                    spdlog::debug(".NODESET V({})={:.3f}V applied to x[{}]",
                                 node_name, voltage, node_id - 1);
                } else {
                    spdlog::warn(".NODESET V({})={} failed: NodeId {} out of range",
                                node_name, voltage, node_id);
                }
            } catch (const std::exception& e) {
                spdlog::warn(".NODESET V({})={} failed: {}", node_name, voltage, e.what());
            }
        }
    }

    // Smart initial value estimation for BJT circuits
    // Apply to all BJT circuits to prevent numerical instability
    // when base-emitter voltage difference is too large in initial guess

    // Check if circuit has BJT devices
    bool has_bjt = false;
    for (const auto& device : circuit_.get_devices()) {
        if (device->type() == DeviceType::BJT_NPN || device->type() == DeviceType::BJT_PNP) {
            has_bjt = true;
            break;
        }
    }

    if (!has_bjt) {
        spdlog::debug("No BJT devices found, using zero initial values");
        return;
    }

    // Find Vcc: look for voltage sources with positive DC value connected to GND
    Real vcc = 0.0;
    for (const auto& device : circuit_.get_devices()) {
        if (device->type() == DeviceType::VSOURCE) {
            auto* vsrc = dynamic_cast<const devices::VoltageSource*>(device.get());
            if (vsrc && vsrc->nn() == 0 && vsrc->dc_value() > 0.0) {
                vcc = std::max(vcc, vsrc->dc_value());
            }
        }
    }

    // If we found a supply voltage, set smart initial values for BJTs in amplifier circuits
    if (vcc > 0.0) {
        spdlog::debug("AC amplifier detected, Vcc = {:.2f}V, setting smart BJT initial values", vcc);

        // Collect all BJT_NPN devices
        std::vector<const devices::BJT_NPN*> bjts;
        for (const auto& device : circuit_.get_devices()) {
            if (device->type() == DeviceType::BJT_NPN) {
                auto* bjt = dynamic_cast<const devices::BJT_NPN*>(device.get());
                if (bjt) {
                    bjts.push_back(bjt);
                }
            }
        }

        // Detect Darlington pairs: Q1.emitter == Q2.base
        std::set<const devices::BJT_NPN*> processed;

        for (size_t i = 0; i < bjts.size(); ++i) {
            if (processed.count(bjts[i])) continue;

            const auto* q1 = bjts[i];
            NodeId ne1 = q1->emitter();
            NodeId nb1 = q1->base();
            NodeId nc1 = q1->collector();

            // Check if this is part of a Darlington pair
            const devices::BJT_NPN* q2 = nullptr;
            for (size_t j = 0; j < bjts.size(); ++j) {
                if (i == j) continue;
                if (bjts[j]->base() == ne1) {
                    q2 = bjts[j];
                    break;
                }
            }

            if (q2) {
                // Darlington pair detected: Q1 drives Q2
                NodeId ne2 = q2->emitter();
                NodeId nc2 = q2->collector();

                spdlog::debug("Darlington pair detected: {} (Q1) drives {} (Q2)",
                             q1->name(), q2->name());

                // Set consistent initial values for Darlington pair
                // Q1: Vbe1 = 0.7V, Ve1 = 0.7V → Vb1 = 1.4V
                // Q2: Vbe2 = 0.7V, Ve2 = 0.0V → Vb2 = 0.7V (= Ve1)

                if (nb1 > 0 && nb1 <= mna_.num_nodes) {
                    x[nb1 - 1] = 1.4;  // Q1 base
                }
                if (ne1 > 0 && ne1 <= mna_.num_nodes) {
                    x[ne1 - 1] = 0.7;  // Q1 emitter = Q2 base
                }
                if (nc1 > 0 && nc1 <= mna_.num_nodes) {
                    x[nc1 - 1] = vcc / 2.0;  // Q1 collector (if not tied to Vcc)
                }
                if (ne2 > 0 && ne2 <= mna_.num_nodes) {
                    x[ne2 - 1] = 0.0;  // Q2 emitter (will be adjusted by Re)
                }
                if (nc2 > 0 && nc2 <= mna_.num_nodes) {
                    x[nc2 - 1] = vcc / 2.0;  // Q2 collector
                }

                spdlog::debug("Darlington Q1 {}: Vb={:.2f}V, Vc={:.2f}V, Ve={:.2f}V",
                             q1->name(),
                             (nb1 > 0 && nb1 <= mna_.num_nodes) ? x[nb1 - 1] : 0.0,
                             (nc1 > 0 && nc1 <= mna_.num_nodes) ? x[nc1 - 1] : 0.0,
                             (ne1 > 0 && ne1 <= mna_.num_nodes) ? x[ne1 - 1] : 0.0);
                spdlog::debug("Darlington Q2 {}: Vb={:.2f}V, Vc={:.2f}V, Ve={:.2f}V",
                             q2->name(),
                             (ne1 > 0 && ne1 <= mna_.num_nodes) ? x[ne1 - 1] : 0.0,
                             (nc2 > 0 && nc2 <= mna_.num_nodes) ? x[nc2 - 1] : 0.0,
                             (ne2 > 0 && ne2 <= mna_.num_nodes) ? x[ne2 - 1] : 0.0);

                processed.insert(q1);
                processed.insert(q2);
            } else {
                // Regular BJT (not part of Darlington pair)
                // Only set initial values for BJT with emitter resistor (not grounded emitter)
                if (ne1 > 0 && ne1 <= mna_.num_nodes) {
                    std::uint32_t ne_idx = ne1 - 1;
                    x[ne_idx] = 0.1;  // Small voltage drop across Re

                    if (nb1 > 0 && nb1 <= mna_.num_nodes) {
                        std::uint32_t nb_idx = nb1 - 1;
                        x[nb_idx] = 0.7;  // Typical Vbe for forward-active
                    }
                    if (nc1 > 0 && nc1 <= mna_.num_nodes) {
                        std::uint32_t nc_idx = nc1 - 1;
                        x[nc_idx] = vcc / 2.0;  // Mid-supply bias point
                    }

                    spdlog::debug("BJT {}: Vb={:.2f}V, Vc={:.2f}V, Ve={:.2f}V (initial guess)",
                                 q1->name(),
                                 (nb1 > 0 && nb1 <= mna_.num_nodes) ? x[nb1 - 1] : 0.0,
                                 (nc1 > 0 && nc1 <= mna_.num_nodes) ? x[nc1 - 1] : 0.0,
                                 x[ne_idx]);

                    processed.insert(q1);
                } else {
                    spdlog::debug("BJT {} has grounded emitter, skipping smart initialization", q1->name());
                    processed.insert(q1);
                }
            }
        }
    } else {
        spdlog::debug("No supply voltage found, using zero initial values");
    }
}

void DCSolver::build_mna_system(const DenseVector& node_voltages) {
    // Zero the MNA system
    mna_.zero();

    // Stamp all devices
    stamp_devices(node_voltages);

    spdlog::debug("Built MNA system: nnz={}", mna_.G.nnz());
}

void DCSolver::stamp_devices(const DenseVector& node_voltages) {
    const auto& devices = circuit_.get_devices();

    // Step 2.3: Use unified ParallelDeviceStamper
    // benchmark results: threshold=10000 based on empirical data
    ParallelDeviceStamper::execute(
        devices, mna_, thread_local_mna_mgr_,
        [&](const devices::Device& dev, MNASystem& mna) {
            if (dev.is_linear()) {
                dev.stamp_dc_linear(mna, node_voltages);
            } else {
                // CRITICAL FIX: Stamp gmin first for numerical stability
                // This prevents singular matrix when nonlinear devices (BJT/diode)
                // are in cut-off state
                dev.stamp_gmin(mna, node_voltages);
                dev.stamp_dc_nonlinear(mna, node_voltages);
            }
        },
        {.threshold = 10000, .enable_profiling = true, .name = "DC"}
    );
}

void DCSolver::compute_residual(const DenseVector& x, DenseVector& f) const {
    // Residual: f = Gx - b
    f.resize(x.size());
    std::fill(f.begin(), f.end(), 0.0);

    // Compute Gx using COO format
    for (std::size_t k = 0; k < mna_.G.values.size(); ++k) {
        std::uint32_t i = mna_.G.rows[k];
        std::uint32_t j = mna_.G.cols[k];
        Real value = mna_.G.values[k];

        if (i < x.size() && j < x.size()) {
            f[i] += value * x[j];
        }
    }

    // Subtract b
    for (std::size_t i = 0; i < std::min(f.size(), mna_.b.size()); ++i) {
        f[i] -= mna_.b[i];
    }
}

bool DCSolver::check_convergence(const DenseVector& x_new,
                                 const DenseVector& x_old,
                                 const DenseVector& f_new) const {
    // Step 2.1: Delegate to ConvergenceChecker
    const auto& dc_config = config_.dc;

    // Build ConvergenceConfig from SimulationConfig
    ConvergenceConfig conv_config;
    conv_config.abstol = dc_config.abstol;
    conv_config.reltol = dc_config.reltol;
    conv_config.vntol = dc_config.vntol;
    conv_config.check_residual = true;  // DC mode: check residual
    conv_config.relax_for_linear = true;
    conv_config.linear_relax_factor = 1e6;
    conv_config.stability_threshold = dc_config.vntol * 1e-3;  // Very tight threshold
    conv_config.branch_tol_multiplier = 1e3;

    // Create checker with current num_nodes
    ConvergenceChecker checker(mna_.num_nodes);

    return checker.check(x_new, x_old, conv_config, &f_new);
}

ConvergenceStatus DCSolver::solve_standard(DenseVector& solution, SolverStats& stats) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Initialize statistics
    stats = SolverStats{};

    // Initialize solution
    DenseVector x_old;
    initialize_solution(x_old);

    spdlog::info("Starting DC analysis (max_iter={})", config_.dc.max_iterations);

    const bool dynamic_gmin_enabled = config_.dc.enable_dynamic_gmin;
    Real dynamic_gmin_value = std::clamp(config_.dc.dynamic_gmin_start,
                                         config_.dc.dynamic_gmin_min,
                                         config_.dc.dynamic_gmin_max);
    if (!std::isfinite(dynamic_gmin_value) || dynamic_gmin_value < 0.0) {
        dynamic_gmin_value = 0.0;
    }
    Real dynamic_prev_residual = std::numeric_limits<Real>::infinity();
    std::uint32_t dynamic_stall_counter = 0;

    // Newton-Raphson iteration
    for (std::uint32_t iter = 0; iter < config_.dc.max_iterations; ++iter) {
        stats.total_iterations++;

        // Build MNA system for current operating point
        build_mna_system(x_old);
        stats.jacobian_evals++;

        if (dynamic_gmin_enabled) {
            Real applied_gmin = std::max(config_.gmin, dynamic_gmin_value);
            if (applied_gmin > 0.0) {
                add_gmin_to_nodes(applied_gmin);
                spdlog::debug("Dynamic Gmin applied (iter {}): {:.3e} S", iter, applied_gmin);
            }
        }

        // Debug: dump matrix for iter 1
        if (iter == 1) {
            spdlog::trace("=== MNA Matrix Dump (Iter 1) ===");
            spdlog::trace("System size: {}", mna_.G.n_rows);
            spdlog::trace("b vector:");
            for (std::size_t i = 0; i < std::min(mna_.b.size(), size_t(6)); ++i) {
                spdlog::trace("  b[{}] = {:.6e}", i, mna_.b[i]);
            }

            // Dump relevant G matrix rows (base row 2 and collector row 3)
            spdlog::trace("G matrix (base row 2):");
            for (std::size_t k = 0; k < mna_.G.nnz(); ++k) {
                if (mna_.G.rows[k] == 2 && mna_.G.cols[k] <= 5) {
                    spdlog::trace("  G[{}][{}] = {:.6e}",
                                 mna_.G.rows[k], mna_.G.cols[k], mna_.G.values[k]);
                }
            }
            spdlog::trace("G matrix (collector row 3, ALL columns):");
            for (std::size_t k = 0; k < mna_.G.nnz(); ++k) {
                if (mna_.G.rows[k] == 3) {
                    spdlog::trace("  G[{}][{}] = {:.6e}",
                                 mna_.G.rows[k], mna_.G.cols[k], mna_.G.values[k]);
                }
            }
        }

        // Factorize and solve
        bool factorize_ok;
        if (iter == 0) {
            // First iteration: analyze and factorize
            factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
        } else {
            // Subsequent iterations: refactorize with same pattern
            factorize_ok = lin_solver_->refactorize(mna_.G);

            // If refactorize fails due to sparsity pattern change, fall back to full analysis
            if (!factorize_ok) {
                spdlog::info("Refactorize failed ({}), falling back to analyze_and_factorize",
                            lin_solver_->last_error());
                factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
                if (factorize_ok) {
                    spdlog::info("Fallback to analyze_and_factorize successful");
                } else {
                    spdlog::error("Fallback to analyze_and_factorize also failed: {}",
                                 lin_solver_->last_error());
                }
            }
        }

        if (!factorize_ok) {
            spdlog::error("Matrix factorization failed: {}",
                         lin_solver_->last_error());
            return ConvergenceStatus::SINGULAR_MATRIX;
        }

        // Solve for Newton update: G * dx = -f = b - G*x_old
        // Since we have G*x = b, we solve G*x_new = b
        DenseVector x_new;
        bool solve_ok = lin_solver_->solve(mna_.b, x_new);
        stats.linear_solves++;

        if (!solve_ok) {
            spdlog::error("Linear solve failed: {}", lin_solver_->last_error());
            return ConvergenceStatus::NUMERICAL_ERROR;
        }

        // Debug: dump solution for iter 1
        if (iter == 1) {
            spdlog::trace("=== Solution Vector (Iter 1) ===");
            for (std::size_t i = 0; i < std::min(x_new.size(), size_t(6)); ++i) {
                spdlog::trace("  x[{}] = {:.6e}", i, x_new[i]);
            }

            // Write debug files for external verification (only if env var is set)
            if (std::getenv("KIRIMASPICE_DEBUG_DUMP")) {
                dump_mna_system_to_files(x_new);
            }
        }

        // Compute residual for convergence check
        DenseVector f_new;
        compute_residual(x_new, f_new);
        stats.function_evals++;

        // Log iteration info
        Real max_residual = 0.0;
        for (Real r : f_new) {
            max_residual = std::max(max_residual, std::abs(r));
        }

        // Voltage limiting: Limit individual node voltage changes to prevent extreme steps
        // Relax rules:
        //  - Purely線形回路では無効化（早期収束のため）
        //  - 非線形回路でも上限を 5V/step に緩和して高電圧バイアスでの停滞を防ぐ
        //  - Dynamic Gmin 有効時は従来どおり無効化
        const bool small_system = mna_.size() <= 4; // tiny circuits converge stably without limiting
        const bool voltage_limit_enabled = !dynamic_gmin_enabled && has_nonlinear_devices_ && !small_system;

        // Relax voltage limiting after several iterations to avoid stagnation at high bias
        constexpr std::uint32_t VOLT_LIMIT_RELAX_ITER = 10;
        const bool relax_limit = (iter >= VOLT_LIMIT_RELAX_ITER);

        const Real max_voltage_step = (voltage_limit_enabled && !relax_limit)
            ? 5.0
            : std::numeric_limits<Real>::infinity();
        Real max_delta_unscaled = 0.0;
        for (std::size_t i = 0; i < x_new.size(); ++i) {
            max_delta_unscaled = std::max(max_delta_unscaled, std::abs(x_new[i] - x_old[i]));
        }
        spdlog::debug("  Newton step: max_delta={:.3e} V, threshold={:.3e} V", max_delta_unscaled, max_voltage_step);
        if (max_delta_unscaled > max_voltage_step) {
            const Real scale = max_voltage_step / max_delta_unscaled;
            spdlog::info("  Voltage limiting: Scaling Newton step by {:.6f} (max_delta={:.3e} V)",
                         scale, max_delta_unscaled);
            for (std::size_t i = 0; i < x_new.size(); ++i) {
                x_new[i] = x_old[i] + scale * (x_new[i] - x_old[i]);
            }
        }

        Real max_change = 0.0;
        for (std::size_t i = 0; i < x_new.size(); ++i) {
            max_change = std::max(max_change, std::abs(x_new[i] - x_old[i]));
        }

        spdlog::info("Iter {}: max_residual={:.3e}, max_change={:.3e}",
                     iter, max_residual, max_change);

        // DEBUG: Log node-by-node voltages and changes for iterations 10-20
        if (iter >= 10 && iter <= 20) {
            spdlog::info("  Node voltages at iter {} (num_nodes={}):", iter, x_new.size());
            for (std::size_t i = 0; i < x_new.size(); ++i) {
                Real delta = x_new[i] - x_old[i];
                spdlog::info("    Node {}: V={:10.6f}, ΔV={:+.6e}",
                            i + 1, x_new[i], delta);
            }
        }

        if (dynamic_gmin_enabled) {
            const Real improvement_threshold = std::clamp(config_.dc.dynamic_gmin_improvement_threshold, 0.0, 0.99);
            bool residual_improved = std::isfinite(dynamic_prev_residual) ?
                (max_residual < dynamic_prev_residual * (1.0 - improvement_threshold)) : true;

            if (max_residual < config_.dc.dynamic_gmin_residual_target) {
                residual_improved = true;
            }

            if (residual_improved) {
                dynamic_stall_counter = 0;
                Real min_allowed = std::max(config_.dc.dynamic_gmin_min, 0.0);
                if (dynamic_gmin_value > min_allowed) {
                    Real decreased = dynamic_gmin_value * config_.dc.dynamic_gmin_decrease_factor;
                    Real new_value = std::max(min_allowed, decreased);
                    if (new_value < dynamic_gmin_value) {
                        dynamic_gmin_value = new_value;
                        spdlog::debug("Dynamic Gmin decreased to {:.3e} S", dynamic_gmin_value);
                    }
                }
            } else {
                dynamic_stall_counter++;
                std::uint32_t stall_limit = std::max<std::uint32_t>(1, config_.dc.dynamic_gmin_stall_iterations);
                if (dynamic_stall_counter >= stall_limit &&
                    dynamic_gmin_value < config_.dc.dynamic_gmin_max) {
                    Real base = dynamic_gmin_value;
                    if (base <= 0.0) {
                        base = std::max({config_.dc.dynamic_gmin_min, config_.gmin, 1e-15});
                    }
                    Real increased = base * config_.dc.dynamic_gmin_increase_factor;
                    // Protect against overflow and non-finite values
                    if (!std::isfinite(increased) || increased > config_.dc.dynamic_gmin_max) {
                        increased = config_.dc.dynamic_gmin_max;
                    }
                    Real new_value = std::min(config_.dc.dynamic_gmin_max, increased);
                    if (new_value > dynamic_gmin_value) {
                        dynamic_gmin_value = new_value;
                        spdlog::debug("Dynamic Gmin increased to {:.3e} S after {} stalled iterations",
                                      dynamic_gmin_value, dynamic_stall_counter);
                    }
                    dynamic_stall_counter = 0;
                }
            }

            dynamic_prev_residual = max_residual;
        }

        // Check convergence
        if (check_convergence(x_new, x_old, f_new)) {
            // Converged!
            solution = x_new;
            last_solution_ = x_new;  // Store for branch current extraction

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<Real> elapsed = end_time - start_time;
            stats.total_time_seconds = elapsed.count();

            spdlog::info("DC analysis converged in {} iterations ({:.3f} ms)",
                        iter + 1, stats.total_time_seconds * 1000.0);

            return ConvergenceStatus::SUCCESS;
        }

        // Update for next iteration
        x_old = x_new;
    }

    // Failed to converge
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<Real> elapsed = end_time - start_time;
    stats.total_time_seconds = elapsed.count();

    spdlog::warn("DC analysis failed to converge after {} iterations",
                config_.dc.max_iterations);

    return ConvergenceStatus::MAX_ITERATIONS;
}

ConvergenceStatus DCSolver::solve(DenseVector& solution, SolverStats& stats) {
    // Apply global temperature before starting analysis (only if explicitly set via .TEMP or .OPTIONS TEMP)
    // This preserves device-level temperature settings when no global temperature is specified
    if (config_.temperature_explicitly_set) {
        apply_temperature_to_devices(circuit_, config_.temperature);
    }

    // Check if source stepping is enabled
    if (config_.dc.enable_source_stepping) {
        return solve_with_source_stepping(solution, stats);
    }

    // Check if Gmin stepping is enabled
    if (config_.dc.enable_gmin_stepping) {
        return solve_with_gmin_stepping(solution, stats);
    }

    if (config_.dc.enable_continuation_method && !config_.dc.enable_source_stepping) {
        auto status = solve_with_continuation(solution, stats);
        if (status == ConvergenceStatus::SUCCESS) {
            return status;
        }
    }

    auto status = solve_standard(solution, stats);
    if (status != ConvergenceStatus::SUCCESS && config_.dc.enable_pseudo_transient) {
        auto fallback = solve_with_pseudo_transient(solution, stats);
        if (fallback == ConvergenceStatus::SUCCESS) {
            return fallback;
        }
    }

    return status;
}

void DCSolver::dump_mna_system_to_files(const DenseVector& x_new) const {
    // Write COO format
    std::ofstream coo_file("debug_coo.txt");
    if (coo_file.is_open()) {
        coo_file << "# COO Format: row col value\n";
        coo_file << "# Matrix size: " << mna_.G.n_rows << " x " << mna_.G.n_cols << "\n";
        coo_file << "# Number of entries: " << mna_.G.nnz() << "\n";
        for (std::size_t k = 0; k < mna_.G.nnz(); ++k) {
            coo_file << mna_.G.rows[k] << " " << mna_.G.cols[k] << " "
                    << std::scientific << std::setprecision(15) << mna_.G.values[k] << "\n";
        }
        coo_file.close();
        spdlog::info("Wrote debug_coo.txt");
    }

    // Write CSC format
    std::vector<std::uint32_t> col_ptr, row_idx;
    std::vector<Real> values_csc;
    mna_.G.to_csc(col_ptr, row_idx, values_csc);

    std::ofstream csc_file("debug_csc.txt");
    if (csc_file.is_open()) {
        csc_file << "# CSC Format\n";
        csc_file << "# Matrix size: " << mna_.G.n_rows << " x " << mna_.G.n_cols << "\n";
        csc_file << "# Number of nonzeros: " << values_csc.size() << "\n\n";

        csc_file << "# col_ptr (length = n_cols + 1 = " << col_ptr.size() << ")\n";
        for (std::size_t i = 0; i < col_ptr.size(); ++i) {
            csc_file << col_ptr[i];
            if (i < col_ptr.size() - 1) csc_file << " ";
        }
        csc_file << "\n\n";

        csc_file << "# row_idx (length = nnz = " << row_idx.size() << ")\n";
        for (std::size_t i = 0; i < row_idx.size(); ++i) {
            csc_file << row_idx[i];
            if (i < row_idx.size() - 1) csc_file << " ";
        }
        csc_file << "\n\n";

        csc_file << "# values (length = nnz = " << values_csc.size() << ")\n";
        csc_file << std::scientific << std::setprecision(15);
        for (std::size_t i = 0; i < values_csc.size(); ++i) {
            csc_file << values_csc[i];
            if (i < values_csc.size() - 1) csc_file << " ";
        }
        csc_file << "\n";
        csc_file.close();
        spdlog::info("Wrote debug_csc.txt");
    }

    // Write b vector
    std::ofstream b_file("debug_b.txt");
    if (b_file.is_open()) {
        b_file << "# RHS vector b (length = " << mna_.b.size() << ")\n";
        b_file << std::scientific << std::setprecision(15);
        for (std::size_t i = 0; i < mna_.b.size(); ++i) {
            b_file << mna_.b[i] << "\n";
        }
        b_file.close();
        spdlog::info("Wrote debug_b.txt");
    }

    // Write x vector
    std::ofstream x_file("debug_x.txt");
    if (x_file.is_open()) {
        x_file << "# Solution vector x (length = " << x_new.size() << ")\n";
        x_file << std::scientific << std::setprecision(15);
        for (std::size_t i = 0; i < x_new.size(); ++i) {
            x_file << x_new[i] << "\n";
        }
        x_file.close();
        spdlog::info("Wrote debug_x.txt");
    }
}

// Source Stepping support

void DCSolver::set_source_scale_factor(Real scale) {
    // Set scale factor for all independent voltage and current sources
    for (const auto& device : circuit_.get_devices()) {
        if (device->type() == DeviceType::VSOURCE) {
            auto* vsrc = dynamic_cast<devices::VoltageSource*>(device.get());
            if (vsrc) {
                vsrc->set_scale_factor(scale);
            }
        } else if (device->type() == DeviceType::ISOURCE) {
            auto* isrc = dynamic_cast<devices::CurrentSource*>(device.get());
            if (isrc) {
                isrc->set_scale_factor(scale);
            }
        }
    }
}

ConvergenceStatus DCSolver::solve_with_source_stepping(DenseVector& solution, SolverStats& stats) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Initialize statistics
    stats = SolverStats{};

    spdlog::info("Starting DC analysis with Source Stepping");
    spdlog::info("  Steps: {}", config_.dc.source_stepping_steps);
    spdlog::info("  Start fraction: {:.2f}% ({:.4f})",
                config_.dc.source_stepping_start * 100.0, config_.dc.source_stepping_start);

    // Calculate step increments (logarithmic spacing is more robust)
    std::vector<Real> scale_factors;
    Real start_scale = config_.dc.source_stepping_start;
    Real end_scale = 1.0;

    // Generate scale factors (logarithmic)
    std::uint32_t num_steps = config_.dc.source_stepping_steps;
    for (std::uint32_t i = 0; i < num_steps; ++i) {
        // Logarithmic interpolation: log(scale) = log(start) + i/(n-1) * (log(end) - log(start))
        Real log_scale = std::log(start_scale) +
                        static_cast<Real>(i) / (num_steps - 1) *
                        (std::log(end_scale) - std::log(start_scale));
        scale_factors.push_back(std::exp(log_scale));
    }
    scale_factors.back() = 1.0;  // Ensure final step is exactly 1.0

    // Initialize solution vector
    DenseVector x_current;
    initialize_solution(x_current);

    // Source stepping loop
    for (std::uint32_t step = 0; step < num_steps; ++step) {
        Real scale = scale_factors[step];

        spdlog::info("Source Stepping: Step {}/{}, scale={:.4f}",
                    step + 1, num_steps, scale);

        // Set source scale factor
        set_source_scale_factor(scale);

        // Solve DC operating point with current scale factor
        DenseVector x_old = x_current;  // Use previous solution as initial guess

        // Newton-Raphson iteration for this step
        // ITL6: Use source_stepping_max_iterations if set, otherwise use ITL1 (max_iterations)
        std::uint32_t max_iter = (config_.dc.source_stepping_max_iterations > 0)
                                ? config_.dc.source_stepping_max_iterations
                                : config_.dc.max_iterations;
        bool step_converged = false;
        for (std::uint32_t iter = 0; iter < max_iter; ++iter) {
            stats.total_iterations++;

            // Build MNA system for current operating point
            build_mna_system(x_old);
            stats.jacobian_evals++;

            // Factorize and solve
            bool factorize_ok;
            if (iter == 0 && step == 0) {
                // First iteration of first step: analyze and factorize
                factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
            } else {
                // Subsequent iterations: refactorize with same pattern
                factorize_ok = lin_solver_->refactorize(mna_.G);

                // If refactorize fails due to sparsity pattern change, fall back to full analysis
                if (!factorize_ok) {
                    spdlog::debug("Refactorize failed, falling back to analyze_and_factorize");
                    factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
                }
            }

            if (!factorize_ok) {
                spdlog::error("Matrix factorization failed: {}", lin_solver_->last_error());
                // Restore scale factor to 1.0 before returning
                set_source_scale_factor(1.0);
                return ConvergenceStatus::SINGULAR_MATRIX;
            }

            // Solve linear system
            DenseVector x_new;
            bool solve_ok = lin_solver_->solve(mna_.b, x_new);
            stats.linear_solves++;

            if (!solve_ok) {
                spdlog::error("Linear solve failed: {}", lin_solver_->last_error());
                // Restore scale factor to 1.0 before returning
                set_source_scale_factor(1.0);
                return ConvergenceStatus::NUMERICAL_ERROR;
            }

            // Compute residual for convergence check
            DenseVector f_new;
            compute_residual(x_new, f_new);
            stats.function_evals++;

            // Log iteration info
            Real max_residual = 0.0;
            for (Real r : f_new) {
                max_residual = std::max(max_residual, std::abs(r));
            }

            Real max_change = 0.0;
            for (std::size_t i = 0; i < x_new.size(); ++i) {
                max_change = std::max(max_change, std::abs(x_new[i] - x_old[i]));
            }

            spdlog::debug("  Iter {}: max_residual={:.3e}, max_change={:.3e}",
                         iter, max_residual, max_change);

            // Check convergence
            if (check_convergence(x_new, x_old, f_new)) {
                // Converged for this step
                x_current = x_new;
                step_converged = true;
                spdlog::info("  Step {} converged in {} iterations", step + 1, iter + 1);
                break;
            }

            // Update for next iteration
            x_old = x_new;
        }

        if (!step_converged) {
            spdlog::warn("Source Stepping: Step {} failed to converge", step + 1);
            // Restore scale factor to 1.0 before returning
            set_source_scale_factor(1.0);
            return ConvergenceStatus::MAX_ITERATIONS;
        }
    }

    // Source stepping completed successfully
    solution = x_current;
    last_solution_ = x_current;  // Store for branch current extraction

    // Restore scale factor to 1.0
    set_source_scale_factor(1.0);

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<Real> elapsed = end_time - start_time;
    stats.total_time_seconds = elapsed.count();

    spdlog::info("Source Stepping converged in {} total iterations ({:.3f} ms)",
                stats.total_iterations, stats.total_time_seconds * 1000.0);

    return ConvergenceStatus::SUCCESS;
}

// Gmin Stepping support

void DCSolver::add_gmin_to_nodes(Real gmin) {
    // Add Gmin conductance from each node (except GND) to ground
    // This adds gmin to the diagonal of the conductance matrix G[i,i]
    const std::uint32_t num_nodes = circuit_.num_nodes() - 1; // Exclude GND

    for (std::uint32_t i = 0; i < num_nodes; ++i) {
        // Add Gmin to diagonal: G[i,i] += gmin
        // add_entry() automatically sums duplicates
        mna_.G.add_entry(i, i, gmin);
    }
}

ConvergenceStatus DCSolver::solve_with_gmin_stepping(DenseVector& solution, SolverStats& stats) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Initialize statistics
    stats = SolverStats{};

    spdlog::info("Starting DC analysis with Gmin Stepping");
    spdlog::info("  Steps: {}", config_.dc.gmin_stepping_steps);
    spdlog::info("  Gmin range: {:.3e} S -> {:.3e} S",
                config_.dc.gmin_start, config_.dc.gmin_end);

    // Calculate Gmin values (logarithmic spacing from start to end)
    std::vector<Real> gmin_values;
    Real gmin_start = config_.dc.gmin_start;
    Real gmin_end = config_.dc.gmin_end;

    // Generate Gmin values (logarithmic)
    std::uint32_t num_steps = config_.dc.gmin_stepping_steps;
    for (std::uint32_t i = 0; i < num_steps; ++i) {
        // Logarithmic interpolation: log(gmin) = log(start) + i/(n-1) * (log(end) - log(start))
        Real log_gmin = std::log(gmin_start) +
                       static_cast<Real>(i) / (num_steps - 1) *
                       (std::log(gmin_end) - std::log(gmin_start));
        gmin_values.push_back(std::exp(log_gmin));
    }
    gmin_values.back() = gmin_end;  // Ensure final step is exactly gmin_end

    // Initialize solution vector
    DenseVector x_current;
    initialize_solution(x_current);

    // Gmin stepping loop
    for (std::uint32_t step = 0; step < num_steps; ++step) {
        Real gmin = gmin_values[step];

        spdlog::info("Gmin Stepping: Step {}/{}, Gmin={:.3e} S",
                    step + 1, num_steps, gmin);

        // Solve DC operating point with current Gmin value
        DenseVector x_old = x_current;  // Use previous solution as initial guess

        // Newton-Raphson iteration for this step
        // ITL6: Use source_stepping_max_iterations if set, otherwise use ITL1 (max_iterations)
        std::uint32_t max_iter = (config_.dc.source_stepping_max_iterations > 0)
                                ? config_.dc.source_stepping_max_iterations
                                : config_.dc.max_iterations;
        bool step_converged = false;
        for (std::uint32_t iter = 0; iter < max_iter; ++iter) {
            stats.total_iterations++;

            // Build MNA system for current operating point
            build_mna_system(x_old);
            stats.jacobian_evals++;

            // Add Gmin to all nodes (this is the key difference from standard solve)
            add_gmin_to_nodes(gmin);

            // Factorize and solve
            bool factorize_ok;
            if (iter == 0 && step == 0) {
                // First iteration of first step: analyze and factorize
                factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
            } else {
                // Subsequent iterations: refactorize with same pattern
                factorize_ok = lin_solver_->refactorize(mna_.G);

                // If refactorize fails due to sparsity pattern change, fall back to full analysis
                if (!factorize_ok) {
                    spdlog::debug("Refactorize failed, falling back to analyze_and_factorize");
                    factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
                }
            }

            if (!factorize_ok) {
                spdlog::error("Matrix factorization failed: {}", lin_solver_->last_error());
                return ConvergenceStatus::SINGULAR_MATRIX;
            }

            // Solve linear system
            DenseVector x_new;
            bool solve_ok = lin_solver_->solve(mna_.b, x_new);
            stats.linear_solves++;

            if (!solve_ok) {
                spdlog::error("Linear solve failed: {}", lin_solver_->last_error());
                return ConvergenceStatus::NUMERICAL_ERROR;
            }

            // Compute residual for convergence check (without Gmin contribution)
            DenseVector f_new;
            compute_residual(x_new, f_new);
            stats.function_evals++;

            // Log iteration info
            Real max_residual = 0.0;
            for (Real r : f_new) {
                max_residual = std::max(max_residual, std::abs(r));
            }

            Real max_change = 0.0;
            for (std::size_t i = 0; i < x_new.size(); ++i) {
                max_change = std::max(max_change, std::abs(x_new[i] - x_old[i]));
            }

            spdlog::debug("  Iter {}: max_residual={:.3e}, max_change={:.3e}",
                         iter, max_residual, max_change);

            // Check convergence
            if (check_convergence(x_new, x_old, f_new)) {
                // Converged for this step
                x_current = x_new;
                step_converged = true;
                spdlog::info("  Step {} converged in {} iterations", step + 1, iter + 1);
                break;
            }

            // Update for next iteration
            x_old = x_new;
        }

        if (!step_converged) {
            spdlog::warn("Gmin Stepping: Step {} failed to converge", step + 1);
            return ConvergenceStatus::MAX_ITERATIONS;
        }
    }

    // Gmin stepping completed successfully
    solution = x_current;
    last_solution_ = x_current;  // Store for branch current extraction

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<Real> elapsed = end_time - start_time;
    stats.total_time_seconds = elapsed.count();

    spdlog::info("Gmin Stepping converged in {} total iterations ({:.3f} ms)",
                stats.total_iterations, stats.total_time_seconds * 1000.0);

    return ConvergenceStatus::SUCCESS;
}

ConvergenceStatus DCSolver::solve_with_continuation(DenseVector& solution, SolverStats& stats) {
    spdlog::info("Starting DC analysis with continuation method");
    bool original_source = config_.dc.enable_source_stepping;
    auto original_steps = config_.dc.source_stepping_steps;
    auto original_start = config_.dc.source_stepping_start;

    config_.dc.enable_source_stepping = true;
    config_.dc.source_stepping_steps = std::max<std::uint32_t>(2, config_.dc.continuation_steps);
    config_.dc.source_stepping_start = std::clamp(config_.dc.continuation_start, 0.0, 1.0);

    auto status = solve_with_source_stepping(solution, stats);

    config_.dc.enable_source_stepping = original_source;
    config_.dc.source_stepping_steps = original_steps;
    config_.dc.source_stepping_start = original_start;

    return status;
}

void DCSolver::apply_pseudo_capacitance_terms(Real capacitance, Real timestep, const DenseVector& prev_state) {
    if (capacitance <= 0.0 || timestep <= 0.0) {
        return;
    }

    Real coeff = capacitance / timestep;
    std::uint32_t num_nodes = mna_.num_nodes;
    for (std::uint32_t i = 0; i < num_nodes; ++i) {
        mna_.G.add_entry(i, i, coeff);
        if (i < mna_.b.size() && i < prev_state.size()) {
            mna_.b[i] += coeff * prev_state[i];
        }
    }
}

ConvergenceStatus DCSolver::solve_with_pseudo_transient(DenseVector& solution, SolverStats& stats) {
    spdlog::info("Starting pseudo-transient fallback (dt={:.3e}, steps={})",
                 config_.dc.pseudo_transient_dt, config_.dc.pseudo_transient_steps);

    DenseVector state = solution;
    if (state.empty()) {
        initialize_solution(state);
    }

    DenseVector prev_state = state;
    Real dt = std::max(config_.dc.pseudo_transient_dt, 1e-15);
    Real cap = std::max(config_.dc.pseudo_transient_capacitance, 1e-18);

    for (std::uint32_t step = 0; step < config_.dc.pseudo_transient_steps; ++step) {
        build_mna_system(prev_state);
        apply_pseudo_capacitance_terms(cap, dt, prev_state);

        bool factorize_ok;
        if (step == 0) {
            factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
        } else {
            factorize_ok = lin_solver_->refactorize(mna_.G);
            if (!factorize_ok) {
                factorize_ok = lin_solver_->analyze_and_factorize(mna_.G);
            }
        }
        if (!factorize_ok) {
            spdlog::warn("Pseudo-transient factorization failed: {}", lin_solver_->last_error());
            return ConvergenceStatus::SINGULAR_MATRIX;
        }

        DenseVector x_new;
        if (!lin_solver_->solve(mna_.b, x_new)) {
            spdlog::warn("Pseudo-transient linear solve failed: {}", lin_solver_->last_error());
            return ConvergenceStatus::NUMERICAL_ERROR;
        }
        prev_state = x_new;
    }

    set_initial_guess_override(prev_state);
    auto original_max_iter = config_.dc.max_iterations;
    config_.dc.max_iterations = std::max<std::uint32_t>(original_max_iter, 25u);
    auto status = solve_standard(solution, stats);
    config_.dc.max_iterations = original_max_iter;
    clear_initial_guess_override();
    return status;
}

// ============================================================================
// DC Sweep analysis
// ============================================================================

bool DCSolver::set_source_value(const std::string& source_name, Real value) {
    // Search for voltage or current source by name
    for (const auto& device : circuit_.get_devices()) {
        if (device->name() == source_name) {
            // Check if it's a voltage source
            if (auto* vsrc = dynamic_cast<devices::VoltageSource*>(device.get())) {
                vsrc->set_dc_value(value);
                return true;
            }
            // Check if it's a current source
            if (auto* isrc = dynamic_cast<devices::CurrentSource*>(device.get())) {
                isrc->set_dc_value(value);
                return true;
            }
        }
    }
    return false;  // Source not found
}

Real DCSolver::get_original_source_value(const std::string& source_name) const {
    // Search for voltage or current source by name
    for (const auto& device : circuit_.get_devices()) {
        if (device->name() == source_name) {
            // Check if it's a voltage source
            if (auto* vsrc = dynamic_cast<devices::VoltageSource*>(device.get())) {
                return vsrc->dc_value();
            }
            // Check if it's a current source
            if (auto* isrc = dynamic_cast<devices::CurrentSource*>(device.get())) {
                return isrc->dc_value();
            }
        }
    }
    return 0.0;  // Source not found, return 0
}

void DCSolver::set_initial_guess_override(const DenseVector& guess) {
    initial_guess_override_ = guess;
    use_initial_guess_override_ = true;
}

void DCSolver::clear_initial_guess_override() {
    use_initial_guess_override_ = false;
    initial_guess_override_.clear();
}

ConvergenceStatus DCSolver::solve_sweep(const DCSweepConfig& config, DCSweepResult& result) {
    using namespace std::chrono;
    auto start_time = high_resolution_clock::now();

    // Apply temperature before sweeping (only if explicitly set)
    if (config_.temperature_explicitly_set) {
        apply_temperature_to_devices(circuit_, config_.temperature);
    }

    // Check if this is a nested (2D) sweep
    if (config.nested_sweep) {
        spdlog::info("Starting DC Sweep analysis (2D): primary={} [{}, {}, step={}], nested={} [{}, {}, step={}]",
                    config.source_name, config.start_value, config.stop_value, config.step_value,
                    config.nested_sweep->source_name,
                    config.nested_sweep->start_value, config.nested_sweep->stop_value, config.nested_sweep->step_value);

        // Save original source values
        Real original_primary = get_original_source_value(config.source_name);
        Real original_nested = get_original_source_value(config.nested_sweep->source_name);

        // Initialize result
        result.points.clear();
        result.status = ConvergenceStatus::SUCCESS;
        result.total_iterations = 0;

        // Check if both sources exist
        if (!set_source_value(config.source_name, config.start_value)) {
            spdlog::error("DC Sweep: Primary source '{}' not found", config.source_name);
            result.status = ConvergenceStatus::NUMERICAL_ERROR;
            return ConvergenceStatus::NUMERICAL_ERROR;
        }
        if (!set_source_value(config.nested_sweep->source_name, config.nested_sweep->start_value)) {
            spdlog::error("DC Sweep: Nested source '{}' not found", config.nested_sweep->source_name);
            result.status = ConvergenceStatus::NUMERICAL_ERROR;
            return ConvergenceStatus::NUMERICAL_ERROR;
        }

        // Generate sweep points for both axes
        std::size_t num_primary = config.num_points();
        std::size_t num_nested = config.nested_sweep->num_points();
        Real primary_dir = (config.stop_value >= config.start_value) ? 1.0 : -1.0;
        Real nested_dir = (config.nested_sweep->stop_value >= config.nested_sweep->start_value) ? 1.0 : -1.0;

        // Pre-allocate result array
        std::size_t total_points = num_primary * num_nested;
        result.points.resize(total_points);

#if 0  // DISABLED: 2D DC Sweep parallelization (same reason as 1D - sequential dependencies)
        // Parallelize primary sweep, sequential nested sweep
        spdlog::debug("DC Sweep 2D: Using OpenMP parallelization for primary sweep");

        #pragma omp parallel for schedule(dynamic)
        for (std::size_t i = 0; i < num_primary; ++i) {
            Real primary_value = config.start_value + primary_dir * std::abs(config.step_value) * i;

            // Clamp to stop value
            if (primary_dir > 0 && primary_value > config.stop_value) {
                primary_value = config.stop_value;
            } else if (primary_dir < 0 && primary_value < config.stop_value) {
                primary_value = config.stop_value;
            }

            // Thread-local solver instance
            DCSolver thread_solver(circuit_, config_);
            thread_solver.set_source_value(config.source_name, primary_value);

            DenseVector x_prev;  // Previous solution for initial guess (within nested sweep)

            // Inner loop: Nested sweep (sequential for each primary value)
            for (std::size_t j = 0; j < num_nested; ++j) {
                Real nested_value = config.nested_sweep->start_value + nested_dir * std::abs(config.nested_sweep->step_value) * j;

                // Clamp to stop value
                if (nested_dir > 0 && nested_value > config.nested_sweep->stop_value) {
                    nested_value = config.nested_sweep->stop_value;
                } else if (nested_dir < 0 && nested_value < config.nested_sweep->stop_value) {
                    nested_value = config.nested_sweep->stop_value;
                }

                thread_solver.set_source_value(config.nested_sweep->source_name, nested_value);

                // Solve DC operating point
                DenseVector solution;
                SolverStats stats;

                // Use previous solution as initial guess
                if (!x_prev.empty()) {
                    thread_solver.initialize_solution(solution);
                    solution = x_prev;
                }

                // ITL2: Use dc_sweep_max_iterations if set
                std::uint32_t original_max_iter = thread_solver.config_.dc.max_iterations;
                if (thread_solver.config_.dc.dc_sweep_max_iterations > 0) {
                    thread_solver.config_.dc.max_iterations = thread_solver.config_.dc.dc_sweep_max_iterations;
                }

                ConvergenceStatus status = thread_solver.solve(solution, stats);

                // Restore original max_iterations
                thread_solver.config_.dc.max_iterations = original_max_iter;

                // Store result point (2D, thread-safe direct indexing)
                DCSweepPoint point;
                point.sweep_value = primary_value;
                point.nested_sweep_value = nested_value;
                std::size_t node_count = std::min<std::size_t>(thread_solver.mna_.num_nodes, solution.size());
                point.node_voltages.assign(solution.begin(), solution.begin() + node_count);
                point.branch_currents = thread_solver.extract_all_branch_currents(solution);
                point.status = status;
                point.iterations = stats.total_iterations;

                std::size_t point_idx = i * num_nested + j;
                result.points[point_idx] = std::move(point);

                // Update total iterations (atomic)
                #pragma omp atomic
                result.total_iterations += stats.total_iterations;

                if (status != ConvergenceStatus::SUCCESS) {
                    spdlog::warn("DC Sweep: Failed to converge at ({}, {}) = ({}, {})",
                                config.source_name, config.nested_sweep->source_name,
                                primary_value, nested_value);
                    #pragma omp critical
                    {
                        result.status = status;
                    }
                }

                x_prev = solution;

                spdlog::trace("DC Sweep 2D: Point ({},{}) / ({},{}) value=({}, {}) converged in {} iterations",
                            i + 1, j + 1, num_primary, num_nested,
                            primary_value, nested_value, stats.total_iterations);
            }
        }
#else
        // Sequential fallback (no OpenMP)
        DenseVector x_prev;  // Previous solution for initial guess

        // Outer loop: Primary sweep
        for (std::size_t i = 0; i < num_primary; ++i) {
            Real primary_value = config.start_value + primary_dir * std::abs(config.step_value) * i;

            // Clamp to stop value
            if (primary_dir > 0 && primary_value > config.stop_value) {
                primary_value = config.stop_value;
            } else if (primary_dir < 0 && primary_value < config.stop_value) {
                primary_value = config.stop_value;
            }

            set_source_value(config.source_name, primary_value);

            // Inner loop: Nested sweep
            for (std::size_t j = 0; j < num_nested; ++j) {
                Real nested_value = config.nested_sweep->start_value + nested_dir * std::abs(config.nested_sweep->step_value) * j;

                // Clamp to stop value
                if (nested_dir > 0 && nested_value > config.nested_sweep->stop_value) {
                    nested_value = config.nested_sweep->stop_value;
                } else if (nested_dir < 0 && nested_value < config.nested_sweep->stop_value) {
                    nested_value = config.nested_sweep->stop_value;
                }

                set_source_value(config.nested_sweep->source_name, nested_value);

                // Solve DC operating point
                DenseVector solution;
                SolverStats stats;

                // Use previous solution as initial guess
                if (!x_prev.empty()) {
                    initialize_solution(solution);
                    solution = x_prev;
                }

                // ITL2: Use dc_sweep_max_iterations if set
                std::uint32_t original_max_iter = config_.dc.max_iterations;
                if (config_.dc.dc_sweep_max_iterations > 0) {
                    config_.dc.max_iterations = config_.dc.dc_sweep_max_iterations;
                }

                ConvergenceStatus status = solve(solution, stats);

                // Restore original max_iterations
                config_.dc.max_iterations = original_max_iter;

                // Store result point (2D)
                DCSweepPoint point;
                point.sweep_value = primary_value;
                point.nested_sweep_value = nested_value;
                std::size_t node_count = std::min<std::size_t>(mna_.num_nodes, solution.size());
                point.node_voltages.assign(solution.begin(), solution.begin() + node_count);
                point.branch_currents = extract_all_branch_currents(solution);
                point.status = status;
                point.iterations = stats.total_iterations;

                std::size_t point_idx = i * num_nested + j;
                result.points[point_idx] = std::move(point);
                result.total_iterations += stats.total_iterations;

                if (status != ConvergenceStatus::SUCCESS) {
                    spdlog::warn("DC Sweep: Failed to converge at ({}, {}) = ({}, {})",
                                config.source_name, config.nested_sweep->source_name,
                                primary_value, nested_value);
                    result.status = status;
                }

                x_prev = solution;

                spdlog::debug("DC Sweep 2D: Point ({},{}) / ({},{}) value=({}, {}) converged in {} iterations",
                            i + 1, j + 1, num_primary, num_nested,
                            primary_value, nested_value, stats.total_iterations);
            }
        }
#endif

        // Restore original source values
        set_source_value(config.source_name, original_primary);
        set_source_value(config.nested_sweep->source_name, original_nested);

        auto end_time = high_resolution_clock::now();
        std::chrono::duration<Real> elapsed = end_time - start_time;

        spdlog::info("DC Sweep 2D completed: {} points ({}x{}), {} total iterations ({:.3f} ms)",
                    result.points.size(), num_primary, num_nested,
                    result.total_iterations, elapsed.count() * 1000.0);

        return result.all_converged() ? ConvergenceStatus::SUCCESS : result.status;
    }

    // 1D Sweep (original implementation)
    spdlog::info("Starting DC Sweep analysis: source={} range=[{}, {}] step={}",
                config.source_name, config.start_value, config.stop_value, config.step_value);

    // Save original source value
    Real original_value = get_original_source_value(config.source_name);

    // Initialize result
    result.points.clear();
    result.status = ConvergenceStatus::SUCCESS;
    result.total_iterations = 0;

    // Check if source exists
    if (!set_source_value(config.source_name, config.start_value)) {
        spdlog::error("DC Sweep: Source '{}' not found", config.source_name);
        result.status = ConvergenceStatus::NUMERICAL_ERROR;
        return ConvergenceStatus::NUMERICAL_ERROR;
    }

    // Generate sweep points
    std::size_t num_points = config.num_points();
    Real direction = (config.stop_value >= config.start_value) ? 1.0 : -1.0;

    // Pre-allocate result array
    result.points.resize(num_points);

    // DC Sweep parallelization is DISABLED due to strong sequential dependencies
    // Each sweep point uses the previous solution as initial guess for faster convergence.
    // Parallelizing would break this dependency chain and cause poor convergence.
    // Decision: Keep DC Sweep sequential (unlike Parameter Sweep and Monte Carlo which are embarrassingly parallel)

#if 0  // DISABLED: DC Sweep parallelization
    // Determine chunk size based on number of threads and points
    int num_threads = omp_get_max_threads();
    std::size_t chunk_size = std::max<std::size_t>(1, (num_points + num_threads - 1) / num_threads);
    std::size_t num_chunks = (num_points + chunk_size - 1) / chunk_size;

    spdlog::debug("DC Sweep: Using {} threads, {} chunks of size {}", num_threads, num_chunks, chunk_size);

    // Parallel execution of chunks
    #pragma omp parallel
    {
        // Each thread processes its assigned chunks
        #pragma omp for schedule(dynamic)
        for (std::size_t chunk_idx = 0; chunk_idx < num_chunks; ++chunk_idx) {
            std::size_t chunk_start = chunk_idx * chunk_size;
            std::size_t chunk_end = std::min(chunk_start + chunk_size, num_points);

            // Thread-local solver instance (avoid race conditions)
            DCSolver thread_solver(circuit_, config_);
            DenseVector x_prev;  // Previous solution for initial guess (within chunk)

            // Sequential execution within chunk
            for (std::size_t i = chunk_start; i < chunk_end; ++i) {
                Real sweep_value = config.start_value + direction * std::abs(config.step_value) * i;

                // Clamp to stop value
                if (direction > 0 && sweep_value > config.stop_value) {
                    sweep_value = config.stop_value;
                } else if (direction < 0 && sweep_value < config.stop_value) {
                    sweep_value = config.stop_value;
                }

                // Set source to current sweep value (thread-local copy)
                thread_solver.set_source_value(config.source_name, sweep_value);

                // Solve DC operating point
                DenseVector solution;
                SolverStats stats;

                // Use previous solution as initial guess (within chunk only)
                if (!x_prev.empty()) {
                    thread_solver.initialize_solution(solution);
                    solution = x_prev;
                }

                // ITL2: Use dc_sweep_max_iterations if set
                std::uint32_t original_max_iter = thread_solver.config_.dc.max_iterations;
                if (thread_solver.config_.dc.dc_sweep_max_iterations > 0) {
                    thread_solver.config_.dc.max_iterations = thread_solver.config_.dc.dc_sweep_max_iterations;
                }

                ConvergenceStatus status = thread_solver.solve(solution, stats);

                // Restore original max_iterations
                thread_solver.config_.dc.max_iterations = original_max_iter;

                // Store result point (thread-safe direct indexing)
                DCSweepPoint point;
                point.sweep_value = sweep_value;
                point.nested_sweep_value = std::nullopt;
                std::size_t node_count = std::min<std::size_t>(thread_solver.mna_.num_nodes, solution.size());
                point.node_voltages.assign(solution.begin(), solution.begin() + node_count);
                point.branch_currents = thread_solver.extract_all_branch_currents(solution);
                point.status = status;
                point.iterations = stats.total_iterations;

                result.points[i] = std::move(point);

                // Update total iterations (requires atomic operation)
                #pragma omp atomic
                result.total_iterations += stats.total_iterations;

                if (status != ConvergenceStatus::SUCCESS) {
                    spdlog::warn("DC Sweep: Failed to converge at sweep value {}", sweep_value);
                    #pragma omp critical
                    {
                        result.status = status;
                    }
                }

                // Save solution for next point's initial guess (within chunk)
                x_prev = solution;

                spdlog::trace("DC Sweep: Point {}/{} value={} converged in {} iterations (chunk {})",
                             i + 1, num_points, sweep_value, stats.total_iterations, chunk_idx);
            }
        }
    }
#else
    // Sequential fallback (no OpenMP)
    DenseVector x_prev;  // Previous solution for initial guess

    for (std::size_t i = 0; i < num_points; ++i) {
        Real sweep_value = config.start_value + direction * std::abs(config.step_value) * i;

        // Clamp to stop value
        if (direction > 0 && sweep_value > config.stop_value) {
            sweep_value = config.stop_value;
        } else if (direction < 0 && sweep_value < config.stop_value) {
            sweep_value = config.stop_value;
        }

        set_source_value(config.source_name, sweep_value);

        // Solve DC operating point
        DenseVector solution;
        SolverStats stats;

        // Use previous solution as initial guess
        if (!x_prev.empty()) {
            initialize_solution(solution);
            solution = x_prev;
        }

        // ITL2: Use dc_sweep_max_iterations if set
        std::uint32_t original_max_iter = config_.dc.max_iterations;
        if (config_.dc.dc_sweep_max_iterations > 0) {
            config_.dc.max_iterations = config_.dc.dc_sweep_max_iterations;
        }

        ConvergenceStatus status = solve(solution, stats);

        // Restore original max_iterations
        config_.dc.max_iterations = original_max_iter;

        // Store result point
        DCSweepPoint point;
        point.sweep_value = sweep_value;
        point.nested_sweep_value = std::nullopt;
        std::size_t node_count = std::min<std::size_t>(mna_.num_nodes, solution.size());
        point.node_voltages.assign(solution.begin(), solution.begin() + node_count);
        point.branch_currents = extract_all_branch_currents(solution);
        point.status = status;
        point.iterations = stats.total_iterations;

        result.points[i] = std::move(point);
        result.total_iterations += stats.total_iterations;

        if (status != ConvergenceStatus::SUCCESS) {
            spdlog::warn("DC Sweep: Failed to converge at sweep value {}", sweep_value);
            result.status = status;
        }

        x_prev = solution;

        spdlog::debug("DC Sweep: Point {}/{} value={} converged in {} iterations",
                     i + 1, num_points, sweep_value, stats.total_iterations);
    }
#endif

    // Restore original source value
    set_source_value(config.source_name, original_value);

    auto end_time = high_resolution_clock::now();
    std::chrono::duration<Real> elapsed = end_time - start_time;

    spdlog::info("DC Sweep completed: {} points, {} total iterations ({:.3f} ms)",
                result.points.size(), result.total_iterations, elapsed.count() * 1000.0);

    return result.all_converged() ? ConvergenceStatus::SUCCESS : result.status;
}

ConvergenceStatus DCSolver::solve_temp_sweep(TemperatureSweepResult& result) {
    using namespace std::chrono;
    auto start_time = high_resolution_clock::now();

    spdlog::info("Starting Temperature Sweep analysis: {} temperature points",
                config_.temp_sweep.temperatures.size());

    // Save original temperature
    Real original_temp = config_.temperature;

    // Initialize result
    result.points.clear();
    result.status = ConvergenceStatus::SUCCESS;
    result.total_iterations = 0;

    DenseVector x_prev;  // Previous solution for initial guess

    // Temperature sweep loop
    for (std::size_t i = 0; i < config_.temp_sweep.temperatures.size(); ++i) {
        Real temp = config_.temp_sweep.temperatures[i];

        // Update configuration temperature
        config_.temperature = temp;

        // Update temperature for all temperature-dependent devices
        apply_temperature_to_devices(circuit_, temp);

        spdlog::debug("Temperature Sweep: Point {}/{} T={:.2f}K ({:.2f}°C)",
                     i + 1, config_.temp_sweep.temperatures.size(),
                     temp, temp - 273.15);

        // Solve DC operating point
        DenseVector solution;
        SolverStats stats;

        // Use previous solution as initial guess (if available)
        if (!x_prev.empty()) {
            initialize_solution(solution);
            solution = x_prev;
        }

        ConvergenceStatus status = solve(solution, stats);

        // Store results
        TemperatureSweepPoint point;
        point.temperature = temp;
        point.node_voltages.resize(mna_.num_nodes);
        point.branch_currents.resize(mna_.num_branches);

        // Extract node voltages and branch currents from solution
        for (std::uint32_t j = 0; j < mna_.num_nodes; ++j) {
            point.node_voltages[j] = solution[j];
        }
        for (std::uint32_t j = 0; j < mna_.num_branches; ++j) {
            point.branch_currents[j] = solution[mna_.num_nodes + j];
        }

        point.status = status;
        point.iterations = stats.total_iterations;

        result.points.push_back(point);
        result.total_iterations += stats.total_iterations;

        // Check convergence
        if (status != ConvergenceStatus::SUCCESS) {
            spdlog::warn("Temperature Sweep: Failed to converge at T={:.2f}K ({:.2f}°C)",
                        temp, temp - 273.15);
            result.status = status;
            // Continue to next temperature (don't fail entire sweep)
        }

        // Save solution for next temperature's initial guess
        x_prev = solution;

        spdlog::debug("Temperature Sweep: Point {}/{} T={:.2f}K converged in {} iterations",
                     i + 1, config_.temp_sweep.temperatures.size(), temp, stats.total_iterations);
    }

    // Restore original temperature
    config_.temperature = original_temp;
    apply_temperature_to_devices(circuit_, original_temp);

    auto end_time = high_resolution_clock::now();
    std::chrono::duration<Real> elapsed = end_time - start_time;

    spdlog::info("Temperature Sweep completed: {} points, {} total iterations ({:.3f} ms)",
                result.points.size(), result.total_iterations, elapsed.count() * 1000.0);

    return result.all_converged() ? ConvergenceStatus::SUCCESS : result.status;
}

// ============================================================================
// Branch current extraction
// ============================================================================

std::optional<Real> DCSolver::get_branch_current(const std::string& device_name) const {
    // Check if we have a valid solution
    if (last_solution_.empty()) {
        spdlog::warn("get_branch_current({}): No solution available yet (call solve() first)",
                     device_name);
        return std::nullopt;
    }

    // Get device from circuit
    auto device = circuit_.get_device(device_name);
    if (!device) {
        spdlog::warn("get_branch_current({}): Device not found", device_name);
        return std::nullopt;
    }

    // Check if device has a branch current variable
    const auto type = device->type();
    BranchId branch_id = INVALID_BRANCH;

    if (type == DeviceType::VSOURCE) {
        auto* vsource = dynamic_cast<const devices::VoltageSource*>(device.get());
        if (vsource) {
            branch_id = vsource->branch_id();
        }
    } else if (type == DeviceType::VCVS) {
        auto* vcvs = dynamic_cast<const devices::VCVS*>(device.get());
        if (vcvs) {
            branch_id = vcvs->branch_id();
        }
    } else if (type == DeviceType::CCVS) {
        auto* ccvs = dynamic_cast<const devices::CCVS*>(device.get());
        if (ccvs) {
            branch_id = ccvs->branch_id();
        }
    } else {
        spdlog::warn("get_branch_current({}): Device type {} does not have branch current",
                     device_name, static_cast<int>(type));
        return std::nullopt;
    }

    // Check if branch_id is valid
    if (branch_id == INVALID_BRANCH) {
        spdlog::warn("get_branch_current({}): Device has no branch ID assigned", device_name);
        return std::nullopt;
    }

    // Extract branch current from solution vector
    // Solution structure: [node_voltages (0..num_nodes-1), branch_currents (num_nodes..end)]
    std::uint32_t num_nodes = circuit_.num_nodes() - 1;  // Exclude GND
    std::size_t branch_index = num_nodes + branch_id;

    if (branch_index >= last_solution_.size()) {
        spdlog::error("get_branch_current({}): Branch index {} out of range (solution size: {})",
                     device_name, branch_index, last_solution_.size());
        return std::nullopt;
    }

    Real current = last_solution_[branch_index];
    spdlog::debug("get_branch_current({}): I = {:.6e} A (branch_id={}, index={})",
                 device_name, current, branch_id, branch_index);

    return current;
}

DenseVector DCSolver::extract_all_branch_currents(const DenseVector& solution) const {
    // Calculate number of branches from solution size
    std::uint32_t num_nodes = circuit_.num_nodes() - 1;  // Exclude GND
    std::size_t num_branches = solution.size() - num_nodes;

    // Create branch current vector (indexed by branch_id)
    DenseVector branch_currents(num_branches, 0.0);

    // Extract branch currents for all devices with branches
    for (const auto& device : circuit_.get_devices()) {
        const auto type = device->type();
        BranchId branch_id = INVALID_BRANCH;

        // Check if device has a branch current variable
        if (type == DeviceType::VSOURCE) {
            auto* vsource = dynamic_cast<const devices::VoltageSource*>(device.get());
            if (vsource) {
                branch_id = vsource->branch_id();
            }
        } else if (type == DeviceType::VCVS) {
            auto* vcvs = dynamic_cast<const devices::VCVS*>(device.get());
            if (vcvs) {
                branch_id = vcvs->branch_id();
            }
        } else if (type == DeviceType::CCVS) {
            auto* ccvs = dynamic_cast<const devices::CCVS*>(device.get());
            if (ccvs) {
                branch_id = ccvs->branch_id();
            }
        }

        // Extract branch current if device has one
        if (branch_id != INVALID_BRANCH && branch_id < static_cast<BranchId>(num_branches)) {
            std::size_t branch_index = num_nodes + branch_id;
            if (branch_index < solution.size()) {
                branch_currents[branch_id] = solution[branch_index];
                spdlog::trace("Extracted branch current for {}: I = {:.6e} A (branch_id={})",
                             device->name(), branch_currents[branch_id], branch_id);
            }
        }
    }

    return branch_currents;
}

} // namespace kirimaspice::solvers
