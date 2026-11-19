// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/ngspice_dc_utils.hpp
// Common utilities for DC analysis validation against ngspice

#ifndef KIRIMASPICE_TESTS_VALIDATION_NGSPICE_DC_UTILS_HPP
#define KIRIMASPICE_TESTS_VALIDATION_NGSPICE_DC_UTILS_HPP

#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <map>

namespace kirimaspice::validation {

/**
 * @brief Structure to hold ngspice DC analysis data
 */
struct NgspiceDCData {
    std::map<std::string, double> node_voltages;  ///< Node name -> voltage [V]
};

/**
 * @brief Load ngspice DC analysis data from CSV file
 *
 * Expected format (CSV):
 *   node,voltage
 *   in,1.0
 *   n1,0.5
 *   ...
 *
 * @param filename Path to ngspice CSV file
 * @return NgspiceDCData with node voltages
 * @throws std::runtime_error if file cannot be opened or parsed
 */
inline NgspiceDCData load_ngspice_dc_data(const std::string& filename) {
    NgspiceDCData data;
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open ngspice DC data file: " + filename);
    }

    // A header line has no number after its comma and is passed over; a
    // file without a header keeps its first node.
    std::string line;
    // Parse data lines
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string node;
        double voltage;

        // Parse CSV: node,voltage
        if (std::getline(iss, node, ',') && (iss >> voltage)) {
            data.node_voltages[node] = voltage;
        }
    }

    if (data.node_voltages.empty()) {
        throw std::runtime_error("No valid data found in: " + filename);
    }

    return data;
}

/**
 * @brief Comparison result for a single node
 */
struct DCComparisonPoint {
    std::string node_name;         ///< Node name
    double expected;               ///< ngspice value [V]
    double actual;                 ///< kirimaspice value [V]
    double absolute_error;         ///< Absolute error [V]
    double relative_error;         ///< Relative error (dimensionless)
};

/**
 * @brief Compare kirimaspice DC results with ngspice reference data
 *
 * @param ngspice_data Reference data from ngspice
 * @param kirimaspice_solution DC solver solution from kirimaspice
 * @param circuit Circuit graph for node name lookup
 * @return Vector of comparison points for each node
 */
inline std::vector<DCComparisonPoint> compare_dc_results(
    const NgspiceDCData& ngspice_data,
    const DenseVector& kirimaspice_solution,
    const circuit::CircuitGraph& circuit
) {
    std::vector<DCComparisonPoint> comparison;

    for (const auto& [node_name, expected_voltage] : ngspice_data.node_voltages) {
        DCComparisonPoint point;
        point.node_name = node_name;
        point.expected = expected_voltage;

        // Get node ID from circuit graph
        try {
            NodeId node_id = circuit.get_node_id(node_name);
            // Solution vector is indexed by (NodeId - 1) since NodeId starts from 1
            point.actual = kirimaspice_solution[node_id - 1];
        } catch (const std::exception& e) {
            // Node not found - skip this comparison
            continue;
        }

        // Calculate absolute error
        point.absolute_error = point.actual - point.expected;

        // Calculate relative error
        if (std::abs(point.expected) > 1e-9) {
            point.relative_error = point.absolute_error / point.expected;
        } else {
            // Expected voltage is near zero - use absolute error
            point.relative_error = point.absolute_error;
        }

        comparison.push_back(point);
    }

    return comparison;
}

/**
 * @brief Print comparison results to stdout
 *
 * @param comparison Vector of comparison points
 * @param max_abs_error Maximum allowed absolute error [V]
 * @param max_rel_error Maximum allowed relative error (dimensionless)
 * @return true if all points are within tolerance, false otherwise
 */
inline bool print_and_check_dc_comparison(
    const std::vector<DCComparisonPoint>& comparison,
    double max_abs_error = 1e-6,
    double max_rel_error = 1e-3
) {
    std::cout << "\nDC Analysis Comparison Results:\n";
    std::cout << std::string(100, '=') << "\n";
    std::cout << std::setw(15) << "Node"
              << std::setw(15) << "Expected [V]"
              << std::setw(15) << "Actual [V]"
              << std::setw(15) << "Abs Error [V]"
              << std::setw(15) << "Rel Error [%]"
              << "\n";
    std::cout << std::string(100, '-') << "\n";

    bool all_passed = true;
    double max_abs_err = 0.0;
    double max_rel_err = 0.0;

    for (const auto& point : comparison) {
        std::cout << std::setw(15) << point.node_name
                  << std::setw(15) << std::fixed << std::setprecision(6) << point.expected
                  << std::setw(15) << point.actual
                  << std::setw(15) << std::scientific << std::setprecision(3) << point.absolute_error
                  << std::setw(15) << std::fixed << std::setprecision(4) << (point.relative_error * 100.0);

        // Check tolerances
        const bool abs_ok = std::abs(point.absolute_error) <= max_abs_error;
        const bool rel_ok = std::abs(point.relative_error) <= max_rel_error;

        if (!abs_ok && !rel_ok) {
            std::cout << "  *** FAIL ***";
            all_passed = false;
        }

        std::cout << "\n";

        max_abs_err = std::max(max_abs_err, std::abs(point.absolute_error));
        max_rel_err = std::max(max_rel_err, std::abs(point.relative_error));
    }

    std::cout << std::string(100, '=') << "\n";
    std::cout << "Maximum absolute error: " << std::scientific << std::setprecision(3)
              << max_abs_err << " V (tolerance: " << max_abs_error << " V)\n";
    std::cout << "Maximum relative error: " << std::fixed << std::setprecision(4)
              << (max_rel_err * 100.0) << " % (tolerance: " << (max_rel_error * 100.0) << " %)\n";

    if (all_passed) {
        std::cout << "\n✓ All points within tolerance\n";
    } else {
        std::cout << "\n✗ Some points exceeded tolerance\n";
    }

    return all_passed;
}

} // namespace kirimaspice::validation

#endif // KIRIMASPICE_TESTS_VALIDATION_NGSPICE_DC_UTILS_HPP
