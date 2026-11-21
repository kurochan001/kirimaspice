// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The VDMOS avalanche current above VBD is 1 mA (exp(10 (VDS - VBD)) - 1):
// zero at VBD, so continuous there, and extended linearly where the
// exponential would overflow (core::safe_exp_junction). 700 V through
// 1 Mohm into a device that is off, VBD = 600 V, settles where
// (700 - V) / 1 Mohm = 1 mA (exp(10 (V - 600)) - 1): V = 600.009530 V.
// Before, the current jumped by 1 mA at VBD and overflowed above it, and
// the operating point did not converge.

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

TEST(VdmosAvalanche, ClampsTheDrainContinuouslyAboveVbd) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVDD dd 0 700\nRL dd d 1meg\nVG g 0 0\nM1 d g 0 VD\n.model VD NMOS LEVEL=20 VBD=600\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("d") - 1], 600.009530, 1e-3);
}
