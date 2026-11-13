// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_voltage_limiting.cpp
// Unit tests for Voltage Limiting algorithm

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

using namespace kirimaspice;

class VoltageLimitingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set log level to info to see convergence progress
        spdlog::set_level(spdlog::level::info);
    }
};

// Test 1: Device utils - Vcrit calculation
TEST_F(VoltageLimitingTest, VcritCalculation) {
    // Typical diode: IS=1e-14A, VT=26mV at 300K
    Real vt = 0.02585;  // 300K
    Real is = 1e-14;

    Real vcrit = core::compute_vcrit(vt, is);

    // Vcrit = VT * ln(VT / (√2 * IS))
    // Vcrit = 0.02585 * ln(0.02585 / (1.414 * 1e-14))
    // Vcrit ≈ 0.730V
    EXPECT_NEAR(vcrit, 0.730, 0.01);
}

// Test 2: Device utils - Voltage limiting function (below Vcrit)
TEST_F(VoltageLimitingTest, VoltageLimitingBelowVcrit) {
    Real vt = 0.02585;
    Real is = 1e-14;
    Real vcrit = core::compute_vcrit(vt, is);

    // Voltage below Vcrit should pass through unchanged
    Real v_test = 0.5;  // Below Vcrit (~0.717V)
    Real v_limited = core::limit_pn_junction(v_test, vt, is, true);
    Real derivative = core::limit_pn_junction_derivative(v_test, vt, is, true);

    EXPECT_NEAR(v_limited, v_test, 1e-10);  // No limiting
    EXPECT_NEAR(derivative, 1.0, 1e-10);    // Derivative = 1.0
}

// Test 3: Device utils - Voltage limiting function (above Vcrit)
TEST_F(VoltageLimitingTest, VoltageLimitingAboveVcrit) {
    Real vt = 0.02585;
    Real is = 1e-14;
    Real vcrit = core::compute_vcrit(vt, is);

    // Voltage above Vcrit should be compressed
    Real v_test = 2.0;  // Well above Vcrit (~0.717V)
    Real v_limited = core::limit_pn_junction(v_test, vt, is, true);
    Real derivative = core::limit_pn_junction_derivative(v_test, vt, is, true);

    EXPECT_LT(v_limited, v_test);           // Voltage should be compressed
    EXPECT_GT(v_limited, vcrit);            // But still above Vcrit
    EXPECT_LT(derivative, 1.0);             // Derivative < 1.0 (compression)
    EXPECT_GT(derivative, 0.0);             // But positive

    // Manual calculation: v_limited = vcrit + vt * ln(1 + (v - vcrit)/vt)
    Real expected = vcrit + vt * std::log(1.0 + (v_test - vcrit) / vt);
    EXPECT_NEAR(v_limited, expected, 1e-10);
}

// Test 4: Device utils - Voltage limiting disabled
TEST_F(VoltageLimitingTest, VoltageLimitingDisabled) {
    Real vt = 0.02585;
    Real is = 1e-14;

    Real v_test = 2.0;
    Real v_limited = core::limit_pn_junction(v_test, vt, is, false);  // Disabled
    Real derivative = core::limit_pn_junction_derivative(v_test, vt, is, false);

    EXPECT_NEAR(v_limited, v_test, 1e-10);  // No limiting when disabled
    EXPECT_NEAR(derivative, 1.0, 1e-10);    // Derivative = 1.0
}

// Test 5: Diode circuit - Voltage limiting enabled (default behavior)
TEST_F(VoltageLimitingTest, DiodeCircuit_ConvergesSuccessfully) {
    // Simple diode circuit that converges with voltage limiting
    std::string netlist = R"(
Simple Diode Circuit
Vcc 1 0 DC 5
R1 1 2 1k
D1 2 0 DIODE
.MODEL DIODE D (IS=1e-14 N=1.0)
.DC Vcc 5 5 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS) << "DC solve failed with voltage limiting";

    // Node 2 should be close to diode forward voltage (~0.7V)
    Real v_diode = solution[1];  // Node 2 is at index 1
    EXPECT_NEAR(v_diode, 0.7, 0.2);  // Diode forward voltage

    spdlog::info("Diode circuit converged in {} iterations, V_D={:.6f}V",
                 stats.total_iterations, v_diode);
}

// Test 6: BJT circuit - Voltage limiting improves convergence
TEST_F(VoltageLimitingTest, BJTCircuit_ImprovedConvergence) {
    // BJT circuit with high base resistor (can be difficult to converge)
    std::string netlist = R"(
* BJT Common Emitter Circuit

* BJT model (2N3904)
.MODEL 2N3904 NPN (IS=1e-14 BF=200 NF=1.0 VAF=100 IKF=0.3
+                  ISE=3e-13 NE=2.0 BR=3.0 NR=1.0 VAR=100 IKR=0.3
+                  ISC=0 NC=2.0 RB=1 RE=0.1 RC=1)

Vcc vcc 0 DC 12
Vin in 0 DC 2
Rin in base 100k
Rbe base 0 10k
Q1 collector base emitter 2N3904
Rc vcc collector 4.7k
Re emitter 0 1k
.DC Vin 0 5 0.5
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve with voltage limiting failed for BJT circuit";

    if (status == ConvergenceStatus::SUCCESS) {
        spdlog::info("BJT circuit converged in {} iterations", stats.total_iterations);

        // All node voltages should be between 0 and Vcc
        for (std::size_t i = 0; i < result.circuit->num_nodes() - 1; ++i) {
            Real v = solution[i];
            EXPECT_GE(v, 0.0) << "Node voltage below GND at index " << i;
            EXPECT_LE(v, 12.0) << "Node voltage above Vcc at index " << i;
        }
    }
}

// Test 7: Diode circuit - Voltage limiting with high forward voltage
TEST_F(VoltageLimitingTest, DiodeCircuit_HighForwardVoltage) {
    // Circuit with very high supply voltage
    std::string netlist = R"(
* High Voltage Diode Test
Vcc 1 0 DC 100
R1 1 2 10k
D1 2 0 DIODE
.MODEL DIODE D (IS=1e-14 N=1.0)
.DC Vcc 100 100 1
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit) << "Failed to parse netlist";

    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC solve failed for high voltage diode circuit";

    if (status == ConvergenceStatus::SUCCESS) {
        spdlog::info("High voltage circuit converged in {} iterations", stats.total_iterations);

        // Node 2 should be close to diode forward voltage (~0.7V)
        Real v_diode = solution[1];  // Node 2 is at index 1
        EXPECT_NEAR(v_diode, 0.7, 0.2);  // Diode forward voltage
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
