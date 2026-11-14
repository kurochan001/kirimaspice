// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A P-channel device is the mirror of the N-channel one: with every voltage
// negated, its current from source to drain is the N-channel device's current
// from drain to source, I(VSG, VSD) = I(VGS, VDS). So the current through the
// drain's source changes sign, and the small-signal drain current, gm vgs +
// gds vds from drain to source in both cases, is the same: a common-source
// stage has the same AC gain.

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/finfet.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

namespace {

using AddDevice = std::function<void(CircuitGraph&, bool p_channel)>;

// The current through VD with the source at s * vs, the gate at s * vg and
// the drain at s * vd, s = -1 for the P-channel device.
Real drain_source_current(const AddDevice& add, bool p_channel, Real vs, Real vg, Real vd) {
    const Real s = p_channel ? -1.0 : 1.0;
    CircuitGraph circuit;
    circuit.add_voltage_source("VS", "s", "0", s * vs);
    circuit.add_voltage_source("VG", "g", "0", s * vg);
    circuit.add_voltage_source("VD", "d", "0", s * vd);
    add(circuit, p_channel);
    SimulationConfig config;
    DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    return solver.get_branch_current("VD").value();
}

// The AC voltage at the drain of a common-source stage: the drain fed through
// rd from s * vdd, the source at s * vs, the gate at s * vg with an AC
// amplitude of 1.
Complex drain_ac_voltage(const AddDevice& add, bool p_channel, Real vs, Real vg, Real vdd,
                         Real rd) {
    const Real s = p_channel ? -1.0 : 1.0;
    CircuitGraph circuit;
    // add_voltage_source gives every source an AC amplitude of 1 unless told
    circuit.add_voltage_source("VS", "s", "0", s * vs, 0.0);
    circuit.add_voltage_source("VG", "g", "0", s * vg, 1.0);
    circuit.add_voltage_source("VDD", "dd", "0", s * vdd, 0.0);
    circuit.add_resistor("RD", "dd", "d", rd);
    add(circuit, p_channel);
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1.0;
    config.ac.stop_freq = 1.0;
    config.ac.num_points = 1;
    ACSolver solver(circuit, config);
    const ACResult result = solver.solve();
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(result.frequency_points.size(), 1u);
    if (result.frequency_points.empty()) {
        return Complex(std::nan(""), std::nan(""));
    }
    return result.frequency_points.front().solution[circuit.get_node_id("d") - 1];
}

void expect_mirror(const AddDevice& add, Real vs, Real vg, Real vd, Real vdd, Real rd) {
    const Real n_current = drain_source_current(add, false, vs, vg, vd);
    ASSERT_GT(std::abs(n_current), 1e-6);
    EXPECT_NEAR(drain_source_current(add, true, vs, vg, vd), -n_current,
                1e-9 * std::abs(n_current));

    const Complex n_gain = drain_ac_voltage(add, false, vs, vg, vdd, rd);
    const Complex p_gain = drain_ac_voltage(add, true, vs, vg, vdd, rd);
    ASSERT_GT(std::abs(n_gain), 1e-3);
    EXPECT_NEAR(p_gain.real(), n_gain.real(), 1e-6 * std::abs(n_gain));
    EXPECT_NEAR(p_gain.imag(), n_gain.imag(), 1e-6 * std::abs(n_gain));
}

}  // namespace

TEST(PChannelStamps, GaNHemtMirrorsTheNChannel) {
    // Threshold -2 V for both: the P-channel device conducts for VSG > -2 V
    expect_mirror(
        [](CircuitGraph& c, bool p) {
            if (p) {
                c.add_gan_hemt_p("M1", "d", "g", "s", 0.1, -2.0);
            } else {
                c.add_gan_hemt_n("M1", "d", "g", "s", 0.1, -2.0);
            }
        },
        1.0, 1.0, 6.0, 6.0, 10.0);
}

TEST(PChannelStamps, VdmosMirrorsTheNChannel) {
    // VTO is negative on a P-channel card; RDS differs between the defaults
    expect_mirror(
        [](CircuitGraph& c, bool p) {
            if (p) {
                c.add_vdmos_p("M1", "d", "g", "s", -4.0, 1.0, 0.1);
            } else {
                c.add_vdmos_n("M1", "d", "g", "s", 4.0, 1.0, 0.1);
            }
        },
        1.0, 7.0, 11.0, 11.0, 2.0);
}

TEST(PChannelStamps, FinFetMirrorsTheNChannel) {
    expect_mirror(
        [](CircuitGraph& c, bool p) {
            if (p) {
                c.add_finfet_p("M1", "d", "g", "s", 1, 30e-9, 7e-9, -0.4, 300e-4, 1e5, 70e-3,
                               0.05, 1e-9, 1e-15, 1e-16, 0.0, 1.0, -1e-3, 300.15, 1e-6);
            } else {
                c.add_finfet_n("M1", "d", "g", "s", 1, 30e-9, 7e-9, 0.4, 300e-4, 1e5, 70e-3,
                               0.05, 1e-9, 1e-15, 1e-16, 0.0, 1.0, -1e-3, 300.15, 1e-6);
            }
        },
        0.2, 1.0, 1.0, 1.2, 10e3);
}

// KT1 is the temperature coefficient of the signed threshold: with VTH =
// -0.4 V and KT1 = +1 mV/K the P-channel threshold moves toward 0 as the
// N-channel one with VTH = 0.4 V and KT1 = -1 mV/K does, so at 400 K the
// two still mirror each other.
TEST(PChannelStamps, FinFetThresholdFollowsTheTemperatureAsTheMirror) {
    auto current = [](bool p) {
        CircuitGraph c;
        const Real s = p ? -1.0 : 1.0;
        c.add_voltage_source("VD", "d", "0", s * 0.8, 0.0);
        c.add_voltage_source("VG", "g", "0", s * 0.8, 0.0);
        if (p) {
            c.add_finfet_p("M1", "d", "g", "0", 1, 30e-9, 7e-9, -0.4, 300e-4, 1e5, 70e-3, 0.05,
                           1e-9, 1e-15, 1e-16, 0.0, 1.0, 1e-3, 300.15, 1e-6);
            dynamic_cast<devices::FinFET_P*>(c.get_device("M1").get())->set_temperature(400.0);
        } else {
            c.add_finfet_n("M1", "d", "g", "0", 1, 30e-9, 7e-9, 0.4, 300e-4, 1e5, 70e-3, 0.05,
                           1e-9, 1e-15, 1e-16, 0.0, 1.0, -1e-3, 300.15, 1e-6);
            dynamic_cast<devices::FinFET_N*>(c.get_device("M1").get())->set_temperature(400.0);
        }
        SimulationConfig config;
        DCSolver solver(c, config);
        DenseVector solution;
        SolverStats stats;
        EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        return solver.get_branch_current("VD").value();
    };
    const Real n_current = current(false);
    EXPECT_NEAR(current(true), -n_current, 1e-9 * std::abs(n_current));
}
