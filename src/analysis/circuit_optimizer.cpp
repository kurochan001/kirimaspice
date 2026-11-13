// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/analysis/circuit_optimizer.cpp
#include "kirimaspice/analysis/circuit_optimizer.hpp"

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/measure/measure.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <spdlog/spdlog.h>

#include <nlopt.hpp>  // NLopt integration

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace kirimaspice::analysis {

CircuitOptimizer::CircuitOptimizer(circuit::CircuitGraph& circuit,
                                   const SimulationConfig& config)
    : circuit_(circuit), config_(config), num_evaluations_(0) {
    spdlog::info("CircuitOptimizer initialized with {} parameters",
                 config_.optimizer.parameters.size());
}

OptimizerResult CircuitOptimizer::optimize() {
    OptimizerResult result;
    result.success = false;

    if (config_.optimizer.parameters.empty()) {
        spdlog::error("No optimization parameters specified");
        return result;
    }

    spdlog::info("Starting circuit optimization with {} algorithm",
                 [&]() {
                     switch (config_.optimizer.algorithm) {
                         case OptimizerConfig::Algorithm::COBYLA:
                             return "COBYLA";
                         case OptimizerConfig::Algorithm::BOBYQA:
                             return "BOBYQA";
                         case OptimizerConfig::Algorithm::SLSQP:
                             return "SLSQP";
                         case OptimizerConfig::Algorithm::NELDERMEAD:
                             return "Nelder-Mead";
                         default:
                             return "Unknown";
                     }
                 }());

    // NLopt integration
    std::vector<Real> optimal_params;

    switch (config_.optimizer.algorithm) {
        case OptimizerConfig::Algorithm::COBYLA:
            optimal_params = cobyla_optimize();
            break;
        case OptimizerConfig::Algorithm::BOBYQA:
            optimal_params = bobyqa_optimize();
            break;
        case OptimizerConfig::Algorithm::SLSQP:
            optimal_params = slsqp_optimize();
            break;
        case OptimizerConfig::Algorithm::NELDERMEAD:
            optimal_params = neldermead_optimize();
            break;
        default:
            spdlog::error("Unknown optimization algorithm");
            return result;
    }

    // Evaluate final objective
    Real final_objective = evaluate_objective(optimal_params);

    // Store results
    result.success = true;
    for (std::size_t i = 0; i < optimal_params.size(); ++i) {
        result.optimal_parameters[config_.optimizer.parameters[i].parameter_name] =
            optimal_params[i];
    }
    result.optimal_value = final_objective;
    result.num_evaluations = num_evaluations_;
    result.num_iterations = num_evaluations_;
    result.message = "Grid/Random search completed";

    spdlog::info("Optimization completed: objective={:.6e}, evaluations={}",
                 final_objective, num_evaluations_);

    return result;
}

Real CircuitOptimizer::evaluate_objective(const std::vector<Real>& params) {
    ++num_evaluations_;

    // Apply parameters to circuit
    apply_parameters(params);

    // Run analysis based on analysis type
    Real objective_value = 0.0;
    std::string measure_name = config_.optimizer.objective_measure;

    switch (config_.optimizer.analysis_type) {
        case OptimizerConfig::AnalysisType::DC: {
            // DC analysis
            solvers::DCSolver dc_solver(circuit_, config_);
            DenseVector solution(circuit_.num_nodes());
            SolverStats stats;
            ConvergenceStatus status = dc_solver.solve(solution, stats);

            if (status != ConvergenceStatus::SUCCESS) {
                spdlog::warn("DC solve failed during optimization evaluation");
                return std::numeric_limits<Real>::infinity();
            }

            // Extract objective measure value from DC solution
            // Parse "v(node)" or "i(source)"
            if (measure_name.size() > 2 && measure_name[0] == 'v' && measure_name[1] == '(') {
                std::size_t close_paren = measure_name.find(')');
                if (close_paren != std::string::npos) {
                    std::string node_name = measure_name.substr(2, close_paren - 2);
                    auto node_id = circuit_.get_node_id(node_name);
                    // Node ID is 1-based (ground=0), solution vector is 0-indexed
                    if (node_id != NODE_INVALID && node_id > 0) {
                        objective_value = solution[node_id - 1];
                    }
                }
            }
            break;
        }

        case OptimizerConfig::AnalysisType::TRAN: {
            // Transient analysis
            solvers::TransientSolver tran_solver(circuit_, config_);
            solvers::TransientResult tran_result = tran_solver.solve(
                config_.optimizer.tran_start,
                config_.optimizer.tran_stop,
                config_.optimizer.tran_step
            );

            if (tran_result.status != ConvergenceStatus::SUCCESS) {
                spdlog::warn("Transient solve failed during optimization evaluation");
                return std::numeric_limits<Real>::infinity();
            }

            // Extract objective measure value from transient result
            // For simplicity, use final time point voltage
            // TODO [ROADMAP-OPTIMIZER-1]: Support .MEASURE-style extraction (MAX, MIN, INTEG, etc.)
            // See: (.measure) for existing measurement infrastructure
            if (!tran_result.timepoints.empty()) {
                const auto& final_tp = tran_result.timepoints.back();

                // Parse "v(node)"
                if (measure_name.size() > 2 && measure_name[0] == 'v' && measure_name[1] == '(') {
                    std::size_t close_paren = measure_name.find(')');
                    if (close_paren != std::string::npos) {
                        std::string node_name = measure_name.substr(2, close_paren - 2);
                        auto node_id = circuit_.get_node_id(node_name);
                        if (node_id != NODE_INVALID && node_id < final_tp.node_voltages.size()) {
                            objective_value = final_tp.node_voltages[node_id];
                        }
                    }
                }
            }
            break;
        }

        case OptimizerConfig::AnalysisType::AC: {
            // AC analysis
            // Create a temporary config with AC sweep parameters
            SimulationConfig temp_config = config_;
            temp_config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
            temp_config.ac.start_freq = config_.optimizer.ac_start_freq;
            temp_config.ac.stop_freq = config_.optimizer.ac_stop_freq;
            temp_config.ac.num_points = config_.optimizer.ac_points_per_decade *
                static_cast<std::uint32_t>(std::log10(config_.optimizer.ac_stop_freq /
                                                       config_.optimizer.ac_start_freq));

            solvers::ACSolver ac_solver(circuit_, temp_config);
            solvers::ACResult ac_result = ac_solver.solve();

            if (ac_result.status != ConvergenceStatus::SUCCESS) {
                spdlog::warn("AC solve failed during optimization evaluation");
                return std::numeric_limits<Real>::infinity();
            }

            // Extract objective measure value from AC result
            // For simplicity, use magnitude at final frequency point
            // TODO [ROADMAP-OPTIMIZER-2]: Support bandwidth, gain at specific frequency, etc.
            // See: (Fourier) and (Distortion) for frequency analysis
            if (!ac_result.frequency_points.empty()) {
                const auto& final_fp = ac_result.frequency_points.back();

                // Parse "v(node)"
                if (measure_name.size() > 2 && measure_name[0] == 'v' && measure_name[1] == '(') {
                    std::size_t close_paren = measure_name.find(')');
                    if (close_paren != std::string::npos) {
                        std::string node_name = measure_name.substr(2, close_paren - 2);
                        auto node_id = circuit_.get_node_id(node_name);
                        // Node ID is 1-based (ground=0), solution vector is 0-indexed
                        if (node_id != NODE_INVALID && node_id > 0 && static_cast<size_t>(node_id - 1) < final_fp.solution.size()) {
                            objective_value = std::abs(final_fp.solution[node_id - 1]);
                        }
                    }
                }
            }
            break;
        }

        default:
            spdlog::error("Unknown analysis type for optimization");
            return std::numeric_limits<Real>::infinity();
    }

    // Handle MINIMIZE vs MAXIMIZE
    if (config_.optimizer.objective == OptimizerConfig::Objective::MAXIMIZE) {
        objective_value = -objective_value;  // Convert to minimization problem
    }

    spdlog::trace("Evaluation #{}: objective={:.6e}", num_evaluations_, objective_value);

    return objective_value;
}

void CircuitOptimizer::apply_parameters(const std::vector<Real>& params) {
    if (params.size() != config_.optimizer.parameters.size()) {
        spdlog::error("Parameter size mismatch: {} vs {}",
                      params.size(),
                      config_.optimizer.parameters.size());
        return;
    }

    for (std::size_t i = 0; i < params.size(); ++i) {
        const auto& param_config = config_.optimizer.parameters[i];
        Real value = params[i];

        // Parse "device.parameter" format
        std::string param_name = param_config.parameter_name;
        std::size_t dot_pos = param_name.find('.');

        if (dot_pos == std::string::npos) {
            spdlog::warn("Invalid parameter name format: {}", param_name);
            continue;
        }

        std::string device_name = param_name.substr(0, dot_pos);
        std::string parameter_name = param_name.substr(dot_pos + 1);

        // Get device and set parameter
        auto device = circuit_.get_device(device_name);
        if (device) {
            bool success = device->set_parameter(parameter_name, value);
            if (!success) {
                spdlog::warn("Failed to set parameter {}={}", param_name, value);
            } else {
                spdlog::trace("Set parameter {}={:.6e}", param_name, value);
            }
        } else {
            spdlog::warn("Device not found: {}", device_name);
        }
    }
}

std::vector<Real> CircuitOptimizer::grid_search_optimize() {
    spdlog::info("Using grid search optimization (placeholder implementation)");

    std::size_t num_params = config_.optimizer.parameters.size();
    std::vector<Real> best_params(num_params);
    Real best_objective = std::numeric_limits<Real>::infinity();

    // Grid resolution: 5 points per dimension
    const std::size_t grid_points = 5;

    // Generate grid for each parameter
    std::vector<std::vector<Real>> grids;
    grids.reserve(num_params);

    for (const auto& param : config_.optimizer.parameters) {
        std::vector<Real> grid;
        grid.reserve(grid_points);
        for (std::size_t i = 0; i < grid_points; ++i) {
            Real t = static_cast<Real>(i) / static_cast<Real>(grid_points - 1);
            Real value = param.lower_bound + t * (param.upper_bound - param.lower_bound);
            grid.push_back(value);
        }
        grids.push_back(grid);
    }

    // Evaluate all grid points (Cartesian product)
    std::vector<std::size_t> indices(num_params, 0);

    auto evaluate_grid_point = [&]() {
        std::vector<Real> params(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            params[i] = grids[i][indices[i]];
        }

        Real objective = evaluate_objective(params);
        if (objective < best_objective) {
            best_objective = objective;
            best_params = params;
        }
    };

    // Enumerate all grid points
    bool done = false;
    while (!done) {
        evaluate_grid_point();

        // Increment indices (like odometer)
        std::size_t carry_pos = 0;
        while (carry_pos < num_params) {
            ++indices[carry_pos];
            if (indices[carry_pos] < grid_points) {
                break;
            }
            indices[carry_pos] = 0;
            ++carry_pos;
        }

        if (carry_pos == num_params) {
            done = true;
        }
    }

    spdlog::info("Grid search completed: best objective={:.6e}", best_objective);
    return best_params;
}

std::vector<Real> CircuitOptimizer::random_search_optimize() {
    spdlog::info("Using random search optimization (placeholder implementation)");

    std::size_t num_params = config_.optimizer.parameters.size();
    std::vector<Real> best_params(num_params);
    Real best_objective = std::numeric_limits<Real>::infinity();

    // Random number generator
    std::random_device rd;
    std::mt19937 gen(rd());

    // Evaluate initial values
    for (std::size_t i = 0; i < num_params; ++i) {
        best_params[i] = config_.optimizer.parameters[i].initial_value;
    }
    best_objective = evaluate_objective(best_params);

    // Random search: 100 iterations
    const std::size_t max_iterations = 100;

    for (std::size_t iter = 0; iter < max_iterations; ++iter) {
        std::vector<Real> params(num_params);

        for (std::size_t i = 0; i < num_params; ++i) {
            const auto& param = config_.optimizer.parameters[i];
            std::uniform_real_distribution<Real> dist(param.lower_bound, param.upper_bound);
            params[i] = dist(gen);
        }

        Real objective = evaluate_objective(params);
        if (objective < best_objective) {
            best_objective = objective;
            best_params = params;
            spdlog::debug("Improved objective at iteration {}: {:.6e}", iter, objective);
        }
    }

    spdlog::info("Random search completed: best objective={:.6e}", best_objective);
    return best_params;
}

// ============================================================================
// NLopt COBYLA Implementation
// ============================================================================

double CircuitOptimizer::nlopt_objective_wrapper(const std::vector<double>& x,
                                                std::vector<double>& grad,
                                                void* data) {
    // Cast user data back to CircuitOptimizer*
    auto* optimizer = static_cast<CircuitOptimizer*>(data);

    // Convert std::vector<double> to std::vector<Real>
    std::vector<Real> params(x.begin(), x.end());

    // Evaluate objective function
    Real objective = optimizer->evaluate_objective(params);

    return static_cast<double>(objective);
}

std::vector<Real> CircuitOptimizer::cobyla_optimize() {
    spdlog::info("Using COBYLA optimization (NLopt implementation)");

    std::size_t num_params = config_.optimizer.parameters.size();

    try {
        // Create NLopt optimizer (COBYLA = LN_COBYLA, local derivative-free)
        nlopt::opt opt(nlopt::LN_COBYLA, num_params);

        // Set bounds
        std::vector<double> lower_bounds(num_params);
        std::vector<double> upper_bounds(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            lower_bounds[i] = config_.optimizer.parameters[i].lower_bound;
            upper_bounds[i] = config_.optimizer.parameters[i].upper_bound;
        }
        opt.set_lower_bounds(lower_bounds);
        opt.set_upper_bounds(upper_bounds);

        // Set objective function (minimize)
        opt.set_min_objective(nlopt_objective_wrapper, this);

        // Set stopping criteria
        opt.set_xtol_rel(config_.optimizer.tolerances.xtol_rel);
        if (config_.optimizer.tolerances.xtol_abs > 0.0) {
            opt.set_xtol_abs(config_.optimizer.tolerances.xtol_abs);
        }
        opt.set_ftol_rel(config_.optimizer.tolerances.ftol_rel);
        if (config_.optimizer.tolerances.ftol_abs > 0.0) {
            opt.set_ftol_abs(config_.optimizer.tolerances.ftol_abs);
        }
        opt.set_maxeval(config_.optimizer.max_evaluations);
        if (config_.optimizer.max_time > 0.0) {
            opt.set_maxtime(config_.optimizer.max_time);
        }

        // TODO [ROADMAP-OPTIMIZER-3]: Add nonlinear constraint support (+)
        // NLopt supports inequality/equality constraints via add_inequality_constraint()
        // Data structure OptimizerConfig::constraints already exists in types.hpp
        // for (const auto& constraint : config_.optimizer.constraints) {
        //     opt.add_inequality_constraint(...);
        // }

        // Set initial values
        std::vector<double> x(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            x[i] = config_.optimizer.parameters[i].initial_value;
        }

        // Run optimization
        double minf;
        nlopt::result result = opt.optimize(x, minf);

        // Log result
        std::string result_str;
        switch (result) {
            case nlopt::SUCCESS:
                result_str = "SUCCESS";
                break;
            case nlopt::STOPVAL_REACHED:
                result_str = "STOPVAL_REACHED";
                break;
            case nlopt::FTOL_REACHED:
                result_str = "FTOL_REACHED";
                break;
            case nlopt::XTOL_REACHED:
                result_str = "XTOL_REACHED";
                break;
            case nlopt::MAXEVAL_REACHED:
                result_str = "MAXEVAL_REACHED";
                break;
            case nlopt::MAXTIME_REACHED:
                result_str = "MAXTIME_REACHED";
                break;
            default:
                result_str = "FAILURE";
                break;
        }

        spdlog::info("COBYLA optimization completed: result={}, objective={:.6e}, evaluations={}",
                     result_str, minf, num_evaluations_);

        // Convert result back to std::vector<Real>
        std::vector<Real> optimal_params(x.begin(), x.end());
        return optimal_params;

    } catch (const std::exception& e) {
        spdlog::error("COBYLA optimization failed: {}", e.what());

        // Return initial values on failure
        std::vector<Real> params(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            params[i] = config_.optimizer.parameters[i].initial_value;
        }
        return params;
    }
}

// ============================================================================
// NLopt BOBYQA Implementation
// ============================================================================

std::vector<Real> CircuitOptimizer::bobyqa_optimize() {
    spdlog::info("Using BOBYQA optimization (NLopt implementation)");

    std::size_t num_params = config_.optimizer.parameters.size();

    try {
        // Create NLopt optimizer (BOBYQA = LN_BOBYQA, local derivative-free with trust region)
        nlopt::opt opt(nlopt::LN_BOBYQA, num_params);

        // Set bounds
        std::vector<double> lower_bounds(num_params);
        std::vector<double> upper_bounds(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            lower_bounds[i] = config_.optimizer.parameters[i].lower_bound;
            upper_bounds[i] = config_.optimizer.parameters[i].upper_bound;
        }
        opt.set_lower_bounds(lower_bounds);
        opt.set_upper_bounds(upper_bounds);

        // Set objective function (minimize)
        opt.set_min_objective(nlopt_objective_wrapper, this);

        // Set stopping criteria
        opt.set_xtol_rel(config_.optimizer.tolerances.xtol_rel);
        if (config_.optimizer.tolerances.xtol_abs > 0.0) {
            opt.set_xtol_abs(config_.optimizer.tolerances.xtol_abs);
        }
        opt.set_ftol_rel(config_.optimizer.tolerances.ftol_rel);
        if (config_.optimizer.tolerances.ftol_abs > 0.0) {
            opt.set_ftol_abs(config_.optimizer.tolerances.ftol_abs);
        }
        opt.set_maxeval(config_.optimizer.max_evaluations);
        if (config_.optimizer.max_time > 0.0) {
            opt.set_maxtime(config_.optimizer.max_time);
        }

        // Set initial step size (BOBYQA-specific feature)
        // Use 10% of the parameter range as initial trust region radius
        std::vector<double> initial_step(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            Real range = config_.optimizer.parameters[i].upper_bound -
                         config_.optimizer.parameters[i].lower_bound;
            initial_step[i] = 0.1 * range;
        }
        opt.set_initial_step(initial_step);

        // Set initial values
        std::vector<double> x(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            x[i] = config_.optimizer.parameters[i].initial_value;
        }

        // Run optimization
        double minf;
        nlopt::result result = opt.optimize(x, minf);

        // Log result
        std::string result_str;
        switch (result) {
            case nlopt::SUCCESS:
                result_str = "SUCCESS";
                break;
            case nlopt::STOPVAL_REACHED:
                result_str = "STOPVAL_REACHED";
                break;
            case nlopt::FTOL_REACHED:
                result_str = "FTOL_REACHED";
                break;
            case nlopt::XTOL_REACHED:
                result_str = "XTOL_REACHED";
                break;
            case nlopt::MAXEVAL_REACHED:
                result_str = "MAXEVAL_REACHED";
                break;
            case nlopt::MAXTIME_REACHED:
                result_str = "MAXTIME_REACHED";
                break;
            default:
                result_str = "FAILURE";
                break;
        }

        spdlog::info("BOBYQA optimization completed: result={}, objective={:.6e}, evaluations={}",
                     result_str, minf, num_evaluations_);

        // Convert result back to std::vector<Real>
        std::vector<Real> optimal_params(x.begin(), x.end());
        return optimal_params;

    } catch (const std::exception& e) {
        spdlog::error("BOBYQA optimization failed: {}", e.what());

        // Return initial values on failure
        std::vector<Real> params(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            params[i] = config_.optimizer.parameters[i].initial_value;
        }
        return params;
    }
}

// ============================================================================
// NLopt SLSQP Implementation
// ============================================================================

std::vector<Real> CircuitOptimizer::slsqp_optimize() {
    spdlog::info("Using SLSQP optimization (NLopt implementation)");

    std::size_t num_params = config_.optimizer.parameters.size();

    try {
        // Create NLopt optimizer (SLSQP = LD_SLSQP, local gradient-based with constraints)
        nlopt::opt opt(nlopt::LD_SLSQP, num_params);

        // Set bounds
        std::vector<double> lower_bounds(num_params);
        std::vector<double> upper_bounds(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            lower_bounds[i] = config_.optimizer.parameters[i].lower_bound;
            upper_bounds[i] = config_.optimizer.parameters[i].upper_bound;
        }
        opt.set_lower_bounds(lower_bounds);
        opt.set_upper_bounds(upper_bounds);

        // Set objective function (minimize)
        opt.set_min_objective(nlopt_objective_wrapper, this);

        // Set stopping criteria
        opt.set_xtol_rel(config_.optimizer.tolerances.xtol_rel);
        if (config_.optimizer.tolerances.xtol_abs > 0.0) {
            opt.set_xtol_abs(config_.optimizer.tolerances.xtol_abs);
        }
        opt.set_ftol_rel(config_.optimizer.tolerances.ftol_rel);
        if (config_.optimizer.tolerances.ftol_abs > 0.0) {
            opt.set_ftol_abs(config_.optimizer.tolerances.ftol_abs);
        }
        opt.set_maxeval(config_.optimizer.max_evaluations);
        if (config_.optimizer.max_time > 0.0) {
            opt.set_maxtime(config_.optimizer.max_time);
        }

        // Set initial step size for gradient computation (SLSQP-specific feature)
        // Use 1% of the parameter range as initial step for numerical differentiation
        std::vector<double> initial_step(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            Real range = config_.optimizer.parameters[i].upper_bound -
                         config_.optimizer.parameters[i].lower_bound;
            initial_step[i] = 0.01 * range;  // 1% step for gradient estimation
        }
        opt.set_initial_step(initial_step);

        // Add inequality constraints (if any)
        // SLSQP supports nonlinear constraints: g(x) <= 0 form
        // Note: Constraint support will be added in future implementation
        // For now, only box constraints (lower/upper bounds) are supported
        if (!config_.optimizer.constraints.empty()) {
            spdlog::warn("SLSQP: Nonlinear constraints specified but not yet implemented");
            spdlog::warn("Only box constraints (lower/upper bounds) are currently supported");
        }

        // Set initial values
        std::vector<double> x(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            x[i] = config_.optimizer.parameters[i].initial_value;
        }

        // Run optimization
        double minf;
        nlopt::result result = opt.optimize(x, minf);

        // Log result
        std::string result_str;
        switch (result) {
            case nlopt::SUCCESS:
                result_str = "SUCCESS";
                break;
            case nlopt::FTOL_REACHED:
                result_str = "FTOL_REACHED";
                break;
            case nlopt::XTOL_REACHED:
                result_str = "XTOL_REACHED";
                break;
            case nlopt::MAXEVAL_REACHED:
                result_str = "MAXEVAL_REACHED";
                break;
            case nlopt::MAXTIME_REACHED:
                result_str = "MAXTIME_REACHED";
                break;
            default:
                result_str = "FAILURE";
                break;
        }

        spdlog::info("SLSQP optimization completed: result={}, objective={:.6e}, evaluations={}",
                     result_str, minf, num_evaluations_);

        // Convert result back to std::vector<Real>
        std::vector<Real> optimal_params(x.begin(), x.end());
        return optimal_params;

    } catch (const std::exception& e) {
        spdlog::error("SLSQP optimization failed: {}", e.what());

        // Return initial values on failure
        std::vector<Real> params(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            params[i] = config_.optimizer.parameters[i].initial_value;
        }
        return params;
    }
}

// ============================================================================
// NLopt Nelder-Mead Implementation
// ============================================================================

std::vector<Real> CircuitOptimizer::neldermead_optimize() {
    spdlog::info("Using Nelder-Mead optimization (NLopt implementation)");

    std::size_t num_params = config_.optimizer.parameters.size();

    try {
        // Create NLopt optimizer (Nelder-Mead = LN_NELDERMEAD, local derivative-free simplex method)
        nlopt::opt opt(nlopt::LN_NELDERMEAD, num_params);

        // Set bounds
        std::vector<double> lower_bounds(num_params);
        std::vector<double> upper_bounds(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            lower_bounds[i] = config_.optimizer.parameters[i].lower_bound;
            upper_bounds[i] = config_.optimizer.parameters[i].upper_bound;
        }
        opt.set_lower_bounds(lower_bounds);
        opt.set_upper_bounds(upper_bounds);

        // Set objective function (minimize)
        opt.set_min_objective(nlopt_objective_wrapper, this);

        // Set stopping criteria
        opt.set_xtol_rel(config_.optimizer.tolerances.xtol_rel);
        if (config_.optimizer.tolerances.xtol_abs > 0.0) {
            opt.set_xtol_abs(config_.optimizer.tolerances.xtol_abs);
        }
        opt.set_ftol_rel(config_.optimizer.tolerances.ftol_rel);
        if (config_.optimizer.tolerances.ftol_abs > 0.0) {
            opt.set_ftol_abs(config_.optimizer.tolerances.ftol_abs);
        }
        opt.set_maxeval(config_.optimizer.max_evaluations);
        if (config_.optimizer.max_time > 0.0) {
            opt.set_maxtime(config_.optimizer.max_time);
        }

        // Set initial step size (Nelder-Mead simplex method)
        // Use 10% of the parameter range as initial simplex size
        std::vector<double> initial_step(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            Real range = config_.optimizer.parameters[i].upper_bound -
                         config_.optimizer.parameters[i].lower_bound;
            initial_step[i] = 0.1 * range;  // 10% step for simplex
        }
        opt.set_initial_step(initial_step);

        // Set initial values
        std::vector<double> x(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            x[i] = config_.optimizer.parameters[i].initial_value;
        }

        // Run optimization
        double minf;
        nlopt::result result = opt.optimize(x, minf);

        // Log result
        std::string result_str;
        switch (result) {
            case nlopt::SUCCESS:
                result_str = "SUCCESS";
                break;
            case nlopt::FTOL_REACHED:
                result_str = "FTOL_REACHED";
                break;
            case nlopt::XTOL_REACHED:
                result_str = "XTOL_REACHED";
                break;
            case nlopt::MAXEVAL_REACHED:
                result_str = "MAXEVAL_REACHED";
                break;
            case nlopt::MAXTIME_REACHED:
                result_str = "MAXTIME_REACHED";
                break;
            default:
                result_str = "FAILURE";
                break;
        }

        spdlog::info("Nelder-Mead optimization completed: result={}, objective={:.6e}, evaluations={}",
                     result_str, minf, num_evaluations_);

        // Convert result back to std::vector<Real>
        std::vector<Real> optimal_params(x.begin(), x.end());
        return optimal_params;

    } catch (const std::exception& e) {
        spdlog::error("Nelder-Mead optimization failed: {}", e.what());

        // Return initial values on failure
        std::vector<Real> params(num_params);
        for (std::size_t i = 0; i < num_params; ++i) {
            params[i] = config_.optimizer.parameters[i].initial_value;
        }
        return params;
    }
}

}  // namespace kirimaspice::analysis
