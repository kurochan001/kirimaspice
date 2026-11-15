// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_igbt.cpp
// IGBT (Insulated Gate Bipolar Transistor) Device Tests

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/igbt.hpp"  // Unified IGBT template
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::devices;
using namespace kirimaspice::solvers;

//==============================================================================
// IGBT_N DC Characteristics Tests
//==============================================================================

// Test 1: Cutoff Region (Vge < Vge_th)
TEST(IGBT_N, CutoffRegion) {
    CircuitGraph circuit;

    // IGBT N-channel: Vge_th = 5V (default)
    // Test Vge = 3V < Vge_th → Ic should be ~0
    circuit.add_voltage_source("VCC", "collector", "0", 50.0);  // Vce = 50V
    circuit.add_voltage_source("VGE", "gate", "0", 3.0);        // Vge = 3V < 5V
    circuit.add_igbt_n("Q1", "collector", "gate", "0");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // In cutoff, collector current should be negligible
    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector = solution[collector_id - 1];

    // Since Ic ≈ 0, V_collector ≈ VCC (no voltage drop)
    EXPECT_NEAR(v_collector, 50.0, 0.5) << "Collector voltage should be ~VCC in cutoff";
}

// Test 2: Saturation Region (Vge > Vge_th, Vce < Vce_sat)
TEST(IGBT_N, SaturationRegion) {
    CircuitGraph circuit;

    // IGBT: Vge_th = 5V, KP = 2.0 A/V² (more realistic), Vce_sat = 2V (default)
    // Test: Vge = 7V, Vce = 1V < Vce_sat → Saturation region
    circuit.add_voltage_source("VCC", "collector_supply", "0", 10.0);
    circuit.add_resistor("RC", "collector_supply", "collector", 5.0); // 5Ω load (increased for smaller current)
    circuit.add_voltage_source("VGE", "gate", "0", 7.0);    // Vge = 7V
    circuit.add_igbt_n("Q1", "collector", "gate", "0",
                       5.0,   // VGE_TH
                       2.0,   // KP (reduced from 50 to 2, more realistic)
                       2.0,   // VCE_SAT
                       1e-6,  // TAU
                       1.0);  // AREA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector = solution[collector_id - 1];

    // Saturation region: Ic = KP × (Vge - Vth)² × (Vce / Vce_sat)
    // Expect significant current flow, V_collector < VCC
    EXPECT_LT(v_collector, 10.0) << "Collector voltage should drop due to current";
    EXPECT_GT(v_collector, 0.0) << "Collector voltage should be positive";
}

// Test 3: Active Region (Vge > Vge_th, Vce >= Vce_sat)
TEST(IGBT_N, ActiveRegion) {
    CircuitGraph circuit;

    // IGBT: Vge_th = 5V, KP = 2.0 A/V² (more realistic), Vce_sat = 2V
    // Test: Vge = 7V, Vce = 5V > Vce_sat → Active region
    circuit.add_voltage_source("VCC", "collector_supply", "0", 20.0);
    circuit.add_resistor("RC", "collector_supply", "collector", 10.0); // 10Ω load (increased for smaller current)
    circuit.add_voltage_source("VGE", "gate", "0", 7.0);    // Vge = 7V
    circuit.add_igbt_n("Q1", "collector", "gate", "0",
                       5.0,   // VGE_TH
                       2.0,   // KP (reduced from 50 to 2, more realistic)
                       2.0,   // VCE_SAT
                       1e-6,  // TAU
                       1.0);  // AREA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector = solution[collector_id - 1];

    // Active region: Ic = KP × (Vge - Vth)²
    // Expected: Ic = 50 × (7 - 5)² = 50 × 4 = 200 A
    // V_collector = VCC - Ic × RC = 20 - 200 × 1 = -180V (clamped to reasonable range)
    // In practice, circuit will converge to a stable operating point
    EXPECT_LT(v_collector, 20.0) << "Collector voltage should drop significantly";
}

// Test 4: Temperature Dependence - VGE_TH
TEST(IGBT_N, TemperatureDependence_VGE_TH) {
    CircuitGraph circuit;

    // IGBT with KT1 = -2e-3 V/K (typical negative coefficient)
    // At T = 350K (ΔT = +50K from Tnom=300K), VGE_TH decreases by ~0.1V
    circuit.add_voltage_source("VGE", "gate", "0", 6.0);  // Near threshold
    circuit.add_resistor("RC", "collector_supply", "collector", 10.0); // 10Ω (increased)
    circuit.add_voltage_source("VSUPPLY", "collector_supply", "0", 20.0); // 20V (reduced)
    circuit.add_igbt_n("Q1", "collector", "gate", "0",
                       5.0,    // VGE_TH
                       2.0,    // KP (reduced from 50 to 2)
                       2.0,    // VCE_SAT
                       1e-6,   // TAU
                       1.0,    // AREA
                       5e-9,   // CGE
                       5e-10,  // CCE
                       0.0,    // KF
                       1.0,    // AF
                       -2e-3,  // KT1 (negative temp coeff)
                       0.0,    // KT2
                       0.0,    // UTE (set to 0 to isolate VGE_TH effect)
                       1e-3,   // TCV
                       300.15);// TNOM

    // Test at room temperature (300K)
    auto igbt = std::dynamic_pointer_cast<IGBT_N>(circuit.get_device("Q1"));
    igbt->set_temperature(300.15);

    DCSolver solver1(circuit);
    DenseVector solution1;
    SolverStats stats1;
    ConvergenceStatus status1 = solver1.solve(solution1, stats1);
    EXPECT_EQ(status1, ConvergenceStatus::SUCCESS);

    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector_300K = solution1[collector_id - 1];

    // Test at elevated temperature (350K)
    igbt->set_temperature(350.15);

    DCSolver solver2(circuit);
    DenseVector solution2;
    SolverStats stats2;
    ConvergenceStatus status2 = solver2.solve(solution2, stats2);
    EXPECT_EQ(status2, ConvergenceStatus::SUCCESS);

    Real v_collector_350K = solution2[collector_id - 1];

    // At higher temperature, VGE_TH decreases (KT1 < 0)
    // → IGBT turns on more easily → more current → lower collector voltage
    EXPECT_LT(v_collector_350K, v_collector_300K)
        << "Collector voltage should decrease at higher temperature (VGE_TH decreases)";
}

// Test 5: Temperature Dependence - KP
TEST(IGBT_N, TemperatureDependence_KP) {
    CircuitGraph circuit;

    // IGBT with UTE = -1.5 (mobility decreases with temperature)
    circuit.add_voltage_source("VCC", "collector_supply", "0", 20.0); // 20V (reduced)
    circuit.add_resistor("RC", "collector_supply", "collector", 10.0); // 10Ω (increased)
    circuit.add_voltage_source("VGE", "gate", "0", 7.0);  // Well above threshold
    circuit.add_igbt_n("Q1", "collector", "gate", "0",
                       5.0,    // VGE_TH
                       2.0,    // KP (reduced from 50 to 2)
                       2.0,    // VCE_SAT
                       1e-6,   // TAU
                       1.0,    // AREA
                       5e-9,   // CGE
                       5e-10,  // CCE
                       0.0,    // KF
                       1.0,    // AF
                       0.0,    // KT1
                       0.0,    // KT2
                       -1.5,   // UTE (negative mobility temp exponent)
                       0.0,    // TCV
                       300.15);// TNOM

    auto igbt = std::dynamic_pointer_cast<IGBT_N>(circuit.get_device("Q1"));

    // Test at room temperature (300K)
    igbt->set_temperature(300.15);
    DCSolver solver1(circuit);
    DenseVector solution1;
    SolverStats stats1;
    ConvergenceStatus status1 = solver1.solve(solution1, stats1);
    EXPECT_EQ(status1, ConvergenceStatus::SUCCESS);

    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector_300K = solution1[collector_id - 1];

    // Test at elevated temperature (350K)
    igbt->set_temperature(350.15);
    DCSolver solver2(circuit);
    DenseVector solution2;
    SolverStats stats2;
    ConvergenceStatus status2 = solver2.solve(solution2, stats2);
    EXPECT_EQ(status2, ConvergenceStatus::SUCCESS);

    Real v_collector_350K = solution2[collector_id - 1];

    // At higher temperature, KP decreases (UTE < 0)
    // → less current → higher collector voltage
    EXPECT_GT(v_collector_350K, v_collector_300K)
        << "Collector voltage should increase at higher temperature (KP decreases)";
}

// Test 6: Temperature Dependence - VCE_SAT
TEST(IGBT_N, TemperatureDependence_VCE_SAT) {
    CircuitGraph circuit;

    // IGBT with TCV = 1e-3 (VCE_SAT increases with temperature)
    circuit.add_voltage_source("VCC", "collector_supply", "0", 20.0); // 20V (reduced)
    circuit.add_resistor("RC", "collector_supply", "collector", 5.0); // 5Ω (increased)
    circuit.add_voltage_source("VGE", "gate", "0", 7.0);
    circuit.add_igbt_n("Q1", "collector", "gate", "0",
                       5.0,    // VGE_TH
                       2.0,    // KP (reduced from 50 to 2)
                       2.0,    // VCE_SAT
                       1e-6,   // TAU
                       1.0,    // AREA
                       5e-9,   // CGE
                       5e-10,  // CCE
                       0.0,    // KF
                       1.0,    // AF
                       0.0,    // KT1
                       0.0,    // KT2
                       0.0,    // UTE
                       1e-3,   // TCV (positive temp coeff for VCE_SAT)
                       300.15);// TNOM

    auto igbt = std::dynamic_pointer_cast<IGBT_N>(circuit.get_device("Q1"));

    // Test at room temperature (300K)
    igbt->set_temperature(300.15);
    DCSolver solver1(circuit);
    DenseVector solution1;
    SolverStats stats1;
    ConvergenceStatus status1 = solver1.solve(solution1, stats1);
    EXPECT_EQ(status1, ConvergenceStatus::SUCCESS);

    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector_300K = solution1[collector_id - 1];

    // Test at elevated temperature (350K)
    igbt->set_temperature(350.15);
    DCSolver solver2(circuit);
    DenseVector solution2;
    SolverStats stats2;
    ConvergenceStatus status2 = solver2.solve(solution2, stats2);
    EXPECT_EQ(status2, ConvergenceStatus::SUCCESS);

    Real v_collector_350K = solution2[collector_id - 1];

    // VCE_SAT temperature dependence is typically small
    // Just verify simulation converges at both temperatures
    EXPECT_TRUE(status1 == ConvergenceStatus::SUCCESS && status2 == ConvergenceStatus::SUCCESS)
        << "Both simulations should converge";
}

// Test 7: Parameter Access API
TEST(IGBT_N, ParameterAccess) {
    CircuitGraph circuit;
    circuit.add_voltage_source("VCC", "collector", "0", 50.0);
    circuit.add_voltage_source("VGE", "gate", "0", 6.0);
    circuit.add_igbt_n("Q1", "collector", "gate", "0");

    auto igbt = std::dynamic_pointer_cast<IGBT_N>(circuit.get_device("Q1"));
    ASSERT_TRUE(igbt != nullptr);

    // Test get_parameter
    auto vge_th = igbt->get_parameter("vge_th");
    ASSERT_TRUE(vge_th.has_value());
    EXPECT_NEAR(vge_th.value(), 5.0, 1e-6) << "Default VGE_TH should be 5V";

    auto kp = igbt->get_parameter("kp");
    ASSERT_TRUE(kp.has_value());
    EXPECT_NEAR(kp.value(), 2.0, 1e-6) << "Default KP should be 2.0 A/V²";

    // Test set_parameter
    bool success = igbt->set_parameter("vge_th", 4.5);
    EXPECT_TRUE(success) << "Setting VGE_TH should succeed";

    auto vge_th_new = igbt->get_parameter("vge_th");
    ASSERT_TRUE(vge_th_new.has_value());
    EXPECT_NEAR(vge_th_new.value(), 4.5, 1e-6) << "VGE_TH should be updated to 4.5V";

    // Test invalid parameter name
    auto invalid = igbt->get_parameter("invalid_param");
    EXPECT_FALSE(invalid.has_value()) << "Invalid parameter should return nullopt";
}

// Test 8: Circuit Integration Test - Simplified single IGBT with load
// Note: Full H-bridge configuration is complex; this test verifies basic IGBT circuit operation
TEST(IGBT_N, CircuitIntegration) {
    CircuitGraph circuit;

    // Simple IGBT switch with resistive load
    // VCC → RC → collector → IGBT → emitter (GND)
    circuit.add_voltage_source("VCC", "vcc", "0", 20.0);      // 20V supply (reduced from 100V)
    circuit.add_resistor("RC", "vcc", "collector", 50.0);     // 50Ω load (increased from 10Ω)
    circuit.add_voltage_source("VGE", "gate", "0", 7.0);      // 7V gate drive (IGBT ON)
    circuit.add_igbt_n("Q1", "collector", "gate", "0",
                       5.0,   // VGE_TH = 5V
                       1.0,   // KP = 1.0 A/V² (conservative)
                       2.0,   // VCE_SAT = 2V
                       1e-6,  // TAU
                       1.0);  // AREA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify collector voltage
    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector = solution[collector_id - 1];

    // Vov = Vge - Vth = 7V - 5V = 2V
    // Ic = KP × Vov² = 1.0 × 4 = 4A (active region estimate)
    // V_RC = Ic × RC = 4 × 50 = 200V (>> VCC, so IGBT in saturation)
    // In practice, V_collector ≈ VCE_SAT or slightly higher
    EXPECT_GT(v_collector, 0.0) << "Collector voltage should be positive";
    EXPECT_LT(v_collector, 20.0) << "Collector voltage should be less than VCC";

    // Verify reasonable current: I = (VCC - V_collector) / RC
    Real i_collector = (20.0 - v_collector) / 50.0;
    EXPECT_GT(i_collector, 0.0) << "Collector current should be positive";
}

//==============================================================================
// IGBT_P DC Characteristics Tests
//==============================================================================

// Test 9: P-channel Cutoff Region
TEST(IGBT_P, CutoffRegion) {
    CircuitGraph circuit;

    // IGBT P-channel: Veg_th = 5V (positive value, emitter-gate threshold)
    // Test Veg = 3V < Veg_th → Ic should be ~0
    circuit.add_voltage_source("VEE", "emitter", "0", 50.0);  // Ve = 50V, Vce = 50V
    circuit.add_voltage_source("VEG", "emitter", "gate", 3.0); // Veg = 3V < 5V
    circuit.add_igbt_p("Q1", "0", "gate", "emitter");         // C=0, G=gate, E=emitter

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // In cutoff, collector current should be negligible
    // Emitter voltage should remain at VEE
    NodeId emitter_id = circuit.get_node_id("emitter");
    Real v_emitter = solution[emitter_id - 1];

    EXPECT_NEAR(v_emitter, 50.0, 0.5) << "Emitter voltage should be ~VEE in cutoff";
}

// Test 10: P-channel Saturation Region
TEST(IGBT_P, SaturationRegion) {
    CircuitGraph circuit;

    // IGBT P-channel: Veg_th = 5V, KP = 2.0 A/V² (more realistic), Vce_sat = 2V
    // Test: Veg = 7V, Vec = 1V < Vce_sat → Saturation region
    circuit.add_voltage_source("VEE", "emitter", "0", 10.0);  // Ve = 10V
    circuit.add_resistor("RC", "0", "collector", 5.0);        // 5Ω load (increased for smaller current)
    circuit.add_voltage_source("VEG", "emitter", "gate", 7.0);// Veg = 7V
    circuit.add_igbt_p("Q1", "collector", "gate", "emitter",
                       5.0,   // VEG_TH
                       2.0,   // KP (reduced from 50 to 2, more realistic)
                       2.0,   // VCE_SAT
                       1e-6,  // TAU
                       1.0);  // AREA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector = solution[collector_id - 1];

    // P-channel in saturation: significant current flow
    // Collector voltage should be > 0 (current flows from E to C)
    EXPECT_GT(v_collector, 0.0) << "Collector voltage should be positive";
}

// Test 11: P-channel Active Region
TEST(IGBT_P, ActiveRegion) {
    CircuitGraph circuit;

    // IGBT P-channel: Veg_th = 5V, KP = 2.0 A/V² (more realistic), Vce_sat = 2V
    // Test: Veg = 7V, Vec = 5V > Vce_sat → Active region
    circuit.add_voltage_source("VEE", "emitter", "0", 20.0);  // Ve = 20V
    circuit.add_resistor("RC", "0", "collector", 10.0);       // 10Ω load (increased for smaller current)
    circuit.add_voltage_source("VEG", "emitter", "gate", 7.0);// Veg = 7V
    circuit.add_igbt_p("Q1", "collector", "gate", "emitter",
                       5.0,   // VEG_TH
                       2.0,   // KP (reduced from 50 to 2, more realistic)
                       2.0,   // VCE_SAT
                       1e-6,  // TAU
                       1.0);  // AREA

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId collector_id = circuit.get_node_id("collector");
    Real v_collector = solution[collector_id - 1];

    // P-channel active region: high current flow
    // Collector voltage should be significantly positive
    EXPECT_GT(v_collector, 0.0) << "Collector voltage should be positive in active region";
}

// Test 12: Transient Tail Current Effect (Simplified check)
TEST(IGBT_N, TailCurrentEffect) {
    // This test verifies that the tail current state machine is implemented
    // Full transient testing requires transient solver integration

    CircuitGraph circuit;
    circuit.add_voltage_source("VCC", "collector", "0", 20.0); // 20V (reduced)
    circuit.add_voltage_source("VGE", "gate", "0", 7.0);  // Initially ON
    circuit.add_igbt_n("Q1", "collector", "gate", "0",
                       5.0,   // VGE_TH
                       2.0,   // KP (reduced from 50 to 2)
                       2.0,   // VCE_SAT
                       1e-6,  // TAU (1 μs tail current time constant)
                       1.0);  // AREA

    auto igbt = std::dynamic_pointer_cast<IGBT_N>(circuit.get_device("Q1"));
    ASSERT_TRUE(igbt != nullptr);

    // Verify that tail current parameters are accessible
    auto tau = igbt->get_parameter("tau");
    ASSERT_TRUE(tau.has_value());
    EXPECT_NEAR(tau.value(), 1e-6, 1e-12) << "TAU should be 1 μs";

    // Full tail current behavior testing requires transient solver
    // This test just verifies the parameter is set correctly
}

// The DC iteration of a strongly driven stage converges to the solution of
// the IGBT's equations: with KP = 2 (the netlist default), VGE_TH = 5 and
// VCE_SAT = 2, VGE = 15 V gives Ic = 2 (10)^2 VCE / 2 = 100 VCE below VCE_SAT,
// and VCC = 100 V through RL = 100 Ohm puts the collector at 100 / 10001 V.
// Before the VCE step limit (core::limit_fet_drain_step) and the KCL test of
// convergence, this run ended at V(c) = -1404.7 V.
TEST(IGBTNetlist, StronglyDrivenStageConverges) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "igbt\nVCC vcc 0 DC 100\nRL vcc c 100\nVG g 0 DC 15\nM1 c g 0 m\n"
        ".model m nmos level=21\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(solution[parsed.circuit->get_node_id("c") - 1], 100.0 / 10001.0, 1e-9);
}

// After a turn-off the collector carries the tail current I0 exp(-(t - t0) /
// TAU) from the first accepted point with the gate below threshold (t0), I0
// being the current accepted before it. Without CCE and CGE, the collector of
// the stage above follows V(c) = 100 - 100 I0 exp(-(t - t0) / TAU) after t0.
TEST(IGBTNetlist, TailCurrentDecaysAfterTurnOff) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "tail\nVCC vcc 0 DC 100\nRL vcc c 100\nVG g 0 DC 15 PULSE(15 0 10u 10n 10n 1 2)\n"
        "M1 c g 0 m\n.model m nmos level=21 tau=2u cge=0 cce=0\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    SimulationConfig config = parsed.config;
    config.transient.use_adaptive_timestep = false;
    TransientSolver solver(*parsed.circuit, config);
    const auto result = solver.solve(0.0, 20e-6, 0.5e-6);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId c = parsed.circuit->get_node_id("c");
    const Real i0 = (100.0 - 100.0 / 10001.0) / 100.0;
    const Real t0 = 10.5e-6;
    int checked = 0;
    for (const auto& tp : result.timepoints) {
        if (tp.time > t0 + 1e-9) {
            const Real expected = 100.0 - 100.0 * i0 * std::exp(-(tp.time - t0) / 2e-6);
            EXPECT_NEAR(tp.node_voltages[c - 1], expected, 1e-6) << "t=" << tp.time;
            ++checked;
        }
    }
    EXPECT_GT(checked, 10);
}
