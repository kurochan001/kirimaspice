// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Test KLU integration with simple linear circuit
// Circuit: VCC(5V) -> R1(1k) -> node1 -> R2(1k) -> GND
// Expected: V_node1 = 2.5V (voltage divider)

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;

int main() {
    std::cout << "=== KLU Linear Circuit Test ===\n";
    std::cout << "Circuit: VCC(5V) -> R1(1k) -> node1 -> R2(1k) -> GND\n";
    std::cout << "Expected: V_node1 = 2.5V (voltage divider)\n\n";

    circuit::CircuitGraph circuit;

    // Power supply
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);

    // Resistor divider
    circuit.add_resistor("R1", "vcc", "node1", 1000.0);
    circuit.add_resistor("R2", "node1", "0", 1000.0);

    NodeId node_vcc = circuit.get_node_id("vcc");
    NodeId node_1 = circuit.get_node_id("node1");

    std::cout << "Node IDs: vcc=" << node_vcc
              << ", node1=" << node_1 << "\n\n";

    // DC solver
    SimulationConfig config;
    config.dc.max_iterations = 20;
    config.dc.abstol = 1e-10;
    config.dc.reltol = 1e-6;
    config.dc.vntol = 1e-6;

    // Enable debug logging
    spdlog::set_level(spdlog::level::debug);

    solvers::DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;

    std::cout << "=== Running DC Analysis ===\n\n";
    ConvergenceStatus status = solver.solve(solution, stats);

    // Reset log level
    spdlog::set_level(spdlog::level::info);

    std::cout << "\n=== Results ===\n";
    std::cout << std::fixed << std::setprecision(9);

    double v_vcc = solution[node_vcc - 1];
    double v_node1 = solution[node_1 - 1];

    std::cout << "  V_vcc   = " << v_vcc << " V\n";
    std::cout << "  V_node1 = " << v_node1 << " V\n\n";

    // Verification
    double expected_v_node1 = 2.5;
    double error = std::abs(v_node1 - expected_v_node1);
    double tolerance = 1e-6;

    std::cout << "=== Verification ===\n";
    std::cout << "  Expected: V_node1 = " << expected_v_node1 << " V\n";
    std::cout << "  Error: " << error << " V\n";
    std::cout << "  Tolerance: " << tolerance << " V\n\n";

    if (status == ConvergenceStatus::SUCCESS && error < tolerance) {
        std::cout << "✓ TEST PASSED: KLU integration is correct\n";
        return 0;
    } else {
        std::cout << "✗ TEST FAILED\n";
        if (status != ConvergenceStatus::SUCCESS) {
            std::cout << "  Reason: DC solve did not converge\n";
        } else {
            std::cout << "  Reason: V_node1 error = " << error << " > " << tolerance << "\n";
        }
        return 1;
    }
}
