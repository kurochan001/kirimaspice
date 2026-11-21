// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The transient steps onto every breakpoint of a source, as SPICE does:
// the four corners of each PULSE period, the points of PWL and the delays
// of SIN and EXP (Waveform::breakpoints, TransientSolver::
// run_timestepping_loop). The fixed step keeps the points k TSTEP as well,
// and the step after a corner is a backward-Euler step, as at time 0.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

struct TransientRun {
    solvers::TransientResult result;
    NodeId out = 0;
};

TransientRun run(const std::string& source, double t_stop, double tstep) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\n" + source + "\nR1 in out 1k\nC1 out 0 10p\n.end\n");
    TransientRun r;
    r.out = parsed.circuit->get_node_id("out");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    r.result = solver.solve(0.0, t_stop, tstep);
    EXPECT_EQ(r.result.status, ConvergenceStatus::SUCCESS);
    return r;
}

bool has_point(const solvers::TransientResult& result, double t) {
    return std::any_of(result.timepoints.begin(), result.timepoints.end(),
                       [&](const auto& tp) { return std::abs(tp.time - t) <= 1e-12; });
}

}  // namespace

TEST(TransientBreakpoints, PulseBetweenTwoStepsIsNotMissed) {
    // A 0.2 us pulse into 1 kohm and 10 pF (tau 10 ns) between the steps of
    // 1 us: the steps land on its corners, and the output reaches the pulse
    // (ngspice-47: 1.00008 V at 1.573 us). It never exceeds the 1 V of the
    // source, as the exact response of an RC circuit does not; the
    // trapezoidal step of 20 tau after a corner would overshoot to 1.78 V.
    const TransientRun r = run("V1 in 0 PULSE(0 1 1.5u 1n 1n 0.2u 10u)", 5e-6, 1e-6);
    for (double t : {1.5e-6, 1.501e-6, 1.701e-6, 1.702e-6}) {
        EXPECT_TRUE(has_point(r.result, t)) << t;
    }
    for (int k = 0; k <= 5; ++k) {
        EXPECT_TRUE(has_point(r.result, k * 1e-6)) << k;
    }
    double peak = 0.0;
    for (const auto& tp : r.result.timepoints) {
        peak = std::max(peak, tp.node_voltages[r.out - 1]);
    }
    EXPECT_GT(peak, 0.9);
    EXPECT_LE(peak, 1.0);
}

TEST(TransientBreakpoints, CornersOfPwlSinAndExp) {
    EXPECT_TRUE(has_point(run("V1 in 0 PWL(0 0 1.3u 1 2.7u 0)", 5e-6, 1e-6).result, 1.3e-6));
    EXPECT_TRUE(has_point(run("V1 in 0 PWL(0 0 1.3u 1 2.7u 0)", 5e-6, 1e-6).result, 2.7e-6));
    EXPECT_TRUE(has_point(run("V1 in 0 SIN(0 1 1meg 1.25u)", 5e-6, 1e-6).result, 1.25e-6));
    const TransientRun e = run("V1 in 0 EXP(0 1 0.4u 0.1u 3.6u 0.1u)", 5e-6, 1e-6);
    EXPECT_TRUE(has_point(e.result, 0.4e-6));
    EXPECT_TRUE(has_point(e.result, 3.6e-6));
}

TEST(TransientBreakpoints, PeriodsOfAPulseRepeatTheirCorners) {
    const TransientRun r = run("V1 in 0 PULSE(0 1 0.25u 10n 10n 0.5u 2u)", 6e-6, 1e-6);
    for (double start : {0.25e-6, 2.25e-6, 4.25e-6}) {
        EXPECT_TRUE(has_point(r.result, start)) << start;
        EXPECT_TRUE(has_point(r.result, start + 0.52e-6)) << start;
    }
}

// A corner with no rise or fall has the level from before it (the left
// limit, PulseWaveform::evaluate), and the next point has the new level, as
// for PWL's time given twice. So the operating point of PULSE(0 1 0 0 0 ...)
// is 0 V, and an RC circuit (tau = 10 ns) driven by it charges as from an
// ideal step on the corner: 1 - exp(-(t - t0)/tau) after a rise at t0, and
// that value times exp(-(t - t1)/tau) after the fall at t1. The step after
// a corner is a backward-Euler step, whose local error (h/tau)^2/2 = 0.005
// bounds the error here. (ngspice-47 has the old level on the corner too,
// but behaves as if a zero TR or TF were TSTEP: PULSE(0 1 0 0 0 1m 2m) into
// 1 kohm and 1 uF with .tran 0.1m 1m gives 0.6131 V at 1 ms, the response
// to a rise over 0.1 ms, where the ideal step gives 0.6321 V; run
// 2026-11-21.)
TEST(TransientBreakpoints, CornerWithoutRiseOrFallHasTheLevelBeforeIt) {
    const double tau = 1e3 * 10e-12;
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 in 0 PULSE(0 1 10n 0 0 10n 0)\nR1 in out 1k\nC1 out 0 10p\n.end\n");
    const NodeId in = parsed.circuit->get_node_id("in");
    const NodeId out = parsed.circuit->get_node_id("out");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 25e-9, 1e-9);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_TRUE(has_point(result, 10e-9));
    ASSERT_TRUE(has_point(result, 20e-9));
    const double at_fall = 1.0 - std::exp(-10e-9 / tau);
    for (const auto& tp : result.timepoints) {
        const double t = tp.time;
        const bool high = t > 10e-9 * (1 + 1e-9) && t <= 20e-9 * (1 + 1e-9);
        EXPECT_EQ(tp.node_voltages[in - 1], high ? 1.0 : 0.0) << "t=" << t;
        const double expected = t <= 10e-9 * (1 + 1e-9) ? 0.0
                                : t <= 20e-9 * (1 + 1e-9)
                                    ? 1.0 - std::exp(-(t - 10e-9) / tau)
                                    : at_fall * std::exp(-(t - 20e-9) / tau);
        EXPECT_NEAR(tp.node_voltages[out - 1], expected, 5e-3) << "t=" << t;
    }
}

TEST(TransientBreakpoints, PulseWithoutDelayOrRiseStartsFromItsFirstLevel) {
    const double tau = 1e3 * 10e-12;
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 in 0 PULSE(0 1 0 0 0 10n 20n)\nR1 in out 1k\nC1 out 0 10p\n.end\n");
    const NodeId in = parsed.circuit->get_node_id("in");
    const NodeId out = parsed.circuit->get_node_id("out");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 10e-9, 1e-9);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 2u);
    EXPECT_EQ(result.timepoints.front().node_voltages[in - 1], 0.0);
    for (const auto& tp : result.timepoints) {
        EXPECT_NEAR(tp.node_voltages[out - 1], 1.0 - std::exp(-tp.time / tau), 5e-3)
            << "t=" << tp.time;
    }
}

namespace {

std::string exact(double t) {
    std::ostringstream text;
    text << std::setprecision(17) << t;
    return text.str();
}

}  // namespace

// The transient keeps a point on t_start and on t_stop also when a
// breakpoint lies closer to them than a step can be (a rounding, or 1e-7
// of TSTEP): that breakpoint is taken by them (run_timestepping_loop)
TEST(TransientBreakpoints, BreakpointCloseToTheStartOrStopKeepsThem) {
    for (const double gap : {1e-14, 1e-11}) {
        SCOPED_TRACE(gap);
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\nV1 in 0 PWL(0 0 " + exact(0.3e-3 - gap) + " 0.3 " +
                                          exact(1e-3 - gap) + " 1)\nR1 in out 1k\nC1 out 0 1u\n.end\n");
        solvers::TransientSolver solver(*parsed.circuit, parsed.config);
        const auto result = solver.solve(0.3e-3, 1e-3, 0.1e-3);
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        ASSERT_FALSE(result.timepoints.empty());
        EXPECT_EQ(result.timepoints.front().time, 0.3e-3);
        EXPECT_EQ(result.timepoints.back().time, 1e-3);
    }
}

// A breakpoint that t_start takes still starts the integration again: the
// step after it is a backward-Euler step. A step of 1 kohm into 10 pF
// (tau = 10 ns) a rounding after t_start, with steps of h = 1 us = 100 tau:
// the backward-Euler step leaves the error 1/(1 + h/tau) = 0.0099 V, which
// the trapezoidal steps after it carry with a factor of magnitude below 1;
// a trapezoidal step over the corner would leave 2/(2 + h/tau) = 0.0196 V.
TEST(TransientBreakpoints, BreakpointTakenByTheStartStillRestarts) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 in 0 PULSE(0 1 " + exact(2e-6 + 5e-16) +
                                      " 0 0 1 2)\nR1 in out 1k\nC1 out 0 10p\n.end\n");
    const NodeId out = parsed.circuit->get_node_id("out");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(2e-6, 8e-6, 1e-6);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 2u);
    EXPECT_EQ(result.timepoints.front().time, 2e-6);
    for (const auto& tp : result.timepoints) {
        if (tp.time > 2e-6) {
            EXPECT_LE(std::abs(tp.node_voltages[out - 1] - 1.0), 1.0 / 101.0 + 1e-9)
                << "t=" << tp.time;
        }
    }
}
