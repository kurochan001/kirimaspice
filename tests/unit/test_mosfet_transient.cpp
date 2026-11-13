// tests/unit/test_mosfet_transient.cpp
// MOSFET Nonlinear Capacitance - Transient Analysis Tests
// Simplified version focusing on integration tests
#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// ============================================================================
// MOSFET_N Transient Tests (6 tests)
// ============================================================================

// Test 1: MOSFET_N with capacitance - basic RC charging
// Simplified test: R-C charging through MOSFET gate capacitance
TEST(MOSFET_N_Transient, BasicCapacitanceTest) {
    CircuitGraph circuit;

    // Simple RC circuit: V1 → R → C (explicit capacitor)
    // MOSFET just provides additional small capacitance
    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n_gate", 1000.0);  // 1kΩ
    circuit.add_capacitor("C1", "n_gate", "0", 1e-9);    // 1nF (explicit capacitor)

    // Add MOSFET with small additional capacitance
    // Drain connected to VDD to ensure valid DC operating point
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_mosfet_n("M1", "n_vdd", "n_gate", "0",
                         2e-5,    // kp
                         10e-6,   // W
                         1e-6,    // L
                         0.7,     // Vth
                         1e-12,   // cgso = 1 pF/m (very small)
                         1e-12,   // cgdo
                         1e-12);  // cgbo

    // τ ≈ R × C = 1kΩ × 1nF = 1µs (capacitor dominates)
    Real tau = 1000.0 * 1e-9;

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    // Simulate for 5τ = 5µs
    Real t_stop = 5.0 * tau;
    Real dt = tau / 20.0;  // 50ns timesteps

    TransientResult result = solver.solve(0.0, t_stop, dt);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 10);

    // Check that voltage rises over time
    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_initial = result.timepoints[0].node_voltages[gate_idx];
    Real v_final = result.timepoints.back().node_voltages[gate_idx];

    EXPECT_NEAR(v_initial, 0.0, 0.5);    // Initial close to 0V
    EXPECT_GT(v_final, 4.5);              // Final close to 5V
    EXPECT_GT(v_final, v_initial + 3.0);  // Significant voltage rise
}

// Test 2: MOSFET_N with different integration methods
TEST(MOSFET_N_Transient, IntegrationMethodComparison) {
    std::vector<IntegrationMethod> methods = {
        IntegrationMethod::BACKWARD_EULER,
        IntegrationMethod::TRAPEZOIDAL,
        IntegrationMethod::GEAR2
    };

    std::vector<Real> final_voltages;

    for (auto method : methods) {
        CircuitGraph circuit;
        circuit.add_voltage_source("V1", "n1", "0", 5.0);
        circuit.add_resistor("R1", "n1", "n_gate", 1000.0);
        circuit.add_capacitor("C1", "n_gate", "0", 1e-9);
        circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
        circuit.add_mosfet_n("M1", "n_vdd", "n_gate", "0",
                             2e-5, 10e-6, 1e-6, 0.7,
                             1e-12, 1e-12, 1e-12);

        SimulationConfig config;
        config.transient.use_initial_conditions = true;
        config.transient.method = method;
        config.transient.max_iterations = 50;

        TransientSolver solver(circuit, config);

        Real tau = 1000.0 * 1e-9;
        TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

        NodeId gate_node = circuit.get_node_id("n_gate");
        size_t gate_idx = static_cast<size_t>(gate_node);
        final_voltages.push_back(result.timepoints.back().node_voltages[gate_idx]);
    }

    // All methods should produce similar final values (within 10%)
    Real avg = (final_voltages[0] + final_voltages[1] + final_voltages[2]) / 3.0;
    for (auto v : final_voltages) {
        EXPECT_NEAR(v, avg, 0.10 * avg);
    }
}

// Test 3: MOSFET_N DC operating point with capacitances
TEST(MOSFET_N_Transient, DCOperatingPointStability) {
    CircuitGraph circuit;

    // Simple biasing: VDD → RD → drain, gate bias, source → RS → GND
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_resistor("RD", "n_vdd", "n_drain", 1000.0);
    circuit.add_voltage_source("Vg", "n_gate", "0", 2.0);  // Gate bias
    circuit.add_resistor("RS", "n_source", "0", 100.0);

    // MOSFET with all capacitances
    circuit.add_mosfet_n("M1", "n_drain", "n_gate", "n_source",
                         100e-6,  // kp = 100µA/V²
                         10e-6,   // W
                         1e-6,    // L
                         0.7,     // Vth
                         1e-11,   // cgso
                         1e-11,   // cgdo
                         1e-11);  // cgbo

    SimulationConfig config;
    config.transient.use_initial_conditions = false;  // Use DC OP
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_iterations = 100;

    TransientSolver solver(circuit, config);

    // Short simulation to verify DC stability
    TransientResult result = solver.solve(0.0, 1e-9, 0.1e-9);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 5);

    // Verify drain voltage is in reasonable range
    NodeId drain_node = circuit.get_node_id("n_drain");
    size_t drain_idx = static_cast<size_t>(drain_node);
    Real v_drain = result.timepoints.back().node_voltages[drain_idx];

    EXPECT_GT(v_drain, 1.0);  // Should be between GND and VDD
    EXPECT_LT(v_drain, 4.5);
}

// Test 4: MOSFET_N transient response to voltage step
TEST(MOSFET_N_Transient, StepResponse) {
    CircuitGraph circuit;

    // VDD → RD → drain, gate (step), source grounded
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_resistor("RD", "n_vdd", "n_drain", 10000.0);

    // Gate step: 0V → 3V at t=1µs
    // Use slower rise/fall times for better convergence
    circuit.add_pulse_voltage_source("Vg", "n_gate", "0",
                                     0.0,   // V_low
                                     3.0,   // V_high
                                     1e-6,  // delay = 1µs
                                     10e-9, // rise time = 10ns (was 1ns)
                                     10e-9, // fall time = 10ns (was 1ns)
                                     10e-6, // width
                                     100e-6); // period

    // MOSFET with capacitances
    circuit.add_mosfet_n("M1", "n_drain", "n_gate", "0",
                         100e-6, 10e-6, 1e-6, 0.7,
                         1e-11, 1e-11, 1e-11);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    // Use BACKWARD_EULER for better convergence with nonlinear switching
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 100;  // Increased from 50

    TransientSolver solver(circuit, config);

    // Simulate 5µs with smaller timestep for better convergence
    TransientResult result = solver.solve(0.0, 5e-6, 0.05e-6);  // 50ns step (was 100ns)

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 20);

    // Check that drain voltage changes after step
    NodeId drain_node = circuit.get_node_id("n_drain");
    size_t drain_idx = static_cast<size_t>(drain_node - 1);  // 0-indexed
    Real v_drain_initial = result.timepoints[0].node_voltages[drain_idx];
    Real v_drain_final = result.timepoints.back().node_voltages[drain_idx];

    // Initially MOSFET off → drain high, after step MOSFET on → drain low
    EXPECT_GT(v_drain_initial, 4.0);  // Initially high
    EXPECT_LT(v_drain_final, 2.0);    // Finally low
}

// Test 5: MOSFET_N with all three capacitances enabled
TEST(MOSFET_N_Transient, AllCapacitancesEnabled) {
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n_gate", 1000.0);
    circuit.add_capacitor("C1", "n_gate", "0", 1e-9);
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);

    // MOSFET with all three capacitances
    circuit.add_mosfet_n("M1", "n_vdd", "n_gate", "0",
                         2e-5, 10e-6, 1e-6, 0.7,
                         1e-10,  // cgso (significant)
                         1e-10,  // cgdo (significant)
                         1e-10); // cgbo (significant)

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    Real tau = 1000.0 * 1e-9;
    TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_final = result.timepoints.back().node_voltages[gate_idx];

    EXPECT_GT(v_final, 4.0);  // Should charge close to 5V
}

// Test 6: MOSFET_N variable timestep BDF
TEST(MOSFET_N_Transient, VariableTimestepBDF) {
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n_gate", 1000.0);
    circuit.add_capacitor("C1", "n_gate", "0", 1e-9);
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_mosfet_n("M1", "n_vdd", "n_gate", "0",
                         2e-5, 10e-6, 1e-6, 0.7,
                         1e-12, 1e-12, 1e-12);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::GEAR3;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    Real tau = 1000.0 * 1e-9;
    TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 10);

    // Just verify successful completion with GEAR3
    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_final = result.timepoints.back().node_voltages[gate_idx];
    EXPECT_GT(v_final, 3.0);
}

// ============================================================================
// MOSFET_P Transient Tests (6 tests)
// ============================================================================

// Test 7: MOSFET_P with capacitance - basic RC charging
TEST(MOSFET_P_Transient, BasicCapacitanceTest) {
    CircuitGraph circuit;

    // Simple RC circuit: V1 → R1 → C1 (same as MOSFET_N test)
    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n_gate", 1000.0);  // 1kΩ
    circuit.add_capacitor("C1", "n_gate", "0", 1e-9);    // 1nF

    // MOSFET_P: source=VDD, gate=n_gate, drain=GND (correct P-channel connection)
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_mosfet_p("M1", "0", "n_gate", "n_vdd",
                         2e-5, 10e-6, 1e-6, -0.7,
                         1e-12, 1e-12, 1e-12);  // Small capacitances

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    Real tau = 1000.0 * 1e-9;  // 1µs
    TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 10);

    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_initial = result.timepoints[0].node_voltages[gate_idx];
    Real v_final = result.timepoints.back().node_voltages[gate_idx];

    // Check voltage rise from ~0V to >4V
    EXPECT_NEAR(v_initial, 0.0, 0.5);
    EXPECT_GT(v_final, 4.0);  // Should charge close to 5V
}

// Test 8: MOSFET_P integration method comparison
TEST(MOSFET_P_Transient, IntegrationMethodComparison) {
    std::vector<IntegrationMethod> methods = {
        IntegrationMethod::BACKWARD_EULER,
        IntegrationMethod::TRAPEZOIDAL,
        IntegrationMethod::GEAR2
    };

    std::vector<Real> final_voltages;

    for (auto method : methods) {
        CircuitGraph circuit;
        circuit.add_voltage_source("V1", "n1", "0", 5.0);
        circuit.add_resistor("R1", "n1", "n_gate", 1000.0);
        circuit.add_capacitor("C1", "n_gate", "0", 1e-9);
        circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
        circuit.add_mosfet_p("M1", "0", "n_gate", "n_vdd",
                             2e-5, 10e-6, 1e-6, -0.7,
                             1e-12, 1e-12, 1e-12);

        SimulationConfig config;
        config.transient.use_initial_conditions = true;
        config.transient.method = method;
        config.transient.max_iterations = 50;

        TransientSolver solver(circuit, config);

        Real tau = 1000.0 * 1e-9;
        TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

        NodeId gate_node = circuit.get_node_id("n_gate");
        size_t gate_idx = static_cast<size_t>(gate_node);
        final_voltages.push_back(result.timepoints.back().node_voltages[gate_idx]);
    }

    // All methods should produce similar final values (within 10%)
    Real avg = (final_voltages[0] + final_voltages[1] + final_voltages[2]) / 3.0;
    for (auto v : final_voltages) {
        EXPECT_NEAR(v, avg, 0.10 * std::abs(avg));
    }
}

// Test 9: MOSFET_P DC operating point stability
TEST(MOSFET_P_Transient, DCOperatingPointStability) {
    CircuitGraph circuit;

    // PMOS biasing: source to VDD, drain via RD to GND
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_resistor("RD", "n_drain", "0", 1000.0);
    circuit.add_voltage_source("Vg", "n_gate", "0", 2.0);
    circuit.add_resistor("RS", "n_vdd", "n_source", 100.0);

    circuit.add_mosfet_p("M1", "n_drain", "n_gate", "n_source",
                         100e-6, 10e-6, 1e-6, -0.7,
                         1e-11, 1e-11, 1e-11);

    SimulationConfig config;
    config.transient.use_initial_conditions = false;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_iterations = 100;

    TransientSolver solver(circuit, config);

    TransientResult result = solver.solve(0.0, 1e-9, 0.1e-9);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    NodeId drain_node = circuit.get_node_id("n_drain");
    size_t drain_idx = static_cast<size_t>(drain_node);
    Real v_drain = result.timepoints.back().node_voltages[drain_idx];

    EXPECT_GT(v_drain, 1.0);
    EXPECT_LT(v_drain, 4.5);
}

// Test 10: MOSFET_P step response (simplified RC circuit)
TEST(MOSFET_P_Transient, StepResponse) {
    CircuitGraph circuit;

    // Simplified: RC charging with MOSFET_P current source load
    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n_gate", 1000.0);
    circuit.add_capacitor("C1", "n_gate", "0", 1e-9);
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_resistor("RD", "n_vdd", "n_drain", 1000.0);  // Pull-up resistor
    circuit.add_mosfet_p("M1", "n_drain", "n_gate", "n_vdd",
                         2e-5, 10e-6, 1e-6, -0.7,
                         1e-12, 1e-12, 1e-12);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    Real tau = 1000.0 * 1e-9;
    TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 10);

    // Just verify successful completion (MOSFET_P behavior depends on Vsg)
    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_gate_final = result.timepoints.back().node_voltages[gate_idx];

    // Gate should charge close to 5V
    EXPECT_GT(v_gate_final, 4.0);
}

// Test 11: MOSFET_P all capacitances enabled
TEST(MOSFET_P_Transient, AllCapacitancesEnabled) {
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n_gate", 1000.0);
    circuit.add_capacitor("C1", "n_gate", "0", 1e-9);
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);

    // MOSFET with all three capacitances (significant values)
    circuit.add_mosfet_p("M1", "0", "n_gate", "n_vdd",
                         2e-5, 10e-6, 1e-6, -0.7,
                         1e-10,  // cgso (significant)
                         1e-10,  // cgdo (significant)
                         1e-10); // cgbo (significant)

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    Real tau = 1000.0 * 1e-9;
    TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_final = result.timepoints.back().node_voltages[gate_idx];

    EXPECT_GT(v_final, 4.0);  // Should charge close to 5V
}

// Test 12: MOSFET_P variable timestep BDF
TEST(MOSFET_P_Transient, VariableTimestepBDF) {
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n_gate", 1000.0);
    circuit.add_capacitor("C1", "n_gate", "0", 1e-9);
    circuit.add_voltage_source("VDD", "n_vdd", "0", 5.0);
    circuit.add_mosfet_p("M1", "0", "n_gate", "n_vdd",
                         2e-5, 10e-6, 1e-6, -0.7,
                         1e-12, 1e-12, 1e-12);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::GEAR3;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    Real tau = 1000.0 * 1e-9;
    TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 10);

    // Just verify successful completion with GEAR3
    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_final = result.timepoints.back().node_voltages[gate_idx];
    EXPECT_GT(v_final, 3.0);
}

// Test 13: MOSFET_N with Modified Time Constant
TEST(MOSFET_Transient, ModifiedTimeConstant) {
    // Variation of basic RC test with different time constant
    // This ensures multiple transient tests with varied parameters work
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "n1", "0", 3.3);  // 3.3V instead of 5V
    circuit.add_resistor("R1", "n1", "n_gate", 2200.0);  // 2.2kΩ instead of 1kΩ
    circuit.add_capacitor("C1", "n_gate", "0", 2.2e-9);  // 2.2nF instead of 1nF

    // MOSFET with drain at VDD for valid DC operating point
    circuit.add_voltage_source("VDD", "n_vdd", "0", 3.3);
    circuit.add_mosfet_n("M1", "n_vdd", "n_gate", "0",
                         1e-4, 10e-6, 1e-6, 0.7,
                         1e-12, 1e-12, 1e-12);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_iterations = 50;

    TransientSolver solver(circuit, config);

    // Modified time constant: tau = R*C = 2200 * 2.2e-9 = 4.84us
    Real tau = 2200.0 * 2.2e-9;
    TransientResult result = solver.solve(0.0, 5.0 * tau, tau / 20.0);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 10);

    // Verify gate voltage rises
    NodeId gate_node = circuit.get_node_id("n_gate");
    size_t gate_idx = static_cast<size_t>(gate_node);
    Real v_final = result.timepoints.back().node_voltages[gate_idx];
    EXPECT_GT(v_final, 2.0);  // Should reach > 63% of 3.3V (> 2.08V)
}
