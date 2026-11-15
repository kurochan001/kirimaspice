// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A BSIM4 device's W is the width of all its NF fingers, each W / NF wide,
// and the device's current is that of one finger times NF and M
// (BSIM4Params::get_weff, BSIM4Core::evaluate). NF and M come from the
// element line (set_bsim4_instance_parameter). The expected currents are
// ngspice-47's for the same netlists (run on 2026-11-15).

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

// The current into the drain of M1 at VD = VG = 1 V, minus the current of
// VD, the first voltage source and so the first branch
Real drain_current(const std::string& instance, const std::string& card_extra = "") {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVD d 0 1\nVG g 0 1\n" + instance +
        "\n.model m nmos level=14 version=4.8.3 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4" +
        card_extra + "\n.op\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    return -x[parsed.circuit->num_nodes() - 1];
}

// ngspice prints six digits, and its BSIM4 and this one differ by a few in
// 1e6 at these biases
void expect_current(Real current, Real ngspice) {
    EXPECT_NEAR(current, ngspice, 1e-5 * ngspice);
}

}  // namespace

TEST(Bsim4Fingers, CurrentAgainstFingers) {
    expect_current(drain_current("M1 d g 0 0 m L=1u W=10u"), 4.61339e-4);
    expect_current(drain_current("M1 d g 0 0 m L=1u W=10u NF=2"), 4.41875e-4);
    expect_current(drain_current("M1 d g 0 0 m L=1u W=10u NF=4"), 4.18104e-4);
    expect_current(drain_current("M1 d g 0 0 m L=1u W=10u NF=2 M=3"), 1.32563e-3);
}

// NF on the card is the default of the element line's
TEST(Bsim4Fingers, FingersOnTheCard) {
    expect_current(drain_current("M1 d g 0 0 m L=1u W=10u", " nf=2"), 4.41875e-4);
    expect_current(drain_current("M1 d g 0 0 m L=1u W=10u NF=4", " nf=2"), 4.18104e-4);
}
