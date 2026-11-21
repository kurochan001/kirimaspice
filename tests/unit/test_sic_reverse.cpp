// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The SiC MOSFET (LEVEL=10) conducts in both directions, as SPICE's
// MOSFETs do: with VDS < 0 the source acts as the drain and the same
// current of VGD and VSD flows from source to drain. A drain fed through
// 1 ohm from +1 mV or from -1 mV, gate on, is symmetric. With KP W/L = 1,
// VTO = 2.5 V, RDRIFT = 1 ohm and VGS = 12.5 V the channel is 1/(10 S) in
// series with the drift resistance: Id = 1 mV / (1 + 0.1 + 1) ohm.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double drain_voltage(double supply) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.model msic nmos level=10 kp=1 w=1 l=1 vto=2.5 theta=0 rdrift=1\nVg g 0 12.5\nVs s 0 " +
        std::to_string(supply) + "\nR1 s d 1\nM1 d g 0 msic\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << supply;
    return x.empty() ? 0.0 : x[parsed.circuit->get_node_id("d") - 1];
}

}  // namespace

TEST(SicReverse, ConductsTheSameInBothDirections) {
    const double forward = drain_voltage(1e-3);
    const double reverse = drain_voltage(-1e-3);
    EXPECT_NEAR(reverse, -forward, 1e-8);  // within the Newton tolerances
    // About 1 mV / 2.1 ohm through R1 (the channel is not quite linear)
    EXPECT_NEAR(1e-3 - forward, 1e-3 / 2.1, 2e-6);
}
