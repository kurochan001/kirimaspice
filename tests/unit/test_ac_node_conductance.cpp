// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The AC and noise analyses add no conductance from the nodes to ground, as
// SPICE's do and as the operating point does not: GMIN is a device's own.
// `I1 0 a DC 1m AC 1m` into `R1 a 0 1k` with `.options gmin=1e-4` gives
// v(a) = 1 V in AC and an output noise of 4.071372e-9 V/sqrt(Hz), the
// resistor's sqrt(4kTR), in ngspice-47 (run 2026-11-21); a 1e-4 S shunt
// would give 0.909091 V.

#include <gtest/gtest.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/noise_solver.hpp"

using namespace kirimaspice;

namespace {

const char* kNetlist = "t\nI1 0 a DC 1m AC 1m\nR1 a 0 1k\n.options gmin=1e-4\n.ac lin 1 1k 1k\n.end\n";

}  // namespace

TEST(ACNodeConductance, GminDoesNotLoadTheNodesInAC) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(kNetlist);
    ASSERT_DOUBLE_EQ(parsed.config.gmin, 1e-4);
    solvers::ACSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve();
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const auto v = result.frequency_points.at(0).solution.at(parsed.circuit->get_node_id("a") - 1);
    EXPECT_NEAR(v.real(), 1.0, 1e-12);
    EXPECT_NEAR(v.imag(), 0.0, 1e-12);
}

TEST(ACNodeConductance, GminDoesNotLoadTheNodesInNoise) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(kNetlist);
    solvers::NoiseSolver solver(*parsed.circuit, parsed.config);
    const auto a = parsed.circuit->get_node_id("a");
    const auto result = solver.solve(a, a);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.output_noise.size(), 1u);
    EXPECT_NEAR(result.output_noise[0], 4.071372e-9, 1e-14);
}
