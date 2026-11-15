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
#include "kirimaspice/solvers/transient_solver.hpp"

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

TEST(SmallSignalConsistency, Igbt) {
    // Ic = KP (VGE - VGE_TH)^2, times VCE / VCE_SAT below VCE_SAT; the
    // capacitances do not matter at 1 Hz
    const std::string values = " level=21 kp=1e-3 vge_th=2 vce_sat=2";
    const std::string n_card = ".model m nmos" + values;
    const std::string p_card = ".model m pmos" + values;
    expect_ac_gain_is_the_dc_slope({"N, active", "M1 d g 0 m", n_card, 10.0, 1e3, 3.0});
    expect_ac_gain_is_the_dc_slope({"N, saturation", "M1 d g 0 m", n_card, 10.0, 1e3, 5.0});
    expect_ac_gain_is_the_dc_slope({"P, active", "M1 d g 0 m", p_card, -10.0, 1e3, -3.0});
    expect_ac_gain_is_the_dc_slope({"P, saturation", "M1 d g 0 m", p_card, -10.0, 1e3, -5.0});
}

// The IGBT's channel noise follows its bias: 4 k T (2/3) gm, with gm =
// 2 KP (VGE - VGE_TH) in the active region, 2 mS at VGE = 3 V and 6 mS at 5 V.
TEST(SmallSignalConsistency, IgbtNoiseFollowsTheBias) {
    for (const Real vg : {3.0, 5.0}) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("igbt noise\nVCC c 0 DC 10\nVG g 0 DC " +
                                          std::to_string(vg) +
                                          "\nM1 c g 0 m\n.model m nmos level=21 kp=1e-3 "
                                          "vge_th=2 vce_sat=2\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr);
        DCSolver solver(*parsed.circuit, parsed.config);
        DenseVector solution;
        SolverStats stats;
        ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        const auto sources = parsed.circuit->get_device("M1")->get_noise_sources(solution, 1e3);
        ASSERT_FALSE(sources.empty());
        const Real gm = 2.0 * 1e-3 * (vg - 2.0);
        const Real kelvin = 300.15;
        EXPECT_NEAR(sources[0].spectral_density,
                    4.0 * 1.380649e-23 * kelvin * (2.0 / 3.0) * gm,
                    1e-6 * 4.0 * 1.380649e-23 * kelvin * gm)
            << "VG=" << vg;
    }
}

TEST(SmallSignalConsistency, Level1Mosfet) {
    // The netlist's level-1 MOSFET has no capacitances; at 1 Hz nothing else
    // would matter anyway
    const std::string n_card = ".model m nmos level=1 kp=2e-5 vto=0.7";
    const std::string p_card = ".model m pmos level=1 kp=2e-5 vto=-0.7";
    expect_ac_gain_is_the_dc_slope(
        {"N, saturation", "M1 d g 0 0 m W=10u L=1u", n_card, 5.0, 10e3, 2.0});
    expect_ac_gain_is_the_dc_slope(
        {"N, linear region", "M1 d g 0 0 m W=10u L=1u", n_card, 5.0, 10e3, 4.0});
    expect_ac_gain_is_the_dc_slope(
        {"P, saturation", "M1 d g 0 0 m W=10u L=1u", p_card, -5.0, 10e3, -2.0});
}

// With VDS < 0 drain and source change roles, as in SPICE: the drain at 0 V,
// the gate at 5 V and the source fed from 1 V through 1 kOhm carry
// beta ((VGD - VTO) VSD - VSD^2 / 2) = (1 - VS) / 1k with beta = 2e-4 A/V^2,
// so 0.1 VS^2 - 1.86 VS + 1 = 0 and VS = 0.554144 V (ngspice-47: 0.554 V).
TEST(SmallSignalConsistency, Level1MosfetConductsBackwards) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "reverse\nVG g 0 DC 5\nV1 a 0 DC 1\nR1 a s 1k\nM1 0 g s 0 m W=10u L=1u\n"
        ".model m nmos level=1 kp=2e-5 vto=0.7\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    const Real vs = (1.86 - std::sqrt(1.86 * 1.86 - 0.4)) / 0.2;
    EXPECT_NEAR(solution[parsed.circuit->get_node_id("s") - 1], vs, 1e-6);
}

// The AC solver passes the angular frequency omega to stamp_ac; a device's
// capacitance C then has the admittance j omega C. A gate driven through R
// has H = 1 / (1 + j omega R C), so Im(1/H) / (omega R) is the gate
// capacitance at the operating point, the same at every frequency and the
// same as the time constant of a small step in the transient gives. BSIM4
// took omega for a frequency and multiplied it by 2 pi again.
TEST(SmallSignalConsistency, Bsim4GateCapacitanceInAcAndTransient) {
    const std::string card =
        ".model m nmos level=14 version=4.8.3 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4\n";
    const Real r = 1e5;
    auto ac_capacitance = [&](Real f) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("cg\nVG in 0 DC 0 AC 1\nRG in g 100k\nM1 0 g 0 0 m L=1u W=10u\n" +
                                          card + ".ac lin 1 " + std::to_string(f) + " " +
                                          std::to_string(f) + "\n.end\n");
        EXPECT_NE(parsed.circuit, nullptr);
        ACSolver solver(*parsed.circuit, parsed.config);
        const ACResult result = solver.solve();
        EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
        const Complex h = result.frequency_points[0].solution[parsed.circuit->get_node_id("g") - 1];
        return (1.0 / h).imag() / (2.0 * 3.14159265358979323846 * f * r);
    };
    const Real c_low = ac_capacitance(1e3);
    const Real c_high = ac_capacitance(1e5);
    EXPECT_GT(c_low, 1e-15);
    EXPECT_NEAR(c_high, c_low, 1e-3 * c_low);

    // A 1 mV step charges the gate with tau = R C: at t = tau the gate is at
    // 1 - e^-1 of the step (backward Euler with dt = tau / 200)
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "cg tran\nVG in 0 DC 0 PULSE(0 1m 0 1f 1f 1 2)\nRG in g 100k\nM1 0 g 0 0 m L=1u W=10u\n" +
        card + ".options method=euler\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    SimulationConfig config = parsed.config;
    config.transient.use_adaptive_timestep = false;
    TransientSolver solver(*parsed.circuit, config);
    const Real tau = r * c_low;
    const TransientResult result = solver.solve(0.0, tau, tau / 200.0);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const Real vg = result.timepoints.back().node_voltages[parsed.circuit->get_node_id("g") - 1];
    EXPECT_NEAR(vg / 1e-3, 1.0 - std::exp(-1.0), 0.01);
}

namespace {

// The gate of a MOSFET card charged through 100 kOhm by a 1 mV step, with
// the integration method given: at t = tau = R C (C from AC at 1 kHz) the
// gate is at 1 - e^-1 of the step, whichever method (within 1 %).
void expect_gate_step_follows_tau(const std::string& card, const std::string& element) {
    const Real r = 1e5;
    parser::NetlistParser ac_parser;
    auto ac_parsed = ac_parser.parse_string("cg\nVG in 0 DC 0 AC 1\nRG in g 100k\n" + element +
                                            "\n" + card + "\n.ac lin 1 1000 1000\n.end\n");
    ASSERT_NE(ac_parsed.circuit, nullptr);
    ACSolver ac(*ac_parsed.circuit, ac_parsed.config);
    const ACResult ac_result = ac.solve();
    ASSERT_EQ(ac_result.status, ConvergenceStatus::SUCCESS);
    const Complex h =
        ac_result.frequency_points[0].solution[ac_parsed.circuit->get_node_id("g") - 1];
    const Real c = (1.0 / h).imag() / (2.0 * 3.14159265358979323846 * 1e3 * r);
    ASSERT_GT(c, 1e-16);
    for (const char* method : {"euler", "trap", "gear"}) {
        SCOPED_TRACE(method);
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("cg tran\nVG in 0 DC 0 PULSE(0 1m 0 1f 1f 1 2)\nRG in g 100k\n" +
                                          element + "\n" + card + "\n.options method=" + method +
                                          "\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr);
        SimulationConfig config = parsed.config;
        config.transient.use_adaptive_timestep = false;
        TransientSolver solver(*parsed.circuit, config);
        const Real tau = r * c;
        const TransientResult result = solver.solve(0.0, tau, tau / 200.0);
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        const Real vg =
            result.timepoints.back().node_voltages[parsed.circuit->get_node_id("g") - 1];
        EXPECT_NEAR(vg / 1e-3, 1.0 - std::exp(-1.0), 0.01);
    }
}

}  // namespace

// The capacitances of BSIM3 and BSIM4 integrate as a capacitor does: the
// trapezoidal rule with its history current, BDF2 with the step in beta0.
TEST(SmallSignalConsistency, BsimGateCapacitanceUnderEachMethod) {
    expect_gate_step_follows_tau(
        ".model m nmos level=14 version=4.8.3 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4",
        "M1 0 g 0 0 m L=1u W=10u");
    expect_gate_step_follows_tau(".model m nmos level=8 tox=3e-9 vth0=0.5 u0=0.067 vsat=8e4",
                                 "M1 0 g 0 0 m L=1u W=10u");
    expect_gate_step_follows_tau(".model m pmos level=8 tox=3e-9 vth0=-0.5 u0=0.025 vsat=8e4",
                                 "M1 0 g 0 0 m L=1u W=10u");
}

// The MESFET with its source grounded: the stamps take gm and gds whatever
// terminal is grounded (MESFET::channel_at). The PMF stage mirrors the NMF
// one, with VTO +2 V for -2 V.
TEST(SmallSignalConsistency, Mesfet) {
    const std::string params = " beta=0.01 lambda=0.05 alpha=2 cgs=0 cgd=0 cds=0";
    const std::string n = "Z1 d g 0 NMF" + params;
    const std::string p = "Z1 d g 0 PMF" + params;
    expect_ac_gain_is_the_dc_slope({"N, linear region", n, "", 5.0, 200.0, 0.0});
    expect_ac_gain_is_the_dc_slope({"N, saturation", n, "", 10.0, 200.0, -1.0});
    expect_ac_gain_is_the_dc_slope({"P, linear region", p, "", -5.0, 200.0, 0.0});
    expect_ac_gain_is_the_dc_slope({"P, saturation", p, "", -10.0, 200.0, 1.0});
}

// Every voltage of the PMF stage is minus that of the NMF stage, and its
// drain voltage satisfies KCL with the channel current of the card's
// formula (Curtice, BETA (Vgs - VTO)^2 (1 + LAMBDA Vds) tanh(ALPHA Vds))
TEST(SmallSignalConsistency, MesfetPMirrorsN) {
    const std::string params = " beta=0.01 lambda=0.05 alpha=2 cgs=0 cgd=0 cds=0";
    for (const Real vg : {0.0, -1.0, -1.5}) {
        SCOPED_TRACE(vg);
        const Real vd_n = drain_voltage({"N", "Z1 d g 0 NMF" + params, "", 10.0, 200.0, vg}, vg);
        const Real vd_p =
            drain_voltage({"P", "Z1 d g 0 PMF" + params, "", -10.0, 200.0, -vg}, -vg);
        EXPECT_NEAR(vd_p, -vd_n, 1e-9);
        const Real vov = vg + 2.0;
        const Real id = 0.01 * vov * vov * (1.0 + 0.05 * vd_n) * std::tanh(2.0 * vd_n);
        EXPECT_NEAR((10.0 - vd_n) / 200.0, id, 1e-9);
    }
}

// BSIM4 above 10 V: the model's equations hold for any terminal voltage, and
// the stamp takes them at the voltages it has (no clamp). ngspice-47 gives
// V(d) = 13.06208 V for this stage (run on 2026-11-15).
TEST(SmallSignalConsistency, Bsim4AboveTenVolts) {
    const std::string card =
        ".model m nmos level=14 version=4.8.3 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4";
    const Stage stage{"N, V(d) above 10 V", "M1 d g 0 0 m L=1u W=10u", card, 40.0, 1e3, 1.0};
    EXPECT_NEAR(drain_voltage(stage, stage.vg), 13.06208, 1e-4);
    expect_ac_gain_is_the_dc_slope(stage);
}
