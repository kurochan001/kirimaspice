// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The DC solver starts Newton's method from zero, except at the nodes that
// .NODESET names (DCSolver::initialize_solution); it guesses nothing from
// the kinds of devices or how they are connected. So a .NODESET that gives
// every node its solution is a converged start, whatever the devices are.

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
        EXPECT_LE(again_stats.total_iterations, 2u) << "from scratch: " << stats.total_iterations;
    }
}
