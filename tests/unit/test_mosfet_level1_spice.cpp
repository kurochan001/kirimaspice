// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The level-1 MOSFET of a card is SPICE's (Shichman-Hodges): with
// Vth = VTO + GAMMA (sqrt(PHI - VBS) - sqrt(PHI)),
//   saturation  Id = KP/2 W/L (VGS - Vth)^2 (1 + LAMBDA VDS),
//   linear      Id = KP W/L ((VGS - Vth) VDS - VDS^2/2) (1 + LAMBDA VDS),
// and with the body forward biased sqrt(PHI) - VBS / (2 sqrt(PHI)) in
// place of sqrt(PHI - VBS) (MOSFET_Level1::threshold). The defaults are
// ngspice-47's: VTO 0, KP 2e-5, GAMMA 0, PHI 0.6, LAMBDA 0, W = L = 100 um.
// ngspice-47 gives the same drain currents (run on 2026-11-21): 1.60082e-4 A
// at VBS = -1 V; 4.18233e-4, 4.80605e-4 and 5.47310e-4 A at VBS = 0.3, 0.5
// and 0.7 V; 2.25e-5 A for the card of defaults.

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

const std::string kCard =
    ".model NM NMOS LEVEL=1 VTO=0.7 KP=1e-4 LAMBDA=0.02 GAMMA=0.5 PHI=0.6\n"
    ".model PM PMOS LEVEL=1 VTO=-0.7 KP=1e-4 LAMBDA=0.02 GAMMA=0.5 PHI=0.6\n";

// The drain current, into the drain, of M1 at the terminal voltages given
double drain_current(const std::string& model, double vd, double vg, double vb,
                     const std::string& card = kCard) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nVD d 0 " + std::to_string(vd) + "\nVG g 0 " +
                                      std::to_string(vg) + "\nVB b 0 " + std::to_string(vb) +
                                      "\nM1 d g 0 b " + model + " W=10u L=1u\n" + card + ".end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    // I(VD) flows into the source's + terminal: minus the current into the drain
    return -*solver.get_branch_current("VD");
}

double vth(double vbs) {
    const double sqrt_phi = std::sqrt(0.6);
    const double sarg = vbs <= 0.0 ? std::sqrt(0.6 - vbs) : std::max(0.0, sqrt_phi - vbs / (2 * sqrt_phi));
    return 0.7 + 0.5 * (sarg - sqrt_phi);
}

double saturation(double vgs, double vds, double vbs) {
    const double vov = vgs - vth(vbs);
    return 1e-4 / 2 * 10 * vov * vov * (1 + 0.02 * vds);
}

}  // namespace

TEST(MosfetLevel1Spice, SaturationWithBodyEffectAndLambda) {
    EXPECT_NEAR(drain_current("NM", 2.0, 1.5, -1.0), saturation(1.5, 2.0, -1.0), 1e-12);
    EXPECT_NEAR(drain_current("NM", 2.0, 1.5, -1.0), 1.60082e-4, 1e-9);  // ngspice-47
}

TEST(MosfetLevel1Spice, ForwardBodyBiasTakesTheTangent) {
    const double ngspice[] = {4.18233e-4, 4.80605e-4, 5.47310e-4};
    const double vbs[] = {0.3, 0.5, 0.7};
    for (int i = 0; i < 3; ++i) {
        SCOPED_TRACE(vbs[i]);
        EXPECT_NEAR(drain_current("NM", 2.0, 1.5, vbs[i]), saturation(1.5, 2.0, vbs[i]), 1e-12);
        EXPECT_NEAR(drain_current("NM", 2.0, 1.5, vbs[i]), ngspice[i], 1e-9);
    }
}

TEST(MosfetLevel1Spice, LinearRegionWithLambda) {
    const double vov = 2.5 - vth(-1.0), vds = 0.5;
    const double expected = 1e-4 * 10 * (vov * vds - vds * vds / 2) * (1 + 0.02 * vds);
    EXPECT_NEAR(drain_current("NM", vds, 2.5, -1.0), expected, 1e-12);
}

TEST(MosfetLevel1Spice, PChannelMirrorsNChannel) {
    EXPECT_NEAR(drain_current("PM", -2.0, -1.5, 1.0), -drain_current("NM", 2.0, 1.5, -1.0), 1e-12);
}

TEST(MosfetLevel1Spice, DefaultsOfTheCard) {
    // VTO 0, KP 2e-5, W = L given on the line: KP/2 (W/L) VGS^2 = 2.25e-5 A at
    // W = L and VGS = 1.5 V; the line here gives W/L = 10
    EXPECT_NEAR(drain_current("ND", 2.0, 1.5, 0.0, ".model ND NMOS LEVEL=1\n"), 2e-5 / 2 * 10 * 1.5 * 1.5,
                1e-12);
}

TEST(MosfetLevel1Spice, BodyTransconductanceInAc) {
    // An AC source on the bulk, the drain held by VD: the drain current is
    // gmb vb, with gmb = gm GAMMA / (2 sqrt(PHI - VBS)) and gm = KP W/L (VGS - Vth)(1 + LAMBDA VDS)
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nVD d 0 2\nVG g 0 1.5\nVB b 0 DC -1 AC 1\nR1 d2 d 1\n"
                                      "M1 d2 g 0 b NM W=10u L=1u\n" + kCard + ".end\n");
    SimulationConfig config = parsed.config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = config.ac.stop_freq = 1e3;
    config.ac.num_points = 1;
    solvers::ACSolver solver(*parsed.circuit, config);
    const auto result = solver.solve();
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    // The AC current through R1 (1 ohm) is v(d2): v(d2) = -gmb R / (1 + gds R)
    const NodeId d2 = parsed.circuit->get_node_id("d2");
    const double v_d2 = std::abs(result.frequency_points.at(0).solution.at(d2 - 1));
    // R1 drops Id x 1 ohm of the 2 V (one fixed-point step is exact to 1e-12)
    const double vds = 2.0 - saturation(1.5, 2.0 - saturation(1.5, 2.0, -1.0), -1.0);
    const double vov = 1.5 - vth(-1.0);
    const double gm = 1e-4 * 10 * vov * (1 + 0.02 * vds);
    const double gmb = gm * 0.5 / (2 * std::sqrt(0.6 + 1.0));
    const double gds = 1e-4 / 2 * 10 * vov * vov * 0.02;
    EXPECT_NEAR(v_d2, gmb / (1 + gds), 1e-9 * gmb);
}

TEST(MosfetLevel1Spice, UnusedCardNamesAreWarned) {
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("level1", sink);
    logger->set_level(spdlog::level::warn);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    parser::NetlistParser parser;
    parser.parse_string("t\nV1 d 0 1\nM1 d d 0 0 NX\n.model NX NMOS LEVEL=1 TOX=1e-8\n.end\n");
    spdlog::set_default_logger(previous);
    EXPECT_NE(captured.str().find("MOSFET model NX (level 1): tox is not used"), std::string::npos)
        << captured.str();
}
