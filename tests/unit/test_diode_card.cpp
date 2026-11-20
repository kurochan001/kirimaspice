// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The diode card gives IS, N, CJO (or CJ0), VJ (or PB) and M (or MJ), with
// the defaults of ngspice-47 (VJ 1 V, M 0.5); ngspice-47 takes the same
// three aliases (showmod, 2026-11-21). The junction capacitance in reverse
// bias is CJO / (1 - V/VJ)^M (the SPICE diode), so a resistor R in series
// with the diode at a reverse bias V is a low-pass of 1 / (1 + j w R C(V)).
// A name the diode does not use is named in a warning.

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;

namespace {

// |v(a)| at frequency f of V1 (DC -2 V, AC 1) - 1 kohm - a - diode to ground
double ac_magnitude(const std::string& model, double f) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 in 0 DC -2 AC 1\nR1 in a 1k\nD1 a 0 DM\n" + model +
                                      "\n.end\n");
    SimulationConfig config = parsed.config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = f;
    config.ac.stop_freq = f;
    config.ac.num_points = 1;
    solvers::ACSolver solver(*parsed.circuit, config);
    const auto result = solver.solve();
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId a = parsed.circuit->get_node_id("a");
    return std::abs(result.frequency_points.at(0).solution.at(a - 1));
}

double expected_magnitude(double cjo, double vj, double m, double f) {
    const double c = cjo / std::pow(1.0 + 2.0 / vj, m);
    const double wrc = 2.0 * std::numbers::pi * f * 1e3 * c;
    return 1.0 / std::sqrt(1.0 + wrc * wrc);
}

}  // namespace

TEST(DiodeCard, JunctionCapacitanceFromTheCard) {
    const double f = 50e6;  // near the corner of 1 kohm and about 1 pF
    EXPECT_NEAR(ac_magnitude(".model DM D CJO=2p VJ=0.8 M=0.4", f),
                expected_magnitude(2e-12, 0.8, 0.4, f), 1e-6);
}

TEST(DiodeCard, AliasesAndDefaults) {
    const double f = 50e6;
    EXPECT_NEAR(ac_magnitude(".model DM D CJ0=2p PB=0.8 MJ=0.4", f),
                expected_magnitude(2e-12, 0.8, 0.4, f), 1e-6);
    // VJ 1 V and M 0.5 when the card does not give them
    EXPECT_NEAR(ac_magnitude(".model DM D CJO=2p", f), expected_magnitude(2e-12, 1.0, 0.5, f), 1e-6);
}

TEST(DiodeCard, UnusedNamesAreWarned) {
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("diode_card", sink);
    logger->set_level(spdlog::level::warn);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    parser::NetlistParser parser;
    parser.parse_string("t\nV1 a 0 1\nD1 a 0 DM AREA=2\n.model DM D IS=1e-14 FOO=3\n.end\n");
    spdlog::set_default_logger(previous);
    EXPECT_NE(captured.str().find("Diode model DM: foo is not used"), std::string::npos) << captured.str();
    EXPECT_NE(captured.str().find("D1: 'AREA=2' is not used"), std::string::npos) << captured.str();
}
