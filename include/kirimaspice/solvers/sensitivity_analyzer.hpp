// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/sensitivity_analyzer.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

#include <string>
#include <vector>

namespace kirimaspice::solvers {

/// Sensitivity Analyzer
/// Calculates DC sensitivity: ∂V_out/∂P for each device parameter P
/// Uses numerical differentiation (forward or central difference)
class SensitivityAnalyzer {
public:
    /// Constructor
    /// @param circuit: Circuit to analyze
    /// @param config: Simulation configuration (contains sensitivity settings)
    SensitivityAnalyzer(const circuit::CircuitGraph& circuit,
                       const SimulationConfig& config);

    /// Perform sensitivity analysis
    /// @param nominal_solution: Nominal DC operating point solution
    /// @return Sensitivity analysis results
    SensitivityAnalysisResult analyze(const DenseVector& nominal_solution);

    /// Set numerical differentiation method
    /// @param use_central: true = central difference (more accurate), false = forward difference (faster)
    void set_differentiation_method(bool use_central) {
        use_central_difference_ = use_central;
    }

    /// Set perturbation parameters
    /// @param delta_relative: Relative perturbation (default: 1e-6)
    /// @param delta_absolute: Absolute perturbation for near-zero values (default: 1e-12)
    void set_perturbation(Real delta_relative, Real delta_absolute) {
        delta_relative_ = delta_relative;
        delta_absolute_ = delta_absolute;
    }

    /// Get parameter value from device
    /// @param device_name: Device name
    /// @param parameter_name: Parameter name
    /// @return Current parameter value, or nullopt if not found
    std::optional<Real> get_parameter_value(const std::string& device_name,
                                            const std::string& parameter_name) const;

    /// Set parameter value for device
    /// @param device_name: Device name
    /// @param parameter_name: Parameter name
    /// @param value: New parameter value
    /// @return true if parameter was set successfully
    bool set_parameter_value(const std::string& device_name,
                            const std::string& parameter_name,
                            Real value);

    /// Get all device parameters that can be perturbed
    /// @return Vector of (device_name, parameter_name) pairs
    std::vector<std::pair<std::string, std::string>> get_perturbable_parameters() const;

private:
    /// Calculate sensitivity for a single device parameter
    /// @param device_name: Device name (e.g., "R1", "Q1")
    /// @param parameter_name: Parameter name (e.g., "resistance", "IS", "VTO")
    /// @param output_node: Output node name
    /// @param nominal_solution: Nominal DC solution
    /// @return Sensitivity result for this parameter
    SensitivityResult calculate_parameter_sensitivity(
        const std::string& device_name,
        const std::string& parameter_name,
        const std::string& output_node,
        const DenseVector& nominal_solution);

    /// Calculate perturbation delta for given nominal value
    /// @param nominal_value: Nominal parameter value
    /// @return Perturbation delta (absolute)
    Real calculate_delta(Real nominal_value) const;

    /// Perform DC analysis with perturbed parameter
    /// @return DC solution vector, or nullopt if convergence failed
    std::optional<DenseVector> solve_dc_perturbed() const;

    /// Get output voltage from solution vector
    /// @param solution: DC solution vector
    /// @param output_node: Output node name
    /// @return Output voltage, or nullopt if node not found
    std::optional<Real> get_output_voltage(const DenseVector& solution,
                                           const std::string& output_node) const;

    const circuit::CircuitGraph& circuit_;
    const SimulationConfig& config_;
    mutable DCSolver dc_solver_;  ///< Mutable: solve() is non-const

    // Numerical differentiation settings
    bool use_central_difference_ = false;  ///< Central difference (more accurate) vs forward difference (faster)
    Real delta_relative_ = 1e-6;           ///< Relative perturbation (0.0001% default)
    Real delta_absolute_ = 1e-12;          ///< Absolute perturbation for near-zero values
};

} // namespace kirimaspice::solvers
