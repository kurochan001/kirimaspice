// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_dc_solver.cpp
#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// Test: Simple voltage divider
// V1 = 10V
// R1 = 1k (n1 to n2)
// R2 = 1k (n2 to GND)
// Expected: V(n2) = 5V
TEST(DCSolver, VoltageDivider) {
    CircuitGraph circuit;
    circuit.set_title("Voltage Divider Test");

    // Build circuit
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);
    circuit.add_resistor("R2", "n2", "0", 1000.0);

    // Create solver
    DCSolver solver(circuit);

    // Solve
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    // Check convergence
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Check solution
    // Node 0 (GND) is not in solution vector
    // Node 1 (n1): should be 10V (index 0)
    // Node 2 (n2): should be 5V (index 1)
    // Branch 0 (V1 current): index 2
    ASSERT_GE(solution.size(), 3u);

    EXPECT_NEAR(solution[0], 10.0, 1e-6);  // V(n1) = 10V
    EXPECT_NEAR(solution[1], 5.0, 1e-6);   // V(n2) = 5V

    // V1 delivers 10V / 2kΩ = 5mA; its branch current is positive into n+
    // through the source (NETLIST_CONVENTIONS rule 3), so it is -5mA
    EXPECT_NEAR(solution[2], -0.005, 1e-9);

    // Check statistics
    EXPECT_GT(stats.total_iterations, 0u);
    EXPECT_GT(stats.linear_solves, 0u);
}

// Test: Single resistor
// V1 = 5V
// R1 = 100Ω
// Expected: I = 50mA
TEST(DCSolver, SingleResistor) {
    CircuitGraph circuit;
    circuit.set_title("Single Resistor Test");

    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "0", 100.0);

    DCSolver solver(circuit);

    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // V(n1) = 5V
    ASSERT_GE(solution.size(), 2u);
    EXPECT_NEAR(solution[0], 5.0, 1e-6);

    // V1 delivers 5V / 100Ω = 50mA: branch current -50mA (rule 3)
    EXPECT_NEAR(solution[1], -0.05, 1e-9);
}

// Test: Current source
// I1 = 1mA (n1 to GND)
// R1 = 10kΩ (n1 to GND)
// Expected: V(n1) = 10V
TEST(DCSolver, CurrentSource) {
    CircuitGraph circuit;
    circuit.set_title("Current Source Test");

    circuit.add_current_source("I1", "0", "n1", 0.001);  // 1mA into n1
    circuit.add_resistor("R1", "n1", "0", 10000.0);

    DCSolver solver(circuit);

    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // V(n1) = I * R = 1mA * 10kΩ = 10V
    ASSERT_GE(solution.size(), 1u);
    EXPECT_NEAR(solution[0], 10.0, 1e-6);
}

// Test: Resistor bridge (Wheatstone bridge)
// V1 = 10V
//     n1
//    /  \
//  R1    R2
//  /      \
// n2      n3
//  \      /
//  R3    R4
//    \  /
//    GND
// R1=R3=1k, R2=R4=1k (balanced bridge)
// Expected: V(n2) = V(n3) = 5V
TEST(DCSolver, WheatstoneBridge) {
    CircuitGraph circuit;
    circuit.set_title("Wheatstone Bridge Test");

    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);
    circuit.add_resistor("R2", "n1", "n3", 1000.0);
    circuit.add_resistor("R3", "n2", "0", 1000.0);
    circuit.add_resistor("R4", "n3", "0", 1000.0);

    DCSolver solver(circuit);

    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Balanced bridge: V(n2) = V(n3) = 5V
    ASSERT_GE(solution.size(), 3u);
    EXPECT_NEAR(solution[0], 10.0, 1e-6);  // V(n1)
    EXPECT_NEAR(solution[1], 5.0, 1e-6);   // V(n2)
    EXPECT_NEAR(solution[2], 5.0, 1e-6);   // V(n3)
}

// Test: Multiple voltage sources
// V1 = 10V (n1 to GND)
// V2 = 5V (n2 to GND)
// R1 = 1kΩ (n1 to n2)
// Expected: I = (10V - 5V) / 1kΩ = 5mA
TEST(DCSolver, MultipleVoltageSources) {
    CircuitGraph circuit;
    circuit.set_title("Multiple Voltage Sources Test");

    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_voltage_source("V2", "n2", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);

    DCSolver solver(circuit);

    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    ASSERT_GE(solution.size(), 4u);
    EXPECT_NEAR(solution[0], 10.0, 1e-6);  // V(n1)
    EXPECT_NEAR(solution[1], 5.0, 1e-6);   // V(n2)

    // Branch currents for V1 and V2, positive into n+ through the source
    // (NETLIST_CONVENTIONS rule 3): 5mA flows out of V1 and into V2
    EXPECT_NEAR(solution[2], -0.005, 1e-9);  // I(V1)
    EXPECT_NEAR(solution[3], 0.005, 1e-9);   // I(V2)
}

// Test: .NODESET initial guess
// Test that .NODESET sets initial values for DC analysis
TEST(DCSolver, NODESETInitialGuess) {
    CircuitGraph circuit;
    circuit.set_title("NODESET Initial Guess Test");

    // Build simple voltage divider
    // V1 = 10V
    // R1 = 1k (n1 to n2)
    // R2 = 1k (n2 to GND)
    // Expected: V(n2) = 5V
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);
    circuit.add_resistor("R2", "n2", "0", 1000.0);

    // Create config with .NODESET
    SimulationConfig config;
    config.nodeset["n2"] = 4.5;  // Initial guess near final value

    // Create solver with config
    DCSolver solver(circuit, config);

    // Solve
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    // Check convergence
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Check solution (final values should be correct regardless of initial guess)
    ASSERT_GE(solution.size(), 3u);
    EXPECT_NEAR(solution[0], 10.0, 1e-6);  // V(n1) = 10V
    EXPECT_NEAR(solution[1], 5.0, 1e-6);   // V(n2) = 5V (not 4.5V)

    // V1 delivers 10V / 2kΩ = 5mA; its branch current is positive into n+
    // through the source (NETLIST_CONVENTIONS rule 3), so it is -5mA
    EXPECT_NEAR(solution[2], -0.005, 1e-9);
}

// Test: .NODESET on ground node should be ignored
TEST(DCSolver, NODESETGroundIgnored) {
    CircuitGraph circuit;
    circuit.set_title("NODESET Ground Ignored Test");

    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "0", 100.0);

    // Create config with .NODESET on ground (should be ignored)
    SimulationConfig config;
    config.nodeset["0"] = 1.0;  // Attempt to set ground to 1V (invalid)

    // Create solver with config
    DCSolver solver(circuit, config);

    // Solve (should succeed, ignoring invalid NODESET)
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // V(n1) = 5V (ground NODESET ignored)
    ASSERT_GE(solution.size(), 2u);
    EXPECT_NEAR(solution[0], 5.0, 1e-6);
}

// Test: .NODESET with multiple nodes
TEST(DCSolver, NODESETMultipleNodes) {
    CircuitGraph circuit;
    circuit.set_title("NODESET Multiple Nodes Test");

    // Three-resistor divider: 10V across 3kΩ total
    // V1 = 10V
    // R1 = 1k (n1 to n2)
    // R2 = 1k (n2 to n3)
    // R3 = 1k (n3 to GND)
    // Expected: V(n2) = 6.67V, V(n3) = 3.33V
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);
    circuit.add_resistor("R2", "n2", "n3", 1000.0);
    circuit.add_resistor("R3", "n3", "0", 1000.0);

    // Create config with .NODESET for multiple nodes
    SimulationConfig config;
    config.nodeset["n2"] = 7.0;  // Close to 6.67V
    config.nodeset["n3"] = 3.0;  // Close to 3.33V

    // Create solver with config
    DCSolver solver(circuit, config);

    // Solve
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    // Check convergence
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Check solution
    ASSERT_GE(solution.size(), 4u);
    EXPECT_NEAR(solution[0], 10.0, 1e-6);       // V(n1) = 10V
    EXPECT_NEAR(solution[1], 20.0/3.0, 1e-4);  // V(n2) = 6.67V
    EXPECT_NEAR(solution[2], 10.0/3.0, 1e-4);  // V(n3) = 3.33V
}
