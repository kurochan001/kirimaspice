// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The temperature laws of SPICE's Gummel-Poon BJT (BJT::set_temperature),
// with r = T/TNOM and f = EG/Vt(T) (r - 1) + XTI ln r:
//   IS(T) = IS exp(f)                 (no emission coefficient)
//   BF(T) = BF r^XTB, BR(T) = BR r^XTB
//   ISE(T) = ISE exp(f/NE) / r^XTB, ISC(T) = ISC exp(f/NC) / r^XTB
// Hand values (TNOM 27 C, XTI 3, EG 1.11): IS = 1e-15 at 100 C is
// 8.507327584245869e-12; BF = 100 with XTB = 1.5 is 138.61736767521504;
// ISE = 1e-13, NE = 2 is 6.6539407697694e-12. With XTI = 2.5, EG = 1.15 at
// 75 C: BR = 2 is 2.498458116231319, ISC = 1e-13 with NC = 1.8 is
// 2.9638662852314134e-12.
//
// ngspice-47, run 2026-11-21 (op; Q1 c b 0 qm, NPN):
//   is=1e-15 bf=100 nf=1.2, .temp 100, Vb = 0.8, Vc = 2:
//     i(vc) = -8.58663e-03, i(vb) = -8.58663e-05
//   is=1e-15 bf=100 ise=1e-13 ne=2 xtb=1.5, .temp 100, Vb = 0.5, Vc = 2:
//     i(vc) = -4.81744e-05, i(vb) = -3.63355e-07
//   Q1 0 b e with is=1e-15 bf=100 br=2 nr=1.3 isc=1e-13 nc=1.8 xtb=1.5
//   xti=2.5 eg=1.15, .temp 75, Vb = 0.6, Ve = 2 (B-C junction forward):
//     i(ve) = -3.19507e-06, i(vb) = -1.47704e-06
// This program's constants (CODATA 2018) put it 1e-5 from those. The
// netlists here give the temperature as .OPTIONS TEMP, into which the
// program resolves a .TEMP of one value before the analyses.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

struct Currents {
    double first = 0.0, base = 0.0;
};

// I(first source) and I(Vb) of the operating point
Currents op(const std::string& netlist, const std::string& first) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    return {*solver.get_branch_current(first), *solver.get_branch_current("Vb")};
}

}  // namespace

TEST(BjtTemperature, ParametersFollowTheGummelPoonLaws) {
    // is, bf, nf, isc (-1: IS), br, nr, vaf, var, ikf, ikr, rb, rbm, irb,
    // ise, ne, isc_leak (SPICE's ISC), nc, cje, vje, mje, cjc, vjc, mjc, tf,
    // tr, kf, af, ff, xti, xtb, eg
    devices::BJT_NPN q(1, "Q1", 1, 2, 0, 1e-15, 100.0, 1.2, -1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0,
                       0.0, 0.0, 0.0, 1e-13, 2.0, 0.0, 2.0, 0.0, 0.75, 0.33, 0.0, 0.75, 0.33,
                       0.0, 0.0, 0.0, 1.0, 1.0, 3.0, 1.5, 1.11);
    q.set_temperature(373.15);
    EXPECT_NEAR(q.saturation_current(), 8.507327584245869e-12, 1e-12 * 8.5e-12);  // NF = 1.2 not in it
    EXPECT_NEAR(q.collector_saturation_current(), 8.507327584245869e-12, 1e-12 * 8.5e-12);
    EXPECT_NEAR(q.forward_beta(), 138.61736767521504, 1e-12 * 138.6);
    EXPECT_NEAR(q.reverse_beta(), 1.3861736767521504, 1e-12 * 1.39);

    devices::BJT_NPN r(2, "Q2", 1, 2, 0, 1e-15, 100.0, 1.0, -1.0, 2.0, 1.3, 0.0, 0.0, 0.0, 0.0,
                       0.0, 0.0, 0.0, 0.0, 1.5, 1e-13, 1.8, 0.0, 0.75, 0.33, 0.0, 0.75, 0.33,
                       0.0, 0.0, 0.0, 1.0, 1.0, 2.5, 1.5, 1.15);
    r.set_temperature(348.15);
    EXPECT_NEAR(r.reverse_beta(), 2.498458116231319, 1e-12 * 2.5);

    // The Gummel-Poon currents with those values (and GMIN 1e-12 across
    // each junction), by hand: ISE(T) in q's base current, ISC(T) in r's
    const auto cq = q.currents(0.5, -1.5);
    EXPECT_NEAR(cq.ic, 3.608362967649816e-06, 1e-9 * 3.6e-06);
    EXPECT_NEAR(cq.ib, 4.1851027345653024e-08, 1e-9 * 4.2e-08);
    const auto cr = r.currents(-1.4, 0.6);
    EXPECT_NEAR(cr.ic, -4.672076958296296e-06, 1e-9 * 4.7e-06);
    EXPECT_NEAR(cr.ib, 1.477027117656849e-06, 1e-9 * 1.5e-06);
}

TEST(BjtTemperature, SaturationCurrentHasNoEmissionCoefficient) {
    const auto c = op("bjt\nVb b 0 0.8\nVc c 0 2\nQ1 c b 0 qm\n"
                      ".model qm npn is=1e-15 bf=100 nf=1.2\n.options temp=100\n.end\n",
                      "Vc");
    EXPECT_NEAR(c.first, -8.58663e-03, 5e-5 * 8.58663e-03);  // ngspice-47
    EXPECT_NEAR(c.base, -8.58663e-05, 5e-5 * 8.58663e-05);
}

TEST(BjtTemperature, LeakageCurrentIsDividedByTheBetaFactor) {
    const auto c = op("bjt\nVb b 0 0.5\nVc c 0 2\nQ1 c b 0 qm\n"
                      ".model qm npn is=1e-15 bf=100 ise=1e-13 ne=2 xtb=1.5\n.options temp=100\n.end\n",
                      "Vc");
    EXPECT_NEAR(c.first, -4.81744e-05, 5e-5 * 4.81744e-05);  // ngspice-47
    EXPECT_NEAR(c.base, -3.63355e-07, 5e-5 * 3.63355e-07);
}

TEST(BjtTemperature, ReverseJunctionAndCollectorLeakage) {
    const auto c = op("bjt\nVb b 0 0.6\nVe e 0 2\nQ1 0 b e qm\n"
                      ".model qm npn is=1e-15 bf=100 br=2 nr=1.3 isc=1e-13 nc=1.8 xtb=1.5 "
                      "xti=2.5 eg=1.15\n.options temp=75\n.end\n",
                      "Ve");
    EXPECT_NEAR(c.first, -3.19507e-06, 5e-5 * 3.19507e-06);  // ngspice-47
    EXPECT_NEAR(c.base, -1.47704e-06, 5e-5 * 1.47704e-06);
}
