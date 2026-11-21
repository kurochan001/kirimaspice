// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The transient of the lossy line O: sections of the constant-parameter
// (Bergeron) line, the lossless line with its loss lumped at its ends
// (TLineLossy). A 1 V step through 50 ohm into a line of L = 250 nH/m,
// C = 100 pF/m (Z0 = 50 ohm) and TD = 10 ns (2 m) loaded by 50 ohm. The
// expected values: without loss, the lossless line T's; with loss,
// ngspice-47's LTRA (.model ltra, LEN = 2, `.tran 0.1n 60n`, run
// 2026-11-21), whose final values are the analytic divider of R length.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

struct Wave {
    std::vector<double> t, a, b;
    double at(const std::vector<double>& v, double time) const {
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (t[i] >= time) {
                const double w = (time - t[i - 1]) / (t[i] - t[i - 1]);
                return v[i - 1] + w * (v[i] - v[i - 1]);
            }
        }
        return v.back();
    }
};

Wave run(const std::string& line, const std::string& source = "PULSE(0 1 0 0.1n 0.1n 1 2)") {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 in 0 " + source + "\nR1 in a 50\n" + line + "\nR2 b 0 50\n.end\n");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 60e-9, 0.1e-9);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS) << line;
    const NodeId a = parsed.circuit->get_node_id("a");
    const NodeId b = parsed.circuit->get_node_id("b");
    Wave w;
    for (const auto& tp : result.timepoints) {
        w.t.push_back(tp.time);
        w.a.push_back(tp.node_voltages[a - 1]);
        w.b.push_back(tp.node_voltages[b - 1]);
    }
    return w;
}

}  // namespace

TEST(TLineLossyTransient, WithoutLossItIsTheLosslessLine) {
    // Before, the arriving wave lacked Z0 I(t - TD): the step settled at
    // v(a) = 2/3 V and v(b) = 1/3 V, where the operating point gives 0.5 V
    const Wave o = run("O1 a 0 b 0 R=0 L=250n C=100p G=0 TD=10n");
    const Wave t = run("T1 a 0 b 0 Z0=50 TD=10n");
    for (double time = 1e-9; time < 60e-9; time += 1e-9) {
        EXPECT_NEAR(o.at(o.a, time), t.at(t.a, time), 1e-9) << time;
        EXPECT_NEAR(o.at(o.b, time), t.at(t.b, time), 1e-9) << time;
    }
    EXPECT_NEAR(o.at(o.b, 40e-9), 0.5, 1e-9);
}

TEST(TLineLossyTransient, SmallLossFollowsLtra) {
    // R length = 1 ohm: before, v(b) was 0.0144 V at 40 ns
    const Wave o = run("O1 a 0 b 0 R=0.5 L=250n C=100p G=0 TD=10n");
    EXPECT_NEAR(o.at(o.a, 5e-9), 0.501233, 1.5e-3);
    EXPECT_NEAR(o.at(o.b, 15e-9), 0.495036, 2e-5);
    EXPECT_NEAR(o.at(o.b, 25e-9), 0.495048, 2e-5);
    EXPECT_NEAR(o.at(o.a, 40e-9), 0.504950, 2e-6);
    EXPECT_NEAR(o.at(o.b, 40e-9), 0.495050, 2e-6);
}

TEST(TLineLossyTransient, LargerLossFollowsLtra) {
    // R length = 10 ohm, a fifth of Z0: the input rises as the loss is met
    const Wave o = run("O1 a 0 b 0 R=5 L=250n C=100p G=0 TD=10n");
    EXPECT_NEAR(o.at(o.a, 5e-9), 0.512065, 5e-4);
    EXPECT_NEAR(o.at(o.a, 9.5e-9), 0.522542, 5e-4);
    EXPECT_NEAR(o.at(o.b, 15e-9), 0.453377, 1e-4);
    EXPECT_NEAR(o.at(o.b, 25e-9), 0.454430, 1e-4);
    EXPECT_NEAR(o.at(o.b, 60e-9), 0.454545, 2e-5);  // 50 / 110, analytic
}

TEST(TLineLossyTransient, StartsFromTheOperatingPoint) {
    // A constant source: the line stays in its operating point. Before, its
    // delayed waves were those of 0 V for the first TD.
    const Wave o = run("O1 a 0 b 0 R=0.5 L=250n C=100p G=0 TD=10n", "DC 1");
    for (double time = 0.5e-9; time < 60e-9; time += 0.5e-9) {
        EXPECT_NEAR(o.at(o.b, time), 50.0 / 101.0, 1e-6) << time;
    }
}
