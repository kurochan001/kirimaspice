// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// SFFM as ngspice-47 gives it (vsrcload.c, read 2026-11-21): SFFM(VO VA FC
// MDI FS TD PHASEM PHASEC) is 0 up to TD and then VO + VA sin(2 pi FC t +
// PHASEC + MDI sin(2 pi FS t + PHASEM)), t = time - TD, phases in degrees;
// FC defaults to 5/TSTOP, MDI to 90 and FS to 500/TSTOP, and MDI is kept
// within 0 and FC/FS. ngspice-47 gives these waveforms to 2e-5 V, and 0 V
// in the operating point of a source without a DC value. Before, SFFM took
// five arguments, other defaults, and VO at time 0.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

double value_at(const std::string& source, const std::string& tran, double t) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 a 0 " + source + "\nR1 a 0 1k\n" + tran + "\n.end\n");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, parsed.config.transient.end_time, parsed.config.transient.initial_step);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId a = parsed.circuit->get_node_id("a");
    for (std::size_t i = 1; i < result.timepoints.size(); ++i) {
        const auto& p = result.timepoints[i - 1];
        const auto& q = result.timepoints[i];
        if (q.time >= t) {
            const double w = (t - p.time) / (q.time - p.time);
            return p.node_voltages[a - 1] + w * (q.node_voltages[a - 1] - p.node_voltages[a - 1]);
        }
    }
    return result.timepoints.back().node_voltages[a - 1];
}

}  // namespace

TEST(Sffm, AllArgumentsAsNgspice) {
    const double pi = std::acos(-1.0);
    for (double time : {0.4e-3, 0.8e-3, 1.2e-3, 1.6e-3}) {
        const double t = time - 0.1e-3;
        const double expected = std::sin(2 * pi * 2e3 * t + 45 * pi / 180 + 1.0 * std::sin(2 * pi * 200 * t + 30 * pi / 180));
        EXPECT_NEAR(value_at("SFFM(0 1 2k 1 200 0.1m 30 45)", ".tran 1u 2m", time), expected, 1e-9) << time;
    }
    // 0 up to TD
    EXPECT_NEAR(value_at("SFFM(0.5 1 2k 1 200 0.1m)", ".tran 1u 2m", 0.05e-3), 0.0, 1e-12);
}

TEST(Sffm, DefaultsAsNgspice) {
    // FC = 5/TSTOP: the peak at TSTOP/20; MDI 90 limited to FC/FS = 0.01,
    // whose term is 0 on the points of 1 us (FS = 500 kHz)
    EXPECT_NEAR(value_at("SFFM(0 1)", ".tran 1u 1m", 0.05e-3), 1.0, 1e-9);
    // MDI limited to FC/FS = 2k/200 = 10 when given above it
    EXPECT_NEAR(value_at("SFFM(0 1 2k 50 200)", ".tran 1u 2m", 0.4e-3),
                std::sin(2 * std::acos(-1.0) * 2e3 * 0.4e-3 + 10 * std::sin(2 * std::acos(-1.0) * 200 * 0.4e-3)),
                1e-9);
}

TEST(Sffm, OperatingPointWithoutDcIsZero) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 a 0 SFFM(0.5 1 1k)\nR1 a 0 1k\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("a") - 1], 0.0, 1e-12);
}
