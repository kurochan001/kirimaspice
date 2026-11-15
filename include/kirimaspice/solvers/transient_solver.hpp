// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/transient_solver.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/convergence_checker.hpp"
#include "kirimaspice/solvers/isparse_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"
#include "kirimaspice/solvers/thread_local_mna.hpp"
#include <memory>
#include <vector>
#include <string>

namespace kirimaspice::solvers {

// Forward declaration
class DCSolver;

/// Time point result
struct TimePoint {
    Real time;
    DenseVector node_voltages;
    DenseVector branch_currents;
};

/// Transient analysis results
struct TransientResult {
    std::vector<TimePoint> timepoints;
    SolverStats stats;
    ConvergenceStatus status;
};

/// Transient temperature sweep point
/// Result of transient analysis at a specific temperature
struct TransientTemperatureSweepPoint {
    Real temperature;              ///< Temperature [K]
    TransientResult result;        ///< Transient analysis result at this temperature
};

/// Transient temperature sweep result
/// Collection of transient results at multiple temperatures
struct TransientTemperatureSweepResult {
    std::vector<TransientTemperatureSweepPoint> points;  ///< Results at each temperature
    ConvergenceStatus status;                            ///< Overall status

    /// Check if all temperature points converged successfully
    bool all_converged() const {
        return std::all_of(points.begin(), points.end(),
            [](const TransientTemperatureSweepPoint& p) {
                return p.result.status == ConvergenceStatus::SUCCESS;
            });
    }
};

/// Transient solver using backward Euler integration
class TransientSolver {
public:
    explicit TransientSolver(const circuit::CircuitGraph& circuit,
                            const SimulationConfig& config = SimulationConfig{});
    ~TransientSolver();

    // Explicit copy/move semantics
    // Non-copyable due to unique_ptr and reference members
    TransientSolver(const TransientSolver&) = delete;
    TransientSolver& operator=(const TransientSolver&) = delete;

    // Movable (unique_ptr is movable, reference member prevents default move)
    TransientSolver(TransientSolver&&) noexcept = default;
    TransientSolver& operator=(TransientSolver&&) noexcept = default;

    /// Run transient analysis from t_start to t_stop
    TransientResult solve(Real t_start, Real t_stop, Real timestep);

    /// Run transient temperature sweep
    /// @param t_start Start time [s]
    /// @param t_stop Stop time [s]
    /// @param timestep Time step [s]
    /// @param result Output: temperature sweep result
    /// @return Overall convergence status
    ConvergenceStatus solve_temp_sweep(Real t_start, Real t_stop, Real timestep,
                                       TransientTemperatureSweepResult& result);

    /// Get current MNA system
    const MNASystem& get_mna_system() const { return mna_; }

private:
    const circuit::CircuitGraph& circuit_;
    SimulationConfig config_;
    MNASystem mna_;
    std::unique_ptr<ISparseLinearSolver> lin_solver_;
    std::unique_ptr<DCSolver> dc_solver_;

    // Unified thread-local MNA manager for parallel device stamping
    ThreadLocalMNAManager thread_local_mna_mgr_;  ///< Thread-local MNA systems (reused across timesteps)

    // Adaptive order control state
    std::uint32_t current_order_ = 1;  ///< Current integration order (1=BDF1, 2=BDF2, 3=BDF3)
    std::uint32_t history_count_ = 0;  ///< Number of accumulated history steps

    // Variable-timestep BDF support
    std::vector<Real> timestep_history_;  ///< History of last 3 timesteps for BDF coefficient computation

    // Solution history for predictor-corrector error estimation
    std::vector<DenseVector> solution_history_;  ///< History of last 3 solutions for extrapolation

    // Current simulation time (for time-varying sources)
    Real current_time_ = 0.0;  ///< Current simulation time in transient analysis

    /// Initialize operating point using DC solver
    bool initialize_dc_operating_point(DenseVector& solution);

    /// Build MNA system for transient analysis at current time
    void build_mna_transient(const DenseVector& node_voltages, Real timestep,
                            IntegrationMethod method,
                            const BDFCoefficients* bdf_coef = nullptr,
                            const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr);

    /// Give every device the time of the next stamps, or none after the
    /// transient (Device::set_time)
    void set_device_time(std::optional<Real> time);

    /// Stamp all devices for transient analysis
    void stamp_devices_transient(const DenseVector& node_voltages, Real timestep,
                                IntegrationMethod method,
                                const BDFCoefficients* bdf_coef = nullptr,
                                const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr);

    /// Perform one Newton-Raphson step at given timepoint
    ConvergenceStatus solve_timestep(DenseVector& solution, Real timestep,
                                    std::uint32_t& iterations,
                                    IntegrationMethod method,
                                    const BDFCoefficients* bdf_coef = nullptr,
                                    const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr);

    /// Solve one timestep using full TR-BDF2 two-stage method
    /// Stage 1: Trapezoidal rule from t_n to t_n + γΔt (γ = 2 - √2)
    /// Stage 2: BDF2 from t_n, t_n+γΔt to t_n+Δt
    /// Returns convergence status
    ConvergenceStatus solve_timestep_tr_bdf2(DenseVector& solution, Real timestep,
                                            std::uint32_t& iterations);

    /// Solve one timestep with adaptive timestep control
    /// CRITICAL FIX: Returns {status, accepted_dt, next_dt, accepted}
    /// - accepted_dt: actual timestep that was accepted (may differ from current_dt after rejections)
    /// - next_dt: recommended timestep for next iteration
    std::tuple<ConvergenceStatus, Real, Real, bool>
    solve_timestep_adaptive(DenseVector& solution, Real current_dt,
                           std::uint32_t& iterations, std::uint32_t& rejects);

    /// Check convergence for Newton-Raphson iteration
    bool check_convergence(const DenseVector& x_new,
                          const DenseVector& x_old) const;

    /// Save device states before timestep
    void save_device_states();

    /// Restore device states (for rejected timesteps)
    void restore_device_states();

    /// Accept timestep solution and update device histories
    void accept_timestep_solution(const DenseVector& solution);

    /// Let every device accept node_voltages (index 0 = ground). Mutual
    /// inductances correct the currents their inductors have just stored, so
    /// they accept after every other device, whatever the netlist order.
    void accept_devices(const DenseVector& node_voltages);

    /// Estimate local truncation error using embedded method (BE vs Trap)
    /// Returns maximum normalized LTE across all nodes
    Real estimate_lte(const DenseVector& solution_low_order,
                      const DenseVector& solution_high_order) const;

    /// Predict next solution using extrapolation (Predictor)
    /// Uses linear or quadratic extrapolation based on available history
    /// @param current_dt Current timestep
    /// @return Predicted solution vector
    DenseVector predict_solution(Real current_dt) const;

    /// Estimate local truncation error using Predictor-Corrector difference
    /// Uses difference between predicted and corrected solutions as error estimate
    /// @param predicted Predicted solution (from extrapolation)
    /// @param corrected Corrected solution (from Newton-Raphson)
    /// @return Maximum normalized LTE across all nodes
    Real estimate_lte_predictor_corrector(const DenseVector& predicted,
                                          const DenseVector& corrected) const;

    /// Calculate new timestep based on LTE estimation
    /// Returns {new_timestep, accept_current_step}
    std::pair<Real, bool> adjust_timestep(Real current_dt, Real lte_estimate,
                                          std::uint32_t order) const;

    /// Select integration order based on error estimate and history availability
    /// Returns the appropriate IntegrationMethod for current conditions
    IntegrationMethod select_integration_order(Real lte_estimate);

    /// Update history count after accepting a timestep
    void update_history_count();

    /// Compute variable-timestep BDF2 coefficients
    /// @param dt_n Current timestep
    /// @param dt_prev Previous timestep
    /// @return BDF coefficients for current step
    BDFCoefficients compute_bdf2_coefficients(Real dt_n, Real dt_prev) const;

    /// Compute variable-timestep BDF3 coefficients
    /// @param dt_n Current timestep
    /// @param dt_prev1 Previous timestep (t_n - t_{n-1})
    /// @param dt_prev2 Second previous timestep (t_{n-1} - t_{n-2})
    /// @return BDF coefficients for current step
    BDFCoefficients compute_bdf3_coefficients(Real dt_n, Real dt_prev1, Real dt_prev2) const;

    /// Get BDF coefficients for current method and timestep
    /// @param method Integration method (GEAR2 or GEAR3)
    /// @param dt_n Current timestep
    /// @return BDF coefficients (uniform coefficients if not enough history)
    BDFCoefficients get_bdf_coefficients(IntegrationMethod method, Real dt_n) const;

    // ====== Refactored helper methods for solve() decomposition ======

    /// Check if circuit has dynamic devices (Capacitor or Inductor)
    /// @return true if circuit contains C or L
    bool check_has_dynamic_devices() const;

    /// Initialize simulation using UIC (Use Initial Conditions) mode
    /// Sets all voltages and currents to zero
    /// @param result Result structure to populate
    /// @param t_start Starting time
    /// @param solution Solution vector to initialize
    void initialize_uic_mode(TransientResult& result, Real t_start, DenseVector& solution);

    /// Initialize simulation using standard DC operating point
    /// Solves DC operating point and uses it as initial conditions
    /// @param result Result structure to populate
    /// @param t_start Starting time
    /// @param solution Solution vector to initialize
    /// @return true if initialization succeeded
    bool initialize_dc_mode(TransientResult& result, Real t_start, DenseVector& solution);

    /// Initialize simulation for pure DC circuit (no dynamic devices)
    /// @param result Result structure to populate
    /// @param t_start Starting time
    /// @param solution Solution vector to initialize
    /// @param timestep Timestep for solve
    /// @return true if initialization succeeded
    bool initialize_pure_dc_mode(TransientResult& result, Real t_start,
                                  DenseVector& solution, Real timestep);

    /// Initialize simulation (DC operating point / UIC mode / Pure DC)
    /// @param t_start Starting time
    /// @param timestep Timestep for solve
    /// @return Initialized TransientResult with initial conditions
    TransientResult initialize_simulation(Real t_start, Real timestep);

    /// Run time-stepping loop with adaptive timestep control
    /// @param result Result structure to populate
    /// @param solution Current solution vector
    /// @param t Current time (will be updated)
    /// @param t_stop Stop time
    /// @param dt Current timestep (will be updated)
    /// @param step_count Step counter (will be updated)
    void run_adaptive_timestepping(TransientResult& result, DenseVector& solution,
                                    Real& t, Real t_stop, Real& dt, std::uint32_t& step_count);

    /// Run time-stepping loop with fixed timestep
    /// @param result Result structure to populate
    /// @param solution Current solution vector
    /// @param t Current time (will be updated)
    /// @param t_stop Stop time
    /// @param dt Fixed timestep
    /// @param step_count Step counter (will be updated)
    void run_fixed_timestepping(TransientResult& result, DenseVector& solution,
                                 Real& t, Real t_stop, Real dt, std::uint32_t& step_count);

    /// Run time-stepping loop (adaptive or fixed)
    /// @param result Result structure to populate
    /// @param t_start Starting time
    /// @param t_stop Stop time
    /// @param timestep Initial/fixed timestep
    /// @return true if time-stepping succeeded
    bool run_timestepping_loop(TransientResult& result, Real t_start,
                                Real t_stop, Real timestep);
};

} // namespace kirimaspice::solvers
