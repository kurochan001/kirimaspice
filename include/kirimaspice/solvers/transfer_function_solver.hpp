// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/transfer_function_solver.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/types.hpp"

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
};

} // namespace kirimaspice
