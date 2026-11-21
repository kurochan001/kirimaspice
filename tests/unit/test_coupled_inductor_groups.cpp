// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Inductors that two or more K lines couple among themselves are coupled
// through the inverse of their whole inductance matrix
// (MutualInductance::CoupledGroup), not pair by pair. Three 1 mH inductors,
// L1 driven by 1 mA AC through 10 kohm, L2 into 10 ohm and L3 into 20 ohm,
// at 1 kHz: with k = 0.5 between each two, and with K12 and K23 only (L1
// and L3 coupled through L2), ngspice-47 gives the voltages below (run
// 2026-11-21); the transient of a 1 V step through 10 ohm agrees with
// ngspice-47 (TMAX 0.1 us) as well. Before, each K added the inverse of its
// own 2 x 2 matrix: v(a) was 2.77e-3 + 2.47e-3j for the three K lines.

#include <gtest/gtest.h>

#include <complex>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

const std::string kCircuit =
    "L1 a 0 1m\nL2 b 0 1m\nR2 b 0 10\nL3 c 0 1m\nR3 c 0 20\nK12 L1 L2 0.5\nK23 L2 L3 0.5\n";

std::complex<double> ac_voltage(const parser::NetlistParseResult& parsed, const solvers::ACResult& result,
                                const char* node) {
    return result.frequency_points.at(0).solution.at(parsed.circuit->get_node_id(node) - 1);
}

void expect_ac(const std::string& extra, const std::complex<double> (&expected)[3]) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nI1 0 a AC 1m\nRa a 0 10k\n" + kCircuit + extra +
                                      ".ac lin 1 1k 1k\n.end\n");
    solvers::ACSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve();
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const char* nodes[3] = {"a", "b", "c"};
    for (int i = 0; i < 3; ++i) {
        const auto v = ac_voltage(parsed, result, nodes[i]);
        EXPECT_NEAR(v.real(), expected[i].real(), 1e-9) << nodes[i] << " " << extra;
        EXPECT_NEAR(v.imag(), expected[i].imag(), 1e-9) << nodes[i] << " " << extra;
    }
}

}  // namespace

TEST(CoupledInductorGroups, ThreeCoupledToEachOtherInAc) {
    const std::complex<double> expected[3] = {{9.654811e-04, 5.581330e-03},
                                              {1.574857e-03, 1.945285e-03},
                                              {1.314573e-03, 2.233548e-03}};
    expect_ac("K13 L1 L3 0.5\n", expected);
}

TEST(CoupledInductorGroups, ChainCouplesTheEndsInAc) {
    const std::complex<double> expected[3] = {{7.053843e-04, 5.869726e-03},
                                              {1.314671e-03, 2.233569e-03},
                                              {5.205656e-04, -5.76557e-04}};
    expect_ac("", expected);
}

TEST(CoupledInductorGroups, ThreeCoupledToEachOtherInTransient) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 in 0 PULSE(0 1 0 1u 1u 1 2)\nR1 in a 10\n" + kCircuit +
                                      "K13 L1 L3 0.5\n.end\n");
    SimulationConfig config = parsed.config;
    config.transient.use_adaptive_timestep = false;
    solvers::TransientSolver solver(*parsed.circuit, config);
    const auto result = solver.solve(0.0, 100e-6, 0.1e-6);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId b = parsed.circuit->get_node_id("b");
    auto at = [&](double t) {
        for (std::size_t i = 1; i < result.timepoints.size(); ++i) {
            const auto& p = result.timepoints[i - 1];
            const auto& q = result.timepoints[i];
            if (q.time >= t) {
                const double w = (t - p.time) / (q.time - p.time);
                return p.node_voltages[b - 1] + w * (q.node_voltages[b - 1] - p.node_voltages[b - 1]);
            }
        }
        return result.timepoints.back().node_voltages[b - 1];
    };
    EXPECT_NEAR(at(20e-6), 0.079804, 1e-4);
    EXPECT_NEAR(at(50e-6), 0.147073, 1e-4);
    EXPECT_NEAR(at(100e-6), 0.172372, 1e-4);
}
