// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/devices/behavioral_source.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
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
