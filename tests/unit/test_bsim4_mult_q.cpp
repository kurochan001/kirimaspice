// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The BSIM4 instance's MULT_Q multiplies every charge: MULT_Q = 2 doubles
// both the gate's capacitance (in saturation) and the drain's (junction and
// overlap, gate off), as in ngspice-47 (run 2026-11-21). Before, MULT_Q was
// read and not used.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;

namespace {

// The capacitance at node n from v(n) behind 1 kohm at 1 MHz: 1/v - 1 = j w R C
double capacitance(const std::string& netlist, const char* node) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    solvers::ACSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve();
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const auto v = result.frequency_points.at(0).solution.at(parsed.circuit->get_node_id(node) - 1);
    return ((1.0 / v) - 1.0).imag() / (2.0 * M_PI * 1e6 * 1e3);
}

std::string gate(const char* mult_q) {
    return std::string("t\nVG g0 0 DC 1 AC 1\nR1 g0 g 1k\nVD d 0 DC 0.6\nM1 d g 0 0 NX W=10u L=1u ") + mult_q +
           "\n.model NX NMOS LEVEL=14\n.ac lin 1 1meg 1meg\n.end\n";
}

std::string drain(const char* mult_q) {
    return std::string("t\nVD0 d0 0 DC 1 AC 1\nR1 d0 d 1k\nM1 d 0 0 0 NX W=10u L=1u AD=1e-11 PD=12u ") +
           mult_q + "\n.model NX NMOS LEVEL=14\n.ac lin 1 1meg 1meg\n.end\n";
}

}  // namespace

TEST(BSIM4MultQ, MultipliesEveryCapacitance) {
    const double cg = capacitance(gate(""), "g");
    const double cd = capacitance(drain(""), "d");
    ASSERT_GT(cg, 1e-15);
    ASSERT_GT(cd, 1e-15);
    EXPECT_NEAR(capacitance(gate("MULT_Q=2"), "g"), 2.0 * cg, 1e-3 * cg);
    EXPECT_NEAR(capacitance(drain("MULT_Q=2"), "d"), 2.0 * cd, 1e-3 * cd);
}
