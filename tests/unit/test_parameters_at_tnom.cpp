// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A device parameter that set_parameter and get_parameter carry (the
// sensitivity, Monte Carlo and worst-case analyses) is the card's, at TNOM:
// set_parameter gives the device the value as the card would, scaled to the
// present temperature, and set_temperature, which every analysis with a
// circuit temperature calls at its start, keeps it. The diode's IS, the
// BJT's IS and BF, and the level-1 MOSFET's KP and VTO.
//
// The diode's IS(T) = IS (T/TNOM)^(XTI/N) exp(-EG/N (1/Vt(T) - 1/Vt(TNOM)))
// by hand, with IS = 2e-14, N = 1, XTI = 3, EG = 1.11, TNOM = 300.15 K,
// T = 400.15 K: 2.154774665563709e-09 A, and 0.07766678252456916 A at 0.6 V.

#include <gtest/gtest.h>

#include <cmath>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

constexpr Real kHot = 400.15;

TEST(ParametersAtTnom, DiodeSaturationCurrentIsScaledAndKept) {
    devices::Diode diode(1, "D1", 1, 0, 1e-14);
    diode.set_temperature(kHot);
    ASSERT_TRUE(diode.set_parameter("IS", 2e-14));
    EXPECT_DOUBLE_EQ(diode.get_parameter("IS").value(), 2e-14);
    EXPECT_NEAR(diode.saturation_current(), 2.154774665563709e-09, 1e-12 * 2.15e-09);
    diode.set_temperature(kHot);  // as an analysis at 127 C does
    EXPECT_NEAR(diode.get_current(0.6), 0.07766678252456916, 1e-10 * 0.0777);
}

TEST(ParametersAtTnom, BjtSaturationCurrentAndBetaAreScaledAndKept) {
    devices::BJT_NPN changed(1, "Q1", 1, 2, 0, 1e-15, 100.0);
    devices::BJT_NPN reference(2, "Q2", 1, 2, 0, 2e-15, 150.0);
    // XTB = 1.5 so that BF has a temperature law too
    devices::BJT_NPN changed_xtb(3, "Q3", 1, 2, 0, 1e-15, 100.0, 1.0, -1.0, 1.0, 1.0, 0.0, 0.0,
                                 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5, 0.0, 2.0, 0.0, 0.75, 0.33,
                                 0.0, 0.75, 0.33, 0.0, 0.0, 0.0, 1.0, 1.0, 3.0, 1.5);
    devices::BJT_NPN reference_xtb(4, "Q4", 1, 2, 0, 2e-15, 150.0, 1.0, -1.0, 1.0, 1.0, 0.0, 0.0,
                                   0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5, 0.0, 2.0, 0.0, 0.75, 0.33,
                                   0.0, 0.75, 0.33, 0.0, 0.0, 0.0, 1.0, 1.0, 3.0, 1.5);
    for (auto* q : {&changed, &reference, &changed_xtb, &reference_xtb}) {
        q->set_temperature(350.0);
    }
    for (auto* q : {&changed, &changed_xtb}) {
        ASSERT_TRUE(q->set_parameter("IS", 2e-15));
        ASSERT_TRUE(q->set_parameter("BF", 150.0));
        EXPECT_DOUBLE_EQ(q->get_parameter("IS").value(), 2e-15);
        EXPECT_DOUBLE_EQ(q->get_parameter("BF").value(), 150.0);
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& [ref, q] : {std::pair{&reference, &changed}, std::pair{&reference_xtb, &changed_xtb}}) {
            const auto expected = ref->currents(0.65, -1.0);
            const auto actual = q->currents(0.65, -1.0);
            EXPECT_DOUBLE_EQ(actual.ic, expected.ic);
            EXPECT_DOUBLE_EQ(actual.ib, expected.ib);
        }
        changed.set_temperature(350.0);  // as an analysis at 350 K does
        changed_xtb.set_temperature(350.0);
    }
}

TEST(ParametersAtTnom, Level1TransconductanceAndThresholdAreScaledAndKept) {
    devices::MOSFET_N changed(1, "M1", 1, 2, 0, 1e-4, 10e-6, 1e-6, 0.7);
    devices::MOSFET_N reference(2, "M2", 1, 2, 0, 2e-4, 10e-6, 1e-6, 0.6);
    changed.set_temperature(kHot);
    reference.set_temperature(kHot);
    ASSERT_TRUE(changed.set_parameter("KP", 2e-4));
    ASSERT_TRUE(changed.set_parameter("VTO", 0.6));
    EXPECT_DOUBLE_EQ(changed.get_parameter("KP").value(), 2e-4);
    EXPECT_DOUBLE_EQ(changed.get_parameter("VTO").value(), 0.6);
    for (int pass = 0; pass < 2; ++pass) {
        EXPECT_DOUBLE_EQ(changed.get_drain_current(1.5, 2.0), reference.get_drain_current(1.5, 2.0));
        changed.set_temperature(kHot);
    }
}

// Through the solver: a diode circuit solved at 127 C, its IS changed by
// set_parameter, solved again, carries the current of the circuit built with
// that IS
TEST(ParametersAtTnom, SolverAtACircuitTemperatureKeepsTheParameterSet) {
    auto build = [](Real is) {
        auto circuit = std::make_unique<circuit::CircuitGraph>();
        circuit->add_voltage_source("V1", "a", "0", 0.6);
        circuit->add_resistor("R1", "a", "k", 10.0);
        circuit->add_diode("D1", "k", "0", is);
        return circuit;
    };
    SimulationConfig config;
    config.temperature = kHot;
    config.temperature_explicitly_set = true;
    auto current = [&config](circuit::CircuitGraph& circuit) {
        solvers::DCSolver solver(circuit, config);
        DenseVector solution;
        SolverStats stats;
        EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        return solver.get_branch_current("V1").value();
    };
    auto changed = build(1e-14);
    const Real before = current(*changed);
    ASSERT_TRUE(changed->get_device("D1")->set_parameter("IS", 2e-14));
    const Real after = current(*changed);
    auto reference = build(2e-14);
    const Real expected = current(*reference);
    EXPECT_GT(std::abs(after - before), 1e-3 * std::abs(before));
    EXPECT_NEAR(after, expected, 1e-12 * std::abs(expected));
}

}  // namespace
