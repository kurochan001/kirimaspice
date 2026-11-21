// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The temperature laws of the diode's depletion capacitance, as SPICE's
// (Diode::set_temperature), with Eg(T) = 1.16 - 7.02e-4 T^2/(T + 1108),
// p(T) = -3 Vt(T) ln(T/300.15) + Eg(T) - Eg(300.15) T/300.15, r = T/TNOM,
// Tref = 300.15 K:
//   VJ(T) = (VJ - p(TNOM)) r + p(T), VJ0 = (VJ - p(TNOM)) Tref/TNOM,
//   CJO(T) = CJO (1 + M (4e-4 (T - Tref) - (VJ(T) - VJ0)/VJ0))
//              / (1 + M (4e-4 (TNOM - Tref) - (VJ - VJ0)/VJ0)).
// By hand, CJO = 10 pF, VJ = 0.8 V, M = 0.5, TNOM = 27 C: at 125 C
// VJ(T) = 0.6390665711369865 V, CJO(T) = 11.201833930393834 pF and
// Cj(-2 V) = 5.512352826129087 pF; at -40 C 0.9020187366078943 V,
// 9.22838289620066 pF and 5.144975119710132 pF.
//
// ngspice-47, run 2026-11-21: V1 a 0 DC -2 AC 1, R1 a k 1k, D1 k 0 dm
// (is=1e-14 cjo=10p vj=0.8 m=0.5); V2 a2 0 DC -1 AC 1, R2 a2 k2 1k,
// D2 k2 0 dm2 (is=1e-14 cjo=20p vj=0.6 m=0.33); ac lin 1 10meg 10meg:
//   -40 C: v(k) = 9.053851e-01 - 2.92682e-01j, v(k2) = 5.723789e-01 - 4.94734e-01j
//   125 C: v(k) = 8.928896e-01 - 3.09254e-01j, v(k2) = 5.361376e-01 - 4.98692e-01j
// Before this program applied these laws it took Cj(-2 V) = 5.345 pF, its
// value at 27 C, at every temperature.

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;

TEST(DiodeCapacitanceTemperature, PotentialAndCapacitanceByHand) {
    devices::Diode d(1, "D1", 1, 0, 1e-14, 1.0, 10e-12, 0.8, 0.5);
    d.set_temperature(398.15);
    EXPECT_NEAR(d.junction_potential(), 0.6390665711369865, 1e-12);
    EXPECT_NEAR(d.junction_capacitance(), 11.201833930393834e-12, 1e-12 * 11.2e-12);
    EXPECT_NEAR(d.get_junction_capacitance(-2.0), 5.512352826129087e-12, 1e-12 * 5.5e-12);
    d.set_temperature(233.15);
    EXPECT_NEAR(d.junction_potential(), 0.9020187366078943, 1e-12);
    EXPECT_NEAR(d.junction_capacitance(), 9.22838289620066e-12, 1e-12 * 9.2e-12);
    EXPECT_NEAR(d.get_junction_capacitance(-2.0), 5.144975119710132e-12, 1e-12 * 5.1e-12);
    d.set_temperature(300.15);  // back at TNOM: the card's values exactly
    EXPECT_EQ(d.junction_potential(), 0.8);
    EXPECT_EQ(d.junction_capacitance(), 10e-12);
}

TEST(DiodeCapacitanceTemperature, ParametersSetAtTnomAreScaled) {
    devices::Diode d(1, "D1", 1, 0, 1e-14, 1.0, 10e-12, 0.8, 0.5);
    d.set_temperature(398.15);
    EXPECT_DOUBLE_EQ(d.get_parameter("CJ0").value(), 10e-12);
    EXPECT_DOUBLE_EQ(d.get_parameter("VJ").value(), 0.8);
    ASSERT_TRUE(d.set_parameter("CJ0", 20e-12));
    EXPECT_NEAR(d.junction_capacitance(), 2.0 * 11.201833930393834e-12, 1e-12 * 22.4e-12);
    EXPECT_DOUBLE_EQ(d.get_parameter("CJ0").value(), 20e-12);
}

TEST(DiodeCapacitanceTemperature, AcAgainstNgspice) {
    struct Case {
        double temp_c;
        double k_re, k_im, k2_re, k2_im;
    };
    for (const Case& c : {Case{-40.0, 9.053851e-01, -2.92682e-01, 5.723789e-01, -4.94734e-01},
                          Case{125.0, 8.928896e-01, -3.09254e-01, 5.361376e-01, -4.98692e-01}}) {
        SCOPED_TRACE(c.temp_c);
        parser::NetlistParser parser;
        auto parsed = parser.parse_string(
            "diode cap temp\nV1 a 0 DC -2 AC 1\nR1 a k 1k\nD1 k 0 dm\n"
            "V2 a2 0 DC -1 AC 1\nR2 a2 k2 1k\nD2 k2 0 dm2\n"
            ".model dm D is=1e-14 cjo=10p vj=0.8 m=0.5\n"
            ".model dm2 D is=1e-14 cjo=20p vj=0.6 m=0.33\n"
            ".options temp=" + std::to_string(c.temp_c) + "\n.end\n");
        SimulationConfig config = parsed.config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
        config.ac.start_freq = config.ac.stop_freq = 10e6;
        config.ac.num_points = 1;
        solvers::ACSolver solver(*parsed.circuit, config);
        const auto result = solver.solve();
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        const auto& x = result.frequency_points.at(0).solution;
        const Complex vk = x.at(parsed.circuit->get_node_id("k") - 1);
        const Complex vk2 = x.at(parsed.circuit->get_node_id("k2") - 1);
        EXPECT_NEAR(vk.real(), c.k_re, 2e-6);
        EXPECT_NEAR(vk.imag(), c.k_im, 2e-6);
        EXPECT_NEAR(vk2.real(), c.k2_re, 2e-6);
        EXPECT_NEAR(vk2.imag(), c.k2_im, 2e-6);
    }
}
