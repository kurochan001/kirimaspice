// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// SPICE's level-1 temperature laws (MOSFET_Level1::set_temperature): with
// r = T/TNOM, Eg(T) = 1.16 - 7.02e-4 T^2/(T + 1108) and
// p(T) = -3 Vt(T) ln(T/300.15) + Eg(T) - Eg(300.15) T/300.15,
//   KP(T) = KP r^-1.5, PHI(T) = r PHI + p(T) - r p(TNOM),
//   Vbi(T) = VTO - GAMMA sqrt(PHI) + (PHI(T) - PHI)/2 +- (Eg(TNOM) - Eg(T))/2,
// the threshold at VBS being Vbi(T) + GAMMA sqrt(PHI(T) - VBS) in the
// device's own terms. By hand, VTO(100 C) = Vbi + GAMMA sqrt(PHI(T)) =
// 0.568441 V for VTO 0.7, GAMMA 0.5, PHI 0.6.
//
// ngspice-47, run 2026-11-21: W/L = 10u/1u, VGS = 1.5 V, VDS = 2 V, VBS = 0
// (PMOS: -1.5, -2, 0), "option temp", source current i(vs), which the
// drain-bulk junction leakage of ngspice's level 1 (not modelled here) does
// not reach:
//   nm (vto=0.7 kp=1e-4 gamma=0.5 phi=0.6): -40 C 3.478635e-04,
//     100 C 3.130205e-04, 150 C 3.177351e-04
//   nz (the same with gamma=0): -40 C 3.944378e-04, 100 C 2.749908e-04,
//     150 C 2.552030e-04
//   pm (vto=-0.7, otherwise nm): -40 C -3.31469e-04, 100 C -3.27349e-04,
//     150 C -3.41057e-04
//   at 100 C with VBS = -1 V (PMOS +1 V): nm 1.580630e-04, pm -1.68291e-04
// Before this program applied these laws it gave 230.85 uA for nm at 100 C.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/devices/mosfet_level1.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

const std::string kCards =
    ".model nm nmos level=1 vto=0.7 kp=1e-4 gamma=0.5 phi=0.6\n"
    ".model nz nmos level=1 vto=0.7 kp=1e-4 gamma=0 phi=0.6\n"
    ".model pm pmos level=1 vto=-0.7 kp=1e-4 gamma=0.5 phi=0.6\n";

// I(Vs), the current into the source's voltage source, at temp_c
double source_current(const std::string& model, double temp_c, double vbs) {
    const double s = (model == "pm") ? -1.0 : 1.0;
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "lvl1\nVd d 0 " + std::to_string(s * 2.0) + "\nVg g 0 " + std::to_string(s * 1.5) +
        "\nVb b 0 " + std::to_string(vbs) + "\nVs s 0 0\nM1 d g s b " + model +
        " W=10u L=1u\n" + kCards + ".options temp=" + std::to_string(temp_c) + "\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    return *solver.get_branch_current("Vs");
}

void expect_ngspice(double actual, double ngspice) {
    EXPECT_NEAR(actual, ngspice, 1e-5 * std::abs(ngspice));
}

}  // namespace

TEST(MosfetLevel1Temperature, ThresholdFollowsTheBandGap) {
    devices::MOSFET_N m(1, "M1", 1, 2, 0, 1e-4, 10e-6, 1e-6, 0.7);
    m.set_body(3, 0.5, 0.6, 0.0);
    EXPECT_DOUBLE_EQ(m.threshold_voltage(), 0.7);
    m.set_temperature(373.15);
    EXPECT_NEAR(m.threshold_voltage(), 0.568441, 1e-6);
    EXPECT_NEAR(m.process_transconductance(), 1e-4 * std::pow(373.15 / 300.15, -1.5), 1e-18);
    m.set_temperature(300.15);
    EXPECT_DOUBLE_EQ(m.threshold_voltage(), 0.7);
}

TEST(MosfetLevel1Temperature, NChannelWithBodyEffect) {
    expect_ngspice(source_current("nm", -40.0, 0.0), 3.478635e-04);
    expect_ngspice(source_current("nm", 100.0, 0.0), 3.130205e-04);
    expect_ngspice(source_current("nm", 150.0, 0.0), 3.177351e-04);
    expect_ngspice(source_current("nm", 100.0, -1.0), 1.580630e-04);
}

TEST(MosfetLevel1Temperature, NChannelWithoutBodyEffect) {
    expect_ngspice(source_current("nz", -40.0, 0.0), 3.944378e-04);
    expect_ngspice(source_current("nz", 100.0, 0.0), 2.749908e-04);
    expect_ngspice(source_current("nz", 150.0, 0.0), 2.552030e-04);
}

TEST(MosfetLevel1Temperature, PChannel) {
    expect_ngspice(source_current("pm", -40.0, 0.0), -3.31469e-04);
    expect_ngspice(source_current("pm", 100.0, 0.0), -3.27349e-04);
    expect_ngspice(source_current("pm", 150.0, 0.0), -3.41057e-04);
    expect_ngspice(source_current("pm", 100.0, 1.0), -1.68291e-04);
}
