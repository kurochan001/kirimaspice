// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/**
 * @file convergence_checker.hpp
 * @brief Unified convergence checker for DC and Transient solvers
 *
 * This class consolidates convergence checking logic that was duplicated
 * in DCSolver and TransientSolver.
 *
 * @author Claude (Anthropic)
 * @date 2025-12-05
 */

#pragma once

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"

#include <cstdint>
#include <optional>

namespace kirimaspice::circuit {
class CircuitGraph;
}

namespace kirimaspice::solvers {

/// True when a device limited a junction voltage in its last stamp
/// (Device::limited_in_last_stamp). An iterate that a limited stamp produced
/// is not accepted as converged, whatever its change from the last iterate.
bool any_device_limited(const circuit::CircuitGraph& circuit);

/// .OPTIONS VOLTAGE_LIMITING=0 turns junction limiting off in every device
/// (Device::set_voltage_limiting). With the default of 1 the devices keep
/// their own setting, which is on unless the library turned it off.
void apply_voltage_limiting_option(const circuit::CircuitGraph& circuit,
                                   const SimulationConfig& config);

/**
 * @brief Configuration for convergence checking
 *
 * Contains all tolerance parameters used by ConvergenceChecker.
 * Can be initialized from SimulationConfig.dc or SimulationConfig.transient.
 */
struct ConvergenceConfig {
    Real abstol = 1e-12;    ///< Absolute tolerance for currents
    Real reltol = 1e-3;     ///< Relative tolerance
    Real vntol = 1e-6;      ///< Voltage node tolerance

    /// If true, also check residual (f = Gx - b). Used by DC solver.
    bool check_residual = true;

    /// If true, relax residual tolerance when solution is stable.
    /// Helps linear circuits where floating-point errors prevent abstol satisfaction.
    bool relax_for_linear = true;

    /// Multiplier for residual tolerance when solution is stable
    Real linear_relax_factor = 1e6;

    /// Threshold for solution stability check (very tight)
    Real stability_threshold = 1e-9;

    /// Multiplier for branch current tolerance (vs node voltage)
    Real branch_tol_multiplier = 1e3;
};

/**
 * @brief Unified convergence checker for Newton-Raphson iterations
 *
 * Supports both DC and Transient convergence checking with configurable
 * behavior via ConvergenceConfig.
 *
 * Usage:
 * @code
 * ConvergenceChecker checker(num_nodes);
 * ConvergenceConfig config;
 * config.abstol = 1e-12;
 * config.check_residual = true;  // DC mode
 *
 * bool converged = checker.check(x_new, x_old, config, &f_new);
 * @endcode
 */
class ConvergenceChecker {
public:
    /**
     * @brief Construct a convergence checker
     * @param num_nodes Number of node voltages in the solution vector
     */
    explicit ConvergenceChecker(std::uint32_t num_nodes);

    /**
     * @brief Check if Newton-Raphson iteration has converged
     *
     * For DC analysis (check_residual=true):
     * 1. Check solution stability
     * 2. Check residual against tolerances (relaxed if stable)
     * 3. Check solution change (absolute + relative)
     *
     * For Transient analysis (check_residual=false):
     * 1. Check solution change with combined tolerance
     *
     * @param x_new New solution vector
     * @param x_old Previous solution vector
     * @param config Convergence configuration
     * @param f_new Residual vector (optional, required if check_residual=true)
     * @return true if converged
     */
    bool check(const DenseVector& x_new,
               const DenseVector& x_old,
               const ConvergenceConfig& config,
               const DenseVector* f_new = nullptr) const;

    /**
     * @brief Check if solution is stable (for linear circuit detection)
     *
     * A solution is considered stable when all elements have changed
     * by less than a very tight threshold between iterations.
     *
     * @param x_new New solution vector
     * @param x_old Previous solution vector
     * @param threshold Stability threshold (default: 1e-9)
     * @return true if solution is stable
     */
    bool is_solution_stable(const DenseVector& x_new,
                           const DenseVector& x_old,
                           Real threshold) const;

    /**
     * @brief Get the number of nodes
     * @return Number of node voltages
     */
    std::uint32_t num_nodes() const { return num_nodes_; }

    /**
     * @brief Set the number of nodes (for reuse with different circuits)
     * @param num_nodes Number of node voltages
     */
    void set_num_nodes(std::uint32_t num_nodes) { num_nodes_ = num_nodes; }

private:
    /**
     * @brief Check residual convergence (DC mode)
     * @return true if all residuals are within tolerance
     */
    bool check_residual_convergence(const DenseVector& f_new,
                                    const ConvergenceConfig& config,
                                    Real tol_multiplier) const;

    /**
     * @brief Check solution change convergence (DC mode)
     * @return true if all changes are within tolerance
     */
    bool check_solution_change_dc(const DenseVector& x_new,
                                  const DenseVector& x_old,
                                  const ConvergenceConfig& config) const;

    /**
     * @brief Check solution change convergence (Transient mode)
     * @return true if all changes are within tolerance
     */
    bool check_solution_change_transient(const DenseVector& x_new,
                                         const DenseVector& x_old,
                                         const ConvergenceConfig& config) const;

    std::uint32_t num_nodes_;
};

}  // namespace kirimaspice::solvers
