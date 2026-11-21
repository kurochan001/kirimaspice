// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/sparam_solver.cpp
#include "kirimaspice/solvers/sparam_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"
#include "kirimaspice/devices/device.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace kirimaspice::solvers {

SParameterSolver::SParameterSolver(const circuit::CircuitGraph& circuit,
                                   const SimulationConfig& config)
    : circuit_(circuit)
    , config_(config)
    , total_frequency_points_(0)
    , total_linear_solves_(0) {

    spdlog::info("SParameterSolver created for circuit with {} devices, {} ports",
                 circuit_.get_devices().size(),
                 config_.sparam.ports.size());

    // Validate port configuration
    if (config_.sparam.ports.empty()) {
        throw std::runtime_error("S-parameter analysis requires at least 1 port definition");
    }
}

SParameterResult SParameterSolver::solve() {
    spdlog::info("Starting S-parameter analysis");

    auto start_time = std::chrono::high_resolution_clock::now();

    SParameterResult result;
    result.success = false;
    result.ports = config_.sparam.ports;

    // Build port node ID mapping
    port_node_ids_.clear();
    for (const auto& port : config_.sparam.ports) {
        // Get node IDs from circuit graph
        NodeId pos_id = circuit_.get_node_id(port.node_positive);
        NodeId neg_id = circuit_.get_node_id(port.node_negative);

        port_node_ids_.push_back({pos_id, neg_id});

        spdlog::debug("Port {}: nodes ({}, {}) -> IDs ({}, {}), Z0={} Ω",
                     port.port_number,
                     port.node_positive, port.node_negative,
                     pos_id, neg_id,
                     port.impedance);
    }

    // Step 1: Compute DC operating point
    spdlog::info("Step 1/4: Computing DC operating point");
    dc_solution_ = compute_dc_operating_point();
    if (dc_solution_.empty()) {
        // DC analysis failed (common for RF circuits without DC bias)
        // Initialize with zero solution and continue
        spdlog::warn("DC operating point computation failed - using zero initialization");
        dc_solution_ = DenseVector(circuit_.num_nodes(), 0.0);
    } else {
        spdlog::info("DC operating point computed successfully");
    }

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
        result.success = false;
        return result;
    }

    spdlog::info("Generated {} frequency points ({} Hz to {} Hz)",
                 frequencies.size(), frequencies.front(), frequencies.back());

    // Step 4: Calculate S-parameters at each frequency
    spdlog::info("Step 4/4: Calculating S-parameters at each frequency");
    std::uint32_t num_ports = static_cast<std::uint32_t>(config_.sparam.ports.size());

    for (size_t i = 0; i < frequencies.size(); ++i) {
        Real f = frequencies[i];
        Real omega = 2.0 * M_PI * f;

        spdlog::debug("Calculating S-parameters at frequency {}/{}: f={:.3e} Hz, ω={:.3e} rad/s",
                     i + 1, frequencies.size(), f, omega);

        // Calculate S-parameter matrix at this frequency
        std::vector<std::vector<Complex>> S_matrix = calculate_s_matrix(omega);

        // Check if calculation failed
        if (S_matrix.empty()) {
            spdlog::error("S-parameter calculation failed at frequency {} Hz", f);
            result.success = false;
            return result;
        }

        SParameterPoint point;
        point.frequency = f;
        point.S_matrix = S_matrix;

        result.frequency_points.push_back(point);
        total_frequency_points_++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    spdlog::info("S-parameter analysis complete: {} frequency points, {} ports, {} linear solves, {:.3f} s",
                 total_frequency_points_, num_ports, total_linear_solves_, duration.count() / 1000.0);

    result.success = true;
    return result;
}

DenseVector SParameterSolver::compute_dc_operating_point() {
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

void SParameterSolver::linearize_devices() {
    // Call linearize_at() for each device
    for (const auto& device : circuit_.get_devices()) {
        device->linearize_at(dc_solution_);
    }

    spdlog::debug("Linearized {} devices", circuit_.get_devices().size());
}

std::vector<Real> SParameterSolver::generate_frequency_points() const {
    // The grid of an AC sweep of the same type (ac_frequency_points)
    const auto& sparam_config = config_.sparam;
    std::string sweep_type = sparam_config.sweep_type;
    std::transform(sweep_type.begin(), sweep_type.end(), sweep_type.begin(), ::toupper);
    SimulationConfig::AC ac;
    if (sweep_type == "LIN") {
        ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    } else if (sweep_type == "DEC") {
        ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    } else if (sweep_type == "OCT") {
        ac.sweep_type = SimulationConfig::AC::SweepType::OCT;
    } else {
        spdlog::error("Unknown sweep type: {} (expected LIN/DEC/OCT)", sweep_type);
        return {};
    }
    ac.start_freq = sparam_config.start_freq;
    ac.stop_freq = sparam_config.stop_freq;
    ac.num_points = sparam_config.num_points;
    return ac_frequency_points(ac, config_.dc.reltol);
}

std::vector<std::vector<Complex>> SParameterSolver::calculate_s_matrix(Real omega) {
    std::uint32_t num_ports = static_cast<std::uint32_t>(config_.sparam.ports.size());

    // Initialize S-parameter matrix (N×N)
    std::vector<std::vector<Complex>> S_matrix(num_ports, std::vector<Complex>(num_ports, Complex(0.0, 0.0)));

    // For each excitation port j (1-indexed)
    for (std::uint32_t j = 1; j <= num_ports; ++j) {
        spdlog::trace("  Exciting port {}/{}", j, num_ports);

        // Solve AC system with port j excited
        ComplexDenseVector solution = solve_with_port_excitation(omega, j);

        // Check if solve failed
        if (solution.empty()) {
            spdlog::error("Failed to solve for port {} excitation - S-parameter calculation aborted", j);
            return std::vector<std::vector<Complex>>();  // Return empty matrix on failure
        }

        // Extract port voltages and currents
        std::vector<Complex> V_ports(num_ports);
        std::vector<Complex> I_ports(num_ports);

        for (std::uint32_t i = 1; i <= num_ports; ++i) {
            V_ports[i-1] = get_port_voltage(solution, i);
            I_ports[i-1] = get_port_current(solution, i, omega, j);  // Pass excitation port j

            spdlog::trace("    Port {}: V = ({:.6e}, {:.6e}), I = ({:.6e}, {:.6e})",
                         i, V_ports[i-1].real(), V_ports[i-1].imag(),
                         I_ports[i-1].real(), I_ports[i-1].imag());
        }

        // Calculate S-parameters S_ij for all i
        for (std::uint32_t i = 1; i <= num_ports; ++i) {
            Real Z0_i = config_.sparam.ports[i-1].impedance;
            Real Z0_j = config_.sparam.ports[j-1].impedance;

            bool is_excitation = (i == j);

            Complex S_ij = calculate_s_parameter(V_ports[i-1], I_ports[i-1],
                                                 V_ports[j-1], I_ports[j-1],
                                                 Z0_i, Z0_j,
                                                 is_excitation);

            S_matrix[i-1][j-1] = S_ij;

            spdlog::trace("    S_{}{} = ({:.6e}, {:.6e}) = {:.6f} ∠ {:.2f}°",
                         i, j, S_ij.real(), S_ij.imag(),
                         std::abs(S_ij), std::arg(S_ij) * 180.0 / M_PI);
        }
    }

    return S_matrix;
}

ComplexDenseVector SParameterSolver::solve_with_port_excitation(Real omega, std::uint32_t excitation_port) {
    std::uint32_t num_nodes = circuit_.num_nodes();

    // Build complex MNA system with port excitation
    ComplexMNASystem sys_complex;
    sys_complex.resize(num_nodes, 0);  // No branches in AC analysis
    sys_complex.zero();

    build_sparam_system(sys_complex, omega, excitation_port);

    // Convert to real system
    MNASystem system_real;
    convert_to_real_system(sys_complex.Y, sys_complex.I, system_real);

    // Solve real system
    DenseVector V_real = solve_linear_system(system_real);

    // Check if solve failed
    if (V_real.empty()) {
        spdlog::error("Linear system solve failed for port {} excitation", excitation_port);
        return ComplexDenseVector();  // Return empty vector on failure
    }

    // Convert back to complex
    ComplexDenseVector V_complex;
    convert_to_complex_solution(V_real, V_complex);

    total_linear_solves_++;

    return V_complex;
}

void SParameterSolver::build_sparam_system(ComplexMNASystem& sys, Real omega, std::uint32_t excitation_port) {
    std::uint32_t num_ports = static_cast<std::uint32_t>(config_.sparam.ports.size());

    // Step 1: Stamp all circuit devices (AC analysis)
    for (const auto& device : circuit_.get_devices()) {
        device->stamp_ac(sys, omega, dc_solution_);
    }

    // Step 2: Add port excitation and terminations
    for (std::uint32_t p = 1; p <= num_ports; ++p) {
        const auto& port = config_.sparam.ports[p-1];
        const auto& [pos_id, neg_id] = port_node_ids_[p-1];
        Real Z0 = port.impedance;

        if (p == excitation_port) {
            // Excite this port with 1V @ 0° voltage source
            // Add to current source vector (voltage source as current injection)
            // For simplicity, we add a parallel conductance and current source
            // This is equivalent to a Thevenin equivalent with V_source = 1V, Z_source = Z0

            // V_port = 1V (desired)
            // I_source = V_port / Z0 = 1 / Z0
            // G_source = 1 / Z0

            Real G0 = 1.0 / Z0;
            Complex I_source(G0, 0.0);  // I = V / Z0 = 1V / Z0

            // Add admittance (1/Z0) to diagonal
            if (pos_id != 0) {
                sys.Y.add_entry(pos_id - 1, pos_id - 1, Complex(G0, 0.0));
            }
            if (neg_id != 0) {
                sys.Y.add_entry(neg_id - 1, neg_id - 1, Complex(G0, 0.0));
            }
            if (pos_id != 0 && neg_id != 0) {
                sys.Y.add_entry(pos_id - 1, neg_id - 1, Complex(-G0, 0.0));
                sys.Y.add_entry(neg_id - 1, pos_id - 1, Complex(-G0, 0.0));
            }

            // Add current source
            if (pos_id != 0) {
                sys.I[pos_id - 1] += I_source;
            }
            if (neg_id != 0) {
                sys.I[neg_id - 1] -= I_source;
            }

            spdlog::trace("  Port {} (excitation): G0={:.6e} S, I_source={:.6e} A",
                         p, G0, std::abs(I_source));
        } else {
            // Terminate this port with characteristic impedance Z0
            // Add parallel conductance G0 = 1/Z0
            Real G0 = 1.0 / Z0;

            if (pos_id != 0) {
                sys.Y.add_entry(pos_id - 1, pos_id - 1, Complex(G0, 0.0));
            }
            if (neg_id != 0) {
                sys.Y.add_entry(neg_id - 1, neg_id - 1, Complex(G0, 0.0));
            }
            if (pos_id != 0 && neg_id != 0) {
                sys.Y.add_entry(pos_id - 1, neg_id - 1, Complex(-G0, 0.0));
                sys.Y.add_entry(neg_id - 1, pos_id - 1, Complex(-G0, 0.0));
            }

            spdlog::trace("  Port {} (terminated): G0={:.6e} S", p, G0);
        }
    }
}

void SParameterSolver::convert_to_real_system(const ComplexSparseMatrix& Y_complex,
                                              const ComplexDenseVector& I_complex,
                                              MNASystem& system_real) {
    std::uint32_t N = static_cast<std::uint32_t>(I_complex.size());

    // Real system size: 2N (for Re and Im parts)
    // Treat the imaginary part as "virtual nodes", so total_size = 2N
    system_real.resize(2 * N, 0);  // 2N nodes (Re and Im), no branches
    system_real.b.resize(2 * N);

    // Build real system:
    // [Re(Y)  -Im(Y)] [Re(V)]   [Re(I)]
    // [Im(Y)   Re(Y)] [Im(V)] = [Im(I)]

    // Add numerical stabilization to prevent singular matrix in resistive-only circuits
    // This is equivalent to adding a tiny conductance to avoid zero rows/columns
    const Real epsilon = 1e-12;  // Numerical stabilization term (similar to GMIN)

    for (std::uint32_t row = 0; row < Y_complex.rows.size(); ++row) {
        std::uint32_t i = Y_complex.rows[row];
        std::uint32_t j = Y_complex.cols[row];
        Complex y = Y_complex.values[row];

        Real y_re = y.real();
        Real y_im = y.imag();

        // Top-left: Re(Y)
        system_real.G.add_entry(i, j, y_re);
        // Top-right: -Im(Y)
        system_real.G.add_entry(i, j + N, -y_im);
        // Bottom-left: Im(Y)
        system_real.G.add_entry(i + N, j, y_im);
        // Bottom-right: Re(Y)
        system_real.G.add_entry(i + N, j + N, y_re);
    }

    // Add epsilon to diagonal for numerical stability (prevent singular matrix in pure resistive circuits)
    for (std::uint32_t i = 0; i < N; ++i) {
        system_real.G.add_entry(i, i + N, epsilon);     // Top-right diagonal
        system_real.G.add_entry(i + N, i, epsilon);     // Bottom-left diagonal
    }

    // RHS vector
    for (std::uint32_t i = 0; i < N; ++i) {
        system_real.b[i] = I_complex[i].real();       // Re(I)
        system_real.b[i + N] = I_complex[i].imag();   // Im(I)
    }
}

void SParameterSolver::convert_to_complex_solution(const DenseVector& V_real,
                                                   ComplexDenseVector& V_complex) {
    std::uint32_t N = static_cast<std::uint32_t>(V_real.size()) / 2;
    V_complex.resize(N);

    for (std::uint32_t i = 0; i < N; ++i) {
        V_complex[i] = Complex(V_real[i], V_real[i + N]);
    }
}

DenseVector SParameterSolver::solve_linear_system(MNASystem& system) {
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
            spdlog::info("S-Param: refactorize failed ({}), falling back to analyze_and_factorize",
                         lin_solver_->last_error());
            ok = lin_solver_->analyze_and_factorize(system.G);
            first_factor_done_ = ok;
        }
    }

    if (!ok) {
        // The system holds the nodes alone, not in the order of
        // CircuitGraph::unknown_name, so no unknown is named
        spdlog::error("Linear solver factorization failed: {}", lin_solver_->last_error());
        return DenseVector();
    }

    DenseVector x;
    if (!lin_solver_->solve(system.b, x)) {
        spdlog::error("Linear solver failed: {}", lin_solver_->last_error());
        return DenseVector();
    }

    spdlog::trace("Linear system solved: {} variables", x.size());
    return x;
}

Complex SParameterSolver::get_port_voltage(const ComplexDenseVector& solution, std::uint32_t port_num) const {
    const auto& [pos_id, neg_id] = port_node_ids_[port_num - 1];

    Complex V_pos = (pos_id == 0) ? Complex(0.0, 0.0) : solution[pos_id - 1];
    Complex V_neg = (neg_id == 0) ? Complex(0.0, 0.0) : solution[neg_id - 1];

    return V_pos - V_neg;
}

Complex SParameterSolver::get_port_current(const ComplexDenseVector& solution,
                                           std::uint32_t port_num,
                                           Real omega,
                                           std::uint32_t excitation_port) const {
    (void)omega;  // Unused in current implementation (reserved for future use)

    // Port current calculation (current flowing FROM network INTO port)
    //
    // In S-parameter theory, I_port is the current flowing from the network into the port.
    // This is critical for the correct calculation of incident and reflected waves.
    //
    // Our MNA circuit model uses Norton equivalent for excitation:
    // - For EXCITATION port: I_source = 1/Z0 (current source) || Y0 = 1/Z0 (termination)
    // - For TERMINATED port: Y0 = 1/Z0 (termination only)
    //
    // After solving MNA, we get port voltage V_port. The port current is calculated as:
    //
    // For EXCITATION port (has current source):
    //   KCL at port node: I_source = V_port * Y0 + I_from_network
    //   Therefore: I_from_network = I_source - V_port * Y0 = 1/Z0 - V_port/Z0
    //   I_port = I_from_network = (1 - V_port) / Z0
    //
    // For TERMINATED port (no current source):
    //   KCL at port node: 0 = V_port * Y0 + I_from_network
    //   Therefore: I_from_network = -V_port * Y0 = -V_port/Z0
    //   I_port = I_from_network = -V_port / Z0
    //
    // Note: I_port is positive when current flows from network into port (out of DUT).

    const auto& port = config_.sparam.ports[port_num - 1];
    Real Z0 = port.impedance;
    Complex V_port = get_port_voltage(solution, port_num);

    Complex I_port;
    if (port_num == excitation_port) {
        // Excitation port: account for current source contribution
        // I_port = (1 - V_port) / Z0
        I_port = (Complex(1.0, 0.0) - V_port) / Z0;
        spdlog::trace("Port {} (EXCITATION): V = ({:.6e}, {:.6e}), I = ({:.6e}, {:.6e})",
                     port_num, V_port.real(), V_port.imag(), I_port.real(), I_port.imag());
    } else {
        // Terminated port: only termination contributes
        // I_port = -V_port / Z0
        I_port = -V_port / Z0;
        spdlog::trace("Port {} (TERMINATED): V = ({:.6e}, {:.6e}), I = ({:.6e}, {:.6e})",
                     port_num, V_port.real(), V_port.imag(), I_port.real(), I_port.imag());
    }

    return I_port;
}

Complex SParameterSolver::calculate_s_parameter(const Complex& V_i, const Complex& I_i,
                                                const Complex& V_j, const Complex& I_j,
                                                Real Z0_i, Real Z0_j,
                                                bool is_excitation_port) const {
    // Power-normalized S-parameters (ensures reciprocity for networks with different port impedances)
    //
    // Incident and reflected power waves:
    //   a_i = (V_i + Z0_i * I_i) / sqrt(Z0_i)   [incident wave at port i]
    //   b_i = (V_i - Z0_i * I_i) / sqrt(Z0_i)   [reflected wave at port i]
    //
    // S_ij = b_i / a_j (when only port j is excited)
    //
    // The sqrt(Z0) normalization ensures that |a|^2 and |b|^2 represent power in watts,
    // and guarantees reciprocity: S_ij = S_ji for passive reciprocal networks.
    //
    // Reference: "Microwave Engineering" by Pozar, Chapter 4

    Real sqrt_Z0_i = std::sqrt(Z0_i);
    Real sqrt_Z0_j = std::sqrt(Z0_j);

    // Calculate power-normalized incident and reflected waves
    Complex b_i = (V_i - Z0_i * I_i) / sqrt_Z0_i;  // Reflected wave at port i
    Complex a_j = (V_j + Z0_j * I_j) / sqrt_Z0_j;  // Incident wave at port j

    // S_ij = b_i / a_j (when only port j is excited)
    if (std::abs(a_j) < 1e-20) {
        spdlog::warn("Incident wave at port {} is near zero, S-parameter calculation may be inaccurate", is_excitation_port ? 'j' : 'i');
        return Complex(0.0, 0.0);
    }

    Complex S_ij = b_i / a_j;

    spdlog::trace("    S_param calc: a_j=({:.6e},{:.6e}), b_i=({:.6e},{:.6e}), S_ij=({:.6e},{:.6e})",
                 a_j.real(), a_j.imag(), b_i.real(), b_i.imag(), S_ij.real(), S_ij.imag());

    return S_ij;
}

} // namespace kirimaspice::solvers
