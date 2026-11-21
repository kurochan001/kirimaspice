// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// M BSIM4 devices in parallel carry M times every capacitance, as they
// carry M times the current: driven through 1 Mohm at 100 MHz, the gate of
// M = 2 is the gate of two M = 1 devices side by side.

#include <gtest/gtest.h>

#include <complex>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;

namespace {

std::complex<double> gate_voltage(const std::string& devices) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVD d 0 1\nVG in 0 DC 1.8 AC 1\nRG in g 1meg\n" + devices +
        ".model NB NMOS LEVEL=14 VERSION=4.8.1 TOXE=3e-9 VTH0=0.5 U0=0.067 VSAT=8e4\n.end\n");
    SimulationConfig config = parsed.config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1e8;
    config.ac.stop_freq = 1e8;
    config.ac.num_points = 1;
    solvers::ACSolver solver(*parsed.circuit, config);
    const auto result = solver.solve();
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    return result.frequency_points.at(0).solution.at(parsed.circuit->get_node_id("g") - 1);
}

}  // namespace

TEST(Bsim4MultiplierCapacitance, MultiplierScalesTheGateCapacitance) {
    const auto two = gate_voltage("M1 d g 0 0 NB W=10u L=1u M=2\n");
    const auto pair = gate_voltage("M1 d g 0 0 NB W=10u L=1u\nM2 d g 0 0 NB W=10u L=1u\n");
    const auto one = gate_voltage("M1 d g 0 0 NB W=10u L=1u\n");
    EXPECT_NEAR(std::abs(two - pair), 0.0, 1e-9 * std::abs(pair));
    EXPECT_GT(std::abs(two - one), 1e-3 * std::abs(one));  // M does change it
}
