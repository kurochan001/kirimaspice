// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_source_stepping.cpp
// Unit tests for Source Stepping algorithm

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

using namespace kirimaspice;

class SourceSteppingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set log level to info to see Source Stepping progress
        spdlog::set_level(spdlog::level::info);
    }
};

// Test 1: Basic functionality - Same result with and without Source Stepping
TEST_F(SourceSteppingTest, SimpleResistorCircuit_IdenticalResults) {
    // Simple voltage divider circuit that should converge easily
    std::string netlist = R"(Simple Resistor Divider
Vcc 1 0 DC 12
R1 1 2 1k
R2 2 0 1k
.DC Vcc 12 12 1
.end
)";

    parser::NetlistParser parser;

    // Solve without source stepping
    auto result1 = parser.parse_string(netlist);
    ASSERT_TRUE(result1.circuit) << "Failed to parse netlist";

    result1.config.dc.enable_source_stepping = false;
    solvers::DCSolver solver1(*result1.circuit, result1.config);

    DenseVector solution1;
    SolverStats stats1;
    auto status1 = solver1.solve(solution1, stats1);

    ASSERT_EQ(status1, ConvergenceStatus::SUCCESS) << "DC solve failed without source stepping";

    // Solve with source stepping
    auto result2 = parser.parse_string(netlist);
    ASSERT_TRUE(result2.circuit) << "Failed to parse netlist";

    result2.config.dc.enable_source_stepping = true;
    result2.config.dc.source_stepping_steps = 5;
    result2.config.dc.source_stepping_start = 0.1;
    solvers::DCSolver solver2(*result2.circuit, result2.config);

    DenseVector solution2;
    SolverStats stats2;
    auto status2 = solver2.solve(solution2, stats2);

    ASSERT_EQ(status2, ConvergenceStatus::SUCCESS) << "DC solve failed with source stepping";

    // Compare solutions (should be identical within tolerance)
    ASSERT_EQ(solution1.size(), solution2.size());
    for (std::size_t i = 0; i < solution1.size(); ++i) {
        EXPECT_NEAR(solution1[i], solution2[i], 1e-6)
            << "Solution mismatch at index " << i;
    }

    // Node 2 should be 6V (voltage divider: 12V * 1k/(1k+1k) = 6V)
    Real v_node2_without = solution1[1];  // Node 2 is at index 1 (node 1 is at index 0)
    Real v_node2_with = solution2[1];
    EXPECT_NEAR(v_node2_without, 6.0, 0.1);
    EXPECT_NEAR(v_node2_with, 6.0, 0.1);
}

// Test 2: Emitter resistor circuit - Previously problematic
TEST_F(SourceSteppingTest, BJT_EmitterResistor_Converges) {
    // BJT circuit with emitter resistor (previously caused matrix singularity)
    std::string netlist = R"(* BJT Common Emitter with Emitter Resistor

* BJT model (simplified 2N3904)
.MODEL 2N3904 NPN (IS=1e-14 BF=200 NF=1.0 VAF=100 IKF=0.3
+                  ISE=3e-13 NE=2.0 BR=3.0 NR=1.0 VAR=100 IKR=0.3
+                  ISC=0 NC=2.0 RB=1 RE=0.1 RC=1)

Vcc vcc 0 DC 12
Vin in 0 DC 5
Rin in base 10k
Rbe base 0 100k
Q1 collector base emitter 2N3904
Rc vcc collector 4.7k
Re emitter 0 1k
.DC Vin 0 10 0.5
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    // Enable source stepping
    result.config.dc.enable_source_stepping = true;
    result.config.dc.source_stepping_steps = 10;
    result.config.dc.source_stepping_start = 0.01;  // Start at 1%

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve with source stepping failed for emitter resistor circuit";

    if (status == ConvergenceStatus::SUCCESS) {
        spdlog::info("Source Stepping succeeded in {} iterations", stats.total_iterations);

        // Verify solution is physically reasonable
        // All node voltages should be between 0 and Vcc (12V)
        for (std::size_t i = 0; i < result.circuit->num_nodes() - 1; ++i) {
            Real v = solution[i];
            EXPECT_GE(v, 0.0) << "Node voltage below GND at index " << i;
            EXPECT_LE(v, 12.0) << "Node voltage above Vcc at index " << i;
        }
    }
}

// Test 3: High voltage circuit - Difficult convergence
TEST_F(SourceSteppingTest, HighVoltageCircuit_ImprovedConvergence) {
    // Circuit with high supply voltage that may be difficult to converge
    std::string netlist = R"(* High Voltage Resistor Divider

Vcc 1 0 DC 100
R1 1 2 10k
R2 2 0 10k
.DC Vcc 100 100 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    // Enable source stepping with fine steps
    result.config.dc.enable_source_stepping = true;
    result.config.dc.source_stepping_steps = 20;
    result.config.dc.source_stepping_start = 0.05;  // Start at 5%

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve with source stepping failed for high voltage circuit";

    // Node 2 should be 50V (voltage divider: 100V * 10k/(10k+10k) = 50V)
    Real v_node2 = solution[1];
    EXPECT_NEAR(v_node2, 50.0, 0.5);
}

// Test 4: Multiple voltage sources
TEST_F(SourceSteppingTest, MultipleVoltageSources_AllScaled) {
    // Circuit with multiple voltage sources - all should be scaled
    std::string netlist = R"(* Multiple Voltage Sources

V1 1 0 DC 12
V2 2 0 DC 5
R1 1 3 1k
R2 2 3 1k
R3 3 0 1k
.DC V1 12 12 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    // Enable source stepping
    result.config.dc.enable_source_stepping = true;
    result.config.dc.source_stepping_steps = 5;
    result.config.dc.source_stepping_start = 0.2;  // Start at 20%

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve with source stepping failed for multiple voltage sources";

    // Verify node 3 voltage is reasonable
    // Using superposition: V3 = (12V * R2||R3)/(R1 + R2||R3) + (5V * R1||R3)/(R2 + R1||R3)
    // R2||R3 = R1||R3 = 1k||1k = 500Ω
    // V3 = (12 * 500)/(1000 + 500) + (5 * 500)/(1000 + 500)
    //    = 12 * 1/3 + 5 * 1/3 = 4 + 1.67 = 5.67V
    Real v_node3 = solution[2];  // Node 3 is at index 2
    EXPECT_NEAR(v_node3, 5.67, 0.1);
}

// Test 5: Current source scaling
TEST_F(SourceSteppingTest, CurrentSource_Scaled) {
    // Circuit with current source
    std::string netlist = R"(* Current Source Circuit

I1 0 1 DC 1m
R1 1 0 1k
.DC I1 1m 1m 0.1m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    // Enable source stepping
    result.config.dc.enable_source_stepping = true;
    result.config.dc.source_stepping_steps = 5;
    result.config.dc.source_stepping_start = 0.1;

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve with source stepping failed for current source";

    // Node 1 should be 1V (V = I * R = 1mA * 1kΩ = 1V)
    Real v_node1 = solution[0];
    EXPECT_NEAR(v_node1, 1.0, 0.01);
}

// Test 6: Verify scale factor reset after solve
TEST_F(SourceSteppingTest, ScaleFactorResetAfterSolve) {
    std::string netlist = R"(* Simple Circuit

Vcc 1 0 DC 10
R1 1 0 1k
.DC Vcc 10 10 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    result.config.dc.enable_source_stepping = true;
    result.config.dc.source_stepping_steps = 3;
    result.config.dc.source_stepping_start = 0.5;

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // After solve, scale factors should be reset to 1.0
    // This is implicitly tested by checking if a second solve gives the same result
    DenseVector solution2;
    SolverStats stats2;
    auto status2 = solver.solve(solution2, stats2);

    ASSERT_EQ(status2, ConvergenceStatus::SUCCESS);

    // Solutions should match
    ASSERT_EQ(solution.size(), solution2.size());
    for (std::size_t i = 0; i < solution.size(); ++i) {
        EXPECT_NEAR(solution[i], solution2[i], 1e-9)
            << "Scale factor not properly reset";
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
