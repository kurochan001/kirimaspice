// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// How an instance line X<name> <nodes...> <type> [NAME=value ...] is read
// (split_instance_line) and expanded. ngspice-47 gives the same operating
// points, and stops on the same circular definitions (run on 2026-11-15).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

struct OperatingPoint {
    parser::NetlistParseResult parsed;
    ConvergenceStatus status = ConvergenceStatus::MAX_ITERATIONS;
    DenseVector solution;

    Real voltage(const std::string& node) const {
        return solution[parsed.circuit->get_node_id(node) - 1];
    }
};

// Parse and expand, as the program does (main.cpp)
parser::NetlistParseResult parse(const std::string& netlist) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    parsed.circuit->set_simulation_config(&parsed.config);
    parsed.circuit->expand_all_subcircuits();
    return parsed;
}

OperatingPoint solve(const std::string& netlist) {
    OperatingPoint op{parse(netlist)};
    op.parsed.circuit->set_simulation_config(&op.parsed.config);
    solvers::DCSolver solver(*op.parsed.circuit, op.parsed.config);
    SolverStats stats;
    op.status = solver.solve(op.solution, stats);
    return op;
}

// The message of the error that parsing and expanding the netlist throws, or
// "" if none
std::string parse_error(const std::string& netlist) {
    try {
        parse(netlist);
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

}  // namespace

// The type is the last token that is not an assignment; a node of a nested
// instance may have the name of a subcircuit. X2 divides 1 V by two 1 kOhm.
TEST(SubcircuitInstances, NodeNamedLikeASubcircuit) {
    const auto op = solve(
        "t\n.subckt load a b\nR1 a b 1k\n.ends load\n.subckt top in\nR2 in load 1k\n"
        "X2 load 0 load\n.ends top\nV1 in 0 1\nX1 in top\n.op\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(op.voltage("X1.load"), 0.5, 1e-9);
}

// A definition that holds an instance of itself never ends: it is a circular
// reference, found before any expansion
TEST(SubcircuitInstances, InstanceOfItselfIsCircular) {
    const std::string message = parse_error(
        "t\n.subckt a p\nR1 p 0 1k\nX1 p a\nX2 p a\n.ends a\nV1 in 0 1\nX0 in a\n.op\n.end\n");
    EXPECT_NE(message.find("Circular"), std::string::npos) << message;
}

// ... whatever the case of the instance line
TEST(SubcircuitInstances, LowerCaseInstanceInACycle) {
    const std::string message = parse_error(
        "t\n.subckt a p\nR1 p 0 1k\nx1 p b\n.ends a\n.subckt b p\nx1 p a\n.ends b\n"
        "V1 in 0 1\nX0 in a\n.op\n.end\n");
    EXPECT_NE(message.find("Circular"), std::string::npos) << message;
}

// The names of the assignments of a nested instance are not case-sensitive:
// rv=2k replaces the default rv=1k (ngspice-47 draws 0.5 mA from V1)
TEST(SubcircuitInstances, NestedInstanceParameterInLowerCase) {
    const auto op = solve(
        "t\n.subckt inner a b rv=1k\nR1 a b {rv}\n.ends inner\n.subckt outer a b\n"
        "X2 a b inner rv=2k\n.ends outer\nV1 in 0 1\nX1 in 0 outer\n.op\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    const auto r = std::dynamic_pointer_cast<devices::Resistor>(op.parsed.circuit->get_device("X1.X2.R1"));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(r->resistance(), 2e3);
}

// A .PARAM line in the body replaces a global value of the same name in
// another case, for the lines after it (SubcircuitDefinition::
// substitute_parameters); ngspice-47 draws 1 mA from V1
TEST(SubcircuitInstances, LocalParameterOverAGlobalOneInAnotherCase) {
    const auto op = solve(
        "t\n.param RV=5k\n.subckt load a b\n.param rv=1k\nR1 a b {rv}\n.ends load\n"
        "V1 in 0 1\nX1 in 0 load\n.op\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    const auto r = std::dynamic_pointer_cast<devices::Resistor>(op.parsed.circuit->get_device("X1.R1"));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(r->resistance(), 1e3);
}

// Braces know the constants pi and e wherever they are: in a .PARAM line, in
// an element value and in a subcircuit (REFERENCE_MANUAL, Expressions)
TEST(SubcircuitInstances, ConstantsInEveryExpression) {
    const auto op = solve(
        "t\n.param re={1000*e}\n.subckt load a b\nR1 a b {1000*e}\nR2 a b {1000*pi}\n"
        ".ends load\nV1 in 0 1\nX1 in 0 load\nR3 in 0 {re}\nR4 in 0 {1000*e}\n.op\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    const auto resistance = [&](const std::string& name) {
        const auto r = std::dynamic_pointer_cast<devices::Resistor>(op.parsed.circuit->get_device(name));
        return r ? r->resistance() : 0.0;
    };
    EXPECT_DOUBLE_EQ(resistance("X1.R1"), 1000 * 2.718281828459045);
    EXPECT_DOUBLE_EQ(resistance("X1.R2"), 1000 * 3.141592653589793);
    EXPECT_DOUBLE_EQ(resistance("R3"), 1000 * 2.718281828459045);
    EXPECT_DOUBLE_EQ(resistance("R4"), 1000 * 2.718281828459045);
}

// A .SUBCKT definition applies to the whole netlist wherever it stands, so an
// instance may come before it (process_line, UnresolvedDefinition); ngspice-47
// draws 1 mA from V1
TEST(SubcircuitInstances, InstanceBeforeItsDefinition) {
    const auto op = solve(
        "t\nV1 in 0 1\nX1 in 0 load\n.subckt load a b\nR1 a b 1k\n.ends load\n.op\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    const auto r = std::dynamic_pointer_cast<devices::Resistor>(op.parsed.circuit->get_device("X1.R1"));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(r->resistance(), 1e3);
}

// ... and an instance of a subcircuit that the netlist does not define is an
// error at the end
TEST(SubcircuitInstances, InstanceOfAnUndefinedSubcircuit) {
    const std::string message = parse_error("t\nV1 in 0 1\nX1 in 0 nosuch\n.op\n.end\n");
    EXPECT_NE(message.find("nosuch"), std::string::npos) << message;
}
