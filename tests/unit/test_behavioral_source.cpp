// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/devices/behavioral_source.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using kirimaspice::devices::BehavioralSource;
using kirimaspice::BehavioralSourceType;
using kirimaspice::DenseVector;
using kirimaspice::MNASystem;
using kirimaspice::NodeId;
using kirimaspice::Real;

namespace {

MNASystem make_mna(std::uint32_t nodes, std::uint32_t branches = 0) {
    MNASystem mna;
    mna.resize(nodes, branches);
    mna.zero();
    return mna;
}

DenseVector make_voltages(std::uint32_t nodes) {
    return DenseVector(nodes, 0.0);
}

} // namespace

TEST(BehavioralSource, NullEvaluatorReturnsZero) {
    // CURRENT source with null evaluator should fall back to zero without throwing.
    BehavioralSource src(/*id*/1, "BNULL",
                         /*np*/1, /*nn*/0,
                         BehavioralSourceType::CURRENT,
                         "I=TIME", nullptr);

    auto mna = make_mna(/*nodes*/1);
    auto v = make_voltages(1);

    src.stamp_dc_nonlinear(mna, v);

    // No entries should be added because evaluated value is 0.
    EXPECT_EQ(mna.b[0], 0.0);
    EXPECT_TRUE(mna.G.nnz() == 0);
}

TEST(BehavioralSource, ExceptionEvaluatorFallsBackToZero) {
    auto throwing_eval = [](Real /*t*/, const DenseVector& /*v*/) -> Real {
        throw std::runtime_error("boom");
    };

    BehavioralSource src(/*id*/2, "BEXC",
                         /*np*/1, /*nn*/0,
                         BehavioralSourceType::CURRENT,
                         "I=bad()", throwing_eval);

    auto mna = make_mna(/*nodes*/1);
    auto v = make_voltages(1);

    src.stamp_dc_nonlinear(mna, v);

    EXPECT_EQ(mna.b[0], 0.0);
    EXPECT_TRUE(mna.G.nnz() == 0);
}

namespace {

// B1 out 0 V={2*V(in)} with V(in) = 1 V: V(out) = 2 V, and the source
// needs a branch current variable of its own.
constexpr const char* kVoltageModeNetlist = R"(Voltage-mode B source
V1 in 0 DC 1
R1 in 0 1k
B1 out 0 V={2*V(in)}
R2 out 0 1k
.end
)";

} // namespace

TEST(BehavioralSource, VoltageModeInOperatingPoint) {
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(kVoltageModeNetlist);
    ASSERT_NE(result.circuit, nullptr);

    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    kirimaspice::SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    EXPECT_EQ(solution.size(), 4u);  // V(in), V(out), I(V1), I(B1)
    const NodeId out = result.circuit->get_node_id("out");
    EXPECT_NEAR(solution[out - 1], 2.0, 1e-9);
}

TEST(BehavioralSource, VoltageModeInTransient) {
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(kVoltageModeNetlist);
    ASSERT_NE(result.circuit, nullptr);

    kirimaspice::solvers::TransientSolver solver(*result.circuit, result.config);
    auto tran = solver.solve(0.0, 5e-6, 1e-6);
    ASSERT_EQ(tran.status, kirimaspice::ConvergenceStatus::SUCCESS);
    ASSERT_FALSE(tran.timepoints.empty());
    const NodeId out = result.circuit->get_node_id("out");
    EXPECT_NEAR(tran.timepoints.back().node_voltages[out - 1], 2.0, 1e-9);
}

// v(node) in lower case names a node voltage too: B1 0 out I={0.001*v(in)}
// with V(in) = 1 V drives 1 mA into 1 kOhm, so V(out) = 1 V.
TEST(BehavioralSource, LowerCaseNodeVoltage) {
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(R"(Lower-case v()
V1 in 0 DC 1
R1 in 0 1k
B1 0 out I={0.001*v(in)}
R2 out 0 1k
.end
)");
    ASSERT_NE(result.circuit, nullptr);

    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    kirimaspice::SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    const NodeId out = result.circuit->get_node_id("out");
    EXPECT_NEAR(solution[out - 1], 1.0, 1e-9);
}

// An expression that cannot be compiled is an error in the netlist, given
// at the B line, not a source of 0.
TEST(BehavioralSource, ExpressionThatDoesNotCompileIsAParseError) {
    kirimaspice::parser::NetlistParser parser;
    try {
        parser.parse_string(R"(Expression without its last operand
V1 in 0 DC 1
R1 in 0 1k
B1 0 out I={0.001*V(in)+}
R2 out 0 1k
.end
)");
        FAIL() << "expected a ParseError";
    } catch (const kirimaspice::parser::ParseError& e) {
        EXPECT_EQ(e.line(), 4u);
    }
}

// TIME is the time of the transient (as in SPICE's B source): with
// V(out) = 2 V(in) + 1000 TIME and V(in) = 1 V, V(out) = 2 + 1000 t at every
// time point, also in a circuit with a capacitor, whose transient starts from
// an operating point at time 0. A second transient on the same devices starts
// again from time 0.
TEST(BehavioralSource, TimeIsTheTransientTime) {
    for (const char* extra : {"", "C1 out 0 1n\n"}) {
        kirimaspice::parser::NetlistParser parser;
        auto result = parser.parse_string(std::string("TIME\nV1 in 0 DC 1\n"
                                                      "B1 out 0 V={2*V(in)+1000*TIME}\n"
                                                      "R1 out 0 1k\n") +
                                          extra + ".end\n");
        ASSERT_NE(result.circuit, nullptr);
        const NodeId out = result.circuit->get_node_id("out");
        for (int run = 0; run < 2; ++run) {
            kirimaspice::solvers::TransientSolver solver(*result.circuit, result.config);
            auto tran = solver.solve(0.0, 1e-3, 1e-4);
            ASSERT_EQ(tran.status, kirimaspice::ConvergenceStatus::SUCCESS) << extra;
            ASSERT_GE(tran.timepoints.size(), 11u);
            for (const auto& tp : tran.timepoints) {
                EXPECT_NEAR(tp.node_voltages[out - 1], 2.0 + 1000.0 * tp.time, 1e-9)
                    << extra << "run " << run << " t=" << tp.time;
            }
        }
    }
}

namespace {

// V(o) at the operating point of a netlist that drives R1 from o with a B
// source
Real b_output(const std::string& netlist) {
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector x;
    kirimaspice::SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    return x.empty() ? 0.0 : x[result.circuit->get_node_id("o") - 1];
}

}  // namespace

// V(node) reads a node by its name as every element line does: node names
// are compared without case, and may hold '.' and '-' (rewrite_node_voltages);
// ngspice-47 gives 2 V for the first netlist and 3 V for the second
TEST(BehavioralSource, NodeNamesInVoltages) {
    EXPECT_NEAR(b_output("t\nV1 a 0 1\nB1 o 0 V=2*V(A)\nR1 o 0 1k\n.op\n.end\n"), 2.0, 1e-9);
    EXPECT_NEAR(b_output("t\nV1 n.1 0 1\nV2 n-2 0 2\nB1 o 0 V=V(n.1)+V(n-2)\nR1 o 0 1k\n.op\n.end\n"),
                3.0, 1e-9);
}

// Newton's linearization of a B source takes the derivatives of its
// expression (BehavioralSource::linearize): I1 feeds 1 mA into o, and B1
// draws 1e-3 V(o)^2 out of it across 1 MOhm, so V^2 + 1e-3 V - 1 = 0. With
// the value alone, each iteration would move V by 1e6 times the change of
// the current. ngspice-47 gives V(o) = 0.9995001 V.
TEST(BehavioralSource, NewtonTakesTheDerivatives) {
    const Real expected = (-1e-3 + std::sqrt(1e-6 + 4.0)) / 2.0;
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(
        "t\nI1 0 o 1m\nB1 o 0 I=1e-3*V(o)*V(o)\nR1 o 0 1Meg\n.op\n.end\n");
    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector x;
    kirimaspice::SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[result.circuit->get_node_id("o") - 1], expected, 1e-6);
}

// In AC a B source is its linearization at the operating point: V(out) =
// 2 V(in)^2 + V(in) has the slope 5 at V(in) = 1 V, and 1e-3 V(in)^2 drawn
// from o2 across 1 kOhm gives -2; ngspice-47 gives the same
TEST(BehavioralSource, AcIsTheSlopeAtTheOperatingPoint) {
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(
        "t\nV1 in 0 DC 1 AC 1\nB1 out 0 V=2*V(in)*V(in)+V(in)\nR1 out 0 1k\n"
        "B2 o2 0 I=1e-3*V(in)*V(in)\nR2 o2 0 1k\n.ac lin 1 1k 1k\n.end\n");
    kirimaspice::solvers::ACSolver solver(*result.circuit, result.config);
    const auto ac = solver.solve();
    ASSERT_EQ(ac.status, kirimaspice::ConvergenceStatus::SUCCESS);
    ASSERT_EQ(ac.frequency_points.size(), 1u);
    const auto& v = ac.frequency_points[0].solution;
    EXPECT_NEAR(std::abs(v[result.circuit->get_node_id("out") - 1] - kirimaspice::Complex(5.0, 0.0)),
                0.0, 1e-6);
    EXPECT_NEAR(std::abs(v[result.circuit->get_node_id("o2") - 1] - kirimaspice::Complex(-2.0, 0.0)),
                0.0, 1e-6);
}

TEST(BehavioralSource, VoltageBetweenTwoNodes) {
    // V(a,b) is V(a) - V(b), as in SPICE: 2 * (3 V - 1 V) = 4 V (ngspice-47
    // gives v(c) = 4, run 2026-11-21); V(a, 0) names ground as well
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(
        "t\nVA a 0 3\nVB b 0 1\nB1 c 0 V=2*V(a,b)\nR1 c 0 1k\nB2 d 0 V={V(a, 0) + 1}\nR2 d 0 1k\n.end\n");
    ASSERT_NE(result.circuit, nullptr);
    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    kirimaspice::SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(solution[result.circuit->get_node_id("c") - 1], 4.0, 1e-9);
    EXPECT_NEAR(solution[result.circuit->get_node_id("d") - 1], 4.0, 1e-9);
    EXPECT_FALSE(result.circuit->has_node("a,b"));
}

TEST(BehavioralSource, VoltageBetweenTwoNodesInsideASubcircuit) {
    // Both nodes of V(p,m) are renamed into the instance: p is the pin on a
    // (3 V) and m the midpoint of two 1 kohm resistors (1.5 V), so the
    // output is 2 * 1.5 V = 3 V (ngspice-47 gives v(c) = 3, run 2026-11-21)
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(
        "t\nVA a 0 3\nX1 a c SUB\n.subckt SUB p o\nR9 p m 1k\nR8 m 0 1k\nB1 o 0 V=2*V(p,m)\n.ends\n"
        "R1 c 0 1k\n.end\n");
    ASSERT_NE(result.circuit, nullptr);
    result.circuit->expand_all_subcircuits();
    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    kirimaspice::SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(solution[result.circuit->get_node_id("c") - 1], 3.0, 1e-9);
}

// A B source reads the global .PARAM names, in any case, as ngspice-47
// does; the argument of V( is a node, also one with a parameter's name.
// With .param g=2 a=0.5 k=1m and V(a) = 1 V, V={g*V(a)+a*sin(0)+k*1000}
// gives 3 V and V={exp(A)*1e-1} 0.164872 V in ngspice-47 (run
// 2026-11-21). Before, the names were undefined symbols.
TEST(BehavioralSourceParams, GlobalParamsAreRead) {
    kirimaspice::parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.param g=2 a=0.5 k=1m\nV1 a 0 1\nB1 b 0 V={g*V(a)+a*sin(0)+k*1000}\nR1 b 0 1k\n"
        "B2 c 0 V={exp(A)*1e-1}\nR2 c 0 1k\n.end\n");
    kirimaspice::solvers::DCSolver solver(*parsed.circuit, parsed.config);
    kirimaspice::DenseVector x;
    kirimaspice::SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("b") - 1], 3.0, 1e-9);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("c") - 1], 0.1 * std::exp(0.5), 1e-9);
}

// I(<source>) in a B expression is the current of a source whose current is
// an unknown of the circuit (V, E, H, voltage-form B), with the sign of the
// printed I(<source>): positive into the + terminal (NETLIST_CONVENTIONS,
// section 3). The source may come after the B line, and Newton and AC take
// the derivative with respect to that current (BehavioralSource::linearize).
// ngspice-45.2 gives these V(o) (run 2026-11-28): V1 drives 1 mA into 1 kOhm,
// so I(V1) = -1 mA.
TEST(BehavioralSource, CurrentOfASource) {
    const struct {
        std::string what, netlist;
        Real expected;
    } cases[] = {
        {"voltage source", "t\nV1 a 0 1\nR1 a 0 1k\nB1 o 0 V={I(V1)*1k}\nR2 o 0 1k\n.end\n", -1.0},
        {"defined after", "t\nB1 o 0 V={I(v1)*1k}\nR2 o 0 1k\nV1 a 0 1\nR1 a 0 1k\n.end\n", -1.0},
        {"current form", "t\nV1 a 0 1\nR1 a 0 1k\nB1 0 o I={I(V1)*2}\nR2 o 0 1k\n.end\n", -2.0},
        {"nonlinear", "t\nV1 a 0 1\nR1 a 0 1k\nB1 o 0 V={I(V1)*I(V1)*1e6}\nR2 o 0 1k\n.end\n", 1.0},
        {"E", "t\nV1 a 0 1\nE1 d 0 a 0 2\nR3 d 0 1k\nB1 o 0 V={I(E1)*1k}\nR2 o 0 1k\n.end\n", -2.0},
        {"H", "t\nV1 a 0 1\nVs a c 0\nR1 c 0 1k\nH1 d 0 Vs 1k\nR3 d 0 1k\nB1 o 0 V={I(H1)*1k}\nR2 o 0 1k\n.end\n",
         -1.0},
        {"voltage-form B", "t\nV1 a 0 1\nB2 d 0 V={2*V(a)}\nR3 d 0 1k\nB1 o 0 V={I(B2)*1k}\nR2 o 0 1k\n.end\n", -2.0},
        {"its own current", "t\nB1 o 0 V={1 + I(B1)*500}\nR2 o 0 1k\n.end\n", 2.0 / 3.0},
    };
    for (const auto& c : cases) {
        EXPECT_NEAR(b_output(c.netlist), c.expected, 1e-9) << c.what;
    }
}

// ... inside a subcircuit I(Vs) is the body's own Vs (rename_body_line), as
// the controlling source of F and H is; ngspice-45.2 gives V(out) = 1 V
TEST(BehavioralSource, CurrentOfASourceInABody) {
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(
        "t\nV1 in 0 1\n.subckt s a o\nVs a m 0\nR1 m 0 1k\nB1 o 0 V={I(Vs)*1k}\n.ends\n"
        "X1 in out s\nR2 out 0 1k\n.end\n");
    result.circuit->expand_all_subcircuits();
    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector x;
    kirimaspice::SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[result.circuit->get_node_id("out") - 1], 1.0, 1e-9);
}

// ... in AC the slope at the operating point: d(1e6 I^2)/dI = 2e6 I = -2000
// V/A at I(V1) = -1 mA, times the small-signal I(V1) = -1 mA, gives 2 V, as
// ngspice-45.2 gives; and in the transient I(V1) follows SIN(0 1 1k):
// V(o) = -sin(2 pi 1k t)
TEST(BehavioralSource, CurrentOfASourceInAcAndTransient) {
    kirimaspice::parser::NetlistParser parser;
    auto ac_netlist = parser.parse_string(
        "t\nV1 a 0 DC 1 AC 1\nR1 a 0 1k\nB1 o 0 V={I(V1)*I(V1)*1e6}\nR2 o 0 1k\n.ac lin 1 1k 1k\n.end\n");
    kirimaspice::solvers::ACSolver ac_solver(*ac_netlist.circuit, ac_netlist.config);
    const auto ac = ac_solver.solve();
    ASSERT_EQ(ac.status, kirimaspice::ConvergenceStatus::SUCCESS);
    ASSERT_EQ(ac.frequency_points.size(), 1u);
    EXPECT_NEAR(std::abs(ac.frequency_points[0].solution[ac_netlist.circuit->get_node_id("o") - 1] -
                         kirimaspice::Complex(2.0, 0.0)),
                0.0, 1e-6);

    auto tran_netlist = parser.parse_string(
        "t\nV1 a 0 SIN(0 1 1k)\nR1 a 0 1k\nB1 o 0 V={I(V1)*1k}\nR2 o 0 1k\n.end\n");
    kirimaspice::SimulationConfig config = tran_netlist.config;
    config.transient.use_adaptive_timestep = false;
    kirimaspice::solvers::TransientSolver tran_solver(*tran_netlist.circuit, config);
    const auto tran = tran_solver.solve(0.0, 1e-3, 1e-5);
    ASSERT_EQ(tran.status, kirimaspice::ConvergenceStatus::SUCCESS);
    const auto o = tran_netlist.circuit->get_node_id("o") - 1;
    for (const auto& point : tran.timepoints) {
        EXPECT_NEAR(point.node_voltages[o], -std::sin(2.0 * M_PI * 1e3 * point.time), 1e-9) << point.time;
    }
}

// ... and naming a K line, which has no terminal, or a name that no element
// has, is an error that says so
TEST(BehavioralSource, CurrentOfAKLineOrOfNoElementIsAnError) {
    kirimaspice::parser::NetlistParser parser;
    for (const auto& [name, message] : std::initializer_list<std::pair<std::string, std::string>>{
             {"K1", "K1 is not one"}, {"V9", "no element is named V9"}}) {
        try {
            parser.parse_string(
                "t\nV1 a 0 1\nR1 a m 1k\nL1 m 0 1m\nL2 c 0 1m\nR3 c 0 1k\nK1 L1 L2 0.5\n"
                "B1 o 0 V={I(" +
                name + ")*1k}\nR2 o 0 1k\n.end\n");
            ADD_FAILURE() << name << ": no error";
        } catch (const std::exception& e) {
            EXPECT_NE(std::string(e.what()).find(message), std::string::npos) << e.what();
        }
    }
}

// A .FUNC that fails when a B expression calls it, because its body cannot
// be compiled or because it calls itself, is an error at the B line
// (CircuitGraph::add_behavioral_source evaluates the expression once), not
// a source of 0 with a message at every evaluation
TEST(BehavioralSource, FunctionThatFailsIsAnErrorOfTheLine) {
    for (const std::string body : {"{f(x)+1}", "{x*undefined_name}"}) {
        kirimaspice::parser::NetlistParser parser;
        try {
            parser.parse_string("t\n.func f(x) " + body + "\nV1 a 0 1\nB1 a 0 I={f(V(a))/1k}\n.end\n");
            ADD_FAILURE() << body << ": no error";
        } catch (const kirimaspice::parser::ParseError& e) {
            EXPECT_EQ(e.line(), 4u) << body;
            EXPECT_NE(std::string(e.what()).find("cannot evaluate the expression"), std::string::npos)
                << e.what();
        }
    }
}
