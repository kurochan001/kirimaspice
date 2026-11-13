// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_gmin_stepping.cpp
// Unit tests for Gmin Stepping algorithm

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

using namespace kirimaspice;

class GminSteppingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set log level to info to see Gmin Stepping progress
        spdlog::set_level(spdlog::level::info);
    }
};

// Test 1: Basic functionality - Same result with and without Gmin Stepping
TEST_F(GminSteppingTest, SimpleResistorCircuit_IdenticalResults) {
    // Simple voltage divider circuit that should converge easily
    std::string netlist = R"(
Simple Resistor Divider
Vcc 1 0 DC 12
R1 1 2 1k
R2 2 0 1k
.DC Vcc 12 12 1
.end
)";

    parser::NetlistParser parser;

    // Solve without Gmin stepping
    auto result1 = parser.parse_string(netlist);
    ASSERT_TRUE(result1.circuit) << "Failed to parse netlist";

    result1.config.dc.enable_gmin_stepping = false;
    solvers::DCSolver solver1(*result1.circuit, result1.config);

    DenseVector solution1;
    SolverStats stats1;
    auto status1 = solver1.solve(solution1, stats1);

    ASSERT_EQ(status1, ConvergenceStatus::SUCCESS) << "DC solve failed without Gmin stepping";

    // Solve with Gmin stepping
    auto result2 = parser.parse_string(netlist);
    ASSERT_TRUE(result2.circuit) << "Failed to parse netlist";

    result2.config.dc.enable_gmin_stepping = true;
    result2.config.dc.gmin_stepping_steps = 5;
    result2.config.dc.gmin_start = 1e-6;
    result2.config.dc.gmin_end = 1e-12;
    solvers::DCSolver solver2(*result2.circuit, result2.config);

    DenseVector solution2;
    SolverStats stats2;
    auto status2 = solver2.solve(solution2, stats2);

    ASSERT_EQ(status2, ConvergenceStatus::SUCCESS) << "DC solve failed with Gmin stepping";

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

// Test 2: High voltage circuit - Improved convergence
TEST_F(GminSteppingTest, HighVoltageCircuit_ImprovedConvergence) {
    // Circuit with high supply voltage
    std::string netlist = R"(
* High Voltage Resistor Divider

Vcc 1 0 DC 100
R1 1 2 10k
R2 2 0 10k
.DC Vcc 100 100 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    // Enable Gmin stepping with fine steps
    result.config.dc.enable_gmin_stepping = true;
    result.config.dc.gmin_stepping_steps = 10;
    result.config.dc.gmin_start = 1e-5;  // Start with larger Gmin
    result.config.dc.gmin_end = 1e-12;

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve with Gmin stepping failed for high voltage circuit";

    // Node 2 should be 50V (voltage divider: 100V * 10k/(10k+10k) = 50V)
    Real v_node2 = solution[1];
    EXPECT_NEAR(v_node2, 50.0, 0.5);
}

// Test 3: Multiple step counts - Verify algorithm works with different step numbers
TEST_F(GminSteppingTest, VariableStepCounts) {
    std::string netlist = R"(
* Simple Circuit

Vcc 1 0 DC 10
R1 1 0 1k
.DC Vcc 10 10 1
.end
)";

    parser::NetlistParser parser;

    // Test with 3 steps
    {
        auto result = parser.parse_string(netlist);
        result.config.dc.enable_gmin_stepping = true;
        result.config.dc.gmin_stepping_steps = 3;
        solvers::DCSolver solver(*result.circuit, result.config);
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);
        EXPECT_EQ(status, ConvergenceStatus::SUCCESS) << "3 steps failed";
    }

    // Test with 10 steps
    {
        auto result = parser.parse_string(netlist);
        result.config.dc.enable_gmin_stepping = true;
        result.config.dc.gmin_stepping_steps = 10;
        solvers::DCSolver solver(*result.circuit, result.config);
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);
        EXPECT_EQ(status, ConvergenceStatus::SUCCESS) << "10 steps failed";
    }

    // Test with 1 step (edge case)
    {
        auto result = parser.parse_string(netlist);
        result.config.dc.enable_gmin_stepping = true;
        result.config.dc.gmin_stepping_steps = 1;
        solvers::DCSolver solver(*result.circuit, result.config);
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);
        EXPECT_EQ(status, ConvergenceStatus::SUCCESS) << "1 step failed";
    }
}

// Test 4: Gmin reset after solve - Verify multiple solves give same result
TEST_F(GminSteppingTest, GminResetAfterSolve) {
    std::string netlist = R"(
* Simple Circuit

Vcc 1 0 DC 10
R1 1 0 1k
.DC Vcc 10 10 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    result.config.dc.enable_gmin_stepping = true;
    result.config.dc.gmin_stepping_steps = 3;
    result.config.dc.gmin_start = 1e-6;
    result.config.dc.gmin_end = 1e-12;

    solvers::DCSolver solver(*result.circuit, result.config);

    // First solve
    DenseVector solution1;
    SolverStats stats1;
    auto status1 = solver.solve(solution1, stats1);

    ASSERT_EQ(status1, ConvergenceStatus::SUCCESS);

    // Second solve (should give same result if Gmin is properly reset)
    DenseVector solution2;
    SolverStats stats2;
    auto status2 = solver.solve(solution2, stats2);

    ASSERT_EQ(status2, ConvergenceStatus::SUCCESS);

    // Solutions should match
    ASSERT_EQ(solution1.size(), solution2.size());
    for (std::size_t i = 0; i < solution1.size(); ++i) {
        EXPECT_NEAR(solution1[i], solution2[i], 1e-9)
            << "Gmin not properly reset between solves";
    }
}

// Test 5: BJT circuit - Verify Gmin Stepping works with nonlinear devices
TEST_F(GminSteppingTest, BJT_Circuit_Converges) {
    // BJT circuit with emitter resistor
    std::string netlist = R"(
* BJT Common Emitter

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

    // Enable Gmin stepping
    result.config.dc.enable_gmin_stepping = true;
    result.config.dc.gmin_stepping_steps = 10;
    result.config.dc.gmin_start = 1e-6;
    result.config.dc.gmin_end = 1e-12;

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve with Gmin stepping failed for BJT circuit";

    if (status == ConvergenceStatus::SUCCESS) {
        spdlog::info("Gmin Stepping succeeded in {} iterations", stats.total_iterations);

        // Verify solution is physically reasonable
        // All node voltages should be between 0 and Vcc (12V)
        for (std::size_t i = 0; i < result.circuit->num_nodes() - 1; ++i) {
            Real v = solution[i];
            EXPECT_GE(v, 0.0) << "Node voltage below GND at index " << i;
            EXPECT_LE(v, 12.0) << "Node voltage above Vcc at index " << i;
        }
    }
}

// Test 6: Source Stepping priority - When both are enabled, Source Stepping takes priority
TEST_F(GminSteppingTest, SourceSteppingPriority) {
    std::string netlist = R"(
Simple Circuit
Vcc 1 0 DC 10
R1 1 0 1k
.DC Vcc 10 10 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    // Enable both Source Stepping and Gmin Stepping
    result.config.dc.enable_source_stepping = true;
    result.config.dc.source_stepping_steps = 5;
    result.config.dc.source_stepping_start = 0.1;

    result.config.dc.enable_gmin_stepping = true;
    result.config.dc.gmin_stepping_steps = 3;
    result.config.dc.gmin_start = 1e-6;
    result.config.dc.gmin_end = 1e-12;

    solvers::DCSolver solver(*result.circuit, result.config);

    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    // Should succeed (Source Stepping is used, Gmin Stepping is skipped)
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve failed with both stepping methods enabled";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
