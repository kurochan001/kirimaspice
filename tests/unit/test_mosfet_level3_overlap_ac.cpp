// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The overlap capacitances of the level-3 MOSFET in AC (MOSFET_Level3::
// stamp_ac): CGSO W, CGDO W and CGBO L between the gate and the source,
// drain and bulk, also when that terminal is ground. A gate fed through
// R = 1 Mohm by an AC source of 1 V, with one overlap capacitance of 1 pF to
// a grounded terminal and the device cut off (VGS = 0 < VTO), is the RC low
// pass: at omega = 1/(R C) = 1e6 rad/s, v(g) = 1 / (1 + j) = 0.5 - 0.5j.

#include <gtest/gtest.h>

#include <numbers>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;

namespace {

// v(g) at omega = 1e6 rad/s for the element line and the overlap given
Complex gate_voltage(const std::string& element, const std::string& overlap) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "lvl3 overlap\nVin in 0 DC 0 AC 1\nR1 in g 1meg\nVd d 0 0\nVb b 0 0\n" + element +
        "\n.model nm nmos level=3 vto=0.8 kp=6e-5 tox=2e-8 " + overlap + "\n.end\n");
    SimulationConfig config = parsed.config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = config.ac.stop_freq = 1e6 / (2.0 * std::numbers::pi);
    config.ac.num_points = 1;
    solvers::ACSolver solver(*parsed.circuit, config);
    const auto result = solver.solve();
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    return result.frequency_points.at(0).solution.at(parsed.circuit->get_node_id("g") - 1);
}

void expect_rc(const Complex& v) {
    EXPECT_NEAR(v.real(), 0.5, 1e-6);
    EXPECT_NEAR(v.imag(), -0.5, 1e-6);
}

}  // namespace

TEST(MosfetLevel3OverlapAc, GateSourceCapacitanceToAGroundedSource) {
    expect_rc(gate_voltage("M1 d g 0 b nm W=1m L=2u", "cgso=1e-9"));
}

TEST(MosfetLevel3OverlapAc, GateDrainCapacitanceToAGroundedDrain) {
    expect_rc(gate_voltage("M1 0 g s b nm W=1m L=2u\nVs s 0 0", "cgdo=1e-9"));
}

TEST(MosfetLevel3OverlapAc, GateBulkCapacitanceToAGroundedBulk) {
    expect_rc(gate_voltage("M1 d g s 0 nm W=1m L=2u\nVs s 0 0", "cgbo=0.5e-6"));
}

TEST(MosfetLevel3OverlapAc, BetweenTwoNodesAsBefore) {
    // The source held at 0 V by a source of its own: the same low pass
    expect_rc(gate_voltage("M1 d g s b nm W=1m L=2u\nVs s 0 0", "cgso=1e-9"));
}
