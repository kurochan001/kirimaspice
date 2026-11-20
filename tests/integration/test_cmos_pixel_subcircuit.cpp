// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/integration/test_cmos_pixel_subcircuit.cpp
// Integration test for 3T CMOS pixel with subcircuit expansion

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include "source_path.hpp"

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::solvers;
using namespace kirimaspice::circuit;

// Helper function to find node voltage by name
Real get_node_voltage(const DenseVector& solution, const CircuitGraph& circuit, const std::string& node_name) {
    // Ground node check
    if (node_name == "0" || node_name == "GND") {
        return 0.0;
    }

    // Get all node names from the circuit
    auto node_names = circuit.get_node_names();

    // Search for the node name in the list (handles hierarchical names like "Xp1.vout_sf")
    auto it = std::find(node_names.begin(), node_names.end(), node_name);
    if (it == node_names.end()) {
        throw std::runtime_error("Node not found: " + node_name);
    }

    // Calculate index in node_names list (0-based)
    size_t node_index = std::distance(node_names.begin(), it);

    // IMPORTANT: node_names includes ground node "0" at index 0, but solution vector does NOT include ground
    // So we need to subtract 1 from node_index to get the correct solution vector index
    // Example:
    //   node_names[0] = "0" (ground) -> not in solution (always 0V)
    //   node_names[1] = "vdd" -> solution[0]
    //   node_names[2] = "pd" -> solution[1]
    //   etc.

    if (node_index == 0) {
        // This is the ground node "0"
        return 0.0;
    }

    // Convert to solution vector index (subtract 1 to skip ground)
    size_t solution_index = node_index - 1;

    // Get voltage from solution vector
    if (solution_index < solution.size()) {
        return solution[solution_index];
    }

    throw std::runtime_error("Node index out of bounds: " + node_name);
}

// Test subcircuit-based 3T CMOS pixel circuit
TEST(CMOSPixelSubcircuitTest, SubcircuitBasedDCAnalysis) {
    spdlog::set_level(spdlog::level::warn);  // Reduce log noise

    // Parse netlist with subcircuit definition
    NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/netlists/pixel_3t_subckt.sp").string());

    ASSERT_NE(result.circuit, nullptr) << "Failed to parse netlist";

    // Set simulation config for model parameter access during subcircuit expansion
    result.circuit->set_simulation_config(&result.config);

    // Expand subcircuits
    result.circuit->expand_all_subcircuits();

    // Create DC solver with configuration from netlist
    DCSolver dc_solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;

    // Solve DC operating point
    auto status = dc_solver.solve(solution, stats);

    // Verify convergence
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS) << "DC analysis failed to converge";
    EXPECT_LE(stats.total_iterations, 100) << "Convergence required too many iterations";

    // Extract node voltages
    Real v_col = get_node_voltage(solution, *result.circuit, "col");
    Real v_vout_sf = get_node_voltage(solution, *result.circuit, "Xp1.vout_sf");
    Real v_vdd = get_node_voltage(solution, *result.circuit, "vdd");
    Real v_pd = get_node_voltage(solution, *result.circuit, "pd");
    Real v_rowsel = get_node_voltage(solution, *result.circuit, "rowsel");

    // Verify supply voltages
    EXPECT_NEAR(v_vdd, 3.3, 1e-6) << "VDD voltage incorrect";
    EXPECT_NEAR(v_pd, 2.0, 1e-6) << "Photodiode voltage incorrect";
    EXPECT_NEAR(v_rowsel, 3.3, 1e-6) << "Row select voltage incorrect";

    // Verify output voltages (source follower operation)
    // V(col) should be approximately V(pd) - Vth
    EXPECT_NEAR(v_col, 0.618332, 1e-3) << "Column output voltage incorrect";
    EXPECT_NEAR(v_vout_sf, 0.752912, 1e-3) << "Source follower output voltage incorrect";

    // Verify source follower offset (Vgs ≈ Vth + margin)
    Real v_gs_sf = v_pd - v_vout_sf;  // Gate-source voltage of source follower MOSFET
    EXPECT_GT(v_gs_sf, 0.7) << "Source follower not properly biased (Vgs < Vth)";
    EXPECT_LT(v_gs_sf, 2.0) << "Source follower gate-source voltage too high";
}

// Test inline (non-subcircuit) 3T CMOS pixel circuit
TEST(CMOSPixelSubcircuitTest, InlineBasedDCAnalysis) {
    spdlog::set_level(spdlog::level::warn);

    // Parse netlist without subcircuit
    NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/netlists/pixel_3t_inline.sp").string());

    ASSERT_NE(result.circuit, nullptr) << "Failed to parse netlist";

    // Create DC solver
    DCSolver dc_solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;

    // Solve DC operating point
    auto status = dc_solver.solve(solution, stats);

    // Verify convergence
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS) << "DC analysis failed to converge";
    EXPECT_LE(stats.total_iterations, 100) << "Convergence required too many iterations";

    // Extract node voltages
    Real v_col = get_node_voltage(solution, *result.circuit, "col");
    Real v_vout_sf = get_node_voltage(solution, *result.circuit, "vout_sf");
    Real v_vdd = get_node_voltage(solution, *result.circuit, "vdd");
    Real v_pd = get_node_voltage(solution, *result.circuit, "pd");
    Real v_rowsel = get_node_voltage(solution, *result.circuit, "rowsel");

    // Verify supply voltages
    EXPECT_NEAR(v_vdd, 3.3, 1e-6) << "VDD voltage incorrect";
    EXPECT_NEAR(v_pd, 2.0, 1e-6) << "Photodiode voltage incorrect";
    EXPECT_NEAR(v_rowsel, 3.3, 1e-6) << "Row select voltage incorrect";

    // Verify output voltages
    EXPECT_NEAR(v_col, 0.618332, 1e-3) << "Column output voltage incorrect";
    EXPECT_NEAR(v_vout_sf, 0.752912, 1e-3) << "Source follower output voltage incorrect";

    // Verify source follower offset
    Real v_gs_sf = v_pd - v_vout_sf;
    EXPECT_GT(v_gs_sf, 0.7) << "Source follower not properly biased (Vgs < Vth)";
    EXPECT_LT(v_gs_sf, 2.0) << "Source follower gate-source voltage too high";
}

// Test that subcircuit and inline versions produce identical results
TEST(CMOSPixelSubcircuitTest, SubcircuitInlineEquivalence) {
    spdlog::set_level(spdlog::level::warn);

    // Parse both netlists
    NetlistParser parser;
    auto subcircuit_result = parser.parse_file(kirimaspice::test::source_path("tests/netlists/pixel_3t_subckt.sp").string());
    auto inline_result = parser.parse_file(kirimaspice::test::source_path("tests/netlists/pixel_3t_inline.sp").string());

    ASSERT_NE(subcircuit_result.circuit, nullptr);
    ASSERT_NE(inline_result.circuit, nullptr);

    // Set simulation config for model parameter access during subcircuit expansion
    subcircuit_result.circuit->set_simulation_config(&subcircuit_result.config);

    // Expand subcircuits in the subcircuit version
    subcircuit_result.circuit->expand_all_subcircuits();

    // Solve both circuits
    DCSolver subcircuit_solver(*subcircuit_result.circuit, subcircuit_result.config);
    DCSolver inline_solver(*inline_result.circuit, inline_result.config);

    DenseVector subcircuit_solution, inline_solution;
    SolverStats subcircuit_stats, inline_stats;

    auto subcircuit_status = subcircuit_solver.solve(subcircuit_solution, subcircuit_stats);
    auto inline_status = inline_solver.solve(inline_solution, inline_stats);

    ASSERT_EQ(subcircuit_status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(inline_status, ConvergenceStatus::SUCCESS);

    // Extract voltages from subcircuit version
    Real v_col_subcircuit = get_node_voltage(subcircuit_solution, *subcircuit_result.circuit, "col");
    Real v_vout_sf_subcircuit = get_node_voltage(subcircuit_solution, *subcircuit_result.circuit, "Xp1.vout_sf");

    // Extract voltages from inline version
    Real v_col_inline = get_node_voltage(inline_solution, *inline_result.circuit, "col");
    Real v_vout_sf_inline = get_node_voltage(inline_solution, *inline_result.circuit, "vout_sf");

    // Verify that both versions produce identical results
    EXPECT_NEAR(v_col_subcircuit, v_col_inline, 1e-6)
        << "Column voltage differs between subcircuit and inline versions";
    EXPECT_NEAR(v_vout_sf_subcircuit, v_vout_sf_inline, 1e-6)
        << "Source follower voltage differs between subcircuit and inline versions";

    // Verify convergence behavior is similar
    EXPECT_LE(std::abs(static_cast<int>(subcircuit_stats.total_iterations) -
                       static_cast<int>(inline_stats.total_iterations)), 5)
        << "Convergence iteration count differs significantly between versions";
}

// Test model parameter lookup during subcircuit expansion
TEST(CMOSPixelSubcircuitTest, ModelParameterLookup) {
    spdlog::set_level(spdlog::level::info);

    NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/netlists/pixel_3t_subckt.sp").string());

    ASSERT_NE(result.circuit, nullptr);

    // Verify that model parameters were correctly parsed
    ASSERT_TRUE(result.config.models.count("NMOS") > 0)
        << "NMOS model not found in configuration";

    const auto& nmos_model = result.config.models.at("NMOS");

    // Verify KP parameter (should be 120u = 0.00012)
    ASSERT_TRUE(nmos_model.params.count("kp") > 0)
        << "KP parameter not found in NMOS model";
    EXPECT_NEAR(nmos_model.params.at("kp"), 0.00012, 1e-8)
        << "KP parameter value incorrect";

    // Verify VTO parameter
    ASSERT_TRUE(nmos_model.params.count("vto") > 0)
        << "VTO parameter not found in NMOS model";
    EXPECT_NEAR(nmos_model.params.at("vto"), 0.7, 1e-6)
        << "VTO parameter value incorrect";

    // Verify LAMBDA parameter
    ASSERT_TRUE(nmos_model.params.count("lambda") > 0)
        << "LAMBDA parameter not found in NMOS model";
    EXPECT_NEAR(nmos_model.params.at("lambda"), 0.01, 1e-6)
        << "LAMBDA parameter value incorrect";
}

// Test .option directives are properly applied
TEST(CMOSPixelSubcircuitTest, OptionDirectivesParsing) {
    spdlog::set_level(spdlog::level::warn);

    NetlistParser parser;
    auto result = parser.parse_file(kirimaspice::test::source_path("tests/netlists/pixel_3t_subckt.sp").string());

    ASSERT_NE(result.circuit, nullptr);

    // Verify tolerances were set correctly
    EXPECT_NEAR(result.config.dc.abstol, 1e-12, 1e-15)
        << "abstol option not applied";
    EXPECT_NEAR(result.config.dc.reltol, 1e-4, 1e-6)
        << "reltol option not applied";
    EXPECT_NEAR(result.config.dc.vntol, 1e-6, 1e-9)
        << "vntol option not applied";

    // Verify iteration limit was set
    EXPECT_EQ(result.config.dc.max_iterations, 500)
        << "itl1 option not applied";
}
