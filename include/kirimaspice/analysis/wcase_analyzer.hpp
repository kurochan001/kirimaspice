// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/analysis/wcase_analyzer.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <memory>
#include <vector>
#include <string>
#include <map>

namespace kirimaspice::analysis {

/// Worst-case analysis executor
/// Evaluates circuit performance across parameter variations to identify worst/best cases
class WCaseAnalyzer {
public:
    /// Constructor
    /// @param circuit Circuit to analyze
    /// @param config Simulation configuration (includes worst-case settings)
    WCaseAnalyzer(circuit::CircuitGraph& circuit, const SimulationConfig& config);

    /// Execute worst-case analysis
    /// @return Worst-case analysis result with all corner cases evaluated
    WorstCaseResult analyze();

private:
    /// Generate all corner case combinations
    /// @return Vector of corner cases (each case is a map of parameter_name -> value)
    std::vector<std::map<std::string, Real>> generate_corner_cases();

    /// Apply parameter values to circuit
    /// @param parameter_values Map of parameter_name -> value to apply
    void apply_parameters(const std::map<std::string, Real>& parameter_values);

    /// Run analysis for a single corner case
    /// @param case_name Name of the corner case
    /// @param parameter_values Parameter values for this case
    /// @return Case result with output value
    WorstCaseCaseResult run_case(const std::string& case_name,
                                 const std::map<std::string, Real>& parameter_values);

    /// Extract output variable value from analysis result
    /// @param analysis_type Type of analysis (DC/TRAN/AC)
    /// @return Output value
    Real extract_output_value(WorstCaseConfig::AnalysisType analysis_type);

    /// Parse output variable string (e.g., "v(out)", "i(vload)")
    /// @param output_var Output variable string
    /// @param[out] var_type Variable type ("v" or "i")
    /// @param[out] node_or_source Node name (for voltage) or source name (for current)
    void parse_output_variable(const std::string& output_var,
                              std::string& var_type,
                              std::string& node_or_source);

    /// Find worst/best cases from all results
    /// @param all_cases All corner case results
    /// @param objective Objective (MAX or MIN)
    /// @param[out] worst_case_name Worst case name
    /// @param[out] worst_value Worst value
    /// @param[out] worst_params Worst case parameters
    /// @param[out] best_case_name Best case name
    /// @param[out] best_value Best value
    /// @param[out] best_params Best case parameters
    void find_extremes(const std::vector<WorstCaseCaseResult>& all_cases,
                      WorstCaseConfig::Objective objective,
                      std::string& worst_case_name,
                      Real& worst_value,
                      std::map<std::string, Real>& worst_params,
                      std::string& best_case_name,
                      Real& best_value,
                      std::map<std::string, Real>& best_params);

    circuit::CircuitGraph& circuit_;
    const SimulationConfig& config_;

    // Storage for analysis results
    std::vector<Real> dc_voltages_;           // DC node voltages
    std::vector<Real> transient_times_;       // Transient time points
    std::vector<std::vector<Real>> transient_voltages_;  // Transient node voltages
    std::vector<Real> ac_frequencies_;        // AC frequency points
    std::vector<std::vector<Complex>> ac_voltages_;  // AC node voltages (complex)
};

} // namespace kirimaspice::analysis
