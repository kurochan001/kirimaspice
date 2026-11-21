// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/noise_solver.cpp
// Noise Analysis with Adjoint Transfer Function Method
#include "kirimaspice/solvers/noise_solver.hpp"
#include "kirimaspice/solvers/factorization_failure.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace kirimaspice::solvers {

NoiseSolver::NoiseSolver(circuit::CircuitGraph& circuit, const SimulationConfig& config)
    : circuit_(circuit), config_(config) {
    noise_detail_enabled_ = config_.logging.noise_detail;
    spdlog::info("NoiseSolver: Initialized for circuit with {} nodes, {} devices",
                 circuit_.num_nodes(), circuit_.num_devices());
}

NoiseResult NoiseSolver::solve(NodeId input_node, NodeId output_node) {
    spdlog::info("NoiseSolver: Starting noise analysis (input={}, output={})",
                 input_node, output_node);

    NoiseResult result;
    result.status = ConvergenceStatus::SUCCESS;

    // Step 1: Compute DC operating point
    spdlog::info("NoiseSolver: Step 1/4 - Computing DC operating point");
    DCSolver dc_solver(circuit_, config_);
    SolverStats dc_stats;
    ConvergenceStatus dc_status = dc_solver.solve(dc_solution_, dc_stats);

    if (dc_status != ConvergenceStatus::SUCCESS) {
        spdlog::error("NoiseSolver: DC operating point failed");
        result.status = ConvergenceStatus::NUMERICAL_ERROR;
        return result;
    }
    spdlog::info("NoiseSolver: DC operating point computed successfully");

    // Step 2: Linearize devices at DC operating point
    spdlog::info("NoiseSolver: Step 2/4 - Linearizing devices");
    for (const auto& device : circuit_.get_devices()) {
        device->linearize_at(dc_solution_);
    }

    // Step 3: Generate frequency points (use AC config)
    spdlog::info("NoiseSolver: Step 3/4 - Generating frequency points");
    std::vector<Real> frequencies = generate_frequency_points();
    if (frequencies.empty()) {
        spdlog::error("NoiseSolver: No frequency points generated");
        result.status = ConvergenceStatus::NUMERICAL_ERROR;
        return result;
    }
    spdlog::info("NoiseSolver: Generated {} frequency points ({:.3e} Hz to {:.3e} Hz)",
                 frequencies.size(), frequencies.front(), frequencies.back());

    // Initialize result vectors
    result.frequency_points = frequencies;
    result.output_noise.resize(frequencies.size(), 0.0);
    result.input_referred_noise.resize(frequencies.size(), 0.0);

    // Step 4: Calculate noise at each frequency using adjoint method
    spdlog::info("NoiseSolver: Step 4/4 - Computing noise via adjoint method");

    std::uint32_t num_nodes = circuit_.num_nodes() - 1; // Exclude GND
    std::uint32_t num_branches = count_branch_devices();
    if (output_node == 0 || output_node > num_nodes) {
        spdlog::error("NoiseSolver: output node {} is not a node of the circuit", output_node);
        result.status = ConvergenceStatus::NUMERICAL_ERROR;
        return result;
    }

    for (size_t freq_idx = 0; freq_idx < frequencies.size(); ++freq_idx) {
        Real f = frequencies[freq_idx];
        Real omega = 2.0 * M_PI * f;

        // Build complex admittance matrix Y
        ComplexMNASystem system;
        system.resize(num_nodes, num_branches);
        system.zero();

        for (const auto& device : circuit_.get_devices()) {
            device->stamp_ac(system, omega, dc_solution_);
        }
        add_gmin(system);

        // Solve adjoint system: Y^H × Λ = e_out
        // e_out has 1 at output node, 0 elsewhere
        ComplexDenseVector e_out(system.size(), Complex(0.0, 0.0));
        if (output_node > 0 && output_node <= num_nodes) {
            e_out[output_node - 1] = Complex(1.0, 0.0);
        }

        // Solve Y^H × Λ = e_out
        // Y^H (Hermitian transpose) = conj(Y)^T
        ComplexDenseVector lambda = solve_adjoint_system(system.Y, e_out);

        if (lambda.empty()) {
            spdlog::error("NoiseSolver: Adjoint solve failed at f={:.3e} Hz", f);
            result.status = ConvergenceStatus::SINGULAR_MATRIX;
            return result;
        }

        // Collect noise sources from all devices
        std::vector<NoiseSource> noise_sources = collect_noise_sources(f);

        // Calculate output noise: sum of |H|² × S_n
        Real total_noise_squared = 0.0;

        for (const auto& source : noise_sources) {
            // Transfer function: H = Λ[node_p] - Λ[node_n]
            Complex H(0.0, 0.0);
            if (source.node_p > 0 && source.node_p <= num_nodes) {
                H += lambda[source.node_p - 1];
            }
            if (source.node_n > 0 && source.node_n <= num_nodes) {
                H -= lambda[source.node_n - 1];
            }

            // |H|² × S_n
            Real H_mag_squared = std::norm(H);  // |H|²
            Real contribution = H_mag_squared * source.spectral_density;
            total_noise_squared += contribution;

            // Track per-device contributions
            if (result.contributions.find(source.name) == result.contributions.end()) {
                result.contributions[source.name].resize(frequencies.size(), 0.0);
            }
            result.contributions[source.name][freq_idx] = contribution;

            if (noise_detail_enabled_) {
                spdlog::info("NoiseSolver detail: f={:.3e} Hz, src={}, |H|^2={:.3e}, S_n={:.3e}, contrib={:.3e}",
                             f, source.name, H_mag_squared, source.spectral_density, contribution);
            } else {
                spdlog::trace("NoiseSolver: f={:.3e} Hz, source={}, |H|²={:.3e}, S_n={:.3e} V²/Hz, contrib={:.3e}",
                             f, source.name, H_mag_squared, source.spectral_density, contribution);
            }
        }

        // Output noise in V/√Hz
        result.output_noise[freq_idx] = std::sqrt(total_noise_squared);
        if (!std::isfinite(result.output_noise[freq_idx])) {
            spdlog::error("NoiseSolver: output noise is not finite at f={:.3e} Hz", f);
            result.status = ConvergenceStatus::NUMERICAL_ERROR;
            return result;
        }

        // Input-referred noise: output_noise / voltage_gain
        Real gain = calculate_voltage_gain(input_node, output_node, lambda);
        if (gain > 1e-20) {
            result.input_referred_noise[freq_idx] = result.output_noise[freq_idx] / gain;
        } else {
            result.input_referred_noise[freq_idx] = 0.0;
        }

        if (freq_idx % 10 == 0 || freq_idx == frequencies.size() - 1) {
            spdlog::debug("NoiseSolver: f={:.3e} Hz, output_noise={:.3e} V/√Hz, input_referred={:.3e} V/√Hz",
                         f, result.output_noise[freq_idx], result.input_referred_noise[freq_idx]);
        }
    }

    spdlog::info("NoiseSolver: Noise analysis complete, {} frequency points processed",
                 frequencies.size());
    return result;
}

std::vector<NoiseSource> NoiseSolver::collect_noise_sources(Real frequency) {
    std::vector<NoiseSource> sources;

    for (const auto& device : circuit_.get_devices()) {
        auto device_sources = device->get_noise_sources(dc_solution_, frequency);
        for (auto& src : device_sources) {
            sources.push_back(std::move(src));
        }
    }

    spdlog::trace("NoiseSolver: Collected {} noise sources at f={:.3e} Hz",
                 sources.size(), frequency);
    return sources;
}

Real NoiseSolver::calculate_transfer_function(const NoiseSource& source,
                                               const ComplexDenseVector& lambda,
                                               NodeId /*output_node*/) {
    std::uint32_t num_nodes = circuit_.num_nodes() - 1;

    // Transfer function: H = Λ[node_p] - Λ[node_n]
    Complex H(0.0, 0.0);
    if (source.node_p > 0 && source.node_p <= num_nodes) {
        H += lambda[source.node_p - 1];
    }
    if (source.node_n > 0 && source.node_n <= num_nodes) {
        H -= lambda[source.node_n - 1];
    }

    return std::norm(H);  // Return |H|²
}

Real NoiseSolver::calculate_voltage_gain(NodeId input_node,
                                         NodeId output_node,
                                         const ComplexDenseVector& lambda) {
    std::uint32_t num_nodes = circuit_.num_nodes() - 1;

    // For adjoint method, lambda gives the transfer function from output to each node
    // Voltage gain = |Λ[input_node]| (transfer from output excitation to input response)
    // Note: Since we excite at output and measure at input in adjoint, the gain is reciprocal

    // Actually, for the voltage gain from input to output in a standard sense,
    // we need the AC response at output when input is excited
    // For adjoint: Y^H × Λ = e_out means Λ_i gives transfer from source at node i to output
    // So Λ[input_node] gives the transfer function from input to output

    Complex gain_complex(0.0, 0.0);
    if (input_node > 0 && input_node <= num_nodes) {
        // Λ[input] gives the response at output for a unit current at input
        // This is the transimpedance; for voltage gain, we need different treatment
        // For input-referred noise, we typically want |V_out / V_in|

        // Simplified: use |Λ[input]| as an approximation
        // This is valid when input source has a known impedance
        gain_complex = lambda[input_node - 1];
    }

    Real gain = std::abs(gain_complex);

    // Ensure gain is reasonable (not too small)
    return std::max(gain, 1e-20);
}

std::vector<Real> NoiseSolver::generate_frequency_points() const {
    // The frequencies of the AC sweep, as the AC analysis takes them
    return ac_frequency_points(config_.ac, config_.dc.reltol);
}

std::uint32_t NoiseSolver::count_branch_devices() const {
    // One branch current variable per device to which CircuitGraph gave a
    // branch id: voltage sources, VCVS, CCVS and voltage-mode B sources.
    return static_cast<std::uint32_t>(circuit_.get_voltage_sources().size());
}

void NoiseSolver::add_gmin(ComplexMNASystem& system) const {
    Real gmin = config_.gmin;
    if (gmin <= 0.0) {
        return;
    }

    Complex gmin_complex(gmin, 0.0);
    for (std::uint32_t i = 0; i < system.num_nodes; ++i) {
        system.Y.add_entry(i, i, gmin_complex);
    }
}

ComplexDenseVector NoiseSolver::solve_adjoint_system(const ComplexSparseMatrix& Y,
                                                      const ComplexDenseVector& rhs) {
    // Solve Y^H × Λ = rhs (Hermitian transpose)
    // Y^H = conj(Y)^T

    std::uint32_t n = Y.n_rows;
    if (n == 0 || rhs.size() != n) {
        spdlog::error("NoiseSolver: Invalid matrix dimensions for adjoint solve");
        return ComplexDenseVector();
    }

    // Build Y^H (conjugate transpose)
    // For each entry Y(row, col) = val, add conj(val) at (col, row)
    ComplexSparseMatrix Y_H;
    Y_H.n_rows = n;
    Y_H.n_cols = n;
    Y_H.reserve(Y.values.size());

    for (size_t k = 0; k < Y.values.size(); ++k) {
        std::uint32_t row = Y.rows[k];
        std::uint32_t col = Y.cols[k];
        Complex val = Y.values[k];

        // Conjugate transpose: swap row/col, conjugate value
        Y_H.add_entry(col, row, std::conj(val));
    }

    // Convert to real system and solve
    // Real representation of Y^H:
    // [Re(Y^H)  -Im(Y^H)]   [Re(Λ)]   [Re(rhs)]
    // [Im(Y^H)   Re(Y^H)] × [Im(Λ)] = [Im(rhs)]

    MNASystem real_system;
    real_system.resize(2 * n, 0);
    real_system.zero();

    for (size_t k = 0; k < Y_H.values.size(); ++k) {
        std::uint32_t row = Y_H.rows[k];
        std::uint32_t col = Y_H.cols[k];
        Complex val = Y_H.values[k];

        Real re = std::real(val);
        Real im = std::imag(val);

        // Top-left block: Re(Y^H)
        real_system.G.add_entry(row, col, re);
        // Top-right block: -Im(Y^H)
        real_system.G.add_entry(row, col + n, -im);
        // Bottom-left block: Im(Y^H)
        real_system.G.add_entry(row + n, col, im);
        // Bottom-right block: Re(Y^H)
        real_system.G.add_entry(row + n, col + n, re);
    }

    // RHS vector
    for (size_t i = 0; i < n; ++i) {
        real_system.b[i] = std::real(rhs[i]);
        real_system.b[i + n] = std::imag(rhs[i]);
    }

    // Solve using KLU
    auto solver = make_solver(choose_backend());
    if (!solver->analyze_and_factorize(real_system.G)) {
        spdlog::error("NoiseSolver: Adjoint factorization failed: {}",
                      factorization_failure(circuit_, *solver));
        return ComplexDenseVector();
    }

    DenseVector x_real;
    if (!solver->solve(real_system.b, x_real)) {
        spdlog::error("NoiseSolver: Adjoint solve failed: {}", solver->last_error());
        return ComplexDenseVector();
    }

    // Convert back to complex
    ComplexDenseVector lambda(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        lambda[i] = Complex(x_real[i], x_real[i + n]);
    }

    return lambda;
}

}  // namespace kirimaspice::solvers
