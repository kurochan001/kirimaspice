// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A device's equations are written once: its AC stamp linearizes the same
// current as its DC stamp. So the AC gain of a common-source stage at a low
// frequency (where the capacitances do not matter) equals the slope of its DC
// transfer curve, dV(d)/dV(g), taken here as a central difference of two
// operating points.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::solvers;

namespace {

struct Stage {
    std::string what;
    std::string device;  // the element line, drain d, gate g, source 0
    std::string model;   // the .model line
    Real vdd, rl, vg;
};

std::string netlist(const Stage& s, Real vg) {
    return std::string("stage\nVDD dd 0 DC ") + std::to_string(s.vdd) + "\nRL dd d " +
           std::to_string(s.rl) + "\nVG g 0 DC " + std::to_string(vg) + " AC 1\n" + s.device +
           "\n" + s.model + "\n.ac lin 1 1 1\n.end\n";
}

Real drain_voltage(const Stage& s, Real vg) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist(s, vg));
    EXPECT_NE(parsed.circuit, nullptr);
    DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    if (solver.solve(solution, stats) != ConvergenceStatus::SUCCESS) {
        ADD_FAILURE() << "the operating point does not converge";
        return std::nan("");
    }
    return solution[parsed.circuit->get_node_id("d") - 1];
}

void expect_ac_gain_is_the_dc_slope(const Stage& s) {
    SCOPED_TRACE(s.what);
    constexpr Real dv = 1e-4;
    const Real slope = (drain_voltage(s, s.vg + dv) - drain_voltage(s, s.vg - dv)) / (2.0 * dv);

    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist(s, s.vg));
    ASSERT_NE(parsed.circuit, nullptr);
    ACSolver solver(*parsed.circuit, parsed.config);
    const ACResult result = solver.solve();
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.frequency_points.size(), 1u);
    const Complex gain = result.frequency_points[0].solution[parsed.circuit->get_node_id("d") - 1];

    // The stage amplifies at all, so the check is not trivially 0 = 0
    EXPECT_GT(std::abs(slope), 0.1);
    EXPECT_NEAR(gain.real(), slope, 1e-4 * std::abs(slope));
    EXPECT_NEAR(gain.imag(), 0.0, 1e-3 * std::abs(slope));
}

}  // namespace

TEST(SmallSignalConsistency, SicMosfet) {
    // THETA and RDRIFT both act; the P stage mirrors the N one
    const std::string n_card =
        ".model m nmos level=10 kp=1e-3 vto=2 theta=0.1 rdrift=10 w=1 l=1 cgso=0 cgdo=0";
    const std::string p_card =
        ".model m pmos level=10 kp=1e-3 vto=-2 theta=0.1 rdrift=10 w=1 l=1 cgso=0 cgdo=0";
    expect_ac_gain_is_the_dc_slope({"N, linear region", "M1 d g 0 m", n_card, 5.0, 1e3, 4.0});
    expect_ac_gain_is_the_dc_slope({"N, saturation", "M1 d g 0 m", n_card, 10.0, 1e3, 2.5});
    expect_ac_gain_is_the_dc_slope({"P, linear region", "M1 d g 0 m", p_card, -5.0, 1e3, -4.0});
    expect_ac_gain_is_the_dc_slope({"P, saturation", "M1 d g 0 m", p_card, -10.0, 1e3, -2.5});
}

TEST(SmallSignalConsistency, GanHemt) {
    // LAMBDA, RS and RD all act; VTH is that of the effective device, so the
    // P stage, with the voltages mirrored, takes the same card values
    const std::string values = " level=11 beta=0.01 vth=-2 lambda=0.05 rs=2 rd=3 cgs=0 cgd=0 cds=0";
    const std::string n_card = ".model m nmos" + values;
    const std::string p_card = ".model m pmos" + values;
    expect_ac_gain_is_the_dc_slope({"N, linear region", "M1 d g 0 m", n_card, 5.0, 200.0, 0.0});
    expect_ac_gain_is_the_dc_slope({"N, saturation", "M1 d g 0 m", n_card, 20.0, 200.0, -1.5});
    expect_ac_gain_is_the_dc_slope({"P, linear region", "M1 d g 0 m", p_card, -5.0, 200.0, 0.0});
    expect_ac_gain_is_the_dc_slope({"P, saturation", "M1 d g 0 m", p_card, -20.0, 200.0, 1.5});
}
