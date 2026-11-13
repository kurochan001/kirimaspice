// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/ac_solver.cpp
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/constants.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <cmath>
#include <algorithm>
#include <chrono>

// OpenMP parallelization support
#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice::solvers {

ACSolver::ACSolver(const circuit::CircuitGraph& circuit,
                   const SimulationConfig& config)
    : circuit_(circuit)
    , config_(config)
    , total_frequency_points_(0)
    , total_linear_solves_(0) {

    spdlog::info("ACSolver created for circuit with {} devices", circuit_.get_devices().size());
}

ACResult ACSolver::solve() {
    spdlog::info("Starting AC analysis");

    auto start_time = std::chrono::high_resolution_clock::now();

    ACResult result;
    result.status = ConvergenceStatus::SUCCESS;

    // Step 1: Compute DC operating point
    spdlog::info("Step 1/4: Computing DC operating point");
    dc_solution_ = compute_dc_operating_point();
    if (dc_solution_.empty()) {
        spdlog::error("DC operating point computation failed");
        result.status = ConvergenceStatus::NUMERICAL_ERROR;
        return result;
    }
    spdlog::info("DC operating point computed successfully");

    // Step 2: Linearize devices at DC operating point
    spdlog::info("Step 2/4: Linearizing devices at DC operating point");
    linearize_devices();
    spdlog::info("Device linearization complete");

    // Step 3: Generate frequency points
    spdlog::info("Step 3/4: Generating frequency points");
    std::vector<Real> frequencies = generate_frequency_points();

    // Check for empty frequency vector to avoid UB
    if (frequencies.empty()) {
        spdlog::error("Frequency point generation failed - empty frequency vector");
        result.status = ConvergenceStatus::NUMERICAL_ERROR;
        return result;
    }

    spdlog::info("Generated {} frequency points ({} Hz to {} Hz)",
                 frequencies.size(), frequencies.front(), frequencies.back());

    // Step 4: Solve at each frequency
    spdlog::info("Step 4/4: Solving AC system at each frequency");

    // Parallelize frequency sweep loop
    // Each frequency point is independent and can be computed in parallel
    // Thread-safe: solve_at_frequency() creates thread-local linear solver
    #ifdef _OPENMP
    spdlog::info("OpenMP parallelization enabled ({} threads)", omp_get_max_threads());
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (size_t i = 0; i < frequencies.size(); ++i) {
        Real f = frequencies[i];
        Real omega = 2.0 * M_PI * f;

        // Note: spdlog is thread-safe, but we avoid excessive logging in parallel region
        #ifdef _OPENMP
        if (i % 10 == 0) {  // Log every 10th frequency point
        #pragma omp critical
        {
            spdlog::debug("Solving at frequency {}/{}: f={:.3e} Hz, ω={:.3e} rad/s",
                         i + 1, frequencies.size(), f, omega);
        }
        }
        #else
        spdlog::debug("Solving at frequency {}/{}: f={:.3e} Hz, ω={:.3e} rad/s",
                     i + 1, frequencies.size(), f, omega);
        #endif

        ComplexDenseVector V_complex = solve_at_frequency(omega);

        ACFrequencyPoint point;
        point.frequency = f;
        point.omega = omega;
        point.solution = V_complex;

        // Protect concurrent access to shared result vector
        #ifdef _OPENMP
        #pragma omp critical
        #endif
        {
            result.frequency_points.push_back(point);
            total_frequency_points_++;
        }
    }

    // Sort frequency points by frequency (parallel loop produces unordered results)
    #ifdef _OPENMP
    std::sort(result.frequency_points.begin(), result.frequency_points.end(),
              [](const ACFrequencyPoint& a, const ACFrequencyPoint& b) {
                  return a.frequency < b.frequency;
              });
    #endif

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    spdlog::info("AC analysis complete: {} frequency points, {} linear solves, {:.3f} s",
                 total_frequency_points_, total_linear_solves_, duration.count() / 1000.0);

    result.stats.total_time_seconds = duration.count() / 1000.0;
    result.stats.linear_solves = total_linear_solves_;

    return result;
}

DenseVector ACSolver::compute_dc_operating_point() {
    // Use DCSolver to compute DC operating point
    DCSolver dc_solver(circuit_, config_);

    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = dc_solver.solve(solution, stats);

    if (status != ConvergenceStatus::SUCCESS) {
        spdlog::error("DC solve failed with status: {}", static_cast<int>(status));
        return DenseVector();
    }

    spdlog::debug("DC operating point: {} variables", solution.size());

    return solution;
}

void ACSolver::linearize_devices() {
    // Parallelize device linearization
    // Each device linearization is independent (modifies only device internal state)
    const auto& devices = circuit_.get_devices();

    #ifdef _OPENMP
    #pragma omp parallel for schedule(static)
    #endif
    for (size_t i = 0; i < devices.size(); ++i) {
        devices[i]->linearize_at(dc_solution_);
    }

    spdlog::debug("Linearized {} devices", devices.size());
}

std::vector<Real> ACSolver::generate_frequency_points() const {
    const auto& ac_config = config_.ac;
    std::vector<Real> frequencies;

    Real f_start = ac_config.start_freq;
    Real f_stop = ac_config.stop_freq;
    std::uint32_t num_points = ac_config.num_points;

    // Validation:
    // - f_start must be positive
    // - f_stop must be >= f_start (allow single frequency when f_start == f_stop)
    // - num_points must be > 0
    if (f_start <= 0.0 || f_stop < f_start || num_points == 0) {
        spdlog::error("Invalid frequency sweep parameters: f_start={}, f_stop={}, num_points={}",
                     f_start, f_stop, num_points);
        return frequencies;
    }

    // Handle single-frequency case (f_start == f_stop)
    if (f_start == f_stop) {
        frequencies.push_back(f_start);
        spdlog::debug("Single frequency: {} Hz", f_start);
        return frequencies;
    }

    switch (ac_config.sweep_type) {
    case SimulationConfig::AC::SweepType::LIN: {
        // Linear sweep: equal spacing in Hz
        Real df = (f_stop - f_start) / (num_points - 1);
        for (std::uint32_t i = 0; i < num_points; ++i) {
            frequencies.push_back(f_start + i * df);
        }
        spdlog::debug("Linear sweep: {} points from {} to {} Hz (df={} Hz)",
                     num_points, f_start, f_stop, df);
        break;
    }

    case SimulationConfig::AC::SweepType::DEC: {
        // Decade sweep: logarithmic spacing, num_points per decade
        // ngspice-compatible: includes both start and stop frequencies
        // For n decades with m points/decade: total = n*m + 1 points
        Real decades = std::log10(f_stop / f_start);
        std::uint32_t total_points = static_cast<std::uint32_t>(std::round(decades * num_points)) + 1;

        for (std::uint32_t i = 0; i < total_points; ++i) {
            Real log_f = std::log10(f_start) + i * (std::log10(f_stop) - std::log10(f_start)) / (total_points - 1);
            frequencies.push_back(std::pow(10.0, log_f));
        }

        spdlog::debug("Decade sweep: {} points per decade, {:.2f} decades, {} total points",
                     num_points, decades, total_points);
        break;
    }

    case SimulationConfig::AC::SweepType::OCT: {
        // Octave sweep: logarithmic spacing, num_points per octave
        // ngspice-compatible: includes both start and stop frequencies
        // For n octaves with m points/octave: total = n*m + 1 points
        Real octaves = std::log2(f_stop / f_start);
        std::uint32_t total_points = static_cast<std::uint32_t>(std::round(octaves * num_points)) + 1;

        for (std::uint32_t i = 0; i < total_points; ++i) {
            Real log_f = std::log2(f_start) + i * (std::log2(f_stop) - std::log2(f_start)) / (total_points - 1);
            frequencies.push_back(std::pow(2.0, log_f));
        }

        spdlog::debug("Octave sweep: {} points per octave, {:.2f} octaves, {} total points",
                     num_points, octaves, total_points);
        break;
    }

    default:
        spdlog::error("Unknown sweep type: {}", static_cast<int>(ac_config.sweep_type));
        break;
    }

    return frequencies;
}

ComplexDenseVector ACSolver::solve_at_frequency(Real omega) {
    // Build complex admittance matrix Y and current source vector I
    std::uint32_t num_nodes = circuit_.num_nodes() - 1; // Exclude GND
    std::uint32_t num_branches = 0;

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
    if (num_nodes > core::MAX_CIRCUIT_NODES) {
        throw std::runtime_error(fmt::format(
            "Circuit exceeds maximum node limit: {} > {}",
            num_nodes, core::MAX_CIRCUIT_NODES));
    }
    if (num_branches > core::MAX_CIRCUIT_BRANCHES) {
        throw std::runtime_error(fmt::format(
            "Circuit exceeds maximum branch limit: {} > {}",
            num_branches, core::MAX_CIRCUIT_BRANCHES));
    }

    auto complex_system = complex_system_pool_.acquire();
    if (!complex_system) {
        throw std::runtime_error("Failed to acquire ComplexMNASystem from pool");
    }
    complex_system->resize(num_nodes, num_branches);
    complex_system->zero();

    // Stamp all devices with AC admittance
    for (const auto& device : circuit_.get_devices()) {
        device->stamp_ac(*complex_system, omega, dc_solution_);
    }

    // Add small conductance to ground for each node (prevents floating singularities)
    add_ac_gmin(*complex_system);

    spdlog::trace("Built complex MNA system: size={}, nnz={}",
                 complex_system->size(), complex_system->Y.nnz());

    // Convert complex system to real system (real/imag separation)
    auto real_system = real_system_pool_.acquire();
    if (!real_system) {
        throw std::runtime_error("Failed to acquire MNASystem from pool");
    }
    convert_to_real_system(complex_system->Y, complex_system->I, *real_system);

    // Solve real system using KLU
    // Thread-safe linear solver (no factorization reuse in parallel)
    DenseVector V_real;
    #ifdef _OPENMP
    if (omp_in_parallel()) {
        // In parallel region: create thread-local solver (no reuse)
        auto local_solver = make_solver(choose_backend());
        if (!local_solver->analyze_and_factorize(real_system->G)) {
            spdlog::error("Linear solver factorization failed: {}", local_solver->last_error());
            return ComplexDenseVector();
        }
        if (!local_solver->solve(real_system->b, V_real)) {
            spdlog::error("Linear solver failed: {}", local_solver->last_error());
            return ComplexDenseVector();
        }
        #pragma omp atomic
        total_linear_solves_++;
    } else {
        // Sequential: use existing reuse logic
        V_real = solve_linear_system(*real_system);
    }
    #else
    V_real = solve_linear_system(*real_system);
    #endif

    // Convert real solution back to complex
    ComplexDenseVector V_complex;
    convert_to_complex_solution(V_real, V_complex);

    return V_complex;
}

void ACSolver::convert_to_real_system(const ComplexSparseMatrix& Y_complex,
                                      const ComplexDenseVector& I_complex,
                                      MNASystem& system_real) {
    std::uint32_t n = Y_complex.n_rows;

    // Resize with 2n from the start for metadata consistency
    // Real system size is 2N (real part + imaginary part)
    system_real.resize(2 * n, 0);  // Resize to 2n to ensure consistent metadata
    system_real.zero();            // Clear G's COO arrays (required)
    // Note: resize(2n, 0) already sets G.n_rows, G.n_cols, b.size, x.size to 2n

    // Build real system matrix:
    // [Re(Y)  -Im(Y)] [Re(V)]   [Re(I)]
    // [Im(Y)   Re(Y)] [Im(V)] = [Im(I)]

    for (size_t k = 0; k < Y_complex.values.size(); ++k) {
        std::uint32_t row = Y_complex.rows[k];
        std::uint32_t col = Y_complex.cols[k];
        Complex value = Y_complex.values[k];

        Real re = std::real(value);
        Real im = std::imag(value);

        // Top-left block: Re(Y)
        system_real.G.add_entry(row, col, re);

        // Top-right block: -Im(Y)
        system_real.G.add_entry(row, col + n, -im);

        // Bottom-left block: Im(Y)
        system_real.G.add_entry(row + n, col, im);

        // Bottom-right block: Re(Y)
        system_real.G.add_entry(row + n, col + n, re);
    }

    // Build real RHS vector: [Re(I); Im(I)]
    for (size_t i = 0; i < I_complex.size(); ++i) {
        system_real.b[i] = std::real(I_complex[i]);
        system_real.b[i + n] = std::imag(I_complex[i]);
    }

    spdlog::trace("Converted complex system to real: {}x{} -> {}x{}, nnz={} -> {}",
                 n, n, 2*n, 2*n, Y_complex.nnz(), system_real.G.nnz());
}

void ACSolver::convert_to_complex_solution(const DenseVector& V_real,
                                           ComplexDenseVector& V_complex) {
    std::uint32_t n = V_real.size() / 2;
    V_complex.resize(n);

    for (std::uint32_t i = 0; i < n; ++i) {
        Real re = V_real[i];
        Real im = V_real[i + n];
        V_complex[i] = Complex(re, im);
    }

    spdlog::trace("Converted real solution to complex: {} real -> {} complex variables",
                 V_real.size(), n);
}

DenseVector ACSolver::solve_linear_system(MNASystem& system) {
    // Reuse single solver across frequency points
    if (!lin_solver_) {
        lin_solver_ = make_solver(choose_backend());
        first_factor_done_ = false;
    }

    bool ok = false;
    if (!first_factor_done_) {
        ok = lin_solver_->analyze_and_factorize(system.G);
        first_factor_done_ = ok;
    } else {
        ok = lin_solver_->refactorize(system.G);
        if (!ok) {
            spdlog::info("AC: refactorize failed ({}), falling back to analyze_and_factorize",
                         lin_solver_->last_error());
            ok = lin_solver_->analyze_and_factorize(system.G);
            first_factor_done_ = ok;
        }
    }
    if (!ok) {
        spdlog::error("Linear solver factorization failed: {}", lin_solver_->last_error());
        return DenseVector();
    }

    DenseVector x;
    if (!lin_solver_->solve(system.b, x)) {
        spdlog::error("Linear solver failed: {}", lin_solver_->last_error());
        return DenseVector();
    }

    total_linear_solves_++;

    spdlog::trace("Linear system solved: {} variables", x.size());
    return x;
}

void ACSolver::add_ac_gmin(ComplexMNASystem& system) const {
    Real gmin = config_.gmin;
    if (gmin <= 0.0) {
        return;
    }

    std::uint32_t num_nodes = system.num_nodes;
    if (num_nodes == 0) {
        return;
    }

    Complex gmin_complex(gmin, 0.0);
    for (std::uint32_t i = 0; i < num_nodes; ++i) {
        system.Y.add_entry(i, i, gmin_complex);
    }

    spdlog::trace("AC Gmin added to {} nodes ({} S)", num_nodes, gmin);
}

} // namespace kirimaspice::solvers
