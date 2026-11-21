// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Dynamic gmin and gmin stepping add a shunt from every node to ground to
// help the iteration; the solution they return is that of the circuit
// itself, without the shunt, as SPICE ends its gmin aids. 1 uA into
// 1 Mohm is 1 V (a diode on another node makes the circuit nonlinear).

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double voltage_with(const std::string& options) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nI1 0 a 1u\nR1 a 0 1meg\nV2 b 0 0.6\nD1 b 0 DM\n.model DM D IS=1e-14\n.options " +
        options + "\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << options;
    return x.empty() ? 0.0 : x[parsed.circuit->get_node_id("a") - 1];
}

}  // namespace

TEST(GminAids, DynamicGminLeavesNoShunt) {
    // With the shunt of 1e-6 S left in, V(a) would be 0.5 V
    EXPECT_NEAR(voltage_with("dyn_gmin=1 gmin=1e-6"), 1.0, 1e-9);
}

TEST(GminAids, GminSteppingLeavesNoShunt) {
    // With the last step's gmin left in, V(a) would be 0.999999 V
    EXPECT_NEAR(voltage_with("gmin_stepping=1"), 1.0, 1e-9);
}
