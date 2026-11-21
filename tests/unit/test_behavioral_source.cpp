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
constexpr const char* kVoltageModeNetlist = R"(
Voltage-mode B source
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
    auto result = parser.parse_string(R"(
Lower-case v()
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
        parser.parse_string(R"(
Scale suffix inside an expression
V1 in 0 DC 1
R1 in 0 1k
B1 0 out I={1m*V(in)}
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
