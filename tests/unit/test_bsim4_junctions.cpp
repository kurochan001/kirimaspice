// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The junction capacitances of BSIM4 (BSIM4Core::compute_capacitances): the
// bottom junction of area A has CJ A / (1 + Vr / PB)^MJ at the reverse bias
// Vr, for N and P alike, and the drain's parameters that the card does not
// give are the source's (bsim4_params_from_model). A source fed through
// 1 kOhm, with the device off and its other capacitances 0, has
// C = Im(1 / V(s)) / (omega R). ngspice-47 gives the same capacitances for
// these netlists (run on 2026-11-15).

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;

namespace {

const std::string kCard =
    " level=14 version=4.8.3 toxe=3e-9 u0=0.067 vsat=8e4 cgso=0 cgdo=0 cgbo=0 cf=0";

Real capacitance_at_s(const std::string& netlist_body) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nVIN in 0 DC 0 AC 1\nR1 in s 1k\n" + netlist_body +
                                      "\n.ac lin 1 1Meg 1Meg\n.end\n");
    solvers::ACSolver solver(*parsed.circuit, parsed.config);
    const auto ac = solver.solve();
    EXPECT_EQ(ac.status, ConvergenceStatus::SUCCESS);
    const Complex v = ac.frequency_points.at(0).solution[parsed.circuit->get_node_id("s") - 1];
    return (1.0 / v).imag() / (2.0 * M_PI * 1e6 * 1e3);
}

}  // namespace

// 1e-3 F/m^2 over 1e-10 m^2, 1 V reverse, PB 0.8 V, MJ 0.5: N with its bulk
// at -1 V, P with its bulk at +1 V
TEST(Bsim4Junctions, SourceJunctionOfNAndP) {
    const Real expected = 1e-13 / std::sqrt(1.0 + 1.0 / 0.8);
    const std::string junction = " cjs=1e-3 mjs=0.5 pbs=0.8 cjsws=0 cjswgs=0";
    EXPECT_NEAR(capacitance_at_s("VB b 0 DC -1\nM1 0 0 s b m L=1u W=10u AS=1e-10\n.model m nmos" +
                                 kCard + " vth0=0.5" + junction),
                expected, 1e-6 * expected);
    EXPECT_NEAR(capacitance_at_s("VB b 0 DC 1\nM1 0 0 s b m L=1u W=10u AS=1e-10\n.model m pmos" +
                                 kCard + " vth0=-0.5" + junction),
                expected, 1e-6 * expected);
}

// The drain junction of a card that gives only the source's: MJS 0.4
TEST(Bsim4Junctions, DrainTakesTheSourceParameters) {
    const Real expected = 1e-13 / std::pow(1.0 + 1.0 / 0.8, 0.4);
    EXPECT_NEAR(capacitance_at_s("VB b 0 DC -1\nM1 s 0 0 b m L=1u W=10u AD=1e-10\n.model m nmos" +
                                 kCard + " vth0=0.5 cjs=1e-3 mjs=0.4 pbs=0.8 cjsws=0 cjswgs=0"),
                expected, 1e-6 * expected);
}

// With fixed terminal biases, changing CF adds W*CF at each of the source
// and drain terminals. Subtracting the CF=0 result isolates the fringe.
TEST(Bsim4Junctions, OuterFringeOnBothSidesOfTheGate) {
    const std::string card = "\n.model m nmos level=14 toxe=3n epsrox=3.9 vth0=0.5";
    constexpr Real width = 10e-6;
    const Real default_cf = 2.0 * 3.9 * 8.8541878128e-12 / M_PI * std::log1p(400.0 / 3.0);
    for (const std::string device : {"M1 s 0 0 0 m W=10u L=1u", "M1 0 0 s 0 m W=10u L=1u"}) {
        const Real baseline = capacitance_at_s(device + card + " cf=0");
        EXPECT_NEAR(capacitance_at_s(device + card + " cf=2e-10") - baseline,
                    width * 2e-10, 1e-20);
        EXPECT_NEAR(capacitance_at_s(device + card) - baseline, width * default_cf, 1e-20);
    }
}

// BSIM4 manual 11.2 and 12.2; ngspice-47 gives these zero-bias values
// with W=10 um and 1 nF/m on the isolation edge and (by default) gate edge.
TEST(Bsim4Junctions, GateEdgeAndIsolationPerimeters) {
    struct Example {
        std::string instance, model;
        Real source_ff, drain_ff;
    };
    for (const Example& e : std::vector<Example>{
             {"", "", 20, 20}, {"PS=0 PD=0", "", 10, 10},
             {"PS=30u PD=20u", "", 30, 20}, {"PS=30u PD=20u", "permod=0", 40, 30},
             {"NF=2", "", 20, 10}, {"NF=2 MIN=1", "", 10, 20},
             {"NF=3", "", 40.0 / 3, 40.0 / 3}}) {
        SCOPED_TRACE(e.instance + " " + e.model);
        const std::string model = " m W=10u L=1u " + e.instance +
            "\n.model m nmos level=14 vth0=0.5 cjs=0 cgso=0 cgdo=0 cf=0 cjsws=1n " + e.model;
        EXPECT_NEAR(capacitance_at_s("M1 0 0 s 0" + model), e.source_ff * 1e-15, 1e-20);
        EXPECT_NEAR(capacitance_at_s("M1 s 0 0 0" + model), e.drain_ff * 1e-15, 1e-20);
    }
}
