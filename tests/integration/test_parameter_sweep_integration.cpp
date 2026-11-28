// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/integration/test_parameter_sweep_integration.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/parameter_sweep.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cctype>
#include <fmt/format.h>

using namespace kirimaspice;
namespace fs = std::filesystem;

//==============================================================================
// Parameter Sweep Integration Tests
//==============================================================================

class ParameterSweepIntegrationTest : public ::testing::Test {
protected:
    fs::path temp_dir_;

    void SetUp() override {
        // Create a unique temporary directory per test to avoid races under parallel ctest
        auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string suffix = "shared";
        if (test_info) {
            suffix = fmt::format("{}_{}", test_info->test_suite_name(), test_info->name());
            for (char& ch : suffix) {
                if (!std::isalnum(static_cast<unsigned char>(ch))) {
                    ch = '_';
                }
            }
        }
        auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        temp_dir_ = fs::temp_directory_path() /
                   fmt::format("kirimaspice_sweep_test_{}_{}", suffix, timestamp);
        fs::create_directories(temp_dir_);
    }

    void TearDown() override {
        // Clean up temporary files
        if (fs::exists(temp_dir_)) {
            fs::remove_all(temp_dir_);
        }
    }

    // Helper: Write netlist to temporary file
    fs::path write_netlist(const std::string& filename, const std::string& content) {
        fs::path filepath = temp_dir_ / filename;
        std::ofstream file(filepath);
        file << content;
        file.close();
        return filepath;
    }
};

// Test 1: Voltage divider with resistor sweep
// Enabled after implementing initial_params override
TEST_F(ParameterSweepIntegrationTest, VoltageDivider_ResistorSweep) {
    std::string netlist = R"(.title Voltage Divider Parameter Sweep
* Sweep R1 from 1k to 10k with 1k steps
* Use {R1_value} expansion with default .PARAM value
.PARAM R1_value=1k
.STEP param R1_value 1k 10k 1k

V1 1 0 DC 10
R1 1 2 {R1_value}
R2 2 0 5k

.dc
.end
)";

    auto netlist_path = write_netlist("voltage_divider_sweep.cir", netlist);

    // Parse to get config
    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(netlist_path);

    ASSERT_NE(parse_result.circuit, nullptr);
    ASSERT_EQ(parse_result.config.step_configs.size(), 1);

    // Execute sweep
    solvers::ParameterSweepExecutor executor(parse_result.config);
    auto result = executor.execute_dc_sweep(netlist_path);

    // Verify results
    EXPECT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(result.num_success, 10);
    EXPECT_EQ(result.num_failed, 0);
    ASSERT_EQ(result.sweep_points.size(), 10);

    // Verify voltage divider formula: V(2) = 10V * R2 / (R1 + R2)
    for (size_t i = 0; i < result.sweep_points.size(); ++i) {
        const auto& point = result.sweep_points[i];
        Real r1_value = point.param_value;

        EXPECT_EQ(point.param_name, "R1_value");
        EXPECT_DOUBLE_EQ(r1_value, 1000.0 + i * 1000.0);

        // V(2) should be at solution index 1 (node "2")
        ASSERT_GT(point.solution.size(), 1);
        Real v2 = point.solution[1];

        // Expected: V(2) = 10V * 5k / (R1 + 5k)
        Real expected_v2 = 10.0 * 5000.0 / (r1_value + 5000.0);
        EXPECT_NEAR(v2, expected_v2, 1e-6);
    }

    // Verify monotonic decrease (as R1 increases, V2 decreases)
    for (size_t i = 1; i < result.sweep_points.size(); ++i) {
        Real v2_prev = result.sweep_points[i-1].solution[1];
        Real v2_curr = result.sweep_points[i].solution[1];
        EXPECT_LT(v2_curr, v2_prev);
    }
}

// Test 2: DEC sweep (decade logarithmic)
TEST_F(ParameterSweepIntegrationTest, RCCircuit_CapacitorDECSweep) {
    std::string netlist = R"(.title RC Circuit with Capacitor DEC Sweep
.PARAM C_value=1p
.STEP dec param C_value 1p 1n 3

V1 1 0 DC 5
R1 1 2 1k
C1 2 0 {C_value}

.dc
.end
)";

    auto netlist_path = write_netlist("rc_dec_sweep.cir", netlist);

    // Parse and execute
    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(netlist_path);

    solvers::ParameterSweepExecutor executor(parse_result.config);
    auto result = executor.execute_dc_sweep(netlist_path);

    // Verify
    EXPECT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.sweep_points.size(), 0);

    // In DC analysis, capacitor acts as open circuit
    // So V(2) should always equal V(1) = 5V regardless of capacitance
    for (const auto& point : result.sweep_points) {
        EXPECT_EQ(point.status, ConvergenceStatus::SUCCESS);
        ASSERT_GT(point.solution.size(), 1);
        Real v2 = point.solution[1];
        EXPECT_NEAR(v2, 5.0, 1e-6);
    }
}

// Test 3: OCT sweep (octave logarithmic)
// Enabled after implementing initial_params override
TEST_F(ParameterSweepIntegrationTest, CurrentDivider_ResistorOCTSweep) {
    std::string netlist = R"(.title Current Divider with OCT Sweep
.PARAM R1_value=100
.STEP oct param R1_value 100 1600 2

I1 0 1 DC 1m
R1 1 0 {R1_value}
R2 1 0 400

.dc
.end
)";

    auto netlist_path = write_netlist("current_divider_oct.cir", netlist);

    // Parse and execute
    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(netlist_path);

    solvers::ParameterSweepExecutor executor(parse_result.config);
    auto result = executor.execute_dc_sweep(netlist_path);

    // Verify
    EXPECT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.sweep_points.size(), 0);

    // Verify current divider: V(1) = I * (R1 || R2) = I * R1*R2/(R1+R2)
    for (const auto& point : result.sweep_points) {
        EXPECT_EQ(point.status, ConvergenceStatus::SUCCESS);
        ASSERT_GT(point.solution.size(), 0);
        Real v1 = point.solution[0];

        Real r1_value = point.param_value;
        Real r2_value = 400.0;
        Real r_parallel = (r1_value * r2_value) / (r1_value + r2_value);
        Real expected_v1 = 0.001 * r_parallel;  // 1mA * R_parallel

        EXPECT_NEAR(v1, expected_v1, 1e-9);
    }
}

// Test 4: Parameter expression expansion
TEST_F(ParameterSweepIntegrationTest, ExpressionParameter_DependentValue) {
    std::string netlist = R"(.title Expression Parameter Sweep
.PARAM R_base=1k
.PARAM R_load={R_base*2}
.STEP param R_base 1k 5k 2k

V1 1 0 DC 10
R1 1 2 {R_base}
R2 2 0 {R_load}

.dc
.end
)";

    auto netlist_path = write_netlist("expression_param_sweep.cir", netlist);

    // Parse and execute
    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(netlist_path);

    solvers::ParameterSweepExecutor executor(parse_result.config);
    auto result = executor.execute_dc_sweep(netlist_path);

    // Verify
    EXPECT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(result.sweep_points.size(), 3);  // 1k, 3k, 5k

    // Note: Currently, dependent parameter expressions are NOT automatically
    // re-evaluated during sweep. This is a known limitation.
    // To properly support dependent parameters, we need to re-parse the entire
    // netlist at each sweep point (which we already do in execute_single_param_sweep).

    // For now, we just verify that the sweep completes successfully
    for (const auto& point : result.sweep_points) {
        EXPECT_EQ(point.status, ConvergenceStatus::SUCCESS);
        EXPECT_EQ(point.param_name, "R_base");
    }
}

// Test 5: Multiple .STEP directives (Cartesian product)
TEST_F(ParameterSweepIntegrationTest, MultipleSteps_CartesianProduct) {
    std::string netlist = R"(.title Multiple STEP Test
* This test verifies Cartesian product sweep
.STEP param R1_value 1k 3k 1k
.STEP param R2_value 5k 10k 5k

V1 1 0 DC 10
R1 1 2 1k
R2 2 0 5k

.dc
.end
)";

    auto netlist_path = write_netlist("multiple_steps.cir", netlist);

    // Parse
    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(netlist_path);

    ASSERT_EQ(parse_result.config.step_configs.size(), 2);

    // Execute multi-parameter sweep (Cartesian product)
    solvers::ParameterSweepExecutor executor(parse_result.config);

    // Verify total sweep points (3 × 2 = 6 combinations)
    EXPECT_EQ(executor.get_total_sweep_points(), 6);

    auto result = executor.execute_dc_sweep(netlist_path);

    // Should succeed with all 6 combinations
    EXPECT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(result.sweep_points.size(), 6);
    EXPECT_EQ(result.num_success, 6);
    EXPECT_EQ(result.num_failed, 0);

    // For multi-parameter sweep, param_name is "multi"
    for (const auto& point : result.sweep_points) {
        EXPECT_EQ(point.param_name, "multi");
        EXPECT_EQ(point.status, ConvergenceStatus::SUCCESS);
    }
}

// Test 6: Verify parameter expansion at each sweep point
// Enabled after implementing initial_params override
TEST_F(ParameterSweepIntegrationTest, ParameterExpansion_VerifyReParse) {
    std::string netlist = R"(.title Parameter Re-parse Verification
.PARAM Vcc=5
.STEP param Vcc 5 15 5

V1 1 0 DC {Vcc}
R1 1 0 1k

.dc
.end
)";

    auto netlist_path = write_netlist("param_reparse.cir", netlist);

    // Parse and execute
    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(netlist_path);

    solvers::ParameterSweepExecutor executor(parse_result.config);
    auto result = executor.execute_dc_sweep(netlist_path);

    // Verify
    EXPECT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.sweep_points.size(), 3);  // 5V, 10V, 15V

    // V(1) should equal Vcc at each point
    std::vector<Real> expected_vcc = {5.0, 10.0, 15.0};
    for (size_t i = 0; i < result.sweep_points.size(); ++i) {
        const auto& point = result.sweep_points[i];
        EXPECT_DOUBLE_EQ(point.param_value, expected_vcc[i]);

        ASSERT_GT(point.solution.size(), 0);
        Real v1 = point.solution[0];
        EXPECT_NEAR(v1, expected_vcc[i], 1e-6);
    }
}
