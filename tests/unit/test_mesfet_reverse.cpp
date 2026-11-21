// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The MESFET's channel is symmetric, as SPICE's is: with the drain below
// the source, the source acts as the drain. A MESFET fed through 1 ohm at
// its drain from +0.25 V, gate and source at 0 V, is the same as one fed at
// its source from +0.25 V with gate and drain at 0 V; before, the second
// carried no current. The slope at VDS = 0 is the same from both sides.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double fed_voltage(const std::string& netlist, const std::string& node) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    return x.empty() ? 0.0 : x[parsed.circuit->get_node_id(node) - 1];
}

const std::string kCard = ".model ZM NMF BETA=0.1 VTO=-2 LAMBDA=0.05 ALPHA=2\n";

}  // namespace

TEST(MesfetReverse, SourceActsAsTheDrain) {
    const double forward =
        fed_voltage("t\nVDD dd 0 0.25\nR1 dd d 1\nVG g 0 0\nZ1 d g 0 ZM\n" + kCard + ".end\n", "d");
    const double reverse =
        fed_voltage("t\nVSS ss 0 0.25\nR1 ss s 1\nVG g 0 0\nZ1 0 g s ZM\n" + kCard + ".end\n", "s");
    EXPECT_LT(forward, 0.25 - 1e-3);  // the channel conducts
    EXPECT_NEAR(reverse, forward, 1e-8);
}
