// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// How an instance line X<name> <nodes...> <type> [NAME=value ...] is read
// (split_instance_line) and expanded. ngspice-47 gives the same operating
// points, and stops on the same circular definitions (run on 2026-11-15).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

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

// Blanks may stand on either side of the '=' on an instance line and on the
// lines of a body (util::split_netlist_line); ngspice-47 draws 0.5 mA from V1
// and 1.25 mA from VD
TEST(SubcircuitInstances, AssignmentsWithBlanks) {
    const auto op = solve(
        "t\n.model nch nmos (vto=0.5 kp=1e-4)\n.subckt load a b rv=1k\nR1 a b {rv}\n.ends load\n"
        ".subckt cell d g\nM1 d g 0 0 nch W = 100u L =1u\n.ends cell\nV1 in 0 1\n"
        "X1 in 0 load rv = 2k\nVD d 0 1\nVG g 0 1\nX2 d g cell\n.op\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    const auto r = std::dynamic_pointer_cast<devices::Resistor>(op.parsed.circuit->get_device("X1.R1"));
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(r->resistance(), 2e3);
    const auto m = std::dynamic_pointer_cast<devices::MOSFET_N_Level1>(
        op.parsed.circuit->get_device("X2.M1"));
    ASSERT_NE(m, nullptr);
    EXPECT_DOUBLE_EQ(m->channel_width(), 100e-6);
    EXPECT_DOUBLE_EQ(m->channel_length(), 1e-6);
}

// ... and so is an instance name (SubcircuitRegistry::add_instance); the
// second X1 was not expanded. ngspice-47 stops with "device already exists".
TEST(SubcircuitInstances, InstanceNameUsedTwice) {
    const std::string message = parse_error(
        "t\n.subckt load a b\nR1 a b 1k\n.ends load\nV1 in 0 1\nV2 in2 0 1\n"
        "X1 in 0 load\nX1 in2 0 load\n.op\n.end\n");
    EXPECT_NE(message.find("already defined"), std::string::npos) << message;
}

namespace {

// V(out) at the operating point of a netlist: 1 V at in, 1 kOhm from out to
// ground, and the given lines either at the top level or as the body of a
// subcircuit with pins in and out
Real out_voltage(const std::string& lines, const std::string& models, bool in_subcircuit) {
    const std::string netlist = in_subcircuit
        ? "t\n" + models + ".subckt blk in out\n" + lines + ".ends blk\nX1 in out blk\n"
              "V1 in 0 DC 1\nRL out 0 1k\n.op\n.end\n"
        : "t\n" + models + lines + "V1 in 0 DC 1\nRL out 0 1k\n.op\n.end\n";
    const auto op = solve(netlist);
    EXPECT_EQ(op.status, ConvergenceStatus::SUCCESS);
    return op.voltage("out");
}

}  // namespace

// A body is read by the element parsers of the top level (SubcircuitLineReader,
// BodyLineReader): each element in it means what it means outside one, its
// own nodes and the elements it names renamed into the instance
TEST(SubcircuitInstances, BodyMeansWhatTheTopLevelMeans) {
    struct Case {
        std::string what, lines, models;
    };
    const Case cases[] = {
        {"diode", "R1 in n1 1k\nD1 n1 out dm\n", ".model dm D IS=1e-14\n"},
        {"E", "E1 out 0 in 0 2\n", ""},
        {"G", "G1 0 out in 0 1m\n", ""},
        {"F, its source in the body", "Vs in n1 0\nR1 n1 0 1k\nF1 0 out Vs 2\n", ""},
        {"H, its source in the body", "Vs in n1 0\nR1 n1 0 1k\nH1 out 0 Vs 500\n", ""},
        {"B with V(node)", "B1 out 0 V=2*V(in)\n", ""},
        {"B with an internal node", "R1 in n1 1k\nR2 n1 0 1k\nB1 out 0 V=4*V(n1)\n", ""},
        {"BJT", "Vcc n2 0 5\nRc n2 out 10k\nRb in nb 100k\nQ1 out nb 0 qm\n",
         ".model qm npn BF=100 IS=1e-15\n"},
        {"K", "L1 in out 1m\nL2 n1 0 1m\nK1 L1 L2 0.5\nR2 n1 0 1\n", ""},
        {"T", "T1 in 0 out 0 Z0=50 TD=1n\n", ""},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.what);
        const Real flat = out_voltage(c.lines, c.models, false);
        EXPECT_NEAR(out_voltage(c.lines, c.models, true), flat, 1e-9 * std::max(1.0, std::abs(flat)));
    }
}

// ... and a B source's expression sees the instance's parameters, in braces
// or not, beside its node voltages (substitute_behavioral_expression in
// SubcircuitDefinition::substitute_parameters). With V(in) = 0.25 V and g = 2,
// ngspice-45.2 gives 0.5 V for each of these bodies, and 1 V for g=4 on the X
// line; an internal node named as a parameter stays a node (run 2026-11-28).
// Before, braces were evaluated as a parameter expression ("Undefined symbol:
// 'V'") and a name without braces was left undefined.
TEST(SubcircuitInstances, BehavioralExpressionSeesTheInstanceParameters) {
    const std::string top = "V1 in 0 0.25\nR2 out 0 1k\n";
    const struct {
        std::string what, subckt, body, x_line;
        Real expected;
    } cases[] = {
        {"braces", ".subckt amp i o g=2", "B1 o 0 V={g*V(i)}", "X1 in out amp", 0.5},
        {"no braces", ".subckt amp i o g=2", "B1 o 0 V=g*V(i)", "X1 in out amp", 0.5},
        {"braces without parameters", ".subckt amp i o", "B1 o 0 V={2*V(i)}", "X1 in out amp", 0.5},
        {"value on the X line", ".subckt amp i o g=2", "B1 o 0 V={g*V(i)}", "X1 in out amp g=4", 1.0},
        {"body .param", ".subckt amp i o", ".param g=2\nB1 o 0 V={g*V(i)}", "X1 in out amp", 0.5},
        {"node named as a parameter", ".subckt amp i o g=2",
         "R9 i g 1k\nR8 g 0 1k\nB1 o 0 V={g*V(g)*2}", "X1 in out amp", 0.5},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.what);
        try {
            const auto op = solve("t\n" + c.subckt + "\n" + c.body + "\n.ends\n" + top + c.x_line + "\n.end\n");
            ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
            EXPECT_NEAR(op.voltage("out"), c.expected, 1e-12);
        } catch (const std::exception& e) {
            ADD_FAILURE() << e.what();
        }
    }
}

// ... a source's waveform too: SIN(0 1 1k) at 0.1 ms is sin(0.2 pi)
TEST(SubcircuitInstances, WaveformInABody) {
    const auto parsed = parse(
        "t\n.subckt src a b\nV1 a b 0 SIN(0 1 1k)\n.ends src\nX1 in 0 src\nR1 in 0 1k\n.end\n");
    SimulationConfig config = parsed.config;
    config.transient.use_adaptive_timestep = false;
    solvers::TransientSolver solver(*parsed.circuit, config);
    const auto result = solver.solve(0.0, 0.1e-3, 1e-6);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const auto& last = result.timepoints.back();
    EXPECT_NEAR(last.time, 0.1e-3, 1e-12);
    EXPECT_NEAR(last.node_voltages[parsed.circuit->get_node_id("in") - 1],
                std::sin(2.0 * M_PI * 1e3 * 0.1e-3), 1e-9);
}

// ... and a model that the netlist does not define is an error, as at the
// top level (ngspice-47: "could not find a valid modelname")
TEST(SubcircuitInstances, UndefinedModelInABody) {
    const std::string message = parse_error(
        "t\n.model nch nmos (vto=0.5 kp=1e-4)\n.subckt cell d g\nM1 d g 0 0 nhc W=10u L=1u\n"
        ".ends cell\nVD d 0 1\nVG g 0 1\nX1 d g cell\n.op\n.end\n");
    EXPECT_FALSE(message.empty());
}

// The keyword PARAMS: before the parameters of .SUBCKT and X lines is
// optional, as in ngspice-47: 1 V across 2k, 4k (PARAMS: in upper case) and
// the default 1k in parallel draws 1.75 mA there (run 2026-11-21); behind
// 1 kohm the node sits at 1 V x 0.5714k / 1.5714k. Before, PARAMS: was read
// as the subcircuit's type.
TEST(SubcircuitInstances, ParamsKeywordIsOptional) {
    const auto op = solve(
        "t\nV1 in 0 1\nRs in a 1k\nX1 a 0 load params: r=2k\nX2 a 0 load PARAMS: r=4k\nX3 a 0 load\n"
        ".subckt load p q params: r=1k\nR1 p q {r}\n.ends\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    const double parallel = 1.0 / (1.0 / 2e3 + 1.0 / 4e3 + 1.0 / 1e3);
    EXPECT_NEAR(op.voltage("a"), parallel / (parallel + 1e3), 1e-9);
}

// A .PARAM line in a subcircuit body that cannot be read in full is an
// error, as on a global .PARAM line and in ngspice-47 ("Missing }", run
// 2026-11-21); before, `.param x={1k` without its closing brace set
// nothing, without a message
TEST(SubcircuitInstances, UnreadableBodyParamIsAnError) {
    const std::string message = parse_error(
        "t\nV1 a 0 1\nX1 a 0 s\n.subckt s p q\n.param x={1k\nR1 p q 1k\n.ends\n.end\n");
    EXPECT_NE(message.find("Cannot read 'x={1k'"), std::string::npos) << message;
    EXPECT_EQ(parse_error("t\nV1 a 0 1\nX1 a 0 s\n.subckt s p q\n.param x=1k y={2*x}\nR1 p q {y}\n"
                          ".ends\n.end\n"),
              "");
}
