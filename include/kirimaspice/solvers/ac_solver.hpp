// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/ac_solver.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/isparse_solver.hpp"
#include "kirimaspice/utils/memory_pool.hpp"
#include <vector>
#include <memory>

namespace kirimaspice::solvers {

/// The frequencies of an AC sweep (LIN, DEC or OCT) of the given
/// configuration, from start_freq to stop_freq; empty when the
/// configuration is invalid. The AC, noise and S-parameter analyses take
/// their frequencies from here.
///
/// LIN gives num_points points from start to stop. DEC and OCT go up by a
/// fixed ratio from start_freq while the frequency is at most
/// stop_freq + ratio * stop_freq * reltol, as SPICE's AC analysis does:
/// OCT by 2^(1/N); DEC by 10^(1/N) when stop_freq is less than a decade
/// above start_freq, and otherwise by the ratio that reaches stop_freq in
/// floor(N log10(stop/start)) steps (ngspice-47's grids, run 2026-11-21).
/// reltol is the RELTOL of .OPTIONS.
std::vector<Real> ac_frequency_points(const SimulationConfig::AC& ac_config, Real reltol = 1e-3);


/// AC analysis result for a single frequency point
struct ACFrequencyPoint {
    Real frequency;              ///< Frequency [Hz]
    Real omega;                  ///< Angular frequency [rad/s]
    ComplexDenseVector solution; ///< Node voltages (complex phasors)
};

/// AC analysis result
struct ACResult {
    std::vector<ACFrequencyPoint> frequency_points; ///< Results at each frequency
    ConvergenceStatus status;                       ///< Overall convergence status
    SolverStats stats;                              ///< Solver statistics
};

/// AC analysis solver
/// Performs small-signal frequency-domain analysis
class ACSolver {
public:
    /// Constructor
    /// @param circuit Circuit graph containing devices
    /// @param config Simulation configuration (including AC settings)
    ACSolver(const circuit::CircuitGraph& circuit, const SimulationConfig& config);

    /// Destructor
    ~ACSolver() = default;

    // Explicit copy/move semantics
    // Non-copyable due to unique_ptr and reference members
    ACSolver(const ACSolver&) = delete;
    ACSolver& operator=(const ACSolver&) = delete;

    // Movable (unique_ptr is movable, reference member prevents default move)
    ACSolver(ACSolver&&) noexcept = default;
    ACSolver& operator=(ACSolver&&) noexcept = default;

    /// Perform AC analysis
    /// @return AC analysis result with frequency response
    ACResult solve();

private:
    // ========================================================================
    // Internal methods
    // ========================================================================

    /// Compute DC operating point (prerequisite for AC analysis)
    /// @return DC solution vector
    DenseVector compute_dc_operating_point();

    /// Linearize all devices at DC operating point
    /// Computes small-signal parameters (gm, rπ, Cπ, etc.)
    void linearize_devices();

    /// Generate frequency points based on sweep type (lin/dec/oct)
    /// @return Vector of frequencies [Hz]
    std::vector<Real> generate_frequency_points() const;

    /// Solve AC system at a single frequency
    /// @param omega Angular frequency [rad/s]
    /// @return Complex solution vector (phasors)
    ComplexDenseVector solve_at_frequency(Real omega);

    /// Convert complex MNA system to real system (real/imag separation)
    /// Y_complex * V_complex = I_complex
    /// becomes:
    /// [Re(Y)  -Im(Y)] [Re(V)]   [Re(I)]
    /// [Im(Y)   Re(Y)] [Im(V)] = [Im(I)]
    ///
    /// @param Y_complex Complex admittance matrix
    /// @param I_complex Complex current source vector
    /// @param Y_real Output: Real-valued system matrix (2N × 2N)
    /// @param I_real Output: Real-valued RHS vector (2N)
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

    /// Add small conductance (Gmin) from every node to ground in AC admittance matrix
    void add_ac_gmin(ComplexMNASystem& system) const;

    // ========================================================================
    // Member variables
    // ========================================================================

    const circuit::CircuitGraph& circuit_; ///< Circuit graph reference
    const SimulationConfig& config_;       ///< Configuration reference
    DenseVector dc_solution_;              ///< DC operating point solution

    // Statistics
    std::uint64_t total_frequency_points_;  ///< Total frequency points evaluated
    std::uint64_t total_linear_solves_;     ///< Total linear system solves

    // Reusable linear solver for frequency sweep
    std::unique_ptr<ISparseLinearSolver> lin_solver_;
    bool first_factor_done_ = false;

    // Memory pools to recycle large working buffers between frequency points
    mutable utils::MemoryPool<ComplexMNASystem> complex_system_pool_;
    mutable utils::MemoryPool<MNASystem> real_system_pool_;
};

} // namespace kirimaspice::solvers
