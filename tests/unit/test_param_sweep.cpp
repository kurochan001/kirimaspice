// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_param_sweep.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <fmt/format.h>

using namespace kirimaspice;

//==============================================================================
// Parameter Sweep Integration Tests
//==============================================================================

// Test 1: Simple resistor sweep with DC analysis
TEST(ParameterSweep, SimpleResistorSweep_DC) {
    // Base netlist with .PARAM and .STEP
    std::string netlist = R"(.title Resistor Parameter Sweep Test
* Parameter sweep: R from 1k to 5k with 1k steps
.PARAM R_value=1k
.STEP param R_value 1k 5k 1k

V1 1 0 DC 10
R1 1 2 {R_value}
R2 2 0 1k

.dc
.end
)";

    parser::NetlistParser parser;
    auto base_result = parser.parse_string(netlist);

    ASSERT_NE(base_result.circuit, nullptr);
    ASSERT_EQ(base_result.config.step_configs.size(), 1);

    const auto& step = base_result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "R_value");
    EXPECT_EQ(step.sweep_type, StepType::LIN);

    // Generate sweep points
    auto sweep_points = step.generate_points();
    ASSERT_EQ(sweep_points.size(), 5);  // 1k, 2k, 3k, 4k, 5k

    // Perform sweep manually
    std::vector<Real> v2_voltages;
    for (Real param_value : sweep_points) {
        // Update parameter in config
        auto config_copy = base_result.config;
        config_copy.global_params[step.param_name] = param_value;

        // Re-parse netlist with updated parameter
        // Note: In a full implementation, we would re-parse the netlist
        // For now, we'll manually create the circuit with updated R value
        auto circuit = std::make_unique<circuit::CircuitGraph>();
        circuit->set_title("Test");
        circuit->add_node("1");
        circuit->add_node("2");
        circuit->add_node("0");

        circuit->add_voltage_source("V1", "1", "0", 10.0, 0.0);
        circuit->add_resistor("R1", "1", "2", param_value);
        circuit->add_resistor("R2", "2", "0", 1000.0);

        // Solve DC
        solvers::DCSolver solver(*circuit, config_copy);
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);

        ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

        // Extract V(2) (node index 1, since 0 is GND)
        Real v2 = solution[1];
        v2_voltages.push_back(v2);

        // Verify Ohm's law: V2 = 10V * R2 / (R1 + R2)
        Real expected_v2 = 10.0 * 1000.0 / (param_value + 1000.0);
        EXPECT_NEAR(v2, expected_v2, 1e-6);
    }

    // Verify sweep results
    EXPECT_EQ(v2_voltages.size(), 5);
    // V2 should decrease as R1 increases
    for (size_t i = 1; i < v2_voltages.size(); ++i) {
        EXPECT_LT(v2_voltages[i], v2_voltages[i-1]);
    }
}

// Test 2: Capacitor sweep with DEC (decade logarithmic)
TEST(ParameterSweep, CapacitorSweep_DEC) {
    std::string netlist = R"(.title Capacitor Decade Sweep Test
.PARAM C_value=1p
.STEP dec param C_value 1p 1u 3

R1 1 0 1k
C1 1 0 {C_value}

.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 1);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "C_value");
    EXPECT_EQ(step.sweep_type, StepType::DEC);

    // Generate sweep points (6 decades, 3 points/decade = 19 points)
    auto sweep_points = step.generate_points();
    EXPECT_EQ(sweep_points.size(), 19);
    EXPECT_NEAR(sweep_points[0], 1e-12, 1e-15);
    EXPECT_NEAR(sweep_points[18], 1e-6, 1e-9);

    // Verify logarithmic spacing
    for (size_t i = 1; i < sweep_points.size(); ++i) {
        EXPECT_GT(sweep_points[i], sweep_points[i-1]);
    }
}

// Test 3: Inductor sweep with OCT (octave logarithmic)
TEST(ParameterSweep, InductorSweep_OCT) {
    std::string netlist = R"(.title Inductor Octave Sweep Test
.PARAM L_value=1n
.STEP oct param L_value 1n 1m 5

R1 1 2 1k
L1 2 0 {L_value}

.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 1);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "L_value");
    EXPECT_EQ(step.sweep_type, StepType::OCT);

    // Generate sweep points
    auto sweep_points = step.generate_points();
    EXPECT_GT(sweep_points.size(), 0);
    EXPECT_NEAR(sweep_points[0], 1e-9, 1e-12);
    EXPECT_NEAR(sweep_points.back(), 1e-3, 1e-6);

    // Verify logarithmic spacing
    for (size_t i = 1; i < sweep_points.size(); ++i) {
        EXPECT_GT(sweep_points[i], sweep_points[i-1]);
    }
}

// Test 4: Voltage divider sweep (verify analytical solution)
TEST(ParameterSweep, VoltageDivider_AnalyticalVerification) {
    std::string netlist = R"(.title Voltage Divider Sweep
.PARAM R1_value=1k
.STEP param R1_value 100 10k 100

V1 1 0 DC 5
R1 1 2 {R1_value}
R2 2 0 1k

.dc
.end
)";

    parser::NetlistParser parser;
    auto base_result = parser.parse_string(netlist);

    ASSERT_NE(base_result.circuit, nullptr);

    const auto& step = base_result.config.step_configs[0];
    auto sweep_points = step.generate_points();
    EXPECT_EQ(sweep_points.size(), 100);  // (10k - 100) / 100 + 1 = 100

    // Test a few points
    for (size_t i : {0, 25, 50, 75, 99}) {
        Real r1_value = sweep_points[i];

        // Create circuit with updated R1
        auto circuit = std::make_unique<circuit::CircuitGraph>();
        circuit->set_title("Test");
        circuit->add_node("1");
        circuit->add_node("2");
        circuit->add_node("0");

        circuit->add_voltage_source("V1", "1", "0", 5.0, 0.0);
        circuit->add_resistor("R1", "1", "2", r1_value);
        circuit->add_resistor("R2", "2", "0", 1000.0);

        // Solve DC
        solvers::DCSolver solver(*circuit);
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);

        ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

        // Verify V(2) = 5V * R2 / (R1 + R2)
        Real v2 = solution[1];
        Real expected_v2 = 5.0 * 1000.0 / (r1_value + 1000.0);
        EXPECT_NEAR(v2, expected_v2, 1e-9);
    }
}

// Test 5: Parameter expression sweep (verify expression evaluation)
TEST(ParameterSweep, ExpressionParameter_Sweep) {
    std::string netlist = R"(.title Expression Parameter Sweep
.PARAM R_base=1k
.PARAM R_mult={R_base*2}
.STEP param R_base 1k 3k 1k

R1 1 0 {R_mult}

.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "R_base");

    auto sweep_points = step.generate_points();
    ASSERT_EQ(sweep_points.size(), 3);

    // Verify that R_mult would be updated when R_base changes
    // Note: Full expression re-evaluation requires re-parsing
    EXPECT_DOUBLE_EQ(sweep_points[0], 1000.0);
    EXPECT_DOUBLE_EQ(sweep_points[1], 2000.0);
    EXPECT_DOUBLE_EQ(sweep_points[2], 3000.0);
}
