// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The SiC MOSFET (LEVEL=10) conducts in both directions, as SPICE's
// MOSFETs do: with VDS < 0 the source acts as the drain and the same
// current of VGD and VSD flows from source to drain. A drain fed through
// 1 ohm from +1 mV or from -1 mV, gate on, is symmetric. With KP W/L = 1,
// VTO = 2.5 V, RDRIFT = 1 ohm and VGS = 12.5 V the channel is 1/(10 S) in
// series with the drift resistance: Id = 1 mV / (1 + 0.1 + 1) ohm.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double drain_voltage(double supply) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.model msic nmos level=10 kp=1 w=1 l=1 vto=2.5 theta=0 rdrift=1\nVg g 0 12.5\nVs s 0 " +
        std::to_string(supply) + "\nR1 s d 1\nM1 d g 0 msic\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << supply;
    return x.empty() ? 0.0 : x[parsed.circuit->get_node_id("d") - 1];
}

}  // namespace

TEST(SicReverse, ConductsTheSameInBothDirections) {
    const double forward = drain_voltage(1e-3);
    const double reverse = drain_voltage(-1e-3);
    EXPECT_NEAR(reverse, -forward, 1e-8);  // within the Newton tolerances
    // About 1 mV / 2.1 ohm through R1 (the channel is not quite linear)
    EXPECT_NEAR(1e-3 - forward, 1e-3 / 2.1, 2e-6);
}

TEST(SicReverse, DriftResistanceHoldsAtZeroDrainVoltage) {
    // At VDS = 0 the channel (KP W/L VOV = 10 S) is in series with RDRIFT
    // = 1 ohm: gds = 10 / (1 + 10) = 0.909 S, not the 10 S of the channel
    // alone. An AC source on the drain at VDS = 0 drives gds amperes.
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.model msic nmos level=10 kp=1 w=1 l=1 vto=2.5 theta=0 rdrift=1 cgs=0 cgd=0\nVg g 0 12.5\n"
        "VD d 0 DC 0 AC 1\nM1 d g 0 msic\n.end\n");
    SimulationConfig config = parsed.config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1.0;
    config.ac.stop_freq = 1.0;
    config.ac.num_points = 1;
    solvers::ACSolver solver(*parsed.circuit, config);
    const auto result = solver.solve();
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const auto& x = result.frequency_points.at(0).solution;
    EXPECT_NEAR(std::abs(x.back()), 10.0 / 11.0, 1e-4);  // I(VD), the last unknown
}
