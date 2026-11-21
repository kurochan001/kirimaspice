// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The MESFET's channel is symmetric, as SPICE's is: with the drain below
// the source, the source acts as the drain. A MESFET fed through 1 ohm at
// its drain from +0.25 V, gate and source at 0 V, is the same as one fed at
// its source from +0.25 V with gate and drain at 0 V; before, the second
// carried no current. The slope at VDS = 0 is the same from both sides.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
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

namespace {

double statz_drain_current(const std::string& card, double vds, double vgs) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nVD d 0 " + std::to_string(vds) + "\nVG g 0 " + std::to_string(vgs) +
                                      "\nZ1 d g 0 mm\n" + card + "\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    for (std::uint32_t i = 0; i < x.size(); ++i) {
        if (parsed.circuit->unknown_name(i) == "I(VD)") {
            return -x[i];
        }
    }
    ADD_FAILURE() << "no I(VD)";
    return 0.0;
}

}  // namespace

TEST(MesfetStatz, Level2IsStatzModel) {
    // Statz et al. (1987): beta vov^2 / (1 + B vov) (1 + lambda Vds)
    // (1 - (1 - alpha Vds / 3)^3) below Vds = 3/alpha. ngspice-47's MESFET
    // (its LEVEL 1, run 2026-11-21) gives these currents.
    const std::string card = ".model mm NMF LEVEL=2 BETA=0.1 VTO=-2 B=0.3 ALPHA=2 LAMBDA=0";
    EXPECT_NEAR(statz_drain_current(card, 1.0, 0.0), 0.240741, 1e-6);
    EXPECT_NEAR(statz_drain_current(card, 0.5, 0.0), 0.175926, 1e-6);
    EXPECT_NEAR(statz_drain_current(card, 2.0, 0.0), 0.25, 1e-9);  // above 3/alpha
    EXPECT_NEAR(statz_drain_current(".model mm NMF LEVEL=2 BETA=0.05 VTO=-1.5 B=1 ALPHA=1.5 LAMBDA=0.1",
                                    1.0, -0.5),
                2.40625e-2, 1e-7);
}

TEST(MesfetLevels, DefaultIsStatzAndOtherLevelsAreErrors) {
    // An NMF card without LEVEL is Statz's model, as ngspice-47's MESFET is
    // (its LEVEL 1, run 2026-11-21: 0.240741 A); ngspice's other levels are
    // other models, which are errors here
    EXPECT_NEAR(statz_drain_current(".model mm NMF BETA=0.1 VTO=-2 B=0.3 ALPHA=2 LAMBDA=0", 1.0, 0.0), 0.240741,
                1e-6);
    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string("t\nVD d 0 1\nZ1 d 0 0 mm\n.model mm NMF LEVEL=2\n.end\n"),
                 parser::ParseError);
}
