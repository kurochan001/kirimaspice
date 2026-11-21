// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The junction capacitance of the diode is integrated as a capacitor is
// (device_utils::capacitor_companion): the trapezoidal rule takes the
// capacitor's current at the last accepted point, i = 2C/h (v - v1) - i1.
// With M nearly 0 the capacitance is CJO at every voltage, so a 1 kohm
// resistor driving the reverse-biased diode from a ramp of 0 to -1 V over
// 1 ms (RC = 1 ms) is the RC circuit: v(t) = -(t - RC (1 - exp(-t/RC)))/T,
// which is -exp(-1) at t = T = RC.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

double final_voltage(const std::string& method) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 in 0 PWL(0 0 1m -1)\nR1 in out 1k\nD1 out 0 DM\n"
        ".model DM D IS=1e-14 CJO=1e-6 M=1e-8\n.options method=" + method + "\n.end\n");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 1e-3, 1e-5);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS) << method;
    const NodeId out = parsed.circuit->get_node_id("out");
    return result.timepoints.back().node_voltages.at(out - 1);
}

}  // namespace

TEST(DiodeCapacitanceTransient, TrapezoidalFollowsTheRcSolution) {
    // h = RC/100: the trapezoidal error is of order (h/RC)^2
    EXPECT_NEAR(final_voltage("trap"), -std::exp(-1.0), 1e-4);
}

TEST(DiodeCapacitanceTransient, EveryMethodFollowsTheRcSolution) {
    EXPECT_NEAR(final_voltage("gear"), -std::exp(-1.0), 1e-4);
    EXPECT_NEAR(final_voltage("euler"), -std::exp(-1.0), 3e-3);
}

TEST(DiodeCapacitanceTransient, TwoRunsOfOneCircuitAgree) {
    // reset_state starts each transient from no capacitor current
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 in 0 PWL(0 0 1m -1)\nR1 in out 1k\nD1 out 0 DM\n"
        ".model DM D IS=1e-14 CJO=1e-6 M=1e-8\n.end\n");
    const NodeId out = parsed.circuit->get_node_id("out");
    solvers::TransientSolver first(*parsed.circuit, parsed.config);
    const double a = first.solve(0.0, 1e-3, 1e-5).timepoints.back().node_voltages.at(out - 1);
    solvers::TransientSolver second(*parsed.circuit, parsed.config);
    const double b = second.solve(0.0, 1e-3, 1e-5).timepoints.back().node_voltages.at(out - 1);
    EXPECT_DOUBLE_EQ(a, b);
}
