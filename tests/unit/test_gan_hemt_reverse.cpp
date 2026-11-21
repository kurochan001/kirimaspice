// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The GaN HEMT (LEVEL=11) conducts in both directions, as a MOSFET does:
// for VDS < 0 the source acts as the drain and the same current of VGD and
// VSD flows from source to drain. With the gate on (VGS - VTO = 10 V,
// BETA = 1) the channel is about 0.1 ohm, so 10 mA forced into the drain
// gives about +1 mV and 10 mA out of it about -1 mV. Without the swap the
// factor (1 + LAMBDA VDS) of the forward equations reaches 0 at
// VDS = -1/LAMBDA = -100 V, a false operating point.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double forced_drain_voltage(double current) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.model gn nmos level=11 beta=1 vto=2.5 lambda=0.01 cgs=0 cgd=0 cds=0\nVg g 0 12.5\nI1 0 d DC " +
        std::to_string(current) + "\nRbig d 0 1e9\nM1 d g 0 gn\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << current;
    return x.empty() ? 0.0 : x[parsed.circuit->get_node_id("d") - 1];
}

}  // namespace

TEST(GaNHemtReverse, ConductsTheSameInBothDirections) {
    const double forward = forced_drain_voltage(10e-3);
    const double reverse = forced_drain_voltage(-10e-3);
    EXPECT_NEAR(forward, 1e-3, 5e-5);
    // The gate is held against ground, so in reverse VGD is 1 mV larger
    // than VGS forward: the two differ by about 1e-4 of the value
    EXPECT_NEAR(reverse, -forward, 1e-3 * std::abs(forward));
}
