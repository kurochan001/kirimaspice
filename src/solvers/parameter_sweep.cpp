// src/solvers/parameter_sweep.cpp
#include "kirimaspice/solvers/parameter_sweep.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/devices/device.hpp"
#include <spdlog/spdlog.h>
#include <numeric>
#include <fstream>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice::solvers {

ParameterSweepExecutor::ParameterSweepExecutor(const SimulationConfig& config)
    : base_config_(config) {
}

bool ParameterSweepExecutor::is_sweep_enabled() const {
    return !base_config_.step_configs.empty();
}

size_t ParameterSweepExecutor::get_total_sweep_points() const {
    if (base_config_.step_configs.empty()) {
        return 1;  // No sweep, single analysis
    }

    // Cartesian product of all sweep point counts
    size_t total = 1;
    for (const auto& step : base_config_.step_configs) {
        total *= step.num_points();
    }
    return total;
}

ParameterSweepResult ParameterSweepExecutor::execute_dc_sweep(
    const std::string& netlist_path
) {
    // Default DC solver function
    auto default_solver = [](circuit::CircuitGraph& circuit,
                            const SimulationConfig& config,
                            DenseVector& solution,
                            SolverStats& stats) -> ConvergenceStatus {
        DCSolver solver(circuit, config);
        return solver.solve(solution, stats);
    };

    return execute_dc_sweep_custom(netlist_path, default_solver);
}

ParameterSweepResult ParameterSweepExecutor::execute_dc_sweep_custom(
    const std::string& netlist_path,
    DCAnalysisFunc solver_func
) {
    if (!is_sweep_enabled()) {
        spdlog::info("No .STEP directives found, running single analysis");
        ParameterSweepResult result;

        // Parse the netlist and run a single DC analysis
        parser::NetlistParser parser;
        auto parse_result = parser.parse_file(netlist_path);
        if (!parse_result.circuit) {
            spdlog::error("Failed to parse netlist: {}", netlist_path);
            result.overall_status = ConvergenceStatus::MAX_ITERATIONS;
            result.num_success = 0;
            result.num_failed = 1;
            return result;
        }

        // Run single analysis with parsed config (inherits .options, temperature, etc.)
        DenseVector solution;
        SolverStats stats;
        auto status = solver_func(*parse_result.circuit, parse_result.config, solution, stats);

        ParameterSweepPoint point;
        point.param_name = "NOSWEEP";
        point.param_value = 0.0;
        point.solution = solution;
        point.status = status;
        point.stats = stats;
        result.sweep_points.push_back(point);

        result.overall_status = status;
        result.num_success = (status == ConvergenceStatus::SUCCESS) ? 1 : 0;
        result.num_failed = (status == ConvergenceStatus::SUCCESS) ? 0 : 1;
        return result;
    }

    // Support for multiple parameter sweeps (Cartesian product)
    if (base_config_.step_configs.size() == 1) {
        // Single parameter sweep
        const auto& step_config = base_config_.step_configs[0];
        return execute_single_param_sweep(netlist_path, step_config, solver_func);
    } else {
        // Multiple parameter sweep (Cartesian product)
        return execute_multi_param_sweep(netlist_path, solver_func);
    }
}

ParameterSweepResult ParameterSweepExecutor::execute_single_param_sweep(
    const std::string& netlist_path,
    const StepConfig& step_config,
    DCAnalysisFunc solver_func
) {
    ParameterSweepResult result;
    result.num_success = 0;
    result.num_failed = 0;
    result.overall_status = ConvergenceStatus::SUCCESS;

    // Generate sweep points
    auto sweep_values = step_config.generate_points();
    spdlog::info("Parameter sweep: {} from {} to {} ({} points)",
                step_config.param_name,
                step_config.start_value,
                step_config.stop_value,
                sweep_values.size());

    // Preload netlist content to avoid disk I/O on every sweep point
    std::string netlist_content;
    {
        std::ifstream netfile(netlist_path);
        if (!netfile) {
            throw std::runtime_error("Cannot open netlist: " + netlist_path);
        }
        std::stringstream ss;
        ss << netfile.rdbuf();
        netlist_content = ss.str();
    }

    // Parallelize parameter sweep (analysis-level parallelization)
    // Pre-allocate results array for thread-safe direct indexing
    result.sweep_points.resize(sweep_values.size());

#ifdef _OPENMP
    // Parallel sweep execution (each point is independent - embarrassingly parallel)
    #pragma omp parallel for schedule(dynamic)
#endif
    for (size_t i = 0; i < sweep_values.size(); ++i) {
        Real param_value = sweep_values[i];

        spdlog::debug("Sweep point {}/{}: {}={}",
                     i + 1, sweep_values.size(),
                     step_config.param_name, param_value);

        // Create initial_params to override .PARAM definitions in netlist
        std::unordered_map<std::string, Real> initial_params;
        initial_params[step_config.param_name] = param_value;

        // Re-parse netlist with updated parameter
        // initial_params will be applied BEFORE .PARAM directives in netlist
        parser::NetlistParser parser;
        auto parse_result = parser.parse_string(netlist_content, initial_params);

        // Run analysis
        ParameterSweepPoint point;
        point.param_name = step_config.param_name;
        point.param_value = param_value;
        // Also populate param_values for consistency (enhancement)
        point.param_values[step_config.param_name] = param_value;

        point.status = solver_func(
            *parse_result.circuit,
            parse_result.config,
            point.solution,
            point.stats
        );

        // Thread-safe counters update
        if (point.status == ConvergenceStatus::SUCCESS) {
#ifdef _OPENMP
            #pragma omp atomic
#endif
            ++result.num_success;
            spdlog::debug("  Converged in {} iterations",
                         point.stats.total_iterations);
        } else {
#ifdef _OPENMP
            #pragma omp atomic
#endif
            ++result.num_failed;
            // Note: result.overall_status update is not thread-safe, handled after loop
            spdlog::warn("  Failed to converge at {}={}",
                        step_config.param_name, param_value);
        }

        // Direct indexing - thread-safe without critical section
        result.sweep_points[i] = std::move(point);
    }

    // Set overall_status based on failure count (after parallel region)
    if (result.num_failed > 0) {
        // Find first failed point and use its status
        for (const auto& point : result.sweep_points) {
            if (point.status != ConvergenceStatus::SUCCESS) {
                result.overall_status = point.status;
                break;
            }
        }
    }

    spdlog::info("Sweep completed: {}/{} points converged",
                result.num_success, sweep_values.size());

    return result;
}

ParameterSweepResult ParameterSweepExecutor::execute_multi_param_sweep(
    const std::string& netlist_path,
    DCAnalysisFunc solver_func
) {
    ParameterSweepResult result;
    result.num_success = 0;
    result.num_failed = 0;
    result.overall_status = ConvergenceStatus::SUCCESS;

    // Load netlist once to avoid repeated disk I/O
    std::string netlist_content;
    {
        std::ifstream netfile(netlist_path);
        if (!netfile) {
            throw std::runtime_error("Cannot open netlist: " + netlist_path);
        }
        std::stringstream ss;
        ss << netfile.rdbuf();
        netlist_content = ss.str();
    }

    // Generate sweep points for all parameters
    std::vector<std::vector<Real>> all_sweep_values;
    std::vector<std::string> param_names;

    for (const auto& step : base_config_.step_configs) {
        all_sweep_values.push_back(step.generate_points());
        param_names.push_back(step.param_name);
    }

    // Calculate total number of combinations (Cartesian product)
    size_t total_points = get_total_sweep_points();
    spdlog::info("Multi-parameter sweep: {} parameters, {} total combinations",
                base_config_.step_configs.size(), total_points);

    for (size_t i = 0; i < base_config_.step_configs.size(); ++i) {
        spdlog::info("  {}: {} points", param_names[i], all_sweep_values[i].size());
    }

    // Pre-allocate results array for thread-safe direct indexing
    result.sweep_points.resize(total_points);

#ifdef _OPENMP
    // Parallel multi-parameter sweep (each combination is independent)
    #pragma omp parallel for schedule(dynamic)
#endif
    for (size_t combination = 0; combination < total_points; ++combination) {
        // Compute indices for this combination (counter-based indexing)
        std::vector<size_t> indices(base_config_.step_configs.size(), 0);
        size_t remaining = combination;
        for (int i = indices.size() - 1; i >= 0; --i) {
            indices[i] = remaining % all_sweep_values[i].size();
            remaining /= all_sweep_values[i].size();
        }

        // Build parameter map for current combination
        std::unordered_map<std::string, Real> param_values;
        std::string log_msg = fmt::format("Combination {}/{}: ", combination + 1, total_points);

        for (size_t i = 0; i < base_config_.step_configs.size(); ++i) {
            Real value = all_sweep_values[i][indices[i]];
            param_values[param_names[i]] = value;
            log_msg += fmt::format("{}={}, ", param_names[i], value);
        }
        spdlog::debug(log_msg);

        // Pass param_values as initial_params to override .PARAM definitions
        parser::NetlistParser parser;
        auto parse_result = parser.parse_string(netlist_content, param_values);

        // Run analysis
        ParameterSweepPoint point;
        point.param_name = "multi";  // Indicate multi-parameter sweep
        point.param_value = 0.0;     // Not applicable for multi-param
        // Store all parameter values (enhancement)
        point.param_values = param_values;

        point.status = solver_func(
            *parse_result.circuit,
            parse_result.config,
            point.solution,
            point.stats
        );

        // Thread-safe counters update
        if (point.status == ConvergenceStatus::SUCCESS) {
#ifdef _OPENMP
            #pragma omp atomic
#endif
            ++result.num_success;
        } else {
#ifdef _OPENMP
            #pragma omp atomic
#endif
            ++result.num_failed;
            // Note: result.overall_status update is not thread-safe, handled after loop
            spdlog::warn("  Failed to converge");
        }

        // Direct indexing - thread-safe without critical section
        result.sweep_points[combination] = std::move(point);
    }

    // Set overall_status based on failure count (after parallel region)
    if (result.num_failed > 0) {
        // Find first failed point and use its status
        for (const auto& point : result.sweep_points) {
            if (point.status != ConvergenceStatus::SUCCESS) {
                result.overall_status = point.status;
                break;
            }
        }
    }

    spdlog::info("Multi-parameter sweep completed: {}/{} combinations converged",
                result.num_success, total_points);

    return result;
}

} // namespace kirimaspice::solvers
