// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The W and L of a MOSFET whose element line gives none: level 3 takes
// SPICE's DEFW and DEFL, 100 um, and BSIM3 and BSIM4 their instance
// defaults, 5 um, as ngspice-47 does (run 2026-11-21: a level-3 card
// without parameters with drain and gate at 2 V carries 4.14377e-5 A, and
// BSIM3 and BSIM4 carry the current of W = L = 5 um).

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double drain_current(const std::string& level, const std::string& size) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nVD d 0 2\nM1 d d 0 0 NX " + size + "\n.model NX NMOS LEVEL=" +
                                      level + "\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << level << " " << size;
    return x.empty() ? 0.0 : -x.back();  // -I(VD), the last unknown
}

}  // namespace

TEST(MosfetDefaultSize, Level3TakesOneHundredMicrometres) {
    EXPECT_NEAR(drain_current("3", ""), 4.14377e-5, 1e-10);
    EXPECT_DOUBLE_EQ(drain_current("3", ""), drain_current("3", "W=100u L=100u"));
}

TEST(MosfetDefaultSize, Bsim3AndBsim4TakeFiveMicrometres) {
    for (const char* level : {"8", "49", "14", "54"}) {
        EXPECT_DOUBLE_EQ(drain_current(level, ""), drain_current(level, "W=5u L=5u")) << level;
        EXPECT_NE(drain_current(level, ""), drain_current(level, "W=10u L=1u")) << level;
    }
}
