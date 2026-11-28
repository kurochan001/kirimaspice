// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/devices/behavioral_source.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
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

namespace {

// The operating point of a netlist, its subcircuits expanded, by node name
struct OperatingPoint {
    std::unique_ptr<kirimaspice::circuit::CircuitGraph> circuit;
    DenseVector x;
    Real v(const std::string& node) const {
        return x[circuit->get_node_id(node) - 1];
    }
};

OperatingPoint operating_point(const std::string& netlist) {
    kirimaspice::parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.circuit->expand_all_subcircuits();
    kirimaspice::solvers::DCSolver solver(*result.circuit, result.config);
    OperatingPoint op;
    kirimaspice::SolverStats stats;
    EXPECT_EQ(solver.solve(op.x, stats), kirimaspice::ConvergenceStatus::SUCCESS);
    op.circuit = std::move(result.circuit);
    return op;
}

}  // namespace

// I(<name>) of an element without a branch current of its own is the current
// into the element's first terminal: the parser puts a 0 V source in series
// with that terminal and the expression reads its current
// (read_current_probes), as ngspice-45.2 does. ngspice-45.2 gives these
// V(o) = 1 kOhm I(<name>) (run 2026-11-28): the current from the first node
// to the second of R, L, D and the current sources, the collector current of
// Q, the drain current of M and J, the current into port A of a line and
// into the first pin of an X. The element may come after the B line, in
// either case, and its model after both.
TEST(BehavioralSource, CurrentOfAnElement) {
    const std::string read = "\nR2 o 0 1k\n.end\n";
    const struct {
        std::string what, lines;
        Real expected;
    } cases[] = {
        {"resistor", "V1 a 0 1\nR1 a 0 1k\nB1 o 0 V={I(R1)*1k}", 1.0},
        {"resistor, its nodes exchanged", "V1 a 0 1\nR1 0 a 1k\nB1 o 0 V={I(R1)*1k}", -1.0},
        {"named before its line, in lower case", "B1 o 0 V={I(r1)*1k}\nV1 a 0 1\nR1 a 0 1k", 1.0},
        {"without braces", "V1 a 0 1\nR1 a 0 1k\nB1 o 0 V=I(R1)*1k", 1.0},
        {"without braces, with blanks", "V1 a 0 1\nR1 a 0 1k\nB1 o 0 V=I(R1) * 1k + I( R1 )*1k",
         2.0},
        {"capacitor in DC", "V1 a 0 1\nR0 a m 1k\nC1 m 0 1u\nB1 o 0 V={I(C1)*1k}", 0.0},
        {"inductor", "V1 a 0 1\nR0 a m 1k\nL1 m 0 1m\nB1 o 0 V={I(L1)*1k}", 1.0},
        {"diode", "V1 a 0 1\nR0 a m 1k\nD1 m 0 dm\n.model dm D\nB1 o 0 V={I(D1)*1k}", 0.3705592},
        {"diode, its model after it",
         "V1 a 0 1\nR0 a m 1k\nD1 m 0 dm\nB1 o 0 V={I(D1)*1k}\n.model dm D", 0.3705592},
        {"current source", "I1 0 m 1m\nR0 m 0 1k\nB1 o 0 V={I(I1)*1k}", 1.0},
        {"G", "V1 a 0 1\nG1 0 m a 0 1m\nR0 m 0 1k\nB1 o 0 V={I(G1)*1k}", 1.0},
        {"F", "V1 a 0 1\nVs a s 0\nRs s 0 1k\nF1 0 m Vs 2\nR0 m 0 1k\nB1 o 0 V={I(F1)*1k}", 2.0},
        {"current-form B", "V1 a 0 1\nB2 0 m I={1m*V(a)}\nR0 m 0 1k\nB1 o 0 V={I(B2)*1k}", 1.0},
        {"bipolar transistor",
         "V1 a 0 1\nRb a bb 100k\nQ1 c bb 0 qm\nVc cc 0 5\nRc cc c 1k\n.model qm NPN IS=1e-16 "
         "BF=100\n"
         "B1 o 0 V={I(Q1)*1k}",
         0.2605556},
        {"MOSFET",
         "V1 a 0 1\nM1 d a 0 0 mm W=10u L=1u\nVd dd 0 5\nRd dd d 1k\n"
         ".model mm NMOS LEVEL=1 VTO=0.5 KP=1e-4\nB1 o 0 V={I(M1)*1k}",
         0.125},
        {"JFET",
         "V1 a 0 1\nRg a g 1k\nJ1 d g 0 jm\nVd dd 0 5\nRd dd d 1k\n.model jm NJF\nB1 o 0 "
         "V={I(J1)*1k}",
         0.6913958},
        {"lossless line",
         "V1 a 0 1\nR0 a m 1k\nT1 m 0 c 0 Z0=50 TD=1n\nRc c 0 1k\nB1 o 0 V={I(T1)*1k}", 0.5},
        {"subcircuit instance",
         "V1 a 0 1\nX1 a 0 load\n.subckt load p q\nRl p q 1k\n.ends\nB1 o 0 V={I(X1)*1k}", 1.0},
        {"read by a current-form B", "V1 a 0 1\nR1 a 0 1k\nB1 0 o I={I(R1)*2}", 2.0},
    };
    for (const auto& c : cases) {
        // 2e-5 as the device models agree with ngspice; the linear cases are exact
        EXPECT_NEAR(operating_point("t\n" + c.lines + read).v("o"), c.expected,
                    2e-5 * std::max(1.0, c.expected))
            << c.what;
    }
}

// ... for every element with terminals: the current into the first terminal
// is the current of the resistor that alone feeds it, Rs between a and n, so
// V(o) = 1 kOhm I(<name>) = V(a) - V(n). A MESFET and a lossy line carry
// current here; the SCR and the TRIAC are off and carry none.
TEST(BehavioralSource, CurrentOfAnElementIsThatOfItsFirstTerminal) {
    const struct {
        std::string name, lines;
        bool conducts;
    } cases[] = {
        {"Z1", "Rg a g 10k\nZ1 n g 0 zm\n.model zm NMF\n", true},
        {"O1", "O1 n 0 c 0 R=10 L=250n C=100p G=1m TD=5n\nRc c 0 1k\n", true},
        {"S1", "Rg g 0 10k\nS1 n 0 g VBF=50\n", false},
        {"Y1", "Rg g 0 10k\nY1 n 0 g\n", false},
    };
    for (const auto& c : cases) {
        const auto op = operating_point("t\nV1 a 0 5\nRs a n 1k\n" + c.lines + "B1 o 0 V={I(" +
                                        c.name + ")*1k}\nR2 o 0 1k\n.end\n");
        EXPECT_NEAR(op.v("o"), op.v("a") - op.v("n"), 1e-9) << c.name;
        EXPECT_EQ(op.v("o") > 1.0, c.conducts) << c.name;
    }
}

// ... one probe serves every expression that names the element, and the
// probe is a part of the circuit, as in ngspice-45.2: its node,
// <node>_vmeas_<k>, and its source, v_<name>, are in the results. ngspice-45.2
// gives V(b) = 1.5 V and V(c) = 2 V (run 2026-11-28).
TEST(BehavioralSource, CurrentOfAnElementReadTwiceHasOneProbe) {
    const auto op = operating_point(
        "t\nV1 in 0 1\nB1 b 0 V={I(R2)*1k + I(R1)*1k}\nRb b 0 1k\n"
        "B2 c 0 V={I(R1)*2k}\nRc c 0 1k\nR1 in 0 1k\nR2 in m 1k\nR3 m 0 1k\n.end\n");
    EXPECT_NEAR(op.v("b"), 1.5, 1e-9);
    EXPECT_NEAR(op.v("c"), 2.0, 1e-9);
    // V1, the two B sources and the probes of R2 and R1
    EXPECT_EQ(op.circuit->get_voltage_sources().size(), 5u);
    EXPECT_NO_THROW(op.circuit->get_device("v_R1"));
    EXPECT_NO_THROW(op.circuit->get_device("v_R2"));
    EXPECT_TRUE(op.circuit->has_node("in_vmeas_0"));
    EXPECT_TRUE(op.circuit->has_node("in_vmeas_1"));
}

// ... a coupling of the element stays its own: K1 names L1, whose first node
// the probe takes. The current into L1 is that of R1, which alone feeds it,
// at every point of a transient: V(b) = 100 Ohm I(L1) = V(in) - V(a)
TEST(BehavioralSource, CurrentOfACoupledInductor) {
    kirimaspice::parser::NetlistParser parser;
    auto netlist = parser.parse_string(
        "t\nV1 in 0 PULSE(0 1 0 1u 1u 1 2)\nR1 in a 100\nL1 a 0 1m\nL2 c 0 1m\n"
        "R2 c 0 100\nK1 L1 L2 0.5\nB1 b 0 V={I(L1)*100}\nRb b 0 1k\n"
        ".tran 0.2u 40u\n.end\n");
    kirimaspice::SimulationConfig config = netlist.config;
    config.transient.use_adaptive_timestep = false;
    kirimaspice::solvers::TransientSolver solver(*netlist.circuit, config);
    const auto tran = solver.solve(0.0, 40e-6, 0.2e-6);
    ASSERT_EQ(tran.status, kirimaspice::ConvergenceStatus::SUCCESS);
    const auto node = [&](const std::string& name) {
        return netlist.circuit->get_node_id(name) - 1;
    };
    for (const auto& point : tran.timepoints) {
        EXPECT_NEAR(point.node_voltages[node("b")],
                    point.node_voltages[node("in")] - point.node_voltages[node("a")], 1e-9)
            << point.time;
    }
    // The current rises towards 1 V / 100 Ohm; ngspice-45.2 gives V(b) = 0.9639 V at 40 us
    EXPECT_NEAR(tran.timepoints.back().node_voltages[node("b")], 0.9639, 1e-3);
}

// ... inside a subcircuit I(R1) is the body's own R1, with a probe in each
// instance; ngspice-45.2 gives V(out) = V(out2) = 0.5 V (run 2026-11-28)
TEST(BehavioralSource, CurrentOfAnElementInABody) {
    const auto op = operating_point(
        "t\nV1 in 0 1\nX1 in out sense\nX2 in out2 sense\nRo out 0 1k\nRo2 out2 0 1k\n"
        ".subckt sense p q\nR1 p 0 2k\nB1 q 0 V={I(R1)*1k}\n.ends\n.end\n");
    EXPECT_NEAR(op.v("out"), 0.5, 1e-9);
    EXPECT_NEAR(op.v("out2"), 0.5, 1e-9);
}

// ... in AC and in the transient the probe reads the capacitor's current.
// R0 = 1 kOhm into C1 = 1 uF: at the corner frequency V(m) = 0.5 - 0.5j, and
// I(C1) = j omega C V(m) gives V(o) = 0.5 + 0.5j, as in ngspice-45.2; after
// a step of 1 V, I(C1) = 1 mA exp(-t / 1 ms) (ngspice-45.2 gives 0.6068 V,
// 0.3681 V and 0.1354 V at 0.5, 1 and 2 ms; run 2026-11-28)
TEST(BehavioralSource, CurrentOfACapacitorInAcAndTransient) {
    const std::string circuit = "R0 a m 1k\nC1 m 0 1u\nB1 o 0 V={I(C1)*1k}\nR2 o 0 1k\n";
    kirimaspice::parser::NetlistParser parser;
    auto ac_netlist = parser.parse_string("t\nV1 a 0 DC 0 AC 1\n" + circuit +
                                          ".ac lin 1 159.154943 159.154943\n.end\n");
    kirimaspice::solvers::ACSolver ac_solver(*ac_netlist.circuit, ac_netlist.config);
    const auto ac = ac_solver.solve();
    ASSERT_EQ(ac.status, kirimaspice::ConvergenceStatus::SUCCESS);
    const auto vo = ac.frequency_points[0].solution[ac_netlist.circuit->get_node_id("o") - 1];
    EXPECT_NEAR(vo.real(), 0.5, 1e-6);
    EXPECT_NEAR(vo.imag(), 0.5, 1e-6);

    auto tran_netlist = parser.parse_string("t\nV1 a 0 PULSE(0 1 0 1u 1u 1 2)\n" + circuit +
                                            ".tran 10u 3m\n.end\n");
    kirimaspice::SimulationConfig config = tran_netlist.config;
    config.transient.use_adaptive_timestep = false;
    kirimaspice::solvers::TransientSolver tran_solver(*tran_netlist.circuit, config);
    const auto tran = tran_solver.solve(0.0, 3e-3, 1e-5);
    ASSERT_EQ(tran.status, kirimaspice::ConvergenceStatus::SUCCESS);
    const auto o = tran_netlist.circuit->get_node_id("o") - 1;
    std::size_t checked = 0;
    for (const auto& point : tran.timepoints) {
        if (point.time < 1e-4) {
            continue;  // the edge of the step
        }
        EXPECT_NEAR(point.node_voltages[o], std::exp(-point.time / 1e-3), 2e-3) << point.time;
        ++checked;
    }
    EXPECT_GT(checked, 200u);
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
