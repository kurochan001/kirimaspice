// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/sensitivity_analyzer.cpp
#include "kirimaspice/solvers/sensitivity_analyzer.hpp"
#include "kirimaspice/devices/device.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace kirimaspice::solvers {

SensitivityAnalyzer::SensitivityAnalyzer(const circuit::CircuitGraph& circuit,
                                         const SimulationConfig& config)
    : circuit_(circuit)
    , config_(config)
    , dc_solver_(circuit, config) {

    // Initialize from config
    delta_relative_ = config.sensitivity.delta_relative;
    delta_absolute_ = config.sensitivity.delta_absolute;
}

SensitivityAnalysisResult SensitivityAnalyzer::analyze(const DenseVector& nominal_solution) {
    SensitivityAnalysisResult result;
    result.nominal_solution = nominal_solution;
    result.success = false;

    spdlog::info("Starting sensitivity analysis for {} output nodes",
                config_.sensitivity.output_nodes.size());

    // Get all perturbable parameters
    auto parameters = get_perturbable_parameters();
    spdlog::info("Found {} perturbable parameters", parameters.size());

    // Calculate sensitivity for each parameter and each output node
    for (const auto& [device_name, parameter_name] : parameters) {
        for (const auto& output_node : config_.sensitivity.output_nodes) {
            auto sens_result = calculate_parameter_sensitivity(
                device_name, parameter_name, output_node, nominal_solution);

            if (sens_result.nominal_value != 0.0) {  // Only add if parameter exists
                result.sensitivities.push_back(sens_result);
            }
        }
    }

    if (result.sensitivities.empty()) {
        result.error_message = "No valid sensitivity results computed";
        spdlog::warn("Sensitivity analysis: no valid results");
        return result;
    }

    // Sort by relative importance (descending)
    std::sort(result.sensitivities.begin(), result.sensitivities.end(),
             [](const SensitivityResult& a, const SensitivityResult& b) {
                 return a.relative_importance > b.relative_importance;
             });

    result.success = true;
    spdlog::info("Sensitivity analysis completed: {} sensitivity values computed",
                result.sensitivities.size());

    return result;
}

SensitivityResult SensitivityAnalyzer::calculate_parameter_sensitivity(
    const std::string& device_name,
    const std::string& parameter_name,
    const std::string& output_node,
    const DenseVector& nominal_solution) {

    SensitivityResult result;
    result.device_name = device_name;
    result.parameter_name = parameter_name;
    result.output_node = output_node;

    // Get nominal parameter value
    auto nominal_opt = get_parameter_value(device_name, parameter_name);
    if (!nominal_opt) {
        result.nominal_value = 0.0;
        result.sensitivity = 0.0;
        result.normalized_sensitivity = 0.0;
        result.relative_importance = 0.0;
        return result;
    }
    Real nominal_value = *nominal_opt;
    result.nominal_value = nominal_value;

    // Get nominal output voltage
    auto v_nominal_opt = get_output_voltage(nominal_solution, output_node);
    if (!v_nominal_opt) {
        spdlog::warn("Output node '{}' not found in solution", output_node);
        result.nominal_output = 0.0;
        result.sensitivity = 0.0;
        result.normalized_sensitivity = 0.0;
        result.relative_importance = 0.0;
        return result;
    }
    Real v_nominal = *v_nominal_opt;
    result.nominal_output = v_nominal;

    // Calculate perturbation delta
    Real delta = calculate_delta(nominal_value);

    Real sensitivity = 0.0;

    if (use_central_difference_) {
        // Central difference: (f(x+δ) - f(x-δ)) / (2δ)

        // Perturb parameter forward
        set_parameter_value(device_name, parameter_name, nominal_value + delta);
        auto solution_forward = solve_dc_perturbed();

        // Perturb parameter backward
        set_parameter_value(device_name, parameter_name, nominal_value - delta);
        auto solution_backward = solve_dc_perturbed();

        // Restore nominal value
        set_parameter_value(device_name, parameter_name, nominal_value);

        if (!solution_forward || !solution_backward) {
            spdlog::warn("DC solver failed for parameter {}:{} (central difference)",
                        device_name, parameter_name);
            result.sensitivity = 0.0;
            result.normalized_sensitivity = 0.0;
            result.relative_importance = 0.0;
            return result;
        }

        auto v_forward_opt = get_output_voltage(*solution_forward, output_node);
        auto v_backward_opt = get_output_voltage(*solution_backward, output_node);

        if (!v_forward_opt || !v_backward_opt) {
            result.sensitivity = 0.0;
            result.normalized_sensitivity = 0.0;
            result.relative_importance = 0.0;
            return result;
        }

        sensitivity = (*v_forward_opt - *v_backward_opt) / (2.0 * delta);

    } else {
        // Forward difference: (f(x+δ) - f(x)) / δ

        // Perturb parameter forward
        set_parameter_value(device_name, parameter_name, nominal_value + delta);
        auto solution_perturbed = solve_dc_perturbed();

        // Restore nominal value
        set_parameter_value(device_name, parameter_name, nominal_value);

        if (!solution_perturbed) {
            spdlog::warn("DC solver failed for parameter {}:{} (forward difference)",
                        device_name, parameter_name);
            result.sensitivity = 0.0;
            result.normalized_sensitivity = 0.0;
            result.relative_importance = 0.0;
            return result;
        }

        auto v_perturbed_opt = get_output_voltage(*solution_perturbed, output_node);
        if (!v_perturbed_opt) {
            result.sensitivity = 0.0;
            result.normalized_sensitivity = 0.0;
            result.relative_importance = 0.0;
            return result;
        }

        sensitivity = (*v_perturbed_opt - v_nominal) / delta;
    }

    result.sensitivity = sensitivity;

    // Calculate normalized sensitivity: (∂V_out/V_out)/(∂P/P)
    // = (∂V_out/∂P) * (P/V_out)
    if (std::abs(v_nominal) > 1e-15 && std::abs(nominal_value) > 1e-15) {
        result.normalized_sensitivity = sensitivity * (nominal_value / v_nominal);
    } else {
        result.normalized_sensitivity = 0.0;
    }

    result.relative_importance = std::abs(result.normalized_sensitivity);

    return result;
}

std::optional<Real> SensitivityAnalyzer::get_parameter_value(
    const std::string& device_name,
    const std::string& parameter_name) const {

    try {
        auto device = circuit_.get_device(device_name);
        return device->get_parameter(parameter_name);
    } catch (const std::runtime_error&) {
        // Device not found
        spdlog::warn("Device '{}' not found in circuit", device_name);
        return std::nullopt;
    }
}

bool SensitivityAnalyzer::set_parameter_value(
    const std::string& device_name,
    const std::string& parameter_name,
    Real value) {

    try {
        auto device = circuit_.get_device(device_name);
        return device->set_parameter(parameter_name, value);
    } catch (const std::runtime_error&) {
        // Device not found
        spdlog::warn("Device '{}' not found in circuit", device_name);
        return false;
    }
}

Real SensitivityAnalyzer::calculate_delta(Real nominal_value) const {
    // Use relative perturbation for large values, absolute for small values
    Real delta_rel = std::abs(nominal_value) * delta_relative_;
    Real delta_abs = delta_absolute_;

    return std::max(delta_rel, delta_abs);
}

std::optional<DenseVector> SensitivityAnalyzer::solve_dc_perturbed() const {
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver_.solve(solution, stats);
    if (status == ConvergenceStatus::SUCCESS) {
        return solution;
    }
    return std::nullopt;
}

std::optional<Real> SensitivityAnalyzer::get_output_voltage(
    const DenseVector& solution,
    const std::string& output_node) const {

    // Get node ID from name
    NodeId node_id;
    try {
        node_id = circuit_.get_node_id(output_node);
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }

    // Ground node voltage is always 0
    if (node_id == NODE_GND) {
        return 0.0;
    }

    // Check bounds
    if (node_id - 1 >= solution.size()) {
        return std::nullopt;
    }

    return solution[node_id - 1];
}

std::vector<std::pair<std::string, std::string>>
SensitivityAnalyzer::get_perturbable_parameters() const {
    std::vector<std::pair<std::string, std::string>> parameters;

    // Iterate through all devices and collect their perturbable parameters
    for (const auto& device : circuit_.get_devices()) {
        const std::string& device_name = device->name();
        auto param_list = device->get_perturbable_parameters();

        for (const auto& param_name : param_list) {
            parameters.push_back({device_name, param_name});
        }
    }

    spdlog::debug("Found {} perturbable parameters across {} devices",
                 parameters.size(), circuit_.get_devices().size());

    return parameters;
}

} // namespace kirimaspice::solvers
