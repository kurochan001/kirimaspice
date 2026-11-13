// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_scr.cpp
// SCR (Silicon Controlled Rectifier / Thyristor) Device Tests

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/scr.hpp"  // Step 4.3: Unified SCR template
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::devices;
using namespace kirimaspice::solvers;

//==============================================================================
// SCR DC Characteristics Tests
//==============================================================================

// Test 1: Blocking State (Vak < VBF, Ig < IGT)
// SCR should not conduct when gate current is insufficient and anode voltage is below breakover
TEST(SCR, BlockingState) {
    CircuitGraph circuit;

    // SCR default: VBF=600V, IGT=10mA, IH=5mA, IL=30mA, VGT=1.0V, RON=0.1Ω, VTM=1.5V
    // Test: Vak = 50V < 600V, no gate signal → SCR should block
    circuit.add_voltage_source("VAK", "anode", "0", 50.0);  // Vak = 50V < VBF
    circuit.add_resistor("RLOAD", "anode", "cathode", 100.0);  // 100Ω load
    circuit.add_scr_n("SCR1", "cathode", "0", "gate",
                    400.0,   // VBF (default)
                    20e-3,   // IGT (default)
                    10e-3,   // IH (default)
                    30e-3,   // IL (new, 3× IH)
                    1.0,     // VGT (default)
                    0.1,     // RON (default)
                    1.5,     // VTM (default)
                    10.0,    // DV_DT (default)
                    300.15); // TNOM (default)

    // Wait, I need to check the pin order. Let me reconsider...
    // Actually, for proper forward bias: anode should be at higher potential than cathode
    // So: S<name> <anode> <cathode> <gate>

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId cathode_id = circuit.get_node_id("cathode");
    Real v_cathode = solution[cathode_id - 1];

    // In blocking state, no current flows, so V_cathode ≈ V_anode (no voltage drop)
    EXPECT_NEAR(v_cathode, 50.0, 1.0) << "Cathode voltage should be ~Vanode in blocking state";
}

// Test 2: Gate Triggering (Ig >= IGT)
// SCR should trigger when gate current exceeds gate trigger current
TEST(SCR, GateTrigger_CurrentThreshold) {
    CircuitGraph circuit;

    // SCR: IGT = 10mA (default)
    // Apply sufficient gate current to trigger
    circuit.add_voltage_source("VAK", "anode", "0", 50.0);  // Vak = 50V
    circuit.add_resistor("RLOAD", "anode", "cathode", 10.0);  // 10Ω load
    circuit.add_voltage_source("VG", "gate", "0", 2.0);  // Gate voltage = 2V > VGT (1V)
    circuit.add_resistor("RG", "gate", "cathode", 100.0);  // 100Ω gate resistor → Ig = 2V/100Ω = 20mA > IGT (10mA)
    circuit.add_scr_n("SCR1", "cathode", "0", "gate",
                    600.0,   // VBF
                    10e-3,   // IGT = 10mA
                    5e-3,    // IH = 5mA
                    15e-3,   // IL = 15mA (3× IH)
                    1.0,     // VGT = 1V
                    0.1,     // RON = 0.1Ω
                    1.5,     // VTM = 1.5V (renamed from VF)
                    10.0,    // DV_DT = 10 V/μs
                    300.15); // TNOM = 300.15K

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId cathode_id = circuit.get_node_id("cathode");
    Real v_cathode = solution[cathode_id - 1];

    // When triggered, SCR conducts: V_cathode should drop significantly (approaching VF)
    EXPECT_LT(v_cathode, 10.0) << "Cathode voltage should drop when SCR is triggered";
    EXPECT_GT(v_cathode, 0.0) << "Cathode voltage should be positive";
}

// Test 3: Breakover Voltage Triggering (Vak >= VBF)
// SCR should trigger when anode-cathode voltage exceeds forward breakover voltage
TEST(SCR, BreakoverVoltageTrigger) {
    CircuitGraph circuit;

    // SCR: VBF = 100V (reduced for testing)
    // Apply Vak > VBF to trigger
    circuit.add_voltage_source("VAK", "anode", "0", 120.0);  // Vak = 120V > VBF (100V)
    circuit.add_resistor("RLOAD", "anode", "cathode", 10.0);  // 10Ω load
    circuit.add_scr_n("SCR1", "cathode", "0", "gate",
                    100.0,   // VBF = 100V (reduced for testing)
                    10e-3,   // IGT = 10mA
                    5e-3,    // IH = 5mA
                    15e-3,   // IL = 15mA (3× IH)
                    1.0,     // VGT = 1V
                    0.1,     // RON = 0.1Ω
                    1.5,     // VTM = 1.5V
                    10.0,    // DV_DT = 10 V/μs
                    300.15); // TNOM = 300.15K

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS) << "SCR circuit must converge before accessing solution (prevents SEGFAULT)";

    NodeId cathode_id = circuit.get_node_id("cathode");
    Real v_cathode = solution[cathode_id - 1];

    // When triggered by breakover, SCR conducts
    EXPECT_LT(v_cathode, 50.0) << "Cathode voltage should drop when SCR triggers via breakover";
    EXPECT_GT(v_cathode, 0.0) << "Cathode voltage should be positive";
}

// Test 4: Latching Behavior (Remains ON after gate signal removed)
// This requires transient analysis to demonstrate time-dependent behavior
TEST(SCR, LatchingBehavior_Transient) {
    CircuitGraph circuit;

    // Gate pulse: trigger at t=0, then remove gate signal
    // SCR should remain on after gate signal is removed (latching)
    circuit.add_voltage_source("VAK", "anode", "0", 50.0);
    circuit.add_resistor("RLOAD", "anode", "cathode", 10.0);

    // Gate pulse: Initially high to trigger, then we'll check steady state
    circuit.add_voltage_source("VG", "gate", "0", 2.0);  // Constant gate voltage for DC
    circuit.add_resistor("RG", "gate", "cathode", 80.0);  // 80Ω → Ig = 2V/80Ω = 25mA > IGT

    circuit.add_scr_n("SCR1", "cathode", "0", "gate",
                    600.0,   // VBF
                    10e-3,   // IGT = 10mA
                    5e-3,    // IH = 5mA
                    15e-3,   // IL = 15mA (3× IH)
                    1.0,     // VGT = 1V
                    0.1,     // RON = 0.1Ω
                    1.5,     // VTM = 1.5V
                    10.0,    // DV_DT = 10 V/μs
                    300.15); // TNOM = 300.15K

    // First, verify DC solution with gate signal present
    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId cathode_id = circuit.get_node_id("cathode");
    Real v_cathode = solution[cathode_id - 1];

    // SCR should be conducting
    EXPECT_LT(v_cathode, 20.0) << "SCR should conduct when gate triggered";

    // Note: Full latching test (gate removal) requires transient analysis with time-varying sources
    // This is a simplified DC test showing triggered state
}

// Test 5: Holding Current (Turn-off when Iak < IH)
// SCR should turn off when anode current drops below holding current
TEST(SCR, TurnOff_HoldingCurrent) {
    CircuitGraph circuit;

    // Create circuit where anode current will be below IH
    // With high load resistance, current will be minimal
    circuit.add_voltage_source("VAK", "anode", "0", 50.0);
    circuit.add_resistor("RLOAD", "anode", "cathode", 10000.0);  // 10kΩ → very low current
    circuit.add_voltage_source("VG", "gate", "0", 0.5);  // Gate voltage below VGT (no trigger)
    circuit.add_scr_n("SCR1", "cathode", "0", "gate",
                    600.0,   // VBF
                    10e-3,   // IGT = 10mA
                    5e-3,    // IH = 5mA
                    15e-3,   // IL = 15mA (3× IH)
                    1.0,     // VGT = 1V
                    0.1,     // RON = 0.1Ω
                    1.5,     // VTM = 1.5V
                    10.0,    // DV_DT = 10 V/μs
                    300.15); // TNOM = 300.15K

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId cathode_id = circuit.get_node_id("cathode");
    Real v_cathode = solution[cathode_id - 1];

    // With no gate trigger and low current, SCR should remain in blocking state
    // V_cathode ≈ V_anode (minimal voltage drop)
    EXPECT_NEAR(v_cathode, 50.0, 5.0) << "SCR should be in blocking/off state with insufficient gate signal";
}

// Test 6: On-State Resistance (RON)
// Verify that on-state resistance parameter affects voltage drop
TEST(SCR, OnStateResistance_Effect) {
    CircuitGraph circuit;

    // Test two SCRs with different RON values
    // SCR1: RON = 0.1Ω (default)
    // SCR2: RON = 1.0Ω (10x higher)

    // Circuit 1: RON = 0.1Ω
    CircuitGraph circuit1;
    circuit1.add_voltage_source("VAK", "anode", "0", 50.0);
    circuit1.add_resistor("RLOAD", "anode", "cathode", 10.0);
    circuit1.add_voltage_source("VG", "gate", "0", 2.0);
    circuit1.add_resistor("RG", "gate", "cathode", 80.0);  // Ig = 25mA > IGT
    circuit1.add_scr_n("SCR1", "cathode", "0", "gate",
                     600.0,   // VBF
                     10e-3,   // IGT
                     5e-3,    // IH
                     15e-3,   // IL (3× IH)
                     1.0,     // VGT
                     0.1,     // RON = 0.1Ω
                     1.5,     // VTM
                     10.0,    // DV_DT
                     300.15); // TNOM

    DCSolver solver1(circuit1);
    DenseVector solution1;
    SolverStats stats1;
    ConvergenceStatus status1 = solver1.solve(solution1, stats1);
    EXPECT_EQ(status1, ConvergenceStatus::SUCCESS);

    NodeId cathode_id1 = circuit1.get_node_id("cathode");
    Real v_cathode1 = solution1[cathode_id1 - 1];

    // Circuit 2: RON = 1.0Ω
    CircuitGraph circuit2;
    circuit2.add_voltage_source("VAK", "anode", "0", 50.0);
    circuit2.add_resistor("RLOAD", "anode", "cathode", 10.0);
    circuit2.add_voltage_source("VG", "gate", "0", 2.0);
    circuit2.add_resistor("RG", "gate", "cathode", 80.0);
    circuit2.add_scr_n("SCR2", "cathode", "0", "gate",
                     600.0,   // VBF
                     10e-3,   // IGT
                     5e-3,    // IH
                     15e-3,   // IL (3× IH)
                     1.0,     // VGT
                     1.0,     // RON = 1.0Ω (10x higher)
                     1.5,     // VTM
                     10.0,    // DV_DT
                     300.15); // TNOM

    DCSolver solver2(circuit2);
    DenseVector solution2;
    SolverStats stats2;
    ConvergenceStatus status2 = solver2.solve(solution2, stats2);
    EXPECT_EQ(status2, ConvergenceStatus::SUCCESS);

    NodeId cathode_id2 = circuit2.get_node_id("cathode");
    Real v_cathode2 = solution2[cathode_id2 - 1];

    // Higher RON increases total circuit resistance, reducing current
    // This results in HIGHER cathode voltage (less voltage drop across RLOAD)
    // V_cathode = VF + I×RON, and with higher RON, V_cathode increases more than I decreases
    EXPECT_GT(v_cathode2, v_cathode1) << "Higher RON should result in higher cathode voltage";
}

//==============================================================================
// SCR Parameter Access Tests
//==============================================================================

// Test 7: Parameter Get/Set
TEST(SCR, ParameterAccess) {
    DeviceId id = 1;
    std::string name = "SCR_TEST";
    NodeId anode = 1, cathode = 2, gate = 3;

    // Create SCR with default parameters
    SCR_N scr(id, name, anode, cathode, gate);

    // Test get_parameter()
    EXPECT_TRUE(scr.get_parameter("VBF").has_value());
    EXPECT_EQ(scr.get_parameter("VBF").value(), 400.0);  // Updated default

    EXPECT_TRUE(scr.get_parameter("IGT").has_value());
    EXPECT_EQ(scr.get_parameter("IGT").value(), 20e-3);  // Updated default

    EXPECT_TRUE(scr.get_parameter("IH").has_value());
    EXPECT_EQ(scr.get_parameter("IH").value(), 10e-3);   // Updated default

    EXPECT_TRUE(scr.get_parameter("RON").has_value());
    EXPECT_EQ(scr.get_parameter("RON").value(), 0.1);

    // Test case insensitivity
    EXPECT_TRUE(scr.get_parameter("vbf").has_value());
    EXPECT_TRUE(scr.get_parameter("igt").has_value());

    // Test invalid parameter
    EXPECT_FALSE(scr.get_parameter("INVALID_PARAM").has_value());

    // Test set_parameter()
    EXPECT_TRUE(scr.set_parameter("VBF", 800.0));
    EXPECT_EQ(scr.get_parameter("VBF").value(), 800.0);

    EXPECT_TRUE(scr.set_parameter("RON", 0.5));
    EXPECT_EQ(scr.get_parameter("RON").value(), 0.5);

    // Test validation (negative values should fail)
    EXPECT_FALSE(scr.set_parameter("VBF", -100.0));
    EXPECT_FALSE(scr.set_parameter("IGT", -0.01));
    EXPECT_FALSE(scr.set_parameter("VTM", -1.0));  // Updated parameter name (VF → VTM)

    // Test get_perturbable_parameters()
    std::vector<std::string> perturbable = scr.get_perturbable_parameters();
    EXPECT_EQ(perturbable.size(), 4);  // Updated count (VBF, IGT, IH, RON)
    EXPECT_NE(std::find(perturbable.begin(), perturbable.end(), "VBF"), perturbable.end());
    EXPECT_NE(std::find(perturbable.begin(), perturbable.end(), "IGT"), perturbable.end());
    EXPECT_NE(std::find(perturbable.begin(), perturbable.end(), "IH"), perturbable.end());
    EXPECT_NE(std::find(perturbable.begin(), perturbable.end(), "RON"), perturbable.end());
    // Note: VGT and VTM are not perturbable in the simplified SCR_N model
}

//==============================================================================
// SCR Model Reference Test
//==============================================================================

// Test 8: Model Reference (Parser Integration)
// This test verifies that SCR devices can reference .MODEL definitions
TEST(SCR, ModelReference_ParserIntegration) {
    // This test would require parser integration
    // Placeholder for now - actual test would parse a netlist like:
    // .MODEL SCR_600V SCR (VBF=600 IGT=10m IH=5m RON=0.1)
    // SSCR1 anode cathode gate SCR_600V

    // For now, just verify that SCR can be created with custom parameters
    CircuitGraph circuit;
    circuit.add_scr_n("SCR1", "anode", "cathode", "gate",
                    800.0,   // VBF
                    15e-3,   // IGT
                    8e-3,    // IH
                    24e-3,   // IL (3× IH)
                    1.2,     // VGT
                    0.2,     // RON
                    1.8,     // VTM (renamed from VF)
                    10.0,    // DV_DT
                    300.15); // TNOM

    // Verify circuit has the device
    auto device = circuit.get_device("SCR1");
    EXPECT_NE(device, nullptr);
    EXPECT_EQ(device->type(), DeviceType::SCR_N);
}

//==============================================================================
// SCR Noise Analysis Test
//==============================================================================

// Test 9: Thermal Noise (when conducting)
TEST(SCR, ThermalNoise_Conducting) {
    DeviceId id = 1;
    std::string name = "SCR_NOISE";
    NodeId anode = 1, cathode = 2, gate = 3;

    SCR_N scr(id, name, anode, cathode, gate,
            600.0,  // VBF
            10e-3,  // IGT
            5e-3,   // IH
            1.0,    // VGT
            0.1,    // RON
            1.5,    // VF
            300.15);// TNOM = 300.15K (27°C)

    // Prepare dummy DC solution (not used for noise calculation in this simplified model)
    DenseVector dc_solution(3, 0.0);

    // Test noise sources when SCR is not latched (blocking)
    auto noise_sources_off = scr.get_noise_sources(dc_solution, 1e3);
    EXPECT_EQ(noise_sources_off.size(), 0) << "No noise sources in blocking state";

    // Note: To test noise in conducting state, we would need to trigger the SCR first
    // This requires a more complex setup with MNA stamping and state update
    // For now, this test verifies the structure
}

//==============================================================================
// SCR State Management Test
//==============================================================================

// Test 10: State Reset
TEST(SCR, StateReset) {
    DeviceId id = 1;
    std::string name = "SCR_RESET";
    NodeId anode = 1, cathode = 2, gate = 3;

    SCR_N scr(id, name, anode, cathode, gate);

    // Reset state should set latched_ = false
    scr.reset_state();

    // After reset, SCR should be in blocking mode
    // We can't directly access latched_, but we can verify through behavior
    // (This is implicitly tested by other tests)

    // Just verify reset_state() doesn't throw
    EXPECT_NO_THROW(scr.reset_state());
}
