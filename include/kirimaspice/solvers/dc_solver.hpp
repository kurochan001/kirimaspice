// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/dc_solver.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/convergence_checker.hpp"
#include "kirimaspice/solvers/isparse_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"
#include "kirimaspice/solvers/thread_local_mna.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kirimaspice::solvers {

/// DC operating point solver
/// Uses Newton-Raphson iteration to solve nonlinear DC equations
class DCSolver {
public:
    /// Constructor
    /// @param circuit Circuit graph
    /// @param config Simulation configuration
    explicit DCSolver(const circuit::CircuitGraph& circuit,
                      const SimulationConfig& config = SimulationConfig{});

    ~DCSolver();

    // Explicit copy/move semantics
    // Non-copyable due to unique_ptr and reference members
    DCSolver(const DCSolver&) = delete;
    DCSolver& operator=(const DCSolver&) = delete;

    // Movable (unique_ptr is movable, reference member prevents default move)
    DCSolver(DCSolver&&) noexcept = default;
    DCSolver& operator=(DCSolver&&) noexcept = default;

    /// Solve DC operating point
    /// @param solution Output: node voltages and branch currents
    /// @param stats Output: solver statistics
    /// @return Convergence status
    ConvergenceStatus solve(DenseVector& solution, SolverStats& stats);

    /// Solve DC sweep analysis
    /// @param config Sweep configuration
    /// @param result Output: sweep results
    /// @return Overall convergence status
    ConvergenceStatus solve_sweep(const DCSweepConfig& config, DCSweepResult& result);

    /// Solve temperature sweep analysis
    /// @param result Output: temperature sweep results
    /// @return Overall convergence status
    ConvergenceStatus solve_temp_sweep(TemperatureSweepResult& result);

    /// Get MNA system (for debugging)
    const MNASystem& get_mna_system() const { return mna_; }

    /// Get branch current for a device by name
    /// @param device_name Name of device (e.g., "V1", "VCVS1")
    /// @return Branch current [A] if device has branch current, std::nullopt otherwise
    std::optional<Real> get_branch_current(const std::string& device_name) const;

    /// Hold node voltages while solving, as SPICE does with .IC for the
    /// operating point that starts a transient without UIC: each named node
    /// is tied to its value through kHoldConductance, so that the node is
    /// within I / kHoldConductance of the value whatever current I the rest
    /// of the circuit draws. An empty map releases all nodes.
    void hold_node_voltages(const std::map<std::string, Real>& voltages);
    static constexpr Real kHoldConductance = 1e10;  ///< [S]

private:
    const circuit::CircuitGraph& circuit_;  ///< Circuit reference
    SimulationConfig config_;               ///< Simulation configuration
    MNASystem mna_;                         ///< MNA system
    std::unique_ptr<ISparseLinearSolver> lin_solver_;  ///< Linear solver (selectable backend)
    DenseVector last_solution_;             ///< Last solved solution
    bool has_nonlinear_devices_ = false;    ///< True if circuit contains any nonlinear device (for voltage limiting)

    // Unified thread-local MNA manager for parallel device stamping
    ThreadLocalMNAManager thread_local_mna_mgr_;  ///< Thread-local MNA systems (reused across iterations)

    /// Build MNA system for current iteration
    /// @param node_voltages Current solution estimate
    void build_mna_system(const DenseVector& node_voltages);

    /// Stamp all devices into MNA system
    void stamp_devices(const DenseVector& node_voltages);

    /// Newton's convergence test: the step from x_old to x_new is below
    /// RELTOL of the values plus VNTOL (ABSTOL for branch currents), no
    /// device limited its junction in the stamps at x_old or at x_new, and
    /// Kirchhoff's current law holds at x_new with the devices' own currents
    /// there (kirchhoff_current_law_holds). extra_gmin is the gmin from each
    /// node to ground that the iteration solved with (gmin stepping, dynamic
    /// gmin), so that KCL is tested on the same circuit.
    bool check_convergence(const DenseVector& x_new, const DenseVector& x_old,
                           Real extra_gmin = 0.0) const;

    /// Compute residual: f = Gx - b
    void compute_residual(const DenseVector& x, DenseVector& f) const;

    /// Initialize solution vector with reasonable guess
    void initialize_solution(DenseVector& x) const;

    /// Dump MNA system to files for external verification (debugging)
    /// @param x_new Current solution vector
    void dump_mna_system_to_files(const DenseVector& x_new) const;

    /// Extract all branch currents from solution vector
    /// @param solution Full solution vector [node_voltages, branch_currents]
    /// @return Dense vector containing only branch currents (indexed by branch_id)
    DenseVector extract_all_branch_currents(const DenseVector& solution) const;

    // Source Stepping support
    /// Set scale factor for all independent sources (V/I)
    /// @param scale Scale factor (0.0 to 1.0)
    void set_source_scale_factor(Real scale);

    /// Solve DC operating point with source stepping
    /// @param solution Output: node voltages and branch currents
    /// @param stats Output: solver statistics
    /// @return Convergence status
    ConvergenceStatus solve_with_source_stepping(DenseVector& solution, SolverStats& stats);

    // Gmin Stepping support
    /// Add Gmin conductance to all nodes (except GND)
    /// @param gmin Conductance value to add [S]
    void add_gmin_to_nodes(Real gmin);

    /// Solve DC operating point with Gmin stepping
    /// @param solution Output: node voltages and branch currents
    /// @param stats Output: solver statistics
    /// @return Convergence status
    ConvergenceStatus solve_with_gmin_stepping(DenseVector& solution, SolverStats& stats);

    /// Continuation (homotopy) method wrapper
    ConvergenceStatus solve_with_continuation(DenseVector& solution, SolverStats& stats);

    /// Pseudo-transient fallback
    ConvergenceStatus solve_with_pseudo_transient(DenseVector& solution, SolverStats& stats);

    void apply_pseudo_capacitance_terms(Real capacitance, Real timestep, const DenseVector& prev_state);
    void set_initial_guess_override(const DenseVector& guess);
    void clear_initial_guess_override();
    ConvergenceStatus solve_standard(DenseVector& solution, SolverStats& stats);

private:
    /// Set voltage/current source value by name
    /// @param source_name Name of source (e.g., "V1", "I1")
    /// @param value New DC value [V or A]
    /// @return true if source found and set successfully
    bool set_source_value(const std::string& source_name, Real value);

    /// The swept source's DC setting before a sweep (its value and whether
    /// the netlist gave one), and putting it back
    struct SourceSetting {
        devices::VoltageSource* voltage = nullptr;
        devices::CurrentSource* current = nullptr;
        Real value = 0.0;
        bool given = true;
    };
    SourceSetting save_source_setting(const std::string& source_name) const;
    static void restore_source_setting(const SourceSetting& saved);

    bool use_initial_guess_override_ = false;
    DenseVector initial_guess_override_;
    std::vector<std::pair<std::uint32_t, Real>> held_nodes_;  ///< (MNA row, voltage)
};

} // namespace kirimaspice::solvers
