// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/analysis/monte_carlo_executor.cpp
#include "kirimaspice/analysis/monte_carlo_executor.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <exception>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice::analysis {

MonteCarloExecutor::MonteCarloExecutor(const SimulationConfig& config)
    : base_config_(config)
    , rng_(std::make_unique<RandomNumberGenerator>(config.monte_carlo.seed))
{
}

bool MonteCarloExecutor::is_monte_carlo_enabled() const {
    return base_config_.monte_carlo.num_runs > 0
        && !base_config_.monte_carlo.variations.empty();
}

std::uint32_t MonteCarloExecutor::get_num_runs() const {
    return base_config_.monte_carlo.num_runs;
}

std::uint32_t MonteCarloExecutor::get_seed() const {
    return rng_->get_seed();
}

std::map<std::string, Real> MonteCarloExecutor::sample_parameters(std::uint32_t run_index) {
    std::map<std::string, Real> sampled_params;

    for (const auto& variation : base_config_.monte_carlo.variations) {
        DistributionSampler sampler(variation);
        Real sampled_value = sampler.sample(*rng_);

        // Use device_name as the parameter key (e.g., "R1.resistance")
        std::string param_key = variation.device_name + "." + variation.parameter_name;
        sampled_params[param_key] = sampled_value;

        spdlog::debug("  Run {}: {} = {} (nominal: {})",
                     run_index,
                     param_key,
                     sampled_value,
                     variation.nominal_value);
    }

    return sampled_params;
}

Real MonteCarloExecutor::extract_output_value(
    const circuit::CircuitGraph& circuit,
    const DenseVector& solution,
    const std::string& output_node
) {
    // Check if node exists
    if (!circuit.has_node(output_node)) {
        throw std::invalid_argument("Output node not found: " + output_node);
    }

    // Find node ID by name (throws if not found, but we already checked)
    auto node_id = circuit.get_node_id(output_node);

    // Ground node (ID 0) is always 0V
    if (node_id == 0) {
        return 0.0;
    }

    // Node voltages are stored at indices [0, num_nodes-1]
    // Node ID is 1-based (ground=0), so we need node_id - 1 for indexing
    if (node_id > 0 && static_cast<size_t>(node_id - 1) < solution.size()) {
        return solution[node_id - 1];
    }

    throw std::runtime_error("Output node is missing from the solution: " + output_node);
}

MonteCarloResult MonteCarloExecutor::execute_dc_monte_carlo(
    const std::string& netlist_path,
    const std::string& output_node,
    Real spec_min,
    Real spec_max
) {
    // Default DC solver function
    auto default_solver = [](circuit::CircuitGraph& circuit,
                            const SimulationConfig& config,
                            DenseVector& solution,
                            SolverStats& stats) -> ConvergenceStatus {
        solvers::DCSolver solver(circuit, config);
        return solver.solve(solution, stats);
    };

    return execute_dc_monte_carlo_custom(
        netlist_path, output_node, spec_min, spec_max, default_solver
    );
}

MonteCarloResult MonteCarloExecutor::execute_dc_monte_carlo_custom(
    const std::string& netlist_path,
    const std::string& output_node,
    Real spec_min,
    Real spec_max,
    DCAnalysisFunc solver_func
) {
    MonteCarloResult result;
    result.output_node = output_node;
    result.spec_min = spec_min;
    result.spec_max = spec_max;
    result.num_runs = base_config_.monte_carlo.num_runs;
    result.num_converged = 0;

    if (!is_monte_carlo_enabled()) {
        spdlog::warn("Monte Carlo analysis not enabled (num_runs={}, variations={})",
                    base_config_.monte_carlo.num_runs,
                    base_config_.monte_carlo.variations.size());
        result.success = false;
        return result;
    }

    spdlog::info("Starting Monte Carlo analysis: {} runs, {} parameter variations, seed={}",
                result.num_runs,
                base_config_.monte_carlo.variations.size(),
                rng_->get_seed());

    // Pre-allocate results for thread-safe direct indexing
    std::vector<Real> all_values_vec(result.num_runs, std::numeric_limits<Real>::quiet_NaN());
    std::vector<std::map<std::string, Real>> parameter_samples_vec(result.num_runs);
    std::vector<unsigned char> converged_flags(result.num_runs, 0);
    std::vector<std::exception_ptr> errors(result.num_runs);

    const auto base_seed = rng_->get_seed();

#ifdef _OPENMP
    // Parallel Monte Carlo execution (embarrassingly parallel)
    #pragma omp parallel for schedule(dynamic)
#endif
    for (std::uint32_t run = 0; run < result.num_runs; ++run) {
        try {
            spdlog::debug("Monte Carlo run {}/{}", run + 1, result.num_runs);

            // Per-run seed, so the samples do not depend on the threading
            // build or on the execution order.
#ifdef _OPENMP
            RandomNumberGenerator thread_rng(base_seed + run);
#else
            rng_->reset(base_seed + run);
            RandomNumberGenerator& thread_rng = *rng_;
#endif

            // Sample parameter values from distributions (thread-local RNG)
            std::map<std::string, Real> sampled_params;
            for (const auto& variation : base_config_.monte_carlo.variations) {
                DistributionSampler sampler(variation);
                Real sampled_value = sampler.sample(thread_rng);
                std::string param_key = variation.device_name + "." + variation.parameter_name;
                sampled_params[param_key] = sampled_value;

                spdlog::debug("  Run {}: {} = {} (nominal: {})",
                             run + 1, param_key, sampled_value, variation.nominal_value);
            }
            parameter_samples_vec[run] = sampled_params;

            parser::NetlistParser parser;
            auto parse_result = parser.parse_file(netlist_path);
            parse_result.circuit->expand_all_subcircuits();
            apply_parameters_inplace(*parse_result.circuit, sampled_params);

            // Run analysis
            DenseVector solution;
            SolverStats stats;
            auto status = solver_func(
                *parse_result.circuit,
                parse_result.config,
                solution,
                stats
            );

            if (status == ConvergenceStatus::SUCCESS) {
                // Extract output value
                Real output_value = extract_output_value(
                    *parse_result.circuit,
                    solution,
                    output_node
                );

                // Thread-safe direct indexing
                all_values_vec[run] = output_value;
                converged_flags[run] = true;

                spdlog::debug("  Converged: output = {} V", output_value);
            } else {
                spdlog::warn("  Failed to converge at run {}", run + 1);
                // converged_flags[run] remains false
            }
        } catch (...) {
            errors[run] = std::current_exception();
        }
    }

    for (const auto& error : errors) {
        if (error) std::rethrow_exception(error);
    }

    // Collect results from parallel execution
    result.parameter_samples = std::move(parameter_samples_vec);
    for (std::uint32_t run = 0; run < result.num_runs; ++run) {
        if (converged_flags[run]) {
            result.all_values.push_back(all_values_vec[run]);
            ++result.num_converged;
        }
    }

    spdlog::info("Monte Carlo completed: {}/{} runs converged ({}%)",
                result.num_converged,
                result.num_runs,
                100.0 * result.num_converged / result.num_runs);

    // Compute statistics if we have at least one converged run
    if (result.num_converged > 0) {
        result.stats = StatisticalAnalyzer::compute_stats(result.all_values);
        result.histogram = StatisticalAnalyzer::compute_histogram(result.all_values, 20);
        result.yield_percent = StatisticalAnalyzer::compute_yield(
            result.all_values,
            spec_min,
            spec_max,
            result.num_passed,
            result.num_failed
        );

        result.success = true;

        spdlog::info("Monte Carlo statistics:");
        spdlog::info("  Mean: {} V", result.stats.mean);
        spdlog::info("  Std Dev: {} V", result.stats.std_dev);
        spdlog::info("  Min: {} V", result.stats.min);
        spdlog::info("  Max: {} V", result.stats.max);
        spdlog::info("  Median: {} V", result.stats.median);
        spdlog::info("  Yield: {}% ({}/{} passed)",
                    result.yield_percent,
                    result.num_passed,
                    result.num_converged);
    } else {
        spdlog::error("Monte Carlo failed: no runs converged");
        result.success = false;
    }

    return result;
}

// Optimized path: parse once per thread, then apply parameters directly to circuit
MonteCarloResult MonteCarloExecutor::execute_dc_monte_carlo_fast(
    const std::string& netlist_path,
    const std::string& output_node,
    Real spec_min,
    Real spec_max
) {
    MonteCarloResult result;
    result.output_node = output_node;
    result.spec_min = spec_min;
    result.spec_max = spec_max;
    result.num_runs = base_config_.monte_carlo.num_runs;
    result.num_converged = 0;

    if (!is_monte_carlo_enabled()) {
        spdlog::warn("Monte Carlo analysis not enabled (num_runs={}, variations={})",
                     base_config_.monte_carlo.num_runs,
                     base_config_.monte_carlo.variations.size());
        result.success = false;
        return result;
    }

    spdlog::info("Starting Monte Carlo (fast path): {} runs, {} variations, seed={}",
                 result.num_runs,
                 base_config_.monte_carlo.variations.size(),
                 rng_->get_seed());

    // Pre-allocate results for thread-safe direct indexing
    std::vector<Real> all_values_vec(result.num_runs, std::numeric_limits<Real>::quiet_NaN());
    std::vector<std::map<std::string, Real>> parameter_samples_vec(result.num_runs);
    std::vector<unsigned char> converged_flags(result.num_runs, 0);
    std::vector<std::exception_ptr> errors(result.num_runs);

    const auto base_seed = rng_->get_seed();

#ifdef _OPENMP
    // Parallel Monte Carlo (fast path) - each thread parses once
    #pragma omp parallel
    {
        parser::NetlistParseResult thread_parse_result;
        std::exception_ptr parse_error;
        try {
            parser::NetlistParser thread_parser;
            thread_parse_result = thread_parser.parse_file(netlist_path);
            thread_parse_result.circuit->expand_all_subcircuits();
        } catch (...) {
            parse_error = std::current_exception();
        }

        #pragma omp for schedule(dynamic)
        for (std::uint32_t run = 0; run < result.num_runs; ++run) {
            if (parse_error) {
                errors[run] = parse_error;
                continue;
            }
            try {
                auto& thread_circuit = *thread_parse_result.circuit;
                const auto& thread_sim_config = thread_parse_result.config;
                spdlog::debug("Monte Carlo (fast) run {}/{}", run + 1, result.num_runs);

                // Thread-local RNG (deterministic seed = base_seed + run)
                RandomNumberGenerator thread_rng(base_seed + run);

                // Sample parameter values from distributions
                std::map<std::string, Real> sampled_params;
                for (const auto& variation : base_config_.monte_carlo.variations) {
                    DistributionSampler sampler(variation);
                    Real sampled_value = sampler.sample(thread_rng);
                    std::string param_key = variation.device_name + "." + variation.parameter_name;
                    sampled_params[param_key] = sampled_value;

                    spdlog::debug("  Run {}: {} = {} (nominal: {})",
                                 run + 1, param_key, sampled_value, variation.nominal_value);
                }
                parameter_samples_vec[run] = sampled_params;

                // Apply parameters in-place to the thread-local circuit
                apply_parameters_inplace(thread_circuit, sampled_params);

                // Run DC analysis
                DenseVector solution;
                SolverStats stats;
                solvers::DCSolver solver(thread_circuit, thread_sim_config);
                auto status = solver.solve(solution, stats);

                if (status == ConvergenceStatus::SUCCESS) {
                    Real output_value = extract_output_value(thread_circuit, solution, output_node);
                    all_values_vec[run] = output_value;
                    converged_flags[run] = true;
                    spdlog::debug("  Converged (fast): output = {} V", output_value);
                } else {
                    spdlog::warn("  Failed to converge at run {} (fast)", run + 1);
                }
            } catch (...) {
                errors[run] = std::current_exception();
            }
        }
    }
#else
    // Sequential fallback (no OpenMP)
    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(netlist_path);
    parse_result.circuit->expand_all_subcircuits();
    auto& circuit = *parse_result.circuit;
    auto sim_config = parse_result.config;

    for (std::uint32_t run = 0; run < result.num_runs; ++run) {
        spdlog::debug("Monte Carlo (fast) run {}/{}", run + 1, result.num_runs);

        // Same per-run seed as the parallel branch and the normal path.
        rng_->reset(base_seed + run);
        auto sampled_params = sample_parameters(run + 1);
        parameter_samples_vec[run] = sampled_params;

        apply_parameters_inplace(circuit, sampled_params);

        DenseVector solution;
        SolverStats stats;
        solvers::DCSolver solver(circuit, sim_config);
        auto status = solver.solve(solution, stats);

        if (status == ConvergenceStatus::SUCCESS) {
            Real output_value = extract_output_value(circuit, solution, output_node);
            all_values_vec[run] = output_value;
            converged_flags[run] = true;
            spdlog::debug("  Converged (fast): output = {} V", output_value);
        } else {
            spdlog::warn("  Failed to converge at run {} (fast)", run + 1);
        }
    }
#endif

    for (const auto& error : errors) {
        if (error) std::rethrow_exception(error);
    }

    // Collect results from parallel execution
    result.parameter_samples = std::move(parameter_samples_vec);
    for (std::uint32_t run = 0; run < result.num_runs; ++run) {
        if (converged_flags[run]) {
            result.all_values.push_back(all_values_vec[run]);
            ++result.num_converged;
        }
    }

    spdlog::info("Monte Carlo (fast) completed: {}/{} runs converged ({}%)",
                 result.num_converged, result.num_runs,
                 result.num_runs ? (100.0 * result.num_converged / result.num_runs) : 0.0);

    if (result.num_converged > 0) {
        result.stats = StatisticalAnalyzer::compute_stats(result.all_values);
        result.histogram = StatisticalAnalyzer::compute_histogram(result.all_values, 20);
        result.yield_percent = StatisticalAnalyzer::compute_yield(
            result.all_values, spec_min, spec_max, result.num_passed, result.num_failed);
        result.success = true;
    } else {
        result.success = false;
        spdlog::error("Monte Carlo (fast) failed: no runs converged");
    }

    return result;
}

void MonteCarloExecutor::apply_parameters_inplace(
    circuit::CircuitGraph& circuit,
    const std::map<std::string, Real>& parameter_values) {
    // Parameter key format: "Device.Param" (e.g., "R1.resistance")
    for (const auto& [param_key, value] : parameter_values) {
        std::size_t dot_pos = param_key.rfind('.');
        if (dot_pos == std::string::npos) {
            throw std::invalid_argument("Invalid Monte Carlo parameter key: " + param_key);
        }

        std::string device_name = param_key.substr(0, dot_pos);
        std::string parameter_name = param_key.substr(dot_pos + 1);

        auto device = circuit.get_device(device_name);
        if (!device || !device->set_parameter(parameter_name, value)) {
            throw std::invalid_argument("Cannot set Monte Carlo parameter: " + param_key);
        }
        spdlog::trace("Set {}.{} = {:.6e}", device_name, parameter_name, value);
    }
}

} // namespace kirimaspice::analysis
