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
