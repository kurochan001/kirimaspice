// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The IGBT's tail: the stored-carrier current Is follows the channel current
// with the lifetime TAU, dIs/dt = (Ich - Is)/TAU, and the collector carries
// max(Ich, Is) (IGBT, stored_carrier_current). A 15 V gate pulse from 0 to
// 1 us (10 ns edges, threshold 5 V) into 5 ohm from 50 V: the channel carries
// I_on = 9.9602 A while the gate is above threshold, from t_on = 3.33 ns to
// t_off = 1.0167 us, so Is reaches I_on (1 - exp(-(t_off - t_on)/TAU)) and
// then decays as exp(-(t - t_off)/TAU) (analytic, for edges much shorter
// than TAU). Before, the tail started from the collector current at the last
// accepted point above threshold: 6.02 A at 1.5 us with 100 ns steps and
// 0.62 A with 1 ns steps.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

double collector_current_at(double step, double time) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVCC vcc 0 DC 50\nRL vcc c 5\nVG g 0 PULSE(0 15 0 10n 10n 1u 10u)\nM1 c g 0 mi\n"
        ".model mi nmos level=21 kp=1 vge_th=5 vce_sat=2 tau=1u\n.end\n");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 3e-6, step);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId c = parsed.circuit->get_node_id("c");
    for (std::size_t i = 1; i < result.timepoints.size(); ++i) {
        const auto& p1 = result.timepoints[i - 1];
        const auto& p2 = result.timepoints[i];
        if (p2.time >= time) {
            const double w = (time - p1.time) / (p2.time - p1.time);
            const double v = p1.node_voltages[c - 1] + w * (p2.node_voltages[c - 1] - p1.node_voltages[c - 1]);
            return (50.0 - v) / 5.0;
        }
    }
    ADD_FAILURE() << "no point at " << time;
    return 0.0;
}

}  // namespace

TEST(IGBTTail, DoesNotDependOnTheStep) {
    const double fine = collector_current_at(1e-9, 1.5e-6);
    EXPECT_NEAR(collector_current_at(10e-9, 1.5e-6), fine, 1e-3 * fine);
    EXPECT_NEAR(collector_current_at(100e-9, 1.5e-6), fine, 2e-2 * fine);
}

TEST(IGBTTail, FollowsTheStoredCharge) {
    const double tau = 1e-6;
    const double i_on = 9.9602;
    const double t_on = 10e-9 * 5.0 / 15.0;
    const double t_off = 1.01e-6 + 10e-9 * 10.0 / 15.0;
    const double built = i_on * (1.0 - std::exp(-(t_off - t_on) / tau));
    for (double t : {1.5e-6, 2e-6, 3e-6}) {
        const double expected = built * std::exp(-(t - t_off) / tau);
        EXPECT_NEAR(collector_current_at(1e-9, t), expected, 3e-3 * expected) << t;
    }
    // While the gate is on, the collector carries the channel current
    EXPECT_NEAR(collector_current_at(1e-9, 0.5e-6), i_on, 1e-3);
}
