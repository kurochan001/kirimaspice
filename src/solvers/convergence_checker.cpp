/**
 * @file convergence_checker.cpp
 * @brief Implementation of unified convergence checker
 *
 * @author Claude (Anthropic)
 * @date 2025-12-05
 */

#include "kirimaspice/solvers/convergence_checker.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace kirimaspice::solvers {

ConvergenceChecker::ConvergenceChecker(std::uint32_t num_nodes)
    : num_nodes_(num_nodes) {}

bool ConvergenceChecker::check(const DenseVector& x_new,
                               const DenseVector& x_old,
                               const ConvergenceConfig& config,
                               const DenseVector* f_new) const {
    // Size check
    if (x_new.size() != x_old.size()) {
        spdlog::debug("ConvergenceChecker: size mismatch ({} vs {})",
                     x_new.size(), x_old.size());
        return false;
    }

    if (config.check_residual) {
        // DC mode: check residual + solution change
        if (!f_new) {
            spdlog::warn("ConvergenceChecker: check_residual=true but f_new is nullptr");
            return false;
        }

        // Check solution stability for linear circuit relaxation
        Real tol_multiplier = 1.0;
        if (config.relax_for_linear) {
            bool stable = is_solution_stable(x_new, x_old, config.stability_threshold);
            if (stable) {
                tol_multiplier = config.linear_relax_factor;
            }
        }

        // Check residual
        if (!check_residual_convergence(*f_new, config, tol_multiplier)) {
            return false;
        }

        // Check solution change
        if (!check_solution_change_dc(x_new, x_old, config)) {
            return false;
        }

        spdlog::debug("Convergence achieved");
        return true;
    } else {
        // Transient mode: check solution change only
        return check_solution_change_transient(x_new, x_old, config);
    }
}

bool ConvergenceChecker::is_solution_stable(const DenseVector& x_new,
                                            const DenseVector& x_old,
                                            Real threshold) const {
    for (std::size_t i = 0; i < x_new.size(); ++i) {
        Real change = std::abs(x_new[i] - x_old[i]);
        if (change > threshold) {
            return false;
        }
    }
    return true;
}

bool ConvergenceChecker::check_residual_convergence(const DenseVector& f_new,
                                                    const ConvergenceConfig& config,
                                                    Real tol_multiplier) const {
    for (std::size_t i = 0; i < f_new.size(); ++i) {
        Real residual = std::abs(f_new[i]);

        // Different tolerance for node voltages vs branch currents
        Real tol = (i < num_nodes_)
                       ? config.abstol * tol_multiplier
                       : config.abstol * config.branch_tol_multiplier * tol_multiplier;

        if (residual > tol) {
            spdlog::debug("Residual[{}] = {:.3e} > {:.3e} (not converged)",
                         i, residual, tol);
            return false;
        }
    }
    return true;
}

bool ConvergenceChecker::check_solution_change_dc(const DenseVector& x_new,
                                                  const DenseVector& x_old,
                                                  const ConvergenceConfig& config) const {
    for (std::size_t i = 0; i < x_new.size(); ++i) {
        Real change = std::abs(x_new[i] - x_old[i]);
        Real relative_change = change / (std::abs(x_new[i]) + config.vntol);

        if (i < num_nodes_) {
            // Voltage tolerance for nodes
            if (change > config.vntol && relative_change > config.reltol) {
                spdlog::debug("Node[{}]: change={:.3e}, rel={:.3e} (not converged)",
                             i, change, relative_change);
                return false;
            }
        } else {
            // Current tolerance for branches
            if (change > config.abstol && relative_change > config.reltol) {
                spdlog::debug("Branch[{}]: change={:.3e}, rel={:.3e} (not converged)",
                             i - num_nodes_, change, relative_change);
                return false;
            }
        }
    }
    return true;
}

bool ConvergenceChecker::check_solution_change_transient(const DenseVector& x_new,
                                                         const DenseVector& x_old,
                                                         const ConvergenceConfig& config) const {
    // Check voltage convergence (node voltages)
    for (std::size_t i = 0; i < num_nodes_; ++i) {
        Real delta = std::abs(x_new[i] - x_old[i]);
        Real tol = config.reltol * std::max(std::abs(x_new[i]), std::abs(x_old[i])) + config.vntol;

        if (delta > tol) {
            return false;
        }
    }

    // Check current convergence (branch currents)
    for (std::size_t i = num_nodes_; i < x_new.size(); ++i) {
        Real delta = std::abs(x_new[i] - x_old[i]);
        Real tol = config.reltol * std::max(std::abs(x_new[i]), std::abs(x_old[i])) + config.abstol;

        if (delta > tol) {
            return false;
        }
    }

    return true;
}

}  // namespace kirimaspice::solvers
