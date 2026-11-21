// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/analysis/monte_carlo_executor.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/analysis/random_distribution.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace kirimaspice::analysis {

/// Monte Carlo executor
///
/// Executes Monte Carlo analysis for .MC directives.
/// For each Monte Carlo run:
/// 1. Samples random parameter values from specified distributions
/// 2. Re-parses netlist with updated parameter values
/// 3. Runs analysis (DC/Transient/AC)
/// 4. Collects results for statistical analysis
///
/// After all runs complete, computes:
/// - Mean, standard deviation, variance
/// - Min, max, percentiles (1%, 5%, 25%, 50%, 75%, 95%, 99%)
/// - Histogram
/// - Yield (pass/fail against spec limits)
///
/// Usage example:
/// ```cpp
/// parser::NetlistParser parser;
/// auto parse_result = parser.parse_file("circuit.cir");
///
/// if (parse_result.config.monte_carlo.num_runs > 0) {
///     MonteCarloExecutor executor(parse_result.config);
///     auto result = executor.execute_dc_monte_carlo(
///         "circuit.cir",
///         "vout",  // Output node to analyze
///         1.8,     // Spec min (V)
///         2.2      // Spec max (V)
///     );
///
///     std::cout << "Mean: " << result.stats.mean << " V\n";
///     std::cout << "Std Dev: " << result.stats.std_dev << " V\n";
///     std::cout << "Yield: " << result.yield_percent << " %\n";
/// }
/// ```
class MonteCarloExecutor {
public:
    /// Constructor
    /// @param config Base simulation configuration (with monte_carlo config)
    explicit MonteCarloExecutor(const SimulationConfig& config);

    /// Execute DC Monte Carlo analysis
    /// @param netlist_path Path to netlist file
    /// @param output_node Output node to analyze (e.g., "vout")
    /// @param spec_min Minimum specification limit (for yield calculation)
    /// @param spec_max Maximum specification limit (for yield calculation)
    /// @return Monte Carlo results with statistics
    MonteCarloResult execute_dc_monte_carlo(
        const std::string& netlist_path,
        const std::string& output_node,
        Real spec_min = -1e9,
        Real spec_max = 1e9
    );

    /// Execute DC Monte Carlo (optimized path)
    /// - Parses netlist once, then applies sampled parameters directly to the circuit
    /// - Avoids per-run re-parsing to reduce overhead and enable GPU batching later
    /// @param netlist_path Path to netlist file
    /// @param output_node Output node to analyze
    /// @param spec_min Minimum specification limit
    /// @param spec_max Maximum specification limit
    /// @return Monte Carlo results with statistics
    MonteCarloResult execute_dc_monte_carlo_fast(
        const std::string& netlist_path,
        const std::string& output_node,
        Real spec_min = -1e9,
        Real spec_max = 1e9
    );

    /// Execute DC Monte Carlo analysis with custom solver function
    /// @param netlist_path Path to netlist file
    /// @param output_node Output node to analyze
    /// @param spec_min Minimum specification limit
    /// @param spec_max Maximum specification limit
    /// @param solver_func Custom solver function (for testing)
    /// @return Monte Carlo results with statistics
    using DCAnalysisFunc = std::function<ConvergenceStatus(
        circuit::CircuitGraph&,
        const SimulationConfig&,
        DenseVector&,
        SolverStats&
    )>;
    MonteCarloResult execute_dc_monte_carlo_custom(
        const std::string& netlist_path,
        const std::string& output_node,
        Real spec_min,
        Real spec_max,
        DCAnalysisFunc solver_func
    );

    /// Check if Monte Carlo is enabled
    /// @return true if num_runs > 0
    bool is_monte_carlo_enabled() const;

    /// Get number of Monte Carlo runs
    /// @return Number of runs
    std::uint32_t get_num_runs() const;

    /// Get the base seed of the runs
    /// @return Random seed (when .MC gave 0, the one drawn from the random device)
    std::uint32_t get_seed() const;

    /// The seed of the generator of run `run` (from 0) for base seed
    /// `base_seed`. It depends on the pair alone, not on the threading build
    /// or the order of the runs, and is never 0 (which RandomNumberGenerator
    /// takes as "seed from the random device").
    static std::uint32_t run_seed(std::uint32_t base_seed, std::uint32_t run);

private:
    SimulationConfig base_config_;          ///< Base configuration
    std::unique_ptr<RandomNumberGenerator> rng_;  ///< Holds the base seed; runs draw from their own

    /// Sample parameter values for a single Monte Carlo run
    /// @param rng The run's generator
    /// @param run_index Current run index (for logging)
    /// @return Map of parameter name -> sampled value
    std::map<std::string, Real> sample_parameters(RandomNumberGenerator& rng,
                                                  std::uint32_t run_index) const;

    /// Extract output value from solution vector
    /// @param circuit Circuit graph
    /// @param solution Solution vector (node voltages + branch currents)
    /// @param output_node Output node name
    /// @return Output node voltage
    Real extract_output_value(
        const circuit::CircuitGraph& circuit,
        const DenseVector& solution,
        const std::string& output_node
    );

    /// Apply sampled parameters to the circuit in-place
    /// @param circuit Circuit graph to modify
    /// @param parameter_values Map of "Device.Parameter" -> value
    void apply_parameters_inplace(
        circuit::CircuitGraph& circuit,
        const std::map<std::string, Real>& parameter_values
    );
};

} // namespace kirimaspice::analysis
