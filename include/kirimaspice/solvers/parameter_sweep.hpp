// include/kirimaspice/solvers/parameter_sweep.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace kirimaspice::solvers {

/// Parameter sweep result for a single sweep point
struct ParameterSweepPoint {
    // Single parameter support (backward compatible)
    Real param_value;                    ///< Parameter value (for single parameter sweep)
    std::string param_name;              ///< Parameter name (for single parameter sweep)

    // Multi-parameter support (enhancement)
    std::unordered_map<std::string, Real> param_values;  ///< All parameter values (for multi-parameter sweep)

    DenseVector solution;                ///< Analysis solution (node voltages + branch currents)
    SolverStats stats;                   ///< Solver statistics
    ConvergenceStatus status;            ///< Convergence status
};

/// Parameter sweep result container
struct ParameterSweepResult {
    std::vector<ParameterSweepPoint> sweep_points;  ///< All sweep points
    ConvergenceStatus overall_status;               ///< Overall sweep status
    size_t num_success;                             ///< Number of successful points
    size_t num_failed;                              ///< Number of failed points
};

/// Parameter sweep executor
///
/// Executes parameter sweeps for .STEP directives.
/// For each sweep point:
/// 1. Updates global parameter value
/// 2. Re-parses netlist (to apply parameter expansion)
/// 3. Runs analysis (DC/Transient/AC)
/// 4. Collects results
///
/// Usage example:
/// ```cpp
/// parser::NetlistParser parser;
/// auto parse_result = parser.parse_file("circuit.cir");
///
/// if (!parse_result.config.step_configs.empty()) {
///     ParameterSweepExecutor executor(parse_result.config);
///     auto result = executor.execute_dc_sweep(
///         "circuit.cir",
///         [](circuit::CircuitGraph& circuit, const SimulationConfig& config) {
///             solvers::DCSolver solver(circuit, config);
///             DenseVector solution;
///             SolverStats stats;
///             return solver.solve(solution, stats);
///         }
///     );
/// }
/// ```
class ParameterSweepExecutor {
public:
    /// Constructor
    /// @param config Base simulation configuration (with step_configs)
    explicit ParameterSweepExecutor(const SimulationConfig& config);

    /// Execute DC analysis sweep
    /// @param netlist_path Path to netlist file
    /// @return Sweep results
    ParameterSweepResult execute_dc_sweep(const std::string& netlist_path);

    /// Execute DC analysis sweep with custom solver function
    /// @param netlist_path Path to netlist file
    /// @param solver_func Custom solver function (for testing)
    /// @return Sweep results
    using DCAnalysisFunc = std::function<ConvergenceStatus(
        circuit::CircuitGraph&,
        const SimulationConfig&,
        DenseVector&,
        SolverStats&
    )>;
    ParameterSweepResult execute_dc_sweep_custom(
        const std::string& netlist_path,
        DCAnalysisFunc solver_func
    );

    /// Get number of total sweep points (product of all step configs)
    /// @return Total number of sweep points
    size_t get_total_sweep_points() const;

    /// Check if sweep is enabled
    /// @return true if step_configs is not empty
    bool is_sweep_enabled() const;

private:
    SimulationConfig base_config_;  ///< Base configuration

    /// Execute sweep for a single parameter (internal)
    /// @param netlist_path Path to netlist file
    /// @param step_config Step configuration
    /// @param solver_func Solver function
    /// @return Sweep results for single parameter
    ParameterSweepResult execute_single_param_sweep(
        const std::string& netlist_path,
        const StepConfig& step_config,
        DCAnalysisFunc solver_func
    );

    /// Execute sweep for multiple parameters (Cartesian product)
    /// @param netlist_path Path to netlist file
    /// @param solver_func Solver function
    /// @return Sweep results for all parameter combinations
    ParameterSweepResult execute_multi_param_sweep(
        const std::string& netlist_path,
        DCAnalysisFunc solver_func
    );
};

} // namespace kirimaspice::solvers
