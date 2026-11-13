// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/transfer_function_solver.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

#include <string>

namespace kirimaspice {

/// Transfer function analysis result
/// Represents DC small-signal transfer function characteristics
struct TransferFunctionResult {
    Real transfer_gain;     ///< DC small-signal gain (output/input)
    Real input_resistance;  ///< Input resistance [Ω]
    Real output_resistance; ///< Output resistance [Ω]
    std::string output_signal; ///< Output signal specification
    std::string input_source;  ///< Input source name
    bool success = false;   ///< Whether analysis succeeded
};

/// Transfer function solver
/// Computes DC small-signal transfer function using linearization around operating point
class TransferFunctionSolver {
public:
    /// Constructor
    /// @param circuit Circuit graph
    /// @param config Simulation configuration
    TransferFunctionSolver(const circuit::CircuitGraph& circuit, const SimulationConfig& config);

    /// Perform transfer function analysis
    /// @return Transfer function result
    TransferFunctionResult analyze();

    /// Print transfer function result to console
    /// @param result Transfer function result
    static void print_result(const TransferFunctionResult& result);

private:
    const circuit::CircuitGraph& circuit_;
    const SimulationConfig& config_;

    /// Compute DC operating point
    /// @param dc_solver DC solver object
    /// @param solution Output: DC solution vector
    /// @return true if DC analysis succeeded
    bool compute_dc_operating_point(solvers::DCSolver& dc_solver, std::vector<Real>& solution);

    /// Extract node ID from signal specification (e.g., "v(out)" -> node ID)
    /// @param signal Signal specification
    /// @param node_id Output: node ID
    /// @return true if extraction succeeded
    bool extract_node_from_signal(const std::string& signal, NodeId& node_id) const;

    /// Extract source device ID from source name
    /// @param source_name Source name (e.g., "Vin")
    /// @param device_id Output: device ID
    /// @return true if extraction succeeded
    bool extract_source_device_id(const std::string& source_name, DeviceId& device_id) const;

    /// Compute transfer gain (∂V_out/∂V_in)
    /// @param solution DC operating point
    /// @param output_node Output node ID
    /// @param input_device Input source device ID
    /// @return Transfer gain
    Real compute_transfer_gain(const std::vector<Real>& solution,
                               NodeId output_node,
                               DeviceId input_device);

    /// Compute input resistance (∂V_in/∂I_in)
    /// @param solution DC operating point
    /// @param input_device Input source device ID
    /// @param dc_solver DC solver (for branch current extraction)
    /// @return Input resistance [Ω]
    Real compute_input_resistance(const std::vector<Real>& solution,
                                   DeviceId input_device,
                                   const solvers::DCSolver& dc_solver);

    /// Compute output resistance (∂V_out/∂I_out)
    /// @param solution DC operating point
    /// @param output_node Output node ID
    /// @return Output resistance [Ω]
    Real compute_output_resistance(const std::vector<Real>& solution,
                                    NodeId output_node);
};

} // namespace kirimaspice
