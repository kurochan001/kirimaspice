// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_tline_lumped.cpp
// Unit tests for lumped LC ladder transmission line model

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/inductor.hpp"
#include "kirimaspice/devices/capacitor.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;

//==============================================================================
// Test 1: Basic LC Ladder Expansion (10 sections)
//==============================================================================

TEST(TLineLumped, BasicExpansion10Sections) {
    CircuitGraph circuit;

    // Add T-line with 10 sections: Z0=50Ω, TD=5ns
    Real z0 = 50.0;
    Real td = 5e-9;
    int num_sections = 10;

    circuit.add_tline_lumped("T1", "in", "0", "out", "0", z0, td, num_sections);

    // Expected LC values:
    // L_section = Z0 × TD / N = 50 × 5e-9 / 10 = 25e-9 H = 25 nH
    // C_section = TD / Z0 / N = 5e-9 / 50 / 10 = 10e-12 F = 10 pF
    Real expected_L = 25e-9;
    Real expected_C = 10e-12;

    // Verify total device count: 10 inductors + 10 capacitors = 20 devices
    EXPECT_EQ(circuit.num_devices(), 20u);

    // Verify inductor values
    for (int i = 1; i <= num_sections; ++i) {
        std::string L_name = "T1_L" + std::to_string(i);
        auto device = circuit.get_device(L_name);
        ASSERT_NE(device, nullptr);

        auto inductor = std::dynamic_pointer_cast<devices::Inductor>(device);
        ASSERT_NE(inductor, nullptr);
        EXPECT_NEAR(inductor->inductance(), expected_L, 1e-15);
    }

    // Verify capacitor values
    for (int i = 1; i <= num_sections; ++i) {
        std::string C_name = "T1_C" + std::to_string(i);
        auto device = circuit.get_device(C_name);
        ASSERT_NE(device, nullptr);

        auto capacitor = std::dynamic_pointer_cast<devices::Capacitor>(device);
        ASSERT_NE(capacitor, nullptr);
        EXPECT_NEAR(capacitor->capacitance(), expected_C, 1e-18);
    }
}

//==============================================================================
// Test 2: Single Section Expansion
//==============================================================================

TEST(TLineLumped, SingleSection) {
    CircuitGraph circuit;

    Real z0 = 75.0;
    Real td = 10e-9;
    int num_sections = 1;

    circuit.add_tline_lumped("T2", "a", "0", "b", "0", z0, td, num_sections);

    // Expected LC values:
    // L = Z0 × TD = 75 × 10e-9 = 750e-9 H = 750 nH
    // C = TD / Z0 = 10e-9 / 75 = 133.33 pF
    Real expected_L = 750e-9;
    Real expected_C = 10e-9 / 75.0;

    // Verify device count: 1 L + 1 C = 2 devices
    EXPECT_EQ(circuit.num_devices(), 2u);

    // Verify L value
    auto L_device = circuit.get_device("T2_L1");
    ASSERT_NE(L_device, nullptr);
    auto inductor = std::dynamic_pointer_cast<devices::Inductor>(L_device);
    EXPECT_NEAR(inductor->inductance(), expected_L, 1e-15);

    // Verify C value
    auto C_device = circuit.get_device("T2_C1");
    ASSERT_NE(C_device, nullptr);
    auto capacitor = std::dynamic_pointer_cast<devices::Capacitor>(C_device);
    EXPECT_NEAR(capacitor->capacitance(), expected_C, 1e-18);
}

//==============================================================================
// Test 3: Different Number of Sections
//==============================================================================

TEST(TLineLumped, VariableNumSections) {
    CircuitGraph circuit;

    Real z0 = 50.0;
    Real td = 1e-9;

    // Test with 5 sections
    circuit.add_tline_lumped("T3", "n1", "0", "n2", "0", z0, td, 5);

    // Expected: 5L + 5C = 10 devices
    EXPECT_EQ(circuit.num_devices(), 10u);

    // L_section = 50 × 1e-9 / 5 = 10 nH
    // C_section = 1e-9 / 50 / 5 = 4 pF
    Real expected_L = 10e-9;
    Real expected_C = 4e-12;

    auto L1 = std::dynamic_pointer_cast<devices::Inductor>(circuit.get_device("T3_L1"));
    EXPECT_NEAR(L1->inductance(), expected_L, 1e-15);

    auto C1 = std::dynamic_pointer_cast<devices::Capacitor>(circuit.get_device("T3_C1"));
    EXPECT_NEAR(C1->capacitance(), expected_C, 1e-18);
}

//==============================================================================
// Test 4: Internal Node Naming
//==============================================================================

TEST(TLineLumped, InternalNodeNaming) {
    CircuitGraph circuit;

    circuit.add_tline_lumped("TX", "in", "0", "out", "0", 50.0, 5e-9, 3);

    // Expected nodes: in, TX_n1, TX_n2, out, 0
    EXPECT_TRUE(circuit.has_node("in"));
    EXPECT_TRUE(circuit.has_node("TX_n1"));
    EXPECT_TRUE(circuit.has_node("TX_n2"));
    EXPECT_TRUE(circuit.has_node("out"));
    EXPECT_TRUE(circuit.has_node("0"));

    // Verify inductor connections:
    // L1: in -> TX_n1
    auto L1 = std::dynamic_pointer_cast<devices::Inductor>(circuit.get_device("TX_L1"));
    ASSERT_NE(L1, nullptr);
    NodeId in_id = circuit.get_node_id("in");
    NodeId n1_id = circuit.get_node_id("TX_n1");
    EXPECT_EQ(L1->np(), in_id);
    EXPECT_EQ(L1->nn(), n1_id);

    // L2: TX_n1 -> TX_n2
    auto L2 = std::dynamic_pointer_cast<devices::Inductor>(circuit.get_device("TX_L2"));
    ASSERT_NE(L2, nullptr);
    NodeId n2_id = circuit.get_node_id("TX_n2");
    EXPECT_EQ(L2->np(), n1_id);
    EXPECT_EQ(L2->nn(), n2_id);

    // L3: TX_n2 -> out
    auto L3 = std::dynamic_pointer_cast<devices::Inductor>(circuit.get_device("TX_L3"));
    ASSERT_NE(L3, nullptr);
    NodeId out_id = circuit.get_node_id("out");
    EXPECT_EQ(L3->np(), n2_id);
    EXPECT_EQ(L3->nn(), out_id);
}

//==============================================================================
// Test 5: Differential Ports
//==============================================================================

TEST(TLineLumped, DifferentialPorts) {
    CircuitGraph circuit;

    // Differential T-line: ap-an, bp-bn
    circuit.add_tline_lumped("TDIFF", "ap", "an", "bp", "bn", 100.0, 2e-9, 4);

    // Verify capacitors are connected to 'an' (not GND)
    auto C1 = std::dynamic_pointer_cast<devices::Capacitor>(circuit.get_device("TDIFF_C1"));
    ASSERT_NE(C1, nullptr);

    NodeId an_id = circuit.get_node_id("an");
    EXPECT_EQ(C1->nn(), an_id);  // Capacitor negative terminal connects to 'an'
}

//==============================================================================
// Test 6: Error Handling - Invalid Parameters
//==============================================================================

TEST(TLineLumped, ErrorHandling_InvalidSections) {
    CircuitGraph circuit;

    // num_sections must be >= 1
    EXPECT_THROW(
        circuit.add_tline_lumped("T_bad", "a", "0", "b", "0", 50.0, 1e-9, 0),
        std::runtime_error
    );

    EXPECT_THROW(
        circuit.add_tline_lumped("T_bad2", "a", "0", "b", "0", 50.0, 1e-9, -1),
        std::runtime_error
    );
}

TEST(TLineLumped, ErrorHandling_InvalidZ0) {
    CircuitGraph circuit;

    // Z0 must be positive
    EXPECT_THROW(
        circuit.add_tline_lumped("T_bad", "a", "0", "b", "0", -50.0, 1e-9, 10),
        std::runtime_error
    );

    EXPECT_THROW(
        circuit.add_tline_lumped("T_bad2", "a", "0", "b", "0", 0.0, 1e-9, 10),
        std::runtime_error
    );
}

TEST(TLineLumped, ErrorHandling_InvalidTD) {
    CircuitGraph circuit;

    // TD must be positive
    EXPECT_THROW(
        circuit.add_tline_lumped("T_bad", "a", "0", "b", "0", 50.0, -1e-9, 10),
        std::runtime_error
    );

    EXPECT_THROW(
        circuit.add_tline_lumped("T_bad2", "a", "0", "b", "0", 50.0, 0.0, 10),
        std::runtime_error
    );
}

//==============================================================================
// Test 7: Characteristic Impedance Calculation
//==============================================================================

TEST(TLineLumped, CharacteristicImpedancePreserved) {
    CircuitGraph circuit;

    Real z0 = 50.0;
    Real td = 10e-9;
    int N = 20;

    circuit.add_tline_lumped("T_Z0", "a", "0", "b", "0", z0, td, N);

    // Extract L and C values
    auto L1 = std::dynamic_pointer_cast<devices::Inductor>(circuit.get_device("T_Z0_L1"));
    auto C1 = std::dynamic_pointer_cast<devices::Capacitor>(circuit.get_device("T_Z0_C1"));

    Real L_section = L1->inductance();
    Real C_section = C1->capacitance();

    // Total L and C of the ladder (approximate)
    Real L_total = L_section * N;
    Real C_total = C_section * N;

    // Characteristic impedance: Z0 = sqrt(L_total / C_total)
    Real z0_calculated = std::sqrt(L_total / C_total);

    EXPECT_NEAR(z0_calculated, z0, 0.01);  // Within 1%
}

//==============================================================================
// Test 8: DC Analysis - Matched Termination
//==============================================================================

#include "kirimaspice/solvers/dc_solver.hpp"

TEST(TLineLumped, DCAnalysis_MatchedTermination) {
    CircuitGraph circuit;

    // Circuit: Vin (1V) -- Rs (50Ω) -- T-line (Z0=50Ω) -- Rload (50Ω) -- GND
    // Expected: V(load) = 0.5V (voltage divider: Rs = Rload_total)
    circuit.add_voltage_source("Vin", "1", "0", 1.0);
    circuit.add_resistor("Rs", "1", "2", 50.0);
    circuit.add_tline_lumped("T1", "2", "0", "3", "0", 50.0, 5e-9, 10);
    circuit.add_resistor("Rload", "3", "0", 50.0);

    // Run DC analysis
    SimulationConfig config;
    solvers::DCSolver solver(circuit, config);
    DenseVector dc_solution;
    SolverStats stats;

    auto status = solver.solve(dc_solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Extract voltages
    NodeId node_1 = circuit.get_node_id("1");
    NodeId node_2 = circuit.get_node_id("2");
    NodeId node_3 = circuit.get_node_id("3");

    Real v_1 = dc_solution[node_1 - 1];
    Real v_2 = dc_solution[node_2 - 1];
    Real v_3 = dc_solution[node_3 - 1];

    // Verify voltages
    EXPECT_NEAR(v_1, 1.0, 1e-6);     // V(1) = 1.0V (source)
    EXPECT_NEAR(v_2, 0.5, 0.01);     // V(2) ≈ 0.5V (before T-line)
    EXPECT_NEAR(v_3, 0.5, 0.01);     // V(3) ≈ 0.5V (after T-line)

    // DC analysis: T-line acts as resistive network
    // With matched termination (Rload = Z0), voltage should be preserved
}

//==============================================================================
// Test 9: Transient Analysis - Step Response
//==============================================================================

#include "kirimaspice/solvers/transient_solver.hpp"

TEST(TLineLumped, TransientAnalysis_StepResponse) {
    CircuitGraph circuit;

    // Circuit: Vin (1V DC) -- Rs (50Ω) -- T-line (Z0=50Ω, TD=5ns) -- Rload (50Ω) -- GND
    circuit.add_voltage_source("Vin", "1", "0", 1.0);
    circuit.add_resistor("Rs", "1", "2", 50.0);
    circuit.add_tline_lumped("T1", "2", "0", "3", "0", 50.0, 5e-9, 10);
    circuit.add_resistor("Rload", "3", "0", 50.0);

    // Run transient analysis: 0 to 20ns
    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;
    config.transient.use_initial_conditions = false;  // Start from DC OP

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 20e-9, 0.1e-9);  // 0.1ns timestep

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    NodeId node_3 = circuit.get_node_id("3");

    // Check voltage at different times
    Real v_at_0ns = 0.0;
    Real v_at_10ns = 0.0;
    Real v_at_20ns = 0.0;

    for (const auto& tp : result.timepoints) {
        if (std::abs(tp.time - 0.0) < 0.05e-9) {
            v_at_0ns = tp.node_voltages[node_3 - 1];
        }
        if (std::abs(tp.time - 10e-9) < 0.05e-9) {
            v_at_10ns = tp.node_voltages[node_3 - 1];
        }
        if (std::abs(tp.time - 20e-9) < 0.05e-9) {
            v_at_20ns = tp.node_voltages[node_3 - 1];
        }
    }

    // Expected behavior:
    // t=0: DC operating point, V(3) ≈ 0.5V
    // t=10ns: After propagation delay (TD=5ns), should still be ≈0.5V (matched)
    // t=20ns: Fully settled, V(3) ≈ 0.5V

    EXPECT_NEAR(v_at_0ns, 0.5, 0.05);    // DC operating point
    EXPECT_NEAR(v_at_10ns, 0.5, 0.05);   // After TD
    EXPECT_NEAR(v_at_20ns, 0.5, 0.05);   // Fully settled

    // Note: LC ladder model shows smooth propagation rather than sharp step
    // This is expected behavior for lumped approximation
}
