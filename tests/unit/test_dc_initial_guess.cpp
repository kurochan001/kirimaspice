// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The DC solver starts Newton's method from zero, except at the nodes that
// .NODESET names (DCSolver::initialize_solution); it guesses nothing from
// the kinds of devices or how they are connected. So a .NODESET that gives
// every node its solution is a converged start, whatever the devices are,
// for the pass with the nodeset nodes held as for the pass that releases
// them (each at most 2 iterations).

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

// A common-emitter stage with an emitter resistor, and a Darlington pair
const char* const kCircuits[] = {
    "ce\nVcc vcc 0 12\nRB1 vcc b 47k\nRB2 b 0 10k\nRC vcc c 2.2k\nRE e 0 1k\n"
    "Q1 c b e QM\n.model QM NPN BF=100 IS=1e-15\n.end\n",
    "darlington\nVcc vcc 0 12\nRB vcc b1 470k\nRC vcc c 1k\nRE e2 0 100\n"
    "Q1 c b1 e1 QM\nQ2 c e1 e2 QM\n.model QM NPN BF=100 IS=1e-15\n.end\n",
};

}  // namespace

TEST(DCInitialGuess, NodesetAtTheSolutionIsAConvergedStart) {
    for (const char* netlist : kCircuits) {
        SCOPED_TRACE(netlist);
        parser::NetlistParser parser;
        auto parsed = parser.parse_string(netlist);
        DenseVector solution;
        SolverStats stats;
        {
            solvers::DCSolver solver(*parsed.circuit, parsed.config);
            ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        }
        SimulationConfig config = parsed.config;
        const auto names = parsed.circuit->get_node_names();
        for (std::size_t i = 1; i < names.size(); ++i) {
            config.nodeset[names[i]] = solution[i - 1];
        }
        solvers::DCSolver solver(*parsed.circuit, config);
        DenseVector again;
        SolverStats again_stats;
        ASSERT_EQ(solver.solve(again, again_stats), ConvergenceStatus::SUCCESS);
        EXPECT_LE(again_stats.total_iterations, 4u) << "from scratch: " << stats.total_iterations;
    }
}

TEST(DCInitialGuess, NewtonStepIsLimitedByTheJunctionsOnly) {
    // SPICE limits each junction's step (core::limit_junction_step) and has
    // no limit on the Newton step as a whole. Three diodes driven from
    // 100 V through 10 ohm converge in 10 iterations; a clamp of every
    // unknown's step to 5 V for the first ten iterations took 35.
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "diodes\nV1 in 0 100\nR1 in a 10\nD1 a b DM\nD2 b c DM\nD3 c 0 DM\nR2 c 0 1k\n"
        ".model DM D IS=1e-14 N=1\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    EXPECT_LE(stats.total_iterations, 15u);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("a") - 1], 2.67792, 1e-5);
}

namespace {

std::pair<double, double> flip_flop_collectors(const std::string& nodeset) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "flip-flop\nVCC vcc 0 5\nRC1 vcc c1 1k\nRC2 vcc c2 1k\nRB1 c2 b1 10k\nRB2 c1 b2 10k\n"
        "Q1 c1 b1 e QN\nQ2 c2 b2 e QN\nRE e 0 100\n.model QN NPN IS=1e-14 BF=100\n.nodeset " +
        nodeset + "\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << nodeset;
    if (x.empty()) {
        return {0.0, 0.0};
    }
    return {x[parsed.circuit->get_node_id("c1") - 1], x[parsed.circuit->get_node_id("c2") - 1]};
}

}  // namespace

TEST(DCInitialGuess, NodesetChoosesTheStateOfAFlipFlop) {
    // .NODESET as in SPICE: the nodes are held at their values for a first
    // solve, then released. ngspice-47 (run 2026-11-21) gives c1 = 4.652429 V
    // and c2 = 0.552984 V for the first nodeset and the mirror for the second;
    // as an initial guess alone, both gave the symmetric point 1.67894 V.
    const auto [c1, c2] = flip_flop_collectors("V(c1)=5 V(c2)=0.2");
    EXPECT_NEAR(c1, 4.652429, 1e-3);
    EXPECT_NEAR(c2, 0.552984, 1e-3);
    const auto [m1, m2] = flip_flop_collectors("V(c1)=0.2 V(c2)=5");
    EXPECT_NEAR(m1, c2, 1e-6);
    EXPECT_NEAR(m2, c1, 1e-6);
}
