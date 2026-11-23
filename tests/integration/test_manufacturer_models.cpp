// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/integration/test_manufacturer_models.cpp
// File inclusion and circuit operating points with project-owned model cards.

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <filesystem>
#include <fstream>
#include "source_path.hpp"

using namespace kirimaspice;

//==============================================================================
// Test 1: 2N3904 Emitter Follower Circuit
//==============================================================================
TEST(ManufacturerModels, 2N3904_EmitterFollower) {
    // Parse netlist with .INCLUDE directive
    parser::NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/integration/test_2n3904_emitter_follower.cir").string());

    ASSERT_NE(result.circuit, nullptr);

    // Check that model was loaded
    EXPECT_TRUE(result.config.models.find("2N3904") != result.config.models.end());

    const auto& model = result.config.models.at("2N3904");
    EXPECT_EQ(model.device_type, DeviceType::BJT_NPN);
    EXPECT_DOUBLE_EQ(model.params.at("bf"), 200.0);

    // Check circuit structure
    EXPECT_GT(result.circuit->num_devices(), 0);

    // Run DC analysis
    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Check that emitter voltage follows base voltage (approximately)
    // Base voltage should be around 2.5V (input DC level)
    // Emitter voltage should be about 0.7V lower (Vbe drop)
    NodeId base_id = result.circuit->get_node_id("base");
    NodeId emitter_id = result.circuit->get_node_id("emitter");

    Real V_base = solution[base_id - 1];
    Real V_emitter = solution[emitter_id - 1];

    EXPECT_GT(V_base, 1.0);  // Base voltage should be positive
    EXPECT_GT(V_emitter, 0.5);  // Emitter voltage should be positive
    EXPECT_LT(V_base - V_emitter, 1.0);  // Vbe should be < 1V
    EXPECT_GT(V_base - V_emitter, 0.5);  // Vbe should be > 0.5V (typical ~0.7V)
}

//==============================================================================
// Test 2: 1N4148 Rectifier Circuit
//==============================================================================
TEST(ManufacturerModels, 1N4148_Rectifier) {
    // Parse netlist with .INCLUDE directive
    parser::NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/integration/test_1n4148_rectifier.cir").string());

    ASSERT_NE(result.circuit, nullptr);

    // Check that model was loaded
    EXPECT_TRUE(result.config.models.find("1N4148") != result.config.models.end());

    const auto& model = result.config.models.at("1N4148");
    EXPECT_EQ(model.device_type, DeviceType::DIODE);
    EXPECT_DOUBLE_EQ(model.params.at("is"), 2.52e-9);
    EXPECT_DOUBLE_EQ(model.params.at("n"), 1.752);

    // Check circuit structure
    EXPECT_GT(result.circuit->num_devices(), 0);

    // Run DC analysis with positive input voltage
    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // With positive input, diode should conduct
    // Output voltage should be close to input (minus diode drop ~0.7V)
    NodeId out_id = result.circuit->get_node_id("out");
    Real V_out = solution[out_id - 1];

    // For Vin=0, output should be near 0 (capacitor discharged in DC)
    EXPECT_LT(std::abs(V_out), 1.0);
}

//==============================================================================
// Test 3: Multi-Device Integration (BJT + Diode)
//==============================================================================
TEST(ManufacturerModels, MultiDevice_BJT_Diode) {
    // Parse netlist with multiple .INCLUDE directives
    parser::NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/integration/test_multi_device.cir").string());

    ASSERT_NE(result.circuit, nullptr);

    // Check that both models were loaded
    EXPECT_TRUE(result.config.models.find("2N3904") != result.config.models.end());
    EXPECT_TRUE(result.config.models.find("1N4148") != result.config.models.end());

    // Verify model types
    EXPECT_EQ(result.config.models.at("2N3904").device_type, DeviceType::BJT_NPN);
    EXPECT_EQ(result.config.models.at("1N4148").device_type, DeviceType::DIODE);

    // Check circuit structure
    EXPECT_GT(result.circuit->num_devices(), 5);  // Should have multiple devices

    // Run DC analysis
    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Only check voltages if solve succeeded
    if (status == ConvergenceStatus::SUCCESS) {
        // Check basic operation
        NodeId collector_id = result.circuit->get_node_id("collector");
        Real V_collector = solution[collector_id - 1];

        // Collector voltage should be positive (< Vcc)
        EXPECT_GT(V_collector, 0.0);
        EXPECT_LT(V_collector, 12.0);  // Less than Vcc
    }
}

//==============================================================================
// Test 4: Model File Path Resolution
//==============================================================================
TEST(ManufacturerModels, ModelFilePathResolution) {
    // Test that .INCLUDE can find model files with relative paths
    parser::NetlistParser parser;

    // This should succeed if path resolution works correctly
    EXPECT_NO_THROW({
        auto result = parser.parse_file(kirimaspice::test::source_path("tests/integration/test_2n3904_emitter_follower.cir").string());
        EXPECT_NE(result.circuit, nullptr);
    });
}

//==============================================================================
// Test 5: Multiple Model Files
//==============================================================================
TEST(ManufacturerModels, MultipleModelFiles) {
    // Test loading multiple .INCLUDE files in one netlist
    parser::NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/integration/test_multi_device.cir").string());

    ASSERT_NE(result.circuit, nullptr);

    // Should have loaded 2 models from 2 different files
    EXPECT_EQ(result.config.models.size(), 2);

    // Verify both models
    EXPECT_TRUE(result.config.models.find("2N3904") != result.config.models.end());
    EXPECT_TRUE(result.config.models.find("1N4148") != result.config.models.end());
}
