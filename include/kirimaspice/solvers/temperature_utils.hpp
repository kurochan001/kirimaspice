// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/temperature_utils.hpp
/**
 * @file temperature_utils.hpp
 * @brief Temperature utility functions for device temperature propagation
 *
 * Step 2.4: Moved implementation to .cpp to reduce header dependencies.
 * This header now contains only the function declaration.
 */
#pragma once

#include "kirimaspice/core/types.hpp"

namespace kirimaspice::circuit {
class CircuitGraph;  // Forward declaration
}

namespace kirimaspice::solvers {

/**
 * @brief Apply temperature to all temperature-aware devices in the circuit.
 * @param circuit The circuit containing devices
 * @param temp_K Temperature in Kelvin
 */
void apply_temperature_to_devices(const circuit::CircuitGraph& circuit, Real temp_K);

}  // namespace kirimaspice::solvers
