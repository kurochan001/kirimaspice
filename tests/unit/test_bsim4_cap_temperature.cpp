// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// BSIM4's capacitances take the saturation velocity at the temperature as
// the current does, VSAT - AT (T/TNOM - 1) (BSIM4 manual, TEMPMOD 0;
// BSIM4Core::saturation_velocity). They took VSAT - AT (T - TNOM), in
// kelvin, which with the default AT = 3.3e4 reaches 0 at TNOM + 2.4 K: the
// input capacitance of the card below at VD = 0.6 V, in saturation, was
// 7.67e-14 F at 27 C and 1.535e-13 F, that of the linear region, at 30 C.
// With VSAT changing by 0.4 % up to 30 C and 4 % up to 60 C, it changes by
// less than 1 %.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;

namespace {

// The gate's capacitance from v(g) behind 1 kohm at 1 MHz: 1/v - 1 = j w R C
double input_capacitance(double celsius) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVG g0 0 DC 1 AC 1\nR1 g0 g 1k\nVD d 0 DC 0.6\nM1 d g 0 0 NX W=10u L=1u\n"
        ".model NX NMOS LEVEL=14\n.options temp=" + std::to_string(celsius) + "\n.ac lin 1 1meg 1meg\n.end\n");
    solvers::ACSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve();
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const auto v = result.frequency_points.at(0).solution.at(parsed.circuit->get_node_id("g") - 1);
    return ((1.0 / v) - 1.0).imag() / (2.0 * M_PI * 1e6 * 1e3);
}

}  // namespace

TEST(BSIM4CapTemperature, InputCapacitanceFollowsTheSaturationVelocity) {
    const double c27 = input_capacitance(27.0);
    ASSERT_GT(c27, 1e-15);
    EXPECT_NEAR(input_capacitance(30.0), c27, 1e-2 * c27);
    EXPECT_NEAR(input_capacitance(60.0), c27, 1e-2 * c27);
}
