// include/kirimaspice/analysis/circuit_optimizer.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/measure/measure.hpp"
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <functional>

namespace kirimaspice::analysis {

/// Circuit optimizer (+ )
/// Optimizes circuit parameters to minimize/maximize an objective function
/// Supports 4 NLopt algorithms: COBYLA, BOBYQA, SLSQP, Nelder-Mead
class CircuitOptimizer {
public:
    /// Constructor
    /// @param circuit Circuit to optimize
    /// @param config Simulation configuration (includes optimizer settings)
    CircuitOptimizer(circuit::CircuitGraph& circuit, const SimulationConfig& config);

    /// Execute optimization
    /// @return Optimization result with optimal parameters
    OptimizerResult optimize();

private:
    /// Objective function type
    using ObjectiveFunction = std::function<Real(const std::vector<Real>&)>;

    /// Evaluate objective function for given parameter values
    /// @param params Parameter values (in order of config_.optimizer.parameters)
    /// @return Objective function value
    Real evaluate_objective(const std::vector<Real>& params);

    /// Apply parameter values to circuit
    /// @param params Parameter values
    void apply_parameters(const std::vector<Real>& params);

    /// Simple grid search optimization (placeholder implementation, legacy)
    /// @return Optimal parameter values
    std::vector<Real> grid_search_optimize();

    /// Random search optimization (placeholder implementation, legacy)
    /// @return Optimal parameter values
    std::vector<Real> random_search_optimize();

    /// COBYLA optimization using NLopt
    /// @return Optimal parameter values
    std::vector<Real> cobyla_optimize();

    /// BOBYQA optimization using NLopt
    /// Trust region method for derivative-free box-constrained optimization
    /// @return Optimal parameter values
    std::vector<Real> bobyqa_optimize();

    /// SLSQP optimization using NLopt
    /// Gradient-based Sequential Least SQuares Programming
    /// @return Optimal parameter values
    std::vector<Real> slsqp_optimize();

    /// Nelder-Mead optimization using NLopt
    /// Simplex-based derivative-free optimization
    /// @return Optimal parameter values
    std::vector<Real> neldermead_optimize();

    /// NLopt objective function wrapper (static callback)
    /// @param x Parameter values
    /// @param grad Gradient (not used for gradient-free methods)
    /// @param data User data (CircuitOptimizer*)
    /// @return Objective function value
    static double nlopt_objective_wrapper(const std::vector<double>& x,
                                         std::vector<double>& grad,
                                         void* data);

    /// NLopt constraint function wrapper (static callback)
    /// @param x Parameter values
    /// @param grad Gradient (not used)
    /// @param data User data (pair<CircuitOptimizer*, constraint_config>)
    /// @return Constraint function value (c(x) <= 0 form)
    static double nlopt_constraint_wrapper(const std::vector<double>& x,
                                          std::vector<double>& grad,
                                          void* data);

    circuit::CircuitGraph& circuit_;
    const SimulationConfig& config_;
    std::uint32_t num_evaluations_ = 0;
};

} // namespace kirimaspice::analysis
