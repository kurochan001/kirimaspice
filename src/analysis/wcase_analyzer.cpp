// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/analysis/wcase_analyzer.cpp
#include "kirimaspice/analysis/wcase_analyzer.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace kirimaspice::analysis {

WCaseAnalyzer::WCaseAnalyzer(circuit::CircuitGraph& circuit, const SimulationConfig& config)
    : circuit_(circuit), config_(config) {
    spdlog::info("WCaseAnalyzer created for {} parameter variations",
                config_.wcase.variations.size());
}

WorstCaseResult WCaseAnalyzer::analyze() {
    spdlog::info("Starting worst-case analysis");

    if (!config_.wcase.enabled) {
        spdlog::warn("Worst-case analysis is not enabled");
        return WorstCaseResult{};
    }

    // The values of the varied parameters as the circuit has them, to be
    // restored after the cases
    std::map<std::string, Real> original_values;
    for (const auto& var : config_.wcase.variations) {
        const std::size_t dot_pos = var.parameter_name.find('.');
        if (dot_pos == std::string::npos) {
            continue;
        }
        auto device = circuit_.get_device(var.parameter_name.substr(0, dot_pos));
        if (!device) {
            continue;
        }
        if (auto value = device->get_parameter(var.parameter_name.substr(dot_pos + 1))) {
            original_values[var.parameter_name] = *value;
        }
    }

    // Generate all corner case combinations
    auto corner_cases = generate_corner_cases();
    spdlog::info("Generated {} corner cases", corner_cases.size());

    // Run analysis for each corner case
    WorstCaseResult result;
    result.all_cases.reserve(corner_cases.size());

    for (std::size_t i = 0; i < corner_cases.size(); ++i) {
        const auto& params = corner_cases[i];

        // Generate case name
        std::string case_name;
        for (const auto& [param, value] : params) {
            if (!case_name.empty()) {
                case_name += ",";
            }
            case_name += fmt::format("{}={:.6g}", param, value);
        }

        spdlog::info("Running case {}/{}: {}", i + 1, corner_cases.size(), case_name);

        // Run this corner case
        WorstCaseCaseResult case_result = run_case(case_name, params);
        result.all_cases.push_back(case_result);
    }

    // Find worst and best cases
    find_extremes(result.all_cases, config_.wcase.objective,
                 result.worst_case_name, result.worst_value, result.worst_parameter_values,
                 result.best_case_name, result.best_value, result.best_parameter_values);

    // The nominal case is the one combination with every parameter at its
    // typical value (generate_corner_cases names those values <param>_typ;
    // with no variations it is the single empty case)
    for (const auto& case_result : result.all_cases) {
        const auto& values = case_result.parameter_values;
        const bool all_typical = std::all_of(values.begin(), values.end(), [](const auto& entry) {
            return entry.first.ends_with("_typ");
        });
        if (all_typical) {
            result.nominal_value = case_result.output_value;
            break;
        }
    }

    // Leave the circuit as it was found
    apply_parameters(original_values);

    result.success = true;

    spdlog::info("Worst-case analysis complete:");
    spdlog::info("  Worst case: {} = {:.6e}", result.worst_case_name, result.worst_value);
    spdlog::info("  Best case: {} = {:.6e}", result.best_case_name, result.best_value);
    spdlog::info("  Nominal: {:.6e}", result.nominal_value);

    return result;
}

std::vector<std::map<std::string, Real>> WCaseAnalyzer::generate_corner_cases() {
    std::vector<std::map<std::string, Real>> cases;

    if (config_.wcase.variations.empty()) {
        // No variations - just run nominal case
        cases.push_back({});
        return cases;
    }

    // For each parameter, generate min/nominal/max values
    std::vector<std::vector<std::pair<std::string, Real>>> param_values;

    for (const auto& var : config_.wcase.variations) {
        std::vector<std::pair<std::string, Real>> values;

        // Get nominal value from circuit
        Real nominal = 1.0;  // Default fallback

        // Parse "device.parameter" format
        std::size_t dot_pos = var.parameter_name.find('.');
        if (dot_pos != std::string::npos) {
            std::string device_name = var.parameter_name.substr(0, dot_pos);
            std::string parameter_name = var.parameter_name.substr(dot_pos + 1);

            auto device = circuit_.get_device(device_name);
            if (device) {
                auto nominal_opt = device->get_parameter(parameter_name);
                if (nominal_opt.has_value()) {
                    nominal = nominal_opt.value();
                    spdlog::trace("Got nominal value for {}: {:.6e}", var.parameter_name, nominal);
                } else {
                    spdlog::warn("Failed to get parameter {}, using default nominal={:.6e}",
                                var.parameter_name, nominal);
                }
            } else {
                spdlog::warn("Device {} not found, using default nominal={:.6e}",
                            device_name, nominal);
            }
        } else {
            spdlog::warn("Invalid parameter name format: {} (expected device.parameter), using nominal=1.0",
                        var.parameter_name);
        }

        // Calculate min/max based on deviation
        Real min_val, max_val;
        if (var.use_absolute_limits) {
            min_val = var.min_value;
            max_val = var.max_value;
        } else {
            Real deviation = var.deviation_percent / 100.0;
            min_val = nominal * (1.0 - deviation);
            max_val = nominal * (1.0 + deviation);
        }

        values.push_back({var.parameter_name + "_min", min_val});
        values.push_back({var.parameter_name + "_typ", nominal});
        values.push_back({var.parameter_name + "_max", max_val});

        param_values.push_back(values);
    }

    // Generate all combinations (Cartesian product)
    // For N parameters with 3 values each, we have 3^N combinations
    std::size_t num_combinations = 1;
    for (const auto& pv : param_values) {
        num_combinations *= pv.size();
    }

    for (std::size_t comb_idx = 0; comb_idx < num_combinations; ++comb_idx) {
        std::map<std::string, Real> case_params;
        std::size_t idx = comb_idx;

        for (const auto& pv : param_values) {
            std::size_t param_idx = idx % pv.size();
            idx /= pv.size();

            case_params[pv[param_idx].first] = pv[param_idx].second;
        }

        cases.push_back(case_params);
    }

    return cases;
}

void WCaseAnalyzer::apply_parameters(const std::map<std::string, Real>& parameter_values) {
    // Apply parameter values to circuit
    // Parse parameter names (e.g., "R1.resistance" -> device R1, parameter "resistance")
    // Find device in circuit and call device->set_parameter("resistance", value)

    for (const auto& [param_name, value] : parameter_values) {
        // Skip "_min", "_typ", "_max" suffixes added by generate_corner_cases()
        std::string clean_param_name = param_name;
        if (clean_param_name.size() > 4) {
            if (clean_param_name.substr(clean_param_name.size() - 4) == "_min" ||
                clean_param_name.substr(clean_param_name.size() - 4) == "_typ" ||
                clean_param_name.substr(clean_param_name.size() - 4) == "_max") {
                clean_param_name = clean_param_name.substr(0, clean_param_name.size() - 4);
            }
        }

        // Split by dot: "R1.resistance" -> "R1" + "resistance"
        std::size_t dot_pos = clean_param_name.find('.');
        if (dot_pos == std::string::npos) {
            spdlog::warn("Invalid parameter name format: {} (expected device.parameter)", param_name);
            continue;
        }

        std::string device_name = clean_param_name.substr(0, dot_pos);
        std::string parameter_name = clean_param_name.substr(dot_pos + 1);

        // Find device in circuit
        auto device = circuit_.get_device(device_name);
        if (!device) {
            spdlog::warn("Device not found: {}", device_name);
            continue;
        }

        // Set parameter value
        bool success = device->set_parameter(parameter_name, value);
        if (success) {
            spdlog::trace("  Set {}.{} = {:.6e}", device_name, parameter_name, value);
        } else {
            spdlog::warn("Failed to set parameter: {}.{}", device_name, parameter_name);
        }
    }
}

WorstCaseCaseResult WCaseAnalyzer::run_case(const std::string& case_name,
                                            const std::map<std::string, Real>& parameter_values) {
    WorstCaseCaseResult case_result;
    case_result.case_name = case_name;
    case_result.parameter_values = parameter_values;

    // Apply parameters to circuit
    apply_parameters(parameter_values);

    try {
        // Run the appropriate analysis type
        Real output_value = extract_output_value(config_.wcase.analysis_type);

        case_result.output_value = output_value;
        case_result.success = true;

        spdlog::trace("  Output: {:.6e}", output_value);
    } catch (const std::exception& e) {
        spdlog::error("Case {} failed: {}", case_name, e.what());
        case_result.success = false;
        case_result.output_value = std::numeric_limits<Real>::quiet_NaN();
    }

    return case_result;
}

Real WCaseAnalyzer::extract_output_value(WorstCaseConfig::AnalysisType analysis_type) {
    // Parse output variable
    std::string var_type, node_or_source;
    parse_output_variable(config_.wcase.output_variable, var_type, node_or_source);

    // Run analysis and extract value
    // NOTE: This is a placeholder implementation
    // Actual implementation needs to run the appropriate solver

    if (analysis_type == WorstCaseConfig::AnalysisType::DC) {
        // Run DC analysis
        solvers::DCSolver dc_solver(circuit_, config_);
        DenseVector solution(circuit_.num_nodes());
        SolverStats stats;

        ConvergenceStatus status = dc_solver.solve(solution, stats);

        if (status != ConvergenceStatus::SUCCESS) {
            throw std::runtime_error("DC analysis failed");
        }

        if (var_type == "v") {
            // Voltage measurement
            // Node ID is 1-based (ground=0), solution vector is 0-indexed
            NodeId node_id = circuit_.get_node_id(node_or_source);
            if (node_id == 0 || node_id > solution.size()) {
                throw std::runtime_error(fmt::format("Invalid node: {}", node_or_source));
            }
            return solution[node_id - 1];
        } else {
            // Current measurement - not implemented yet
            throw std::runtime_error("Current measurement not yet implemented for worst-case analysis");
        }
    } else if (analysis_type == WorstCaseConfig::AnalysisType::TRAN) {
        // Transient analysis
        throw std::runtime_error("Transient worst-case analysis not yet implemented");
    } else {
        // AC analysis
        throw std::runtime_error("AC worst-case analysis not yet implemented");
    }
}

void WCaseAnalyzer::parse_output_variable(const std::string& output_var,
                                         std::string& var_type,
                                         std::string& node_or_source) {
    // Parse "v(node)" or "i(source)" format
    std::size_t open_paren = output_var.find('(');
    std::size_t close_paren = output_var.find(')');

    if (open_paren == std::string::npos || close_paren == std::string::npos) {
        throw std::runtime_error(fmt::format("Invalid output variable format: {}", output_var));
    }

    var_type = output_var.substr(0, open_paren);
    node_or_source = output_var.substr(open_paren + 1, close_paren - open_paren - 1);

    // Convert to lowercase for consistent comparison
    std::transform(var_type.begin(), var_type.end(), var_type.begin(), ::tolower);
}

void WCaseAnalyzer::find_extremes(const std::vector<WorstCaseCaseResult>& all_cases,
                                  WorstCaseConfig::Objective objective,
                                  std::string& worst_case_name,
                                  Real& worst_value,
                                  std::map<std::string, Real>& worst_params,
                                  std::string& best_case_name,
                                  Real& best_value,
                                  std::map<std::string, Real>& best_params) {
    if (all_cases.empty()) {
        return;
    }

    // Find worst (most extreme according to objective)
    worst_value = (objective == WorstCaseConfig::Objective::MAXIMIZE) ?
                  -std::numeric_limits<Real>::infinity() :
                  std::numeric_limits<Real>::infinity();

    best_value = (objective == WorstCaseConfig::Objective::MAXIMIZE) ?
                 std::numeric_limits<Real>::infinity() :
                 -std::numeric_limits<Real>::infinity();

    for (const auto& case_result : all_cases) {
        if (!case_result.success || std::isnan(case_result.output_value)) {
            continue;
        }

        // Check for worst case
        bool is_worse = (objective == WorstCaseConfig::Objective::MAXIMIZE) ?
                       (case_result.output_value > worst_value) :
                       (case_result.output_value < worst_value);

        if (is_worse) {
            worst_value = case_result.output_value;
            worst_case_name = case_result.case_name;
            worst_params = case_result.parameter_values;
        }

        // Check for best case
        bool is_better = (objective == WorstCaseConfig::Objective::MAXIMIZE) ?
                        (case_result.output_value < best_value) :
                        (case_result.output_value > best_value);

        if (is_better) {
            best_value = case_result.output_value;
            best_case_name = case_result.case_name;
            best_params = case_result.parameter_values;
        }
    }
}

} // namespace kirimaspice::analysis
