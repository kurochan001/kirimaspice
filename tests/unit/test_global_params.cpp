// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The global .PARAM lines are read before every other line, as ngspice-47
// reads them (read_global_definitions; run 2026-11-21): 1 V across R1 gives
// these currents there. Before, a parameter defined after its element was
// an undefined symbol, the second definition of a name was ignored with a
// warning, and an expression took the values of the lines before it.

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double current(const std::string& netlist, const std::unordered_map<std::string, Real>& initial = {}) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist, initial);
    parsed.circuit->expand_all_subcircuits();
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    for (std::uint32_t i = 0; i < x.size(); ++i) {
        if (parsed.circuit->unknown_name(i) == "I(V1)") {
            return -x[i];
        }
    }
    ADD_FAILURE() << "no I(V1)";
    return 0.0;
}

}  // namespace

TEST(GlobalParams, ReadBeforeTheElements) {
    // Defined after its use: r = 2k
    EXPECT_NEAR(current("t\nV1 a 0 1\nR1 a 0 {r}\n.param r=2k\n.end\n"), 0.5e-3, 1e-12);
    // The last of two definitions, also for the element between them: 4k
    EXPECT_NEAR(current("t\n.param r=1k\nV1 a 0 1\nR1 a 0 {r}\n.param r=4k\n.end\n"), 0.25e-3, 1e-12);
}

TEST(GlobalParams, ExpressionsTakeTheFinalValues) {
    // b = {a*2} with a's last definition, 3k: 6k
    EXPECT_NEAR(current("t\n.param a=1k\n.param b={a*2}\n.param a=3k\nV1 a 0 1\nR1 a 0 {b}\n.end\n"),
                1.0 / 6e3, 1e-12);
    // b uses a defined after it: 2k
    EXPECT_NEAR(current("t\n.param b={a*2}\n.param a=1k\nV1 a 0 1\nR1 a 0 {b}\n.end\n"), 0.5e-3, 1e-12);
    // On one line, then a redefined: 6k
    EXPECT_NEAR(current("t\n.param a=1k b={a*2}\nV1 a 0 1\nR1 a 0 {b}\n.param a=3k\n.end\n"), 1.0 / 6e3,
                1e-12);
    // A .FUNC after the .PARAM that calls it
    EXPECT_NEAR(current("t\n.param r={dbl(1k)}\n.func dbl(x) {2*x}\nV1 a 0 1\nR1 a 0 {r}\n.end\n"), 0.5e-3,
                1e-12);
}

TEST(GlobalParams, UnsolvedDefinitionsAreErrors) {
    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string("t\n.param a={b} b={a}\nV1 a 0 1\nR1 a 0 {a}\n.end\n"), parser::ParseError);
    EXPECT_THROW(parser.parse_string("t\n.param a={zz*2}\nV1 a 0 1\nR1 a 0 {a}\n.end\n"), parser::ParseError);
}

TEST(GlobalParams, ValuesSetBeforeParsingAreKept) {
    // A sweep's value (initial_params) is kept over the netlist's lines
    EXPECT_NEAR(current("t\n.param r=1k\nV1 a 0 1\nR1 a 0 {r}\n.param r=4k\n.end\n", {{"r", 5e3}}), 0.2e-3,
                1e-12);
}

// ngspice-47 (2026-11-23) selects the resistor using the final global
// parameter values, even when their definitions follow the conditional.
TEST(GlobalParams, IfAndElseifUseTheFinalValues) {
    const std::string netlist =
        "t\nV1 a 0 1\n.if (mode == 2)\nR1 a 0 1k\n.elseif (mode == 1)\nR2 a 0 2k\n"
        ".else\nR3 a 0 4k\n.endif\n.param mode={pick-1}\n.param pick=2\n.end\n";
    EXPECT_NEAR(current(netlist), 0.5e-3, 1e-12);
    EXPECT_NEAR(current(netlist, {{"PICK", 3}}), 1e-3, 1e-12);
    EXPECT_NEAR(current(netlist, {{"pick", 1}}), 0.25e-3, 1e-12);
}

// ngspice-47 reads global definitions before selecting the elements of a
// branch. The last definition of rv therefore supplies the selected R1.
TEST(GlobalParams, GlobalDefinitionsPrecedeBranchSelection) {
    EXPECT_NEAR(current("t\nV1 a 0 1\n.param flag=1\n.if (flag)\n.param rv=2k\nR1 a 0 {rv}\n"
                        ".else\n.param rv=4k\nR2 a 0 1k\n.endif\n.end\n"), 0.25e-3, 1e-12);
}

TEST(GlobalParams, ConditionsOnlySeeGlobalNetlistParameters) {
    parser::NetlistParser parser;
    const std::string conditional = ".if (flag)\nR1 a 0 1k\n.else\nR2 a 0 2k\n.endif\n.end\n";
    for (const std::string scope : {"", ".subckt load x\n.param flag=1\n.ends\n",
                                    ".control\n.param flag=1\n.endc\n"}) {
        EXPECT_THROW(parser.parse_string("t\nV1 a 0 1\n" + scope + conditional), parser::ParseError)
            << scope;
    }
}

// A .FUNC can be called in every expression in braces, wherever its line is:
// an element value, an .IF condition, and in a subcircuit the defaults of the
// .SUBCKT line, the values of the X line, a .PARAM line of the body and an
// element value of the body (read_global_definitions, util::ExpressionFunctions,
// SubcircuitDefinition::substitute_parameters), and the expression of a B
// source (CircuitGraph::add_behavioral_source). With half(x) = x/2 and 1 V
// across half(4k) = 2 kOhm, or a B source of half(V(a))/1k, ngspice-45.2
// gives 0.5 mA (run 2026-11-28). Before, an element value calling a function
// was "Expression parse error", and a body's braces and a B source
// "Undefined symbol".
TEST(GlobalParams, FunctionsInEveryExpression) {
    const std::string half = ".func half(x) {x/2}\n";
    const std::string blk_tail = ".ends\nV1 a 0 1\n";
    const std::map<std::string, std::string> netlists = {
        {"element", "t\n" + half + "V1 a 0 1\nR1 a 0 {half(4k)}\n.end\n"},
        {"element before .func", "t\nV1 a 0 1\nR1 a 0 {half(4k)}\n" + half + ".end\n"},
        {"two arguments", "t\n.func par(x,y) {x*y/(x+y)}\nV1 a 0 1\nR1 a 0 {par(4k,4k)}\n.end\n"},
        {"three arguments", "t\n.func f(x,y,z) {x+y-z}\nV1 a 0 1\nR1 a 0 {f(1k,2k,1k)}\n.end\n"},
        {".if condition", "t\n" + half + ".param p=4\nV1 a 0 1\n.if (half(p) == 2)\n"
                          "R1 a 0 2k\n.else\nR1 a 0 1k\n.endif\n.end\n"},
        {"body element", "t\n" + half + ".subckt blk n\nR1 n 0 {half(4k)}\n" + blk_tail +
                             "X1 a blk\n.end\n"},
        {"body .param", "t\n" + half + ".subckt blk n\n.param rb={half(4k)}\nR1 n 0 {rb}\n" +
                            blk_tail + "X1 a blk\n.end\n"},
        {".subckt default", "t\n" + half + ".subckt blk n r={half(4k)}\nR1 n 0 {r}\n" + blk_tail +
                                "X1 a blk\n.end\n"},
        {"x line value", "t\n" + half + ".subckt blk n r=1k\nR1 n 0 {r}\n" + blk_tail +
                             "X1 a blk r={half(4k)}\n.end\n"},
        {"b source", "t\n" + half + "V1 a 0 1\nB1 a 0 I={half(V(a))/1k}\n.end\n"},
        {"b source in a body", "t\n" + half + ".subckt blk n\nB1 n 0 I=half(V(n))/1k\n" +
                                   blk_tail + "X1 a blk\n.end\n"},
    };
    for (const auto& [where, netlist] : netlists) {
        try {
            EXPECT_NEAR(current(netlist), 0.5e-3, 1e-12) << where;
        } catch (const std::exception& e) {
            ADD_FAILURE() << where << ": " << e.what();
        }
    }
}

// A .FUNC body sees the parameters of the scope it is called in and the other
// functions, wherever they are defined; an argument hides a parameter of its
// name (util::ExpressionFunction). ngspice-45.2 gives these currents for 1 V
// (run 2026-11-28): in a subcircuit with k=2, f(8k) = 8k/k is 4k although the
// global k is 4, also when a B expression of the body calls f.
TEST(GlobalParams, FunctionBodiesSeeParametersAndFunctions) {
    const struct {
        std::string what, netlist;
        Real expected;
    } cases[] = {
        {"global parameter", "t\n.param k=4\n.func f(x) {x/k}\nV1 a 0 1\nR1 a 0 {f(8k)}\n.end\n", 0.5e-3},
        {"parameter defined later", "t\n.func f(x) {x/k}\nV1 a 0 1\nR1 a 0 {f(8k)}\n.param k=4\n.end\n", 0.5e-3},
        {"in a .param", "t\n.param k=4\n.func f(x) {x/k}\n.param r={f(8k)}\nV1 a 0 1\nR1 a 0 {r}\n.end\n",
         0.5e-3},
        {"argument hides a parameter", "t\n.param x=100\n.func f(x) {x/2}\nV1 a 0 1\nR1 a 0 {f(4k)}\n.end\n",
         0.5e-3},
        {"another function, defined later",
         "t\n.func g(x) {h(x)*2}\n.func h(x) {x/4}\nV1 a 0 1\nR1 a 0 {g(4k)}\n.end\n", 0.5e-3},
        {"instance parameter", "t\n.param k=4\n.func f(x) {x/k}\n.subckt blk n k=2\nR1 n 0 {f(8k)}\n.ends\n"
                               "V1 a 0 1\nX1 a blk\n.end\n", 0.25e-3},
        {"b source", "t\n.param k=2\n.func f(x) {x/k}\nV1 a 0 1\nB1 a 0 I={f(V(a))/1k}\n.end\n", 0.5e-3},
        {"b source in a body", "t\n.param k=4\n.func f(x) {x/k}\n.subckt blk n k=2\nB1 n 0 I={f(V(n))/1k}\n"
                               ".ends\nV1 a 0 1\nX1 a blk\n.end\n", 0.5e-3},
    };
    for (const auto& c : cases) {
        try {
            EXPECT_NEAR(current(c.netlist), c.expected, 1e-12) << c.what;
        } catch (const std::exception& e) {
            ADD_FAILURE() << c.what << ": " << e.what();
        }
    }
}

// A function that calls itself is an error that names it; ngspice-45.2 stops
// with a crash on .func f(x) {f(x)+1} (run 2026-11-28)
TEST(GlobalParams, FunctionThatCallsItselfIsAnError) {
    parser::NetlistParser parser;
    try {
        parser.parse_string("t\n.func f(x) {f(x)+1}\nV1 a 0 1\nR1 a 0 {f(4k)}\n.end\n");
        ADD_FAILURE() << "no error";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find(".FUNC f nests more than"), std::string::npos) << e.what();
    }
}
