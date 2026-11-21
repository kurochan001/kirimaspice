// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// At the info level, the DC solver reports a solve (its start and its
// end), not each Newton iteration: the per-iteration values are debug
// output (DCSolver::solve_standard). So the info output of a solve does
// not grow with the number of iterations it takes.

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

struct InfoSolve {
    std::size_t info_lines = 0;
    std::uint64_t iterations = 0;
    std::string log;
};

// Solve the operating point of netlist with the default logger writing
// info-level messages to a string
InfoSolve solve_logging_info(const std::string& netlist) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);

    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("dc_solver_logging", sink);
    logger->set_level(spdlog::level::info);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);

    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    const auto status = solver.solve(solution, stats);
    spdlog::set_default_logger(previous);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    InfoSolve result;
    result.log = captured.str();
    result.iterations = stats.total_iterations;
    std::istringstream lines(result.log);
    for (std::string line; std::getline(lines, line);) {
        ++result.info_lines;
    }
    return result;
}

}  // namespace

TEST(DCSolverLogging, InfoOutputDoesNotGrowWithIterations) {
    // A linear divider, and a chain of three diodes driven from 100 V
    // through 10 ohm, which takes several times as many Newton iterations
    const InfoSolve linear =
        solve_logging_info("divider\nV1 in 0 1\nR1 in a 1k\nR2 a 0 1k\nR3 a b 1k\nR4 b 0 1k\n.end\n");
    const InfoSolve diodes = solve_logging_info(
        "diodes\nV1 in 0 100\nR1 in a 10\nD1 a b DM\nD2 b c DM\nD3 c 0 DM\nR2 c 0 1k\n"
        ".model DM D IS=1e-14 N=1\n.end\n");
    ASSERT_GE(diodes.iterations, linear.iterations + 5);
    EXPECT_EQ(diodes.info_lines, linear.info_lines) << "linear:\n" << linear.log
                                                    << "diodes:\n" << diodes.log;
}
