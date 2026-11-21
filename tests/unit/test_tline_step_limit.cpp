// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A transmission line reads its delayed waves from the accepted points, so
// the transient takes no step longer than its delay TD
// (Device::max_time_step), as SPICE limits the step to the shortest line
// delay. A matched 50 ohm line of TD = 1 ns driven by a 10 MHz sine of 1 V
// through 50 ohm has v(b) = 0.5 sin(2 pi f (t - TD)); with steps of 5 ns
// the far end lagged by the step, 5 ns, instead of TD.

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

TEST(TLineStepLimit, StepIsNoLongerThanTheDelay) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 in 0 SIN(0 1 10meg)\nR1 in a 50\nT1 a 0 b 0 Z0=50 TD=1n\nR2 b 0 50\n.end\n");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 200e-9, 5e-9);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId b = parsed.circuit->get_node_id("b");
    double worst = 0.0;
    for (const auto& tp : result.timepoints) {
        if (tp.time < 10e-9) {
            continue;
        }
        const double exact = 0.5 * std::sin(2.0 * std::numbers::pi * 10e6 * (tp.time - 1e-9));
        worst = std::max(worst, std::abs(tp.node_voltages[b - 1] - exact));
    }
    EXPECT_LT(worst, 5e-3);  // the trapezoidal error of 1 ns steps on a 100 ns period
}
