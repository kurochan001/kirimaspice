// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The noise analysis takes its frequencies from the AC sweep
// (solvers::ac_frequency_points), so a sweep of one point gives one finite
// frequency, and it reports a failure instead of zero noise when the output
// node is not a node of the circuit.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/noise_solver.hpp"

using namespace kirimaspice;

namespace {

const char* kRc = "t\nV1 in 0 DC 0 AC 1\nR1 in out 1k\nC1 out 0 1n\n.end\n";

}  // namespace

TEST(NoiseSolverFailures, OnePointSweepsGiveOneFiniteFrequency) {
    for (const auto type : {SimulationConfig::AC::SweepType::LIN, SimulationConfig::AC::SweepType::DEC}) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string(kRc);
        SimulationConfig config = parsed.config;
        config.ac.sweep_type = type;
        config.ac.start_freq = 1e3;
        config.ac.stop_freq = 2e3;  // LIN of 1 point, or a decade sweep rounded to 1 point
        config.ac.num_points = 1;
        EXPECT_EQ(solvers::ac_frequency_points(config.ac).size(), 1u);
        solvers::NoiseSolver solver(*parsed.circuit, config);
        const auto result = solver.solve(parsed.circuit->get_node_id("in"),
                                         parsed.circuit->get_node_id("out"));
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        ASSERT_EQ(result.frequency_points.size(), 1u);
        EXPECT_DOUBLE_EQ(result.frequency_points[0], 1e3);
        EXPECT_TRUE(std::isfinite(result.output_noise[0]));
        EXPECT_GT(result.output_noise[0], 0.0);
    }
}

TEST(NoiseSolverFailures, AnOutputThatIsNoNodeIsAFailure) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(kRc);
    SimulationConfig config = parsed.config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1e3;
    config.ac.stop_freq = 1e3;
    config.ac.num_points = 1;
    solvers::NoiseSolver solver(*parsed.circuit, config);
    const auto result = solver.solve(parsed.circuit->get_node_id("in"), 99);
    EXPECT_NE(result.status, ConvergenceStatus::SUCCESS);
}
