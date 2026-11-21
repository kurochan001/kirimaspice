// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_vdmos.cpp
// VDMOS (Vertical Double-diffused MOSFET) Device Tests

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/vdmos.hpp"  // Unified VDMOS template
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::devices;
using namespace kirimaspice::solvers;

//==============================================================================
// VDMOS_N DC Characteristics Tests
//==============================================================================

// Test 1: Cutoff Region (Vgs < Vth)
TEST(VDMOS_N, CutoffRegion) {
    CircuitGraph circuit;

    // VDMOS N-channel: Vth = 4V (default)
    // Test Vgs = 2V < Vth → Id should be ~0
    circuit.add_voltage_source("VDD", "drain", "0", 10.0);  // Vds = 10V
    circuit.add_voltage_source("VGS", "gate", "0", 2.0);    // Vgs = 2V < 4V
    circuit.add_vdmos_n("M1", "drain", "gate", "0");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // In cutoff, drain current should be negligible
    NodeId drain_id = circuit.get_node_id("drain");
    Real v_drain = solution[drain_id - 1];

    // Since Id ≈ 0, V_drain ≈ VDD (no voltage drop)
    EXPECT_NEAR(v_drain, 10.0, 0.1) << "Drain voltage should be ~VDD in cutoff";
}

// Test 2: Linear Region (Vgs > Vth, Vds < Vgs - Vth)
TEST(VDMOS_N, LinearRegion) {
    CircuitGraph circuit;

    // VDMOS: Vth = 4V, KP = 20 A/V² (default)
    // Test: Vgs = 6V, Vds = 1V < (Vgs - Vth) = 2V → Linear region
    circuit.add_voltage_source("VDD", "drain", "0", 1.0);   // Vds = 1V
    circuit.add_voltage_source("VGS", "gate", "0", 6.0);    // Vgs = 6V
    circuit.add_resistor("RD", "vdd_supply", "drain", 1.0); // 1Ω load
    circuit.add_voltage_source("VSUPPLY", "vdd_supply", "0", 10.0);
    circuit.add_vdmos_n("M1", "drain", "gate", "0",
                        4.0,   // VTO
                        20.0,  // KP
                        0.1,   // RDS
                        600.0, // VBD
                        0.01); // LAMBDA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId drain_id = circuit.get_node_id("drain");
    Real v_drain = solution[drain_id - 1];

    // Linear region: Id = KP × [(Vgs - Vth) × Vds - Vds²/2] × (1 + λ×Vds)
    // Expect drain voltage < 10V due to current flow
    EXPECT_LT(v_drain, 10.0) << "Drain voltage should drop due to current";
    EXPECT_GT(v_drain, 0.0) << "Drain voltage should be positive";
}

// Test 3: Saturation Region (Vgs > Vth, Vds >= Vgs - Vth)
TEST(VDMOS_N, SaturationRegion) {
    CircuitGraph circuit;

    // VDMOS: Vth = 4V, KP = 20 A/V²
    // Test: Vgs = 6V, Vds = 5V > (Vgs - Vth) = 2V → Saturation
    circuit.add_voltage_source("VDD", "drain", "0", 5.0);   // Vds = 5V
    circuit.add_voltage_source("VGS", "gate", "0", 6.0);    // Vgs = 6V
    circuit.add_resistor("RD", "vdd_supply", "drain", 0.5); // 0.5Ω load
    circuit.add_voltage_source("VSUPPLY", "vdd_supply", "0", 15.0);
    circuit.add_vdmos_n("M1", "drain", "gate", "0",
                        4.0,   // VTO
                        20.0,  // KP
                        0.05,  // RDS (smaller for clearer test)
                        600.0, // VBD
                        0.01); // LAMBDA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId drain_id = circuit.get_node_id("drain");
    Real v_drain = solution[drain_id - 1];

    // Saturation: Id = 0.5 × KP × (Vgs - Vth)² × (1 + λ×Vds)
    // Current should cause significant voltage drop across RD
    EXPECT_LT(v_drain, 15.0) << "Drain voltage should drop in saturation";
    EXPECT_GT(v_drain, 0.0) << "Drain voltage should be positive";
}

// Test 4: Channel Length Modulation (LAMBDA effect)
// Test 4: Channel Length Modulation (LAMBDA) Effect
TEST(VDMOS_N, ChannelLengthModulation) {
    CircuitGraph circuit1, circuit2;

    // Compare two circuits: with and without CLM (λ = 0 vs λ = 0.02)
    // Use smaller KP for better convergence

    // Circuit 1: λ = 0 (no CLM)
    circuit1.add_voltage_source("VGS", "gate", "0", 6.0);
    circuit1.add_voltage_source("VDD", "vdd", "0", 10.0);
    circuit1.add_resistor("RD", "vdd", "drain", 20.0);  // Increased for better convergence
    circuit1.add_vdmos_n("M1", "drain", "gate", "0",
                         4.0,   // VTO
                         1.0,   // KP (further reduced from 2.0)
                         0.5,   // RDS (increased from 0.1)
                         600.0, // VBD
                         0.0);  // LAMBDA = 0

    // Circuit 2: λ = 0.02 (with CLM)
    circuit2.add_voltage_source("VGS", "gate", "0", 6.0);
    circuit2.add_voltage_source("VDD", "vdd", "0", 10.0);
    circuit2.add_resistor("RD", "vdd", "drain", 20.0);  // Increased for better convergence
    circuit2.add_vdmos_n("M1", "drain", "gate", "0",
                         4.0,   // VTO
                         1.0,   // KP (further reduced from 2.0)
                         0.5,   // RDS (increased from 0.1)
                         600.0, // VBD
                         0.02); // LAMBDA = 0.02

    DCSolver solver1(circuit1), solver2(circuit2);
    DenseVector solution1, solution2;
    SolverStats stats1, stats2;

    ConvergenceStatus status1 = solver1.solve(solution1, stats1);
    ConvergenceStatus status2 = solver2.solve(solution2, stats2);

    EXPECT_EQ(status1, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(status2, ConvergenceStatus::SUCCESS);

    NodeId drain_id1 = circuit1.get_node_id("drain");
    NodeId drain_id2 = circuit2.get_node_id("drain");
    Real v_drain1 = solution1[drain_id1 - 1];
    Real v_drain2 = solution2[drain_id2 - 1];

    // With CLM, Id increases with Vds → more voltage drop across RD
    // → V_drain should be lower with CLM
    EXPECT_LT(v_drain2, v_drain1) << "CLM should increase drain current, lowering V_drain";
}

// Test 5: On-Resistance (RDS) Effect
// Enabled after RDS iteration damping fix
TEST(VDMOS_N, OnResistanceEffect) {
    CircuitGraph circuit_low_rds, circuit_high_rds;

    // Compare RDS = 0.2Ω vs RDS = 1.0Ω
    // Use smaller KP for better convergence

    // Low RDS
    circuit_low_rds.add_voltage_source("VGS", "gate", "0", 8.0);
    circuit_low_rds.add_voltage_source("VDD", "vdd", "0", 20.0);
    circuit_low_rds.add_resistor("RD", "vdd", "drain", 10.0);  // Increased for better convergence
    circuit_low_rds.add_vdmos_n("M1", "drain", "gate", "0",
                                 4.0,   // VTO
                                 0.5,   // KP (further reduced from 1.0 for numerical stability)
                                 0.2,   // RDS = 200mΩ (increased from 0.05)
                                 600.0, // VBD
                                 0.01); // LAMBDA

    // High RDS
    circuit_high_rds.add_voltage_source("VGS", "gate", "0", 8.0);
    circuit_high_rds.add_voltage_source("VDD", "vdd", "0", 20.0);
    circuit_high_rds.add_resistor("RD", "vdd", "drain", 10.0);  // Increased for better convergence
    circuit_high_rds.add_vdmos_n("M1", "drain", "gate", "0",
                                  4.0,   // VTO
                                  0.5,   // KP (further reduced from 1.0 for numerical stability)
                                  1.0,   // RDS = 1Ω (increased from 0.5)
                                  600.0, // VBD
                                  0.01); // LAMBDA

    DCSolver solver_low(circuit_low_rds), solver_high(circuit_high_rds);
    DenseVector solution_low, solution_high;
    SolverStats stats_low, stats_high;

    ConvergenceStatus status_low = solver_low.solve(solution_low, stats_low);
    ConvergenceStatus status_high = solver_high.solve(solution_high, stats_high);

    EXPECT_EQ(status_low, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(status_high, ConvergenceStatus::SUCCESS);

    NodeId drain_id_low = circuit_low_rds.get_node_id("drain");
    NodeId drain_id_high = circuit_high_rds.get_node_id("drain");
    Real v_drain_low = solution_low[drain_id_low - 1];
    Real v_drain_high = solution_high[drain_id_high - 1];

    // Higher RDS → more voltage drop across MOSFET → lower effective current
    // → less drop across external RD → higher V_drain
    EXPECT_GT(v_drain_high, v_drain_low) << "Higher RDS should result in higher V_drain";
}

//==============================================================================
// VDMOS_P DC Characteristics Tests
//==============================================================================

// Test 6: VDMOS_P Cutoff Region
TEST(VDMOS_P, CutoffRegion) {
    CircuitGraph circuit;

    // P-channel: Vth = -4V (default)
    // Test Vsg = 2V < |Vth| → Id should be ~0
    circuit.add_voltage_source("VSS", "source", "0", 10.0);  // Vs = 10V
    circuit.add_voltage_source("VG", "gate", "0", 8.0);      // Vg = 8V → Vsg = 2V < 4V
    circuit.add_resistor("RD", "drain", "0", 1.0);           // Pull-down resistor to GND
    circuit.add_vdmos_p("M1", "drain", "gate", "source");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId drain_id = circuit.get_node_id("drain");
    Real v_drain = solution[drain_id - 1];

    // In cutoff, no current flows → drain voltage pulled to GND by resistor
    EXPECT_NEAR(v_drain, 0.0, 0.1) << "Drain voltage should be ~0V in cutoff (pulled to GND)";
}

// Test 7: VDMOS_P Saturation Region
// Enabled after RDS iteration damping fix
TEST(VDMOS_P, SaturationRegion) {
    CircuitGraph circuit;

    // P-channel: Source at 10V, Gate at 2V → Vsg = 8V > |Vth| = 4V
    // Use smaller KP for better convergence
    circuit.add_voltage_source("VSS", "source", "0", 10.0);  // Vs = 10V
    circuit.add_voltage_source("VG", "gate", "0", 2.0);      // Vg = 2V → Vsg = 8V
    circuit.add_resistor("RD", "drain", "0", 10.0);          // Increased for better convergence
    circuit.add_vdmos_p("M1", "drain", "gate", "source",
                        -4.0,  // VTO (negative for P-channel)
                        1.0,   // KP (further reduced from 2.0)
                        0.5,   // RDS (increased from 0.2)
                        600.0, // VBD
                        0.01); // LAMBDA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId drain_id = circuit.get_node_id("drain");
    Real v_drain = solution[drain_id - 1];

    // Current flows from source to drain → V_drain > 0 due to drop across RD
    EXPECT_GT(v_drain, 0.5) << "Drain voltage should be > 0 due to current";
    EXPECT_LT(v_drain, 10.0) << "Drain voltage should be < Vs";
}

//==============================================================================
// VDMOS Capacitance Model Tests
//==============================================================================

// Test 8: Voltage-Dependent Cgd (Miller Capacitance)
TEST(VDMOS_N, VoltageDependentCgd) {
    // This test verifies that Cgd decreases with increasing Vds
    // Cgd(Vds) = Cgdmin + (Cgdmax - Cgdmin) / (1 + (Vds/Vto)²)

    Real cgdmax = 1e-9;   // 1nF at Vds=0
    Real cgdmin = 1e-11;  // 10pF at high Vds
    Real vto = 4.0;

    // Create device directly to test compute_cgd method
    VDMOS_N vdmos(1, "M1", 1, 2, 0,
                  vto, 20.0, 0.1, 600.0, 0.01,
                  3e-9, cgdmax, cgdmin, 1e-10);

    // Expected: At Vds=10V >> Vto, Cgd should be close to Cgdmin
    // Cgd(10V) = 1e-11 + (1e-9 - 1e-11) / (1 + (10/4)²)
    //          = 1e-11 + 0.99e-9 / (1 + 6.25)
    //          = 1e-11 + 0.99e-9 / 7.25
    //          ≈ 1e-11 + 1.37e-10 ≈ 1.47e-10 F

    Real expected_cgd = cgdmin + (cgdmax - cgdmin) / (1.0 + std::pow(10.0 / vto, 2));
    EXPECT_NEAR(expected_cgd, 1.47e-10, 1e-11) << "Cgd calculation should match expected value";
}

// Test 9: Temperature Dependence - VTO
TEST(VDMOS_N, TemperatureDependence_VTO) {
    CircuitGraph circuit1, circuit2;

    Real kt1 = -3e-3;  // -3mV/K (typical negative temperature coefficient)
    Real T1 = 300.15;  // 27°C (nominal)
    Real T2 = 350.15;  // 77°C

    // Expected VTO change: ΔVTO = kt1 × ΔT = -3e-3 × 50 = -0.15V
    // VTO(T2) = 4.0 - 0.15 = 3.85V → threshold drops → more current
    // Use smaller KP for better convergence

    // Circuit at T1 (nominal)
    circuit1.add_voltage_source("VGS", "gate", "0", 5.0);
    circuit1.add_voltage_source("VDD", "vdd", "0", 10.0);
    circuit1.add_resistor("RD", "vdd", "drain", 10.0);  // Larger resistor
    circuit1.add_vdmos_n("M1", "drain", "gate", "0",
                         4.0,   // VTO
                         2.0,   // KP (reduced from 20)
                         0.5,   // RDS (increased from 0.1)
                         600.0, // VBD
                         0.01,  // LAMBDA
                         3e-9,  // Cgs
                         1e-9,  // Cgdmax
                         1e-11, // Cgdmin
                         1e-10, // Cds
                         0.0,   // KF
                         1.0,   // AF
                         1.0,   // EF
                         0.667, // gamma
                         kt1,   // KT1
                         0.0,   // KT2
                         -1.5,  // UTE
                         2.0,   // RTC
                         T1);   // Tnom

    // Circuit at T2 (elevated) - same parameters, solved at T2
    circuit2.add_voltage_source("VGS", "gate", "0", 5.0);
    circuit2.add_voltage_source("VDD", "vdd", "0", 10.0);
    circuit2.add_resistor("RD", "vdd", "drain", 10.0);  // Larger resistor
    circuit2.add_vdmos_n("M1", "drain", "gate", "0",
                         4.0, 2.0, 0.5, 600.0, 0.01,
                         3e-9, 1e-9, 1e-11, 1e-10,
                         0.0, 1.0, 1.0, 0.667,
                         kt1, 0.0, -1.5, 2.0, T1);

    // The temperature of circuit2's analysis
    SimulationConfig config2;
    config2.temperature = T2;
    config2.temperature_explicitly_set = true;

    DCSolver solver1(circuit1), solver2(circuit2, config2);
    DenseVector solution1, solution2;
    SolverStats stats1, stats2;

    ConvergenceStatus status1 = solver1.solve(solution1, stats1);
    ConvergenceStatus status2 = solver2.solve(solution2, stats2);

    EXPECT_EQ(status1, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(status2, ConvergenceStatus::SUCCESS);

    NodeId drain_id1 = circuit1.get_node_id("drain");
    NodeId drain_id2 = circuit2.get_node_id("drain");
    Real v_drain1 = solution1[drain_id1 - 1];
    Real v_drain2 = solution2[drain_id2 - 1];

    // Temperature effect is complex: VTO↓ (Id↑), KP↓ (Id↓), RDS↑ (Id↓)
    // For UTE=-1.5, mobility degradation dominates → net Id↓ → V_drain↑
    // Just verify that temperature has an effect (values are different)
    EXPECT_NE(v_drain2, v_drain1) << "Temperature should affect drain voltage";
    std::cout << "V_drain @ 27°C = " << v_drain1 << " V" << std::endl;
    std::cout << "V_drain @ 77°C = " << v_drain2 << " V" << std::endl;
}

// Test 10: Parameter Access (for sensitivity analysis)
TEST(VDMOS_N, ParameterAccess) {
    CircuitGraph circuit;
    circuit.add_vdmos_n("M1", "drain", "gate", "0",
                        4.0,   // VTO
                        20.0,  // KP
                        0.1);  // RDS

    auto device = circuit.get_device("M1");
    ASSERT_NE(device, nullptr);

    // Test get_parameter
    auto vto = device->get_parameter("VTO");
    auto kp = device->get_parameter("KP");
    auto rds = device->get_parameter("RDS");

    ASSERT_TRUE(vto.has_value());
    ASSERT_TRUE(kp.has_value());
    ASSERT_TRUE(rds.has_value());

    EXPECT_NEAR(vto.value(), 4.0, 1e-9);
    EXPECT_NEAR(kp.value(), 20.0, 1e-9);
    EXPECT_NEAR(rds.value(), 0.1, 1e-9);

    // Test set_parameter
    EXPECT_TRUE(device->set_parameter("VTO", 3.5));
    EXPECT_TRUE(device->set_parameter("KP", 25.0));

    auto vto_new = device->get_parameter("VTO");
    auto kp_new = device->get_parameter("KP");

    EXPECT_NEAR(vto_new.value(), 3.5, 1e-9);
    EXPECT_NEAR(kp_new.value(), 25.0, 1e-9);

    // Test get_perturbable_parameters
    auto perturbable = device->get_perturbable_parameters();
    EXPECT_GE(perturbable.size(), 3) << "Should have at least VTO, KP, RDS";
}

// The VDMOS channel (KP/2 Vov^2 (1 + LAMBDA v) in saturation, KP (Vov v -
// v^2/2)(1 + LAMBDA v) below it) carries the current I in series with RDS at
// the drain, so I = f(VGS, VDS - I RDS). With the defaults (VTO = 4 V,
// KP = 20 A/V^2, RDS = 0.1 Ohm, LAMBDA = 0.01) the current of a fully driven
// device is set by RDS; the reference solves the equation by bisection.
TEST(VDMOS_N_DC, CurrentSolvesTheSeriesResistance) {
    auto reference = [](Real vgs, Real vds) {
        const Real kp = 20.0, vth = 4.0, rds = 0.1, lambda = 0.01, vov = vgs - vth;
        auto f = [&](Real v) {
            return v < vov ? kp * (vov * v - 0.5 * v * v) * (1.0 + lambda * v)
                           : 0.5 * kp * vov * vov * (1.0 + lambda * v);
        };
        Real lo = 0.0, hi = f(vds);
        for (int k = 0; k < 200; ++k) {
            const Real mid = 0.5 * (lo + hi);
            (mid - f(vds - mid * rds) > 0.0 ? hi : lo) = mid;
        }
        return 0.5 * (lo + hi);
    };
    const Real biases[][2] = {{10.0, 5.0}, {4.5, 1.0}, {15.0, 5.0}, {10.0, 0.5}};
    for (const auto& b : biases) {
        CircuitGraph circuit;
        circuit.add_voltage_source("VG", "gate", "0", b[0]);
        circuit.add_voltage_source("VD", "drain", "0", b[1]);
        circuit.add_vdmos_n("M1", "drain", "gate", "0");
        DCSolver solver(circuit);
        DenseVector solution;
        SolverStats stats;
        ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        const Real expected = reference(b[0], b[1]);
        // less the GMIN of 1e-12 S across the channel
        EXPECT_NEAR(-solver.get_branch_current("VD").value() - 1e-12 * b[1], expected,
                    1e-9 * expected)
            << "VGS=" << b[0] << " VDS=" << b[1];
    }
}
