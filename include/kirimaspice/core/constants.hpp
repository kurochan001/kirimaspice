// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/core/constants.hpp
// Common constants and configuration values for KirimaSpice
#pragma once

#include "types.hpp"
#include <cstddef>

namespace kirimaspice::core {

//=============================================================================
// Numerical Constants
//=============================================================================

/// 0 °C in kelvin. A netlist gives temperatures in °C, as SPICE does: .TEMP,
/// .OPTIONS TEMP, and TNOM on a model card or an element line. The
/// configuration and the devices hold them in kelvin.
constexpr Real ZERO_CELSIUS_IN_KELVIN = 273.15;

/// A temperature of the netlist [°C] in kelvin
constexpr Real celsius_to_kelvin(Real celsius) {
    return celsius + ZERO_CELSIUS_IN_KELVIN;
}

/// Minimum conductance to prevent singular MNA matrix
/// Used in devices for gmin (minimum junction conductance)
constexpr Real GMIN_DEFAULT = 1e-12;  // [S]

/// Small epsilon for numerical comparisons
constexpr Real EPSILON_SMALL = 1e-12;

/// Epsilon for frequency domain calculations
constexpr Real EPSILON_FREQ = 1e-30;

// Named constants for hardcoded numerical values

/// Maximum argument for exp() to avoid overflow
/// exp(80) ≈ 5.5e34, safely below DBL_MAX ≈ 1.8e308
constexpr Real EXP_OVERFLOW_LIMIT = 80.0;

/// Minimum argument for exp() to avoid underflow
constexpr Real EXP_UNDERFLOW_LIMIT = -100.0;

/// Minimum current threshold (1 femtoampere) [A]
/// Used to avoid division by zero in current-dependent calculations
constexpr Real CURRENT_THRESHOLD_FA = 1e-15;

/// PN junction cutoff factor for exponential approximation
/// Below this threshold (× Vt), reverse saturation current dominates
constexpr Real JUNCTION_CUTOFF_FACTOR = -5.0;

/// Maximum voltage limiting step per Newton iteration [V]
/// Prevents excessive voltage changes that could cause divergence
constexpr Real VOLTAGE_LIMITING_STEP = 5.0;

//=============================================================================
// Iteration Limits
//=============================================================================

/// Maximum iterations for subcircuit expansion (prevent infinite loops)
constexpr int MAX_SUBCIRCUIT_ITERATIONS = 100;

/// Maximum recursion depth for subcircuit dependency resolution (prevent stack overflow)
constexpr int MAX_SUBCIRCUIT_DEPTH = 100;

/// Maximum iterations for WHILE/REPEAT loops in .CONTROL blocks
constexpr int MAX_CONTROL_LOOP_ITERATIONS = 10000;

/// Maximum iterations for Newton-Raphson convergence
constexpr int MAX_NR_ITERATIONS = 100;

/// Maximum iterations for parameter sweep inner loop
constexpr int MAX_SWEEP_ITERATIONS = 1000;

//=============================================================================
// Circuit Size Limits
//=============================================================================

/// Maximum number of nodes in a circuit (prevent excessive memory allocation)
/// 1 million nodes should be sufficient for all practical circuits
constexpr std::uint32_t MAX_CIRCUIT_NODES = 1000000;

/// Maximum number of branches in a circuit (prevent excessive memory allocation)
/// 1 million branches should be sufficient for all practical circuits
constexpr std::uint32_t MAX_CIRCUIT_BRANCHES = 1000000;

//=============================================================================
// Default Capacitance Values
//=============================================================================

/// Default gate-source capacitance for MOSFETs [F]
constexpr Real DEFAULT_MOSFET_CGS = 1e-12;

/// Default gate-drain capacitance for MOSFETs [F]
constexpr Real DEFAULT_MOSFET_CGD = 1e-12;

//=============================================================================
// Environment Variables Documentation
//=============================================================================
//
// KirimaSpice uses the following environment variables for configuration:
//
// === Debug Options ===
//
// KIRIMASPICE_DEBUG_DUMP
//   Values: Any (only whether it is set matters)
//   Default: Not set
//   Description: Write the MNA system of the second Newton iteration of
//                each DC solve, overwriting the files of the previous solve
//   Files created: debug_coo.txt, debug_csc.txt, debug_b.txt and
//                  debug_x.txt in the current directory
//
// KIRIMASPICE_BSIM1_DEBUG
//   Values: Any; a non-empty value is the name of the one device to log
//   Default: Not set
//   Description: Enable verbose BSIM1 model debugging output
//
//=============================================================================

//=============================================================================
// Safe Array Access Utilities
//=============================================================================

/// Check if a node ID is valid for array access
/// @param node_id Node identifier (1-based, 0 = ground)
/// @param array_size Size of the node_voltages array
/// @return true if node_id is valid for array access
inline bool is_valid_node_id(NodeId node_id, std::size_t array_size) {
    return node_id != NODE_GND && node_id > 0 &&
           static_cast<std::size_t>(node_id - 1) < array_size;
}

/// Safely get node voltage with bounds checking
/// @param node_voltages Array of node voltages (0-indexed)
/// @param node_id Node identifier (1-based, 0 = ground)
/// @param array_size Size of the node_voltages array
/// @param default_value Value to return for ground or invalid node (default: 0.0)
/// @return Node voltage or default_value
inline Real safe_get_node_voltage(const Real* node_voltages, NodeId node_id,
                                  std::size_t array_size, Real default_value = 0.0) {
    if (node_id == NODE_GND) {
        return 0.0;  // Ground is always 0V
    }
    if (node_id > 0 && static_cast<std::size_t>(node_id - 1) < array_size) {
        return node_voltages[node_id - 1];
    }
    return default_value;  // Invalid node ID
}

/// Safely get node voltage from std::vector with bounds checking
/// @param node_voltages Vector of node voltages (0-indexed)
/// @param node_id Node identifier (1-based, 0 = ground)
/// @param default_value Value to return for ground or invalid node (default: 0.0)
/// @return Node voltage or default_value
inline Real safe_get_node_voltage(const std::vector<Real>& node_voltages,
                                  NodeId node_id, Real default_value = 0.0) {
    return safe_get_node_voltage(node_voltages.data(), node_id,
                                 node_voltages.size(), default_value);
}

/// Voltage of a node in the vector that Device::accept_solution receives,
/// whose element 0 is ground and element k is node k (unlike the stamps'
/// vectors, which hold node k at element k - 1).
inline Real accepted_node_voltage(const std::vector<Real>& node_voltages, NodeId node_id) {
    if (node_id == NODE_GND || node_id >= node_voltages.size()) {
        return 0.0;
    }
    return node_voltages[node_id];
}

}  // namespace kirimaspice::core
