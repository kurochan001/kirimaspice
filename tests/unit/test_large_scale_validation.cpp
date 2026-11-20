// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_large_scale_validation.cpp
// Large-scale circuit validation against ngspice
// 100, 300, 1000, 3000 devices

#include <gtest/gtest.h>
#include "../validation/ngspice_dc_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <filesystem>
#include "source_path.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;
using namespace kirimaspice::validation;

namespace fs = std::filesystem;

//==============================================================================
// Helper Functions
//==============================================================================

/// Create resistor mesh circuit (same as test_large_scale_*.cpp)
CircuitGraph create_resistor_mesh(size_t rows, size_t cols, Real R_value = 1e3) {
    CircuitGraph circuit;

    // Voltage source at top-left corner
    circuit.add_voltage_source("Vin", "n_0_0", "0", 1.0);  // 1V DC source

    // Create mesh: horizontal and vertical resistors
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            std::string node_curr = "n_" + std::to_string(r) + "_" + std::to_string(c);

            // Horizontal resistor (connect to right neighbor)
            if (c + 1 < cols) {
                std::string node_right = "n_" + std::to_string(r) + "_" + std::to_string(c + 1);
                std::string r_name = "Rh_" + std::to_string(r) + "_" + std::to_string(c);
                circuit.add_resistor(r_name, node_curr, node_right, R_value);
            }

            // Vertical resistor (connect to bottom neighbor)
            if (r + 1 < rows) {
                std::string node_bottom = "n_" + std::to_string(r + 1) + "_" + std::to_string(c);
                std::string r_name = "Rv_" + std::to_string(r) + "_" + std::to_string(c);
                circuit.add_resistor(r_name, node_curr, node_bottom, R_value);
            }
        }
    }

    // Ground the bottom-right corner
    std::string ground_node = "n_" + std::to_string(rows - 1) + "_" + std::to_string(cols - 1);
    circuit.add_resistor("Rload", ground_node, "0", R_value);

    return circuit;
}

//==============================================================================
// 100 Devices (10x10 Resistor Mesh)
//==============================================================================
class LargeScaleValidation100 : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = kirimaspice::test::source_path("validation/resistor_mesh_10x10_dc.csv").string();
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path;
        }
    }

    std::string ref_data_path;
};

TEST_F(LargeScaleValidation100, DC_ResistorMesh_MatchesNgspice) {
    // Load ngspice reference data
    NgspiceDCData ref_data = load_ngspice_dc_data(ref_data_path);
    ASSERT_GT(ref_data.node_voltages.size(), 0);

    // Build 10x10 resistor mesh
    auto circuit = create_resistor_mesh(10, 10, 1e3);

    // Run DC analysis
    SimulationConfig config;
    config.dc.max_iterations = 100;
    config.dc.vntol = 1e-6;

    DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;

    auto status = solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Compare with ngspice
    auto comparison = compare_dc_results(ref_data, solution, circuit);

    // Tolerances for large-scale linear circuit
    const double max_abs_error = 1e-6;   // 1 µV absolute tolerance
    const double max_rel_error = 1e-3;   // 0.1% relative tolerance

    double max_abs_err = 0.0;
    double max_rel_err = 0.0;
    bool all_passed = true;

    for (const auto& point : comparison) {
        max_abs_err = std::max(max_abs_err, std::abs(point.absolute_error));
        max_rel_err = std::max(max_rel_err, std::abs(point.relative_error));

        // Check tolerances (either absolute OR relative must pass)
        const bool abs_ok = std::abs(point.absolute_error) <= max_abs_error;
        const bool rel_ok = std::abs(point.relative_error) <= max_rel_error;

        if (!abs_ok && !rel_ok) {
            all_passed = false;
            std::cout << "FAIL at node " << point.node_name << ": "
                      << "expected=" << point.expected << ", "
                      << "actual=" << point.actual << ", "
                      << "abs_error=" << point.absolute_error << ", "
                      << "rel_error=" << (point.relative_error * 100.0) << "%\n";
        }
    }

    std::cout << "\n100 devices - 10x10 Resistor Mesh Validation:\n";
    std::cout << "  Nodes compared: " << comparison.size() << "\n";
    std::cout << "  Max absolute error: " << std::scientific << max_abs_err << " V\n";
    std::cout << "  Max relative error: " << std::fixed << std::setprecision(4) << (max_rel_err * 100.0) << " %\n";
    std::cout << "  Convergence iterations: " << stats.total_iterations << "\n";

    EXPECT_TRUE(all_passed) << "Some nodes exceeded tolerance";
}

//==============================================================================
// 300 Devices (13x13 Resistor Mesh)
//==============================================================================
class LargeScaleValidation300 : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = kirimaspice::test::source_path("validation/resistor_mesh_13x13_dc.csv").string();
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path;
        }
    }

    std::string ref_data_path;
};

TEST_F(LargeScaleValidation300, DC_ResistorMesh_MatchesNgspice) {
    NgspiceDCData ref_data = load_ngspice_dc_data(ref_data_path);
    ASSERT_GT(ref_data.node_voltages.size(), 0);

    auto circuit = create_resistor_mesh(13, 13, 1e3);

    SimulationConfig config;
    config.dc.max_iterations = 100;
    config.dc.vntol = 1e-6;

    DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;

    auto status = solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    auto comparison = compare_dc_results(ref_data, solution, circuit);

    const double max_abs_error = 1e-6;
    const double max_rel_error = 1e-3;

    double max_abs_err = 0.0;
    double max_rel_err = 0.0;
    bool all_passed = true;

    for (const auto& point : comparison) {
        max_abs_err = std::max(max_abs_err, std::abs(point.absolute_error));
        max_rel_err = std::max(max_rel_err, std::abs(point.relative_error));

        const bool abs_ok = std::abs(point.absolute_error) <= max_abs_error;
        const bool rel_ok = std::abs(point.relative_error) <= max_rel_error;

        if (!abs_ok && !rel_ok) {
            all_passed = false;
        }
    }

    std::cout << "\n300 devices - 13x13 Resistor Mesh Validation:\n";
    std::cout << "  Nodes compared: " << comparison.size() << "\n";
    std::cout << "  Max absolute error: " << std::scientific << max_abs_err << " V\n";
    std::cout << "  Max relative error: " << std::fixed << std::setprecision(4) << (max_rel_err * 100.0) << " %\n";
    std::cout << "  Convergence iterations: " << stats.total_iterations << "\n";

    EXPECT_TRUE(all_passed) << "Some nodes exceeded tolerance";
}

//==============================================================================
// 1000 Devices (22x22 Resistor Mesh)
//==============================================================================
class LargeScaleValidation1000 : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = kirimaspice::test::source_path("validation/resistor_mesh_22x22_dc.csv").string();
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path;
        }
    }

    std::string ref_data_path;
};

TEST_F(LargeScaleValidation1000, DC_ResistorMesh_MatchesNgspice) {
    NgspiceDCData ref_data = load_ngspice_dc_data(ref_data_path);
    ASSERT_GT(ref_data.node_voltages.size(), 0);

    auto circuit = create_resistor_mesh(22, 22, 1e3);

    SimulationConfig config;
    config.dc.max_iterations = 100;
    config.dc.vntol = 1e-6;

    DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;

    auto status = solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    auto comparison = compare_dc_results(ref_data, solution, circuit);

    const double max_abs_error = 1e-6;
    const double max_rel_error = 1e-3;

    double max_abs_err = 0.0;
    double max_rel_err = 0.0;
    bool all_passed = true;

    for (const auto& point : comparison) {
        max_abs_err = std::max(max_abs_err, std::abs(point.absolute_error));
        max_rel_err = std::max(max_rel_err, std::abs(point.relative_error));

        const bool abs_ok = std::abs(point.absolute_error) <= max_abs_error;
        const bool rel_ok = std::abs(point.relative_error) <= max_rel_error;

        if (!abs_ok && !rel_ok) {
            all_passed = false;
        }
    }

    std::cout << "\n1000 devices - 22x22 Resistor Mesh Validation:\n";
    std::cout << "  Nodes compared: " << comparison.size() << "\n";
    std::cout << "  Max absolute error: " << std::scientific << max_abs_err << " V\n";
    std::cout << "  Max relative error: " << std::fixed << std::setprecision(4) << (max_rel_err * 100.0) << " %\n";
    std::cout << "  Convergence iterations: " << stats.total_iterations << "\n";

    EXPECT_TRUE(all_passed) << "Some nodes exceeded tolerance";
}

//==============================================================================
// 3000 Devices (39x39 Resistor Mesh)
//==============================================================================
class LargeScaleValidation3000 : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = kirimaspice::test::source_path("validation/resistor_mesh_39x39_dc.csv").string();
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path;
        }
    }

    std::string ref_data_path;
};

TEST_F(LargeScaleValidation3000, DC_ResistorMesh_MatchesNgspice) {
    NgspiceDCData ref_data = load_ngspice_dc_data(ref_data_path);
    ASSERT_GT(ref_data.node_voltages.size(), 0);

    auto circuit = create_resistor_mesh(39, 39, 1e3);

    SimulationConfig config;
    config.dc.max_iterations = 100;
    config.dc.vntol = 1e-6;

    DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;

    auto status = solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    auto comparison = compare_dc_results(ref_data, solution, circuit);

    const double max_abs_error = 1e-6;
    const double max_rel_error = 1e-3;

    double max_abs_err = 0.0;
    double max_rel_err = 0.0;
    bool all_passed = true;

    for (const auto& point : comparison) {
        max_abs_err = std::max(max_abs_err, std::abs(point.absolute_error));
        max_rel_err = std::max(max_rel_err, std::abs(point.relative_error));

        const bool abs_ok = std::abs(point.absolute_error) <= max_abs_error;
        const bool rel_ok = std::abs(point.relative_error) <= max_rel_error;

        if (!abs_ok && !rel_ok) {
            all_passed = false;
        }
    }

    std::cout << "\n3000 devices - 39x39 Resistor Mesh Validation:\n";
    std::cout << "  Nodes compared: " << comparison.size() << "\n";
    std::cout << "  Max absolute error: " << std::scientific << max_abs_err << " V\n";
    std::cout << "  Max relative error: " << std::fixed << std::setprecision(4) << (max_rel_err * 100.0) << " %\n";
    std::cout << "  Convergence iterations: " << stats.total_iterations << "\n";

    EXPECT_TRUE(all_passed) << "Some nodes exceeded tolerance";
}

//==============================================================================
// Main
//==============================================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
