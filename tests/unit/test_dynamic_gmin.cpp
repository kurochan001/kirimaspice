// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_dynamic_gmin.cpp
// Dynamic Gmin unit tests

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <spdlog/spdlog.h>

using namespace kirimaspice;

class DynamicGminTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::info);
    }
};

TEST_F(DynamicGminTest, ParsesOptionsCorrectly) {
    std::string netlist = R"(
* Simple divider with dynamic gmin options
.option dyn_gmin=1 dyn_gmin_start=1e-9 dyn_gmin_min=1e-12 dyn_gmin_max=1e-3 dyn_gmin_inc=5 dyn_gmin_dec=0.4 dyn_gmin_stall=2 dyn_gmin_improve=0.05 dyn_gmin_target=1e-10
V1 1 0 DC 10
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.circuit);
    const auto& dc = result.config.dc;
    EXPECT_TRUE(dc.enable_dynamic_gmin);
    EXPECT_DOUBLE_EQ(dc.dynamic_gmin_start, 1e-9);
    EXPECT_DOUBLE_EQ(dc.dynamic_gmin_min, 1e-12);
    EXPECT_DOUBLE_EQ(dc.dynamic_gmin_max, 1e-3);
    EXPECT_DOUBLE_EQ(dc.dynamic_gmin_increase_factor, 5.0);
    EXPECT_DOUBLE_EQ(dc.dynamic_gmin_decrease_factor, 0.4);
    EXPECT_EQ(dc.dynamic_gmin_stall_iterations, 2u);
    EXPECT_DOUBLE_EQ(dc.dynamic_gmin_improvement_threshold, 0.05);
    EXPECT_DOUBLE_EQ(dc.dynamic_gmin_residual_target, 1e-10);
}

TEST_F(DynamicGminTest, DoesNotSolveACircuitWithoutAGroundPath) {
    // Nodes 1 and 2 have no path to ground: the circuit has no operating
    // point. Dynamic gmin may converge with its shunt from every node to
    // ground, but the solution it returns is that of the circuit without
    // the shunt, so it fails as the plain iteration does; ngspice-47 reports
    // "Dynamic gmin stepping failed" for this circuit (run 2026-11-21).
    std::string netlist = R"(
* Floating nodes with ideal current source
I1 1 2 DC 1m
C1 1 2 1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_TRUE(result.circuit);

    // Baseline without dynamic Gmin should fail to converge (singular matrix)
    SimulationConfig no_dyn_config = result.config;
    no_dyn_config.dc.enable_dynamic_gmin = false;

    solvers::DCSolver solver_no_dyn(*result.circuit, no_dyn_config);
    DenseVector solution_no_dyn;
    SolverStats stats_no_dyn;
    auto status_no_dyn = solver_no_dyn.solve(solution_no_dyn, stats_no_dyn);
    EXPECT_NE(status_no_dyn, ConvergenceStatus::SUCCESS);

    // Dynamic Gmin with aggressive settings, which adds a ground path while
    // it iterates
    SimulationConfig dyn_config = result.config;
    dyn_config.dc.enable_dynamic_gmin = true;
    dyn_config.dc.dynamic_gmin_start = 1e-9;
    dyn_config.dc.dynamic_gmin_min = 1e-12;
    dyn_config.dc.dynamic_gmin_max = 1e-3;
    dyn_config.dc.dynamic_gmin_increase_factor = 10.0;
    dyn_config.dc.dynamic_gmin_decrease_factor = 0.5;
    dyn_config.dc.dynamic_gmin_stall_iterations = 1;
    dyn_config.dc.dynamic_gmin_improvement_threshold = 0.0;
    dyn_config.dc.dynamic_gmin_residual_target = 1e-9;

    solvers::DCSolver solver_dyn(*result.circuit, dyn_config);
    DenseVector solution_dyn;
    SolverStats stats_dyn;
    auto status_dyn = solver_dyn.solve(solution_dyn, stats_dyn);
    EXPECT_NE(status_dyn, ConvergenceStatus::SUCCESS);
}
