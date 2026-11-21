// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Node names are compared without case, as in SPICE (CircuitGraph::add_node,
// get_node_id, has_node): "IN" and "in" are one node, kept in lower case,
// also where .IC and B sources name it. ngspice-47 solves the divider below
// to 0.5 V and the B source to 2 V (run on 2026-11-21).

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

double op_voltage(const std::string& netlist, const std::string& node) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    return x.empty() ? 0.0 : x[parsed.circuit->get_node_id(node) - 1];
}

}  // namespace

TEST(NodeNames, CaseDoesNotMakeAnotherNode) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 IN 0 1\nR1 in OUT 1k\nR2 out 0 1k\n.end\n");
    EXPECT_EQ(parsed.circuit->num_nodes(), 3u);  // 0, in, out
    EXPECT_EQ(parsed.circuit->get_node_id("OUT"), parsed.circuit->get_node_id("out"));
    EXPECT_EQ(parsed.circuit->get_node_names().at(2), "out");
    EXPECT_NEAR(op_voltage("t\nV1 IN 0 1\nR1 in OUT 1k\nR2 out 0 1k\n.end\n", "Out"), 0.5, 1e-12);
}

TEST(NodeNames, BehavioralSourceReadsANodeInAnyCase) {
    EXPECT_NEAR(op_voltage("t\nV1 a 0 1\nB1 o 0 V=2*V(A)\nR1 o 0 1k\n.end\n", "o"), 2.0, 1e-9);
}

TEST(NodeNames, InitialConditionNamesANodeInAnyCase) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nR1 out 0 1k\nC1 OUT 0 1u\n.ic V(Out)=1\n.tran 10u 20u UIC\n.end\n");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 20e-6, 10e-6);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_DOUBLE_EQ(result.timepoints.front().node_voltages.at(parsed.circuit->get_node_id("out") - 1), 1.0);
}

namespace {

double expanded_op_voltage(const std::string& netlist, const std::string& node) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    parsed.circuit->expand_all_subcircuits();
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    return x.empty() ? 0.0 : x[parsed.circuit->get_node_id(node) - 1];
}

}  // namespace

TEST(NodeNames, SubcircuitPinsMatchTheBodyInAnyCase) {
    // The pins are written IN and OUT, the body's nodes in and out: they are
    // the pins, so X1 is a divider of two 1 kohm resistors (0.5 V; ngspice-47
    // gives 0.5 V, run on 2026-11-21)
    EXPECT_NEAR(expanded_op_voltage("t\nV1 a 0 1\nX1 a b DIV\n.subckt DIV IN OUT\n"
                                    "R1 in out 1k\nR2 out 0 1k\n.ends\n.end\n", "b"),
                0.5, 1e-12);
}

TEST(NodeNames, GlobalNodesMatchTheBodyInAnyCase) {
    // .GLOBAL VDD and a body that writes vdd: the body's resistor is on the
    // global node, so the divider gives 0.5 V
    EXPECT_NEAR(expanded_op_voltage("t\n.global VDD\nV1 vdd 0 1\nX1 b HALF\n.subckt HALF OUT\n"
                                    "R1 vdd out 1k\nR2 out 0 1k\n.ends\n.end\n", "b"),
                0.5, 1e-12);
}
