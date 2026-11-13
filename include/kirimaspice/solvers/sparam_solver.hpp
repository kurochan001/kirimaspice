// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/sparam_solver.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/isparse_solver.hpp"
#include <vector>
#include <memory>

namespace kirimaspice::solvers {

/// S-parameter analysis solver
/// Performs network parameter analysis using AC small-signal analysis
class SParameterSolver {
public:
    /// Constructor
    /// @param circuit Circuit graph containing devices
    /// @param config Simulation configuration (including S-parameter settings)
    SParameterSolver(const circuit::CircuitGraph& circuit, const SimulationConfig& config);

    /// Perform S-parameter analysis
    /// @return S-parameter analysis result with frequency sweep
    SParameterResult solve();

private:
    // ========================================================================
    // Internal methods
    // ========================================================================

    /// Compute DC operating point (prerequisite for AC/S-parameter analysis)
    /// @return DC solution vector
    DenseVector compute_dc_operating_point();

    /// Linearize all devices at DC operating point
    /// Computes small-signal parameters (gm, rπ, Cπ, etc.)
    void linearize_devices();

    /// Generate frequency points based on sweep type (LIN/DEC/OCT)
    /// @return Vector of frequencies [Hz]
    std::vector<Real> generate_frequency_points() const;

    /// Calculate S-parameter matrix at a single frequency
    /// @param omega Angular frequency [rad/s]
    /// @return S-parameter matrix S[i][j] (N×N for N-port network)
    std::vector<std::vector<Complex>> calculate_s_matrix(Real omega);

    /// Solve AC system with port excitation
    /// @param omega Angular frequency [rad/s]
    /// @param excitation_port Port number to excite (1-indexed)
    /// @return Complex solution vector (node voltages as phasors)
    ComplexDenseVector solve_with_port_excitation(Real omega, std::uint32_t excitation_port);

    /// Build complex MNA system for S-parameter analysis
    /// @param sys Complex MNA system to populate
    /// @param omega Angular frequency [rad/s]
    /// @param excitation_port Port to excite (1-indexed)
    void build_sparam_system(ComplexMNASystem& sys, Real omega, std::uint32_t excitation_port);

    /// Convert complex MNA system to real system (real/imag separation)
    /// @param Y_complex Complex admittance matrix
    /// @param I_complex Complex current source vector
    /// @param system_real Output: Real-valued system (2N × 2N)
    void convert_to_real_system(const ComplexSparseMatrix& Y_complex,
                                const ComplexDenseVector& I_complex,
                                MNASystem& system_real);

    /// Convert real solution back to complex
    /// @param V_real Real solution vector [Re(V); Im(V)]
    /// @param V_complex Output: Complex solution vector
    void convert_to_complex_solution(const DenseVector& V_real,
                                     ComplexDenseVector& V_complex);

    /// Solve real-valued linear system using KLU
    /// @param system MNA system (Ax = b)
    /// @return Solution vector x
    DenseVector solve_linear_system(MNASystem& system);

    /// Extract port voltage from solution
    /// @param solution Complex node voltage solution
    /// @param port_num Port number (1-indexed)
    /// @return Port voltage V_port = V_pos - V_neg
    Complex get_port_voltage(const ComplexDenseVector& solution, std::uint32_t port_num) const;

    /// Calculate port current from solution
    /// @param solution Complex node voltage solution
    /// @param port_num Port number (1-indexed)
    /// @param omega Angular frequency [rad/s]
    /// @param excitation_port Which port is being excited (1-indexed)
    /// @return Port current I_port (flowing into the port from network)
    Complex get_port_current(const ComplexDenseVector& solution,
                            std::uint32_t port_num,
                            Real omega,
                            std::uint32_t excitation_port) const;

    /// Calculate single S-parameter S_ij
    /// @param V_i Voltage at port i
    /// @param I_i Current at port i (flowing out of port)
    /// @param V_j Voltage at port j
    /// @param I_j Current at port j (flowing out of port)
    /// @param Z0_i Characteristic impedance of port i
    /// @param Z0_j Characteristic impedance of port j
    /// @param is_excitation_port True if port j is the excitation port
    /// @return S_ij = (V_i - Z0_i * I_i) / (V_j + Z0_j * I_j)
    Complex calculate_s_parameter(const Complex& V_i, const Complex& I_i,
                                  const Complex& V_j, const Complex& I_j,
                                  Real Z0_i, Real Z0_j,
                                  bool is_excitation_port) const;

    // ========================================================================
    // Member variables
    // ========================================================================

    const circuit::CircuitGraph& circuit_; ///< Circuit graph reference
    const SimulationConfig& config_;       ///< Configuration reference
    DenseVector dc_solution_;              ///< DC operating point solution

    // Port node ID mapping (port_number -> {node_pos_id, node_neg_id})
    std::vector<std::pair<NodeId, NodeId>> port_node_ids_;

    // Statistics
    std::uint64_t total_frequency_points_;  ///< Total frequency points evaluated
    std::uint64_t total_linear_solves_;     ///< Total linear system solves

    // Reusable linear solver across excitations/frequencies
    std::unique_ptr<ISparseLinearSolver> lin_solver_;
    bool first_factor_done_ = false;
};

} // namespace kirimaspice::solvers
