// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The bipolar transistor is SPICE's Gummel-Poon model (BJT<>::evaluate), one
// device for NPN and PNP, the PNP device the mirror of the NPN device. The
// expected currents are ngspice-47's for the same netlists (run on
// 2026-11-15, printed to 12 digits). They agree to about 1e-5: this program
// takes k/q from the exact SI constants, 8.617333262e-5 V/K, where 2014's
// CODATA values give 8.6173303e-5, and Vbe/Vt ~ 27 times that relative
// difference is ~1e-5 in the current (ngspice's constants not read).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

const std::string kCard =
    "IS=1e-15 BF=100 BR=2 VAF=50 VAR=10 IKF=0.01 IKR=0.005 ISE=1e-14 NE=1.5 ISC=1e-13 NC=1.8";

struct Point {
    Real vc, vb;    // of the NPN device; the PNP device takes -vc, -vb
    Real ic, ib;    // ngspice-47's i(vc), i(vb) for the NPN device
};

// i(vc) and i(vb): the currents of VC and VB, the first two branches
std::pair<Real, Real> source_currents(const std::string& type, Real vc, Real vb) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nVC c 0 " + std::to_string(vc) + "\nVB b 0 " +
                                      std::to_string(vb) + "\nQ1 c b 0 qm\n.model qm " + type +
                                      " " + kCard + "\n.op\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    const std::size_t branches = parsed.circuit->num_nodes() - 1;
    return {x[branches], x[branches + 1]};
}

}  // namespace

TEST(BjtGummelPoon, CurrentsInEachRegionAsNgspice) {
    const Point points[] = {
        {2.0, 0.7, -5.14405923511e-04, -6.35542362693e-06},    // forward active
        {0.1, 0.75, -2.62719266957e-03, -8.28131591095e-05},   // saturation
        {-2.0, -1.4, 1.933544409148e-05, -5.97551328205e-06},  // reverse active
        {3.0, 0.85, -3.69321625247e-02, -1.90443830689e-03},   // high injection
    };
    for (const auto& p : points) {
        SCOPED_TRACE(testing::Message() << "VC = " << p.vc << ", VB = " << p.vb);
        const auto [ic_n, ib_n] = source_currents("npn", p.vc, p.vb);
        EXPECT_NEAR(ic_n, p.ic, 2e-5 * std::abs(p.ic));
        EXPECT_NEAR(ib_n, p.ib, 2e-5 * std::abs(p.ib));
        // The PNP device is the mirror: -VC, -VB give -i(vc), -i(vb)
        const auto [ic_p, ib_p] = source_currents("pnp", -p.vc, -p.vb);
        EXPECT_NEAR(ic_p, -ic_n, 1e-12 * std::abs(ic_n));
        EXPECT_NEAR(ib_p, -ib_n, 1e-12 * std::abs(ib_n));
    }
}

namespace {

const std::string kCapCard =
    "IS=1e-15 BF=100 CJE=1p VJE=0.8 MJE=0.4 CJC=0.5p VJC=0.7 MJC=0.33 TF=1n TR=10n";

// The capacitance at the base, fed through 1 kOhm from a source of DC value
// vb and AC 1, the collector at vc: Im(1 / V(b)) / (omega R)
Real base_capacitance(const std::string& type, Real vb, Real vc) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVIN in 0 DC " + std::to_string(vb) + " AC 1\nR1 in b 1k\nVC c 0 " + std::to_string(vc) +
        "\nQ1 c b 0 qm\n.model qm " + type + " " + kCapCard + "\n.ac lin 1 1Meg 1Meg\n.end\n");
    solvers::ACSolver solver(*parsed.circuit, parsed.config);
    const auto ac = solver.solve();
    EXPECT_EQ(ac.status, ConvergenceStatus::SUCCESS);
    const Complex v = ac.frequency_points.at(0).solution[parsed.circuit->get_node_id("b") - 1];
    return (1.0 / v).imag() / (2.0 * M_PI * 1e6 * 1e3);
}

}  // namespace

// The junction capacitances (BJT<>::capacitances): in cutoff the depletion
// capacitances alone, Cbe(-1 V) + Cbc(-3 V); forward biased, with TF's
// diffusion charge too. ngspice-47 gives 1.011614052098e-12 F and
// 5.075087811496e-12 F; the PNP device is the mirror.
TEST(BjtGummelPoon, CapacitancesAsNgspice) {
    EXPECT_NEAR(base_capacitance("npn", -1.0, 2.0), 1.011614052098e-12, 1e-9 * 1.0116e-12);
    EXPECT_NEAR(base_capacitance("pnp", 1.0, -2.0), 1.011614052098e-12, 1e-9 * 1.0116e-12);
    EXPECT_NEAR(base_capacitance("npn", 0.65, 2.0), 5.075087811496e-12, 2e-5 * 5.075e-12);
    EXPECT_NEAR(base_capacitance("pnp", -0.65, -2.0), base_capacitance("npn", 0.65, 2.0), 1e-20);
}

// A transient through the capacitances (trapezoidal rule): an inverter with a
// 1 V pulse into its base through 10 kOhm. ngspice-47 gives V(c) = 5.00798,
// 4.92352, 4.52967, 4.89538 and 4.99615 V at 20, 30, 50, 60 and 80 ns; this
// program follows within 2 mV (its steps and its integration of the
// capacitances, not of the charges, differ).
TEST(BjtGummelPoon, InverterTransientAsNgspice) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "inv\nVCC vcc 0 5\nVIN in 0 PULSE(0 1 10n 1n 1n 40n 100n)\nRB in b 10k\nRC vcc c 1k\n"
        "Q1 c b 0 qm\n.model qm npn " + kCapCard + "\n.options method=trap\n.end\n");
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 100e-9, 0.05e-9);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId c = parsed.circuit->get_node_id("c");
    auto at = [&](Real t) {
        for (std::size_t k = 1; k < result.timepoints.size(); ++k) {
            const auto& a = result.timepoints[k - 1];
            const auto& b = result.timepoints[k];
            if (b.time >= t) {
                return a.node_voltages[c - 1] + (b.node_voltages[c - 1] - a.node_voltages[c - 1]) *
                                                    (t - a.time) / (b.time - a.time);
            }
        }
        return result.timepoints.back().node_voltages[c - 1];
    };
    const Real times[] = {20e-9, 30e-9, 50e-9, 60e-9, 80e-9};
    const Real ngspice[] = {5.00798, 4.92352, 4.52967, 4.89538, 4.99615};
    for (int k = 0; k < 5; ++k) {
        EXPECT_NEAR(at(times[k]), ngspice[k], 3e-3) << "t = " << times[k];
    }
}

// Negating every source and replacing NPN by PNP negates every node voltage,
// including the capacitor history terms for each supported companion method.
TEST(BjtGummelPoon, MirroredTransientAcrossIntegrationMethods) {
    for (const std::string method : {"be", "trap", "gear2", "gear3"}) {
        SCOPED_TRACE(method);
        auto solve = [&](bool pnp) {
            parser::NetlistParser parser;
            const std::string supply = pnp ? "-5" : "5";
            const std::string pulse = pnp ? "-1" : "1";
            auto parsed = parser.parse_string(
                "inverter\nVCC vcc 0 " + supply + "\nVIN in 0 PULSE(0 " + pulse +
                " 10n 1n 1n 40n 100n)\nRB in b 10k\nRC vcc c 1k\nQ1 c b 0 qm\n.model qm " +
                (pnp ? "pnp " : "npn ") + kCapCard + "\n.options method=" + method + "\n.end\n");
            solvers::TransientSolver solver(*parsed.circuit, parsed.config);
            return solver.solve(0.0, 100e-9, 0.1e-9);
        };
        const auto npn = solve(false), pnp = solve(true);
        ASSERT_EQ(npn.status, ConvergenceStatus::SUCCESS);
        ASSERT_EQ(pnp.status, ConvergenceStatus::SUCCESS);
        ASSERT_EQ(npn.timepoints.size(), pnp.timepoints.size());
        for (std::size_t i = 0; i < npn.timepoints.size(); ++i) {
            SCOPED_TRACE(npn.timepoints[i].time);
            EXPECT_DOUBLE_EQ(npn.timepoints[i].time, pnp.timepoints[i].time);
            ASSERT_EQ(npn.timepoints[i].node_voltages.size(), pnp.timepoints[i].node_voltages.size());
            for (std::size_t n = 0; n < npn.timepoints[i].node_voltages.size(); ++n) {
                EXPECT_NEAR(npn.timepoints[i].node_voltages[n], -pnp.timepoints[i].node_voltages[n], 1e-9);
            }
        }
    }
}

template <typename T>
class BjtSharedModel : public testing::Test {};
using BjtTypes = testing::Types<devices::BJT_NPN, devices::BJT_PNP>;
TYPED_TEST_SUITE(BjtSharedModel, BjtTypes);

// IRB changes the base resistance continuously from RB at zero current to
// RBM at high current. Subtracting sqrt(1 + a) - 1 must not yield 0/0 at
// zero bias or at a bias whose base current is tiny compared with IRB.
TYPED_TEST(BjtSharedModel, BaseResistanceIsFiniteAtZeroAndTinyCurrent) {
    TypeParam q(1, "Q1", 1, 2, 0, 1e-15, 100.0, 1.0, -1.0, 1.0, 1.0,
                0.0, 0.0, 0.0, 0.0, 100.0, 10.0, 1e-3);
    for (Real voltage : {0.0, 1e-12, 1e-6, -1e-6}) {
        SCOPED_TRACE(voltage);
        const Real resistance = q.get_base_resistance(voltage, 0.0);
        EXPECT_TRUE(std::isfinite(resistance));
        EXPECT_NEAR(resistance, 100.0, 1e-9);
    }
}

// Changing IS through the sensitivity API changes both transport junctions
// when the reverse junction uses its default IS. Compare the resulting
// currents with a device constructed with the new IS, before and after a
// temperature change, in forward and reverse operation.
TYPED_TEST(BjtSharedModel, ChangingSaturationCurrentUpdatesBothJunctions) {
    TypeParam changed(1, "Q1", 1, 2, 0, 1e-15);
    TypeParam reference(2, "Q2", 1, 2, 0, 2e-15);
    ASSERT_TRUE(changed.set_parameter("IS", 2e-15));
    for (Real temperature : {300.15, 350.0}) {
        changed.set_temperature(temperature);
        reference.set_temperature(temperature);
        for (const auto& voltages : {std::pair{0.7, -1.3}, std::pair{-1.4, 0.6}}) {
            const auto actual = changed.currents(voltages.first, voltages.second);
            const auto expected = reference.currents(voltages.first, voltages.second);
            EXPECT_DOUBLE_EQ(actual.ic, expected.ic);
            EXPECT_DOUBLE_EQ(actual.ib, expected.ib);
        }
    }
    // An explicitly supplied reverse saturation current stays independent.
    TypeParam independent(3, "Q3", 1, 2, 0, 1e-15, 100.0, 1.0, 3e-15);
    ASSERT_TRUE(independent.set_parameter("IS", 2e-15));
    independent.set_temperature(350.0);
    independent.set_temperature(300.15);
    EXPECT_DOUBLE_EQ(independent.collector_saturation_current(), 3e-15);
}

// Retain the capacitance controls of the sensitivity API: at zero bias and
// with no transit time the two small-signal capacitances are CJE and CJC.
TYPED_TEST(BjtSharedModel, SensitivityChangesJunctionCapacitancesAndBetaAliases) {
    TypeParam q(1, "Q1", 1, 2, 0);
    ASSERT_TRUE(q.set_parameter("cje", 2e-12));
    ASSERT_TRUE(q.set_parameter("cjc", 3e-12));
    EXPECT_EQ(q.get_parameter("CJE"), 2e-12);
    EXPECT_EQ(q.get_parameter("CJC"), 3e-12);
    EXPECT_NEAR(q.get_input_capacitance(0.0, 0.0), 2e-12, 1e-21);
    EXPECT_NEAR(q.get_feedback_capacitance(0.0, 0.0), 3e-12, 1e-21);
    EXPECT_FALSE(q.set_parameter("CJE", -1e-12));
    EXPECT_EQ(q.get_parameter("CJE"), 2e-12);
    ASSERT_TRUE(q.set_parameter("beta_f", 150.0));
    ASSERT_TRUE(q.set_parameter("BETA_R", 3.0));
    EXPECT_EQ(q.get_parameter("BF"), 150.0);
    EXPECT_EQ(q.get_parameter("beta_r"), 3.0);
}

// The Newton and AC derivatives are the derivatives of the current in each
// operating region, including both Early voltages, both high-injection
// terms and both leakage currents. Numerical slopes check the analytic
// derivatives independently of the stamp.
TYPED_TEST(BjtSharedModel, DerivativesFollowCurrentsInEachRegion) {
    TypeParam q(1, "Q1", 1, 2, 0, 1e-15, 100.0, 1.0, -1.0, 2.0, 1.0,
                50.0, 10.0, 0.01, 0.005, 0.0, 0.0, 0.0, 1e-14, 1.5, 1e-13, 1.8);
    constexpr Real h = 1e-6;
    for (const auto& v : {std::pair{0.7, -1.3}, std::pair{0.75, 0.65},
                          std::pair{-1.4, 0.6}, std::pair{0.85, -2.15},
                          std::pair{-1.0, -2.0}}) {
        SCOPED_TRACE(testing::Message() << "Vbe = " << v.first << ", Vbc = " << v.second);
        const auto d = q.derivatives(v.first, v.second);
        const auto be_up = q.currents(v.first + h, v.second);
        const auto be_down = q.currents(v.first - h, v.second);
        const auto bc_up = q.currents(v.first, v.second + h);
        const auto bc_down = q.currents(v.first, v.second - h);
        auto expect_slope = [](Real analytic, Real up, Real down) {
            const Real numerical = (up - down) / (2.0 * h);
            // A reverse-junction slope can be tiny beside the forward
            // current being subtracted. Account for that subtraction's
            // floating-point resolution as well as truncation error.
            const Real roundoff = 4.0 * std::numeric_limits<Real>::epsilon() *
                                   std::max(std::abs(up), std::abs(down)) / h;
            EXPECT_NEAR(analytic, numerical, 1e-6 * std::abs(numerical) + roundoff + 1e-18);
        };
        expect_slope(d.dic_dvbe, be_up.ic, be_down.ic);
        expect_slope(d.dib_dvbe, be_up.ib, be_down.ib);
        expect_slope(d.dic_dvbc, bc_up.ic, bc_down.ic);
        expect_slope(d.dib_dvbc, bc_up.ib, bc_down.ib);
    }
}

// With the Early and high-injection terms absent, both transport junctions
// are continued with slope IS exp(80)/Vt beyond 80 Vt. All four current
// derivatives have closed-form values there, without numerical subtraction.
TYPED_TEST(BjtSharedModel, DerivativesFollowLinearExponentialContinuation) {
    TypeParam q(1, "Q1", 1, 2, 0, 1e-15);
    const Real vt = 1.380649e-23 * 300.15 / 1.602176634e-19;
    const Real conductance = 1e-15 * std::exp(80.0) / vt + 1e-12;
    const auto d = q.derivatives(90.0 * vt, 85.0 * vt);
    EXPECT_NEAR(d.dic_dvbe, conductance, conductance * 1e-14);
    EXPECT_NEAR(d.dic_dvbc, -2.0 * conductance, conductance * 1e-14);
    EXPECT_NEAR(d.dib_dvbe, conductance / 100.0, conductance * 1e-14);
    EXPECT_NEAR(d.dib_dvbc, conductance, conductance * 1e-14);
}

// Integrate CJE (1 - Vbe/VJE)^-MJE analytically, without diffusion charge or
// a B-C capacitance. The accepted charge is exposed to error estimation and
// restored on rejection.
TYPED_TEST(BjtSharedModel, AcceptedChargeSurvivesSaveAndRestore) {
    TypeParam q(1, "Q1", 1, 2, 0);
    ASSERT_TRUE(q.set_parameter("CJE", 2e-12));
    const Real voltage = -0.3;
    const Real potential = 0.75, grading = 0.33;
    const Real charge = 2e-12 * potential *
                        (1.0 - std::pow(1.0 - voltage / potential, 1.0 - grading)) / (1.0 - grading);
    q.accept_solution({0.0, 0.0, TypeParam::polarity * voltage});
    EXPECT_NEAR(q.get_charge(), charge, 1e-25);
    q.save_state();
    q.accept_solution({0.0, 0.0, 0.0});
    EXPECT_DOUBLE_EQ(q.get_charge(), 0.0);
    q.restore_state();
    EXPECT_NEAR(q.get_charge(), charge, 1e-25);
    q.reset_state();
    EXPECT_DOUBLE_EQ(q.get_charge(), 0.0);
}
