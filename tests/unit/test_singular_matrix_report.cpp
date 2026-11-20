// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A failed factorization names the unknown of the singular column
// (solvers::factorization_failure, CircuitGraph::unknown_name): the
// unknowns are the nodes other than ground, then the branch currents of the
// voltage sources, and AC's real form holds them once for the real and once
// for the imaginary parts. A node with no DC path to ground, and a loop of
// voltage sources, make the DC matrix singular at such an unknown.

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/factorization_failure.hpp"

using namespace kirimaspice;

namespace {

class StubSolver final : public solvers::ISparseLinearSolver {
public:
    explicit StubSolver(std::optional<std::uint32_t> column) : column_(column) {}
    bool analyze_and_factorize(const SparseMatrix&) override { return false; }
    bool refactorize(const SparseMatrix&) override { return false; }
    bool solve(const DenseVector&, DenseVector&) override { return false; }
    const std::string& last_error() const override { return error_; }
    std::optional<std::uint32_t> singular_column() const override { return column_; }
    void reset() override {}

private:
    std::optional<std::uint32_t> column_;
    std::string error_ = "factorization failed";
};

// The error lines of a DC solve of netlist
std::string dc_errors(const std::string& netlist) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("singular", sink);
    logger->set_level(spdlog::level::err);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    const auto status = solver.solve(solution, stats);
    spdlog::set_default_logger(previous);
    EXPECT_NE(status, ConvergenceStatus::SUCCESS);
    return captured.str();
}

}  // namespace

TEST(SingularMatrixReport, UnknownsAreNodesThenSourceBranches) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 in 0 1\nR1 in out 1k\nE1 x 0 out 0 2\nR2 x 0 1k\n.end\n");
    const auto& c = *parsed.circuit;
    ASSERT_EQ(c.num_unknowns(), 5u);  // in, out, x; I(V1), I(E1)
    EXPECT_EQ(c.unknown_name(0), "V(in)");
    EXPECT_EQ(c.unknown_name(1), "V(out)");
    EXPECT_EQ(c.unknown_name(2), "V(x)");
    EXPECT_EQ(c.unknown_name(3), "I(V1)");
    EXPECT_EQ(c.unknown_name(4), "I(E1)");
}

TEST(SingularMatrixReport, ColumnIsTakenModuloTheUnknowns) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 in 0 1\nR1 in out 1k\nR2 out 0 1k\n.end\n");
    // Unknowns: V(in), V(out), I(V1); column 4 of AC's real form is the
    // imaginary part of V(out)
    EXPECT_EQ(solvers::factorization_failure(*parsed.circuit, StubSolver(4u)),
              "factorization failed; singular matrix: check V(out)");
    EXPECT_EQ(solvers::factorization_failure(*parsed.circuit, StubSolver(std::nullopt)),
              "factorization failed");
}

TEST(SingularMatrixReport, FloatingNodeIsNamed) {
    const std::string errors = dc_errors("t\nV1 in 0 1\nR1 in 0 1k\nR2 a b 1k\n.end\n");
    EXPECT_TRUE(errors.find("singular matrix: check V(a)") != std::string::npos ||
                errors.find("singular matrix: check V(b)") != std::string::npos)
        << errors;
}

TEST(SingularMatrixReport, VoltageSourceLoopIsNamed) {
    const std::string errors = dc_errors("t\nV1 in 0 1\nV2 in 0 2\nR1 in 0 1k\n.end\n");
    EXPECT_TRUE(errors.find("singular matrix: check I(V1)") != std::string::npos ||
                errors.find("singular matrix: check I(V2)") != std::string::npos)
        << errors;
}
