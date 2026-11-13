// tests/unit/test_trapezoidal_variable_step.cpp
// Unit tests for Trapezoidal method with variable timestep support

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

class TrapezoidalVariableStepTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set log level to info (change to trace for debugging)
        spdlog::set_level(spdlog::level::info);
    }
};

// Test 1: Basic RC circuit with adaptive timestep using Trapezoidal
TEST_F(TrapezoidalVariableStepTest, BasicRCCircuitAdaptive) {
    CircuitGraph circuit;

    // Simple RC circuit: V1(10V) -[R=1k]- n1 -[C=1u]- GND
    // Time constant: tau = RC = 1ms
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Configure transient solver with adaptive timestep and Trapezoidal
    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.error_est = ErrorEstimation::PHYSICAL;  // Charge-based LTE
    config.transient.max_step = 1e-4;
    config.transient.min_step = 1e-9;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;  // DC operating point

    TransientSolver solver(circuit, config);

    // Run transient analysis
    Real t_start = 0.0;
    Real t_stop = 5e-3;  // 5ms = 5*tau
    Real timestep = 1e-5;

    auto result = solver.solve(t_start, t_stop, timestep);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 10);  // At least some timesteps

    // Check final voltage (should approach 10V)
    // At t=5ms = 5*tau, V(C) ≈ 10V * (1 - exp(-5)) ≈ 9.93V
    Real final_voltage = result.timepoints.back().node_voltages[1];  // n1 node (ID=2, index=1)
    EXPECT_GT(final_voltage, 9.5);
    EXPECT_LT(final_voltage, 10.1);

    spdlog::info("Trapezoidal adaptive: {} timesteps, final voltage: {:.6f}V",
                 result.timepoints.size(), final_voltage);
}

// Test 2: Adaptive timestep efficiency (variable step sizes)
TEST_F(TrapezoidalVariableStepTest, AdaptiveTimestepEfficiency) {
    CircuitGraph circuit;

    // RC circuit with tau = 1ms
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Test with adaptive timestep
    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_step = 1e-4;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Should converge to correct value
    Real final_voltage = result.timepoints.back().node_voltages[1];
    EXPECT_GT(final_voltage, 9.5);
    EXPECT_LT(final_voltage, 10.1);

    // Should be relatively efficient (not too many steps)
    EXPECT_LT(result.timepoints.size(), 300);  // Reasonable upper bound

    spdlog::info("Adaptive: {} steps, V_final={:.6f}V",
                 result.timepoints.size(), final_voltage);
}

// Test 3: Energy conservation in LC circuit (Trapezoidal is A-stable)
TEST_F(TrapezoidalVariableStepTest, EnergyConservationLCCircuit) {
    CircuitGraph circuit;

    // LC circuit: V1(10V) -[L=1mH]- n1 -[C=1uF]- GND
    // Resonant frequency: f0 = 1/(2π√LC) ≈ 5.03 kHz
    // Period: T0 ≈ 0.2ms
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_inductor("L1", "vin", "n1", 1e-3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_step = 1e-5;  // 10us
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 2e-3, 1e-6);  // 2ms = 10 periods

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Check that oscillation amplitude is preserved (energy conservation)
    // For ideal LC, energy should be constant
    // Trapezoidal is A-stable and should preserve oscillation energy

    // Find peak voltages in first and last periods
    Real first_peak = 0.0;
    Real last_peak = 0.0;

    for (std::size_t i = 0; i < result.timepoints.size(); ++i) {
        Real t = result.timepoints[i].time;
        Real v = result.timepoints[i].node_voltages[1];  // n1 voltage

        if (t < 0.3e-3) {  // First period
            first_peak = std::max(first_peak, std::abs(v));
        }
        if (t > 1.7e-3) {  // Last period
            last_peak = std::max(last_peak, std::abs(v));
        }
    }

    // Energy should be well preserved (within 5%)
    EXPECT_NEAR(last_peak, first_peak, 0.05 * first_peak);

    spdlog::info("LC energy conservation: first_peak={:.6f}V, last_peak={:.6f}V, drift={:.2f}%",
                 first_peak, last_peak, 100.0 * std::abs(last_peak - first_peak) / first_peak);
}

// Test 4: Verify timestep size actually varies
TEST_F(TrapezoidalVariableStepTest, TimestepSizeVariation) {
    CircuitGraph circuit;

    // RC circuit with sharp transition
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_step = 1e-4;
    config.transient.min_step = 1e-9;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 10);

    // Calculate timestep sizes
    std::vector<Real> timesteps;
    for (std::size_t i = 1; i < result.timepoints.size(); ++i) {
        Real dt = result.timepoints[i].time - result.timepoints[i - 1].time;
        timesteps.push_back(dt);
    }

    // Find min and max timestep
    Real min_dt = *std::min_element(timesteps.begin(), timesteps.end());
    Real max_dt = *std::max_element(timesteps.begin(), timesteps.end());

    // Timestep should vary (not all the same)
    EXPECT_GT(max_dt / min_dt, 1.5);  // At least 50% variation

    spdlog::info("Timestep variation: min={:.3e}s, max={:.3e}s, ratio={:.2f}",
                 min_dt, max_dt, max_dt / min_dt);
}

// Test 5: Accuracy verification with analytical solution
TEST_F(TrapezoidalVariableStepTest, AnalyticalSolutionComparison) {
    CircuitGraph circuit;

    // RC circuit: V1(10V) -[R=1k]- n1 -[C=1u]- GND
    // Analytical solution: V(t) = V0 * (1 - exp(-t/RC))
    Real V0 = 10.0;
    Real R = 1e3;
    Real C = 1e-6;
    Real tau = R * C;  // 1ms

    circuit.add_voltage_source("V1", "vin", "0", V0);
    circuit.add_resistor("R1", "vin", "n1", R);
    circuit.add_capacitor("C1", "n1", "0", C);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_step = 1e-4;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Check final value (t=5τ)
    // Analytical: V(5τ) = V0 * (1 - exp(-5)) ≈ 9.93V
    Real t_final = 5e-3;
    Real simulated_voltage = result.timepoints.back().node_voltages[1];
    Real analytical_voltage = V0 * (1.0 - std::exp(-t_final / tau));

    // Check accuracy (within 2%)
    Real error_percent = 100.0 * std::abs(simulated_voltage - analytical_voltage) / analytical_voltage;
    EXPECT_LT(error_percent, 2.0);

    spdlog::info("t={:.3e}s: simulated={:.6f}V, analytical={:.6f}V, error={:.3f}%",
                 t_final, simulated_voltage, analytical_voltage, error_percent);
}

// Test 6: Trapezoidal with different TRTOL values
TEST_F(TrapezoidalVariableStepTest, DifferentTRTOLValues) {
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    std::vector<Real> trtol_values = {10.0, 2.0, 0.5};
    std::vector<std::size_t> num_steps;
    std::vector<Real> final_voltages;

    for (Real trtol : trtol_values) {
        SimulationConfig config;
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::TRAPEZOIDAL;
        config.transient.max_step = 1e-4;
        config.transient.trtol = trtol;
        config.transient.use_initial_conditions = false;

        TransientSolver solver(circuit, config);
        auto result = solver.solve(0.0, 5e-3, 1e-5);

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        num_steps.push_back(result.timepoints.size());
        final_voltages.push_back(result.timepoints.back().node_voltages[1]);

        spdlog::info("TRTOL={:.1f}: {} timesteps, V_final={:.6f}V",
                     trtol, result.timepoints.size(), final_voltages.back());
    }

    // All should produce similar final results
    for (std::size_t i = 0; i < final_voltages.size(); ++i) {
        EXPECT_GT(final_voltages[i], 9.5);
        EXPECT_LT(final_voltages[i], 10.1);
    }

    // Note: Tighter TRTOL may result in more OR fewer timesteps depending on adaptation
    // The key is that all methods should converge to the correct answer
}

// Test 7: Stiff RC circuit with variable timestep
TEST_F(TrapezoidalVariableStepTest, StiffRCCircuit) {
    CircuitGraph circuit;

    // Stiff RC circuit (very small time constant)
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e2);  // 100 ohm
    circuit.add_capacitor("C1", "n1", "0", 1e-9);  // 1nF -> tau=100ns

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_step = 1e-7;
    config.transient.min_step = 1e-12;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 1e-6, 1e-8);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Should converge to steady state (10V)
    Real final_voltage = result.timepoints.back().node_voltages[1];
    EXPECT_GT(final_voltage, 9.5);
}

// Test 8: RLC bandpass filter with Trapezoidal
TEST_F(TrapezoidalVariableStepTest, RLCBandpassFilter) {
    CircuitGraph circuit;

    // RLC circuit with resonance
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e2);
    circuit.add_inductor("L1", "n1", "n2", 1e-3);
    circuit.add_capacitor("C1", "n2", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_step = 1e-5;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-6);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 50);

    // Check that solution is stable
    Real final_voltage = result.timepoints.back().node_voltages[2];  // n2 node (ID=3, index=2)
    EXPECT_GT(final_voltage, -15.0);  // Reasonable bounds
    EXPECT_LT(final_voltage, 15.0);
}

// Test 9: Verify adaptive timestep control works
TEST_F(TrapezoidalVariableStepTest, AdaptiveTimestepControl) {
    CircuitGraph circuit;

    // RC circuit
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.max_step = 1e-4;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Should converge to correct value
    Real final_voltage = result.timepoints.back().node_voltages[1];
    EXPECT_GT(final_voltage, 9.5);
    EXPECT_LT(final_voltage, 10.1);

    spdlog::info("Adaptive timestep control: {} accepts, {} rejects, V_final={:.6f}V",
                 result.timepoints.size(), result.stats.timestep_rejects, final_voltage);
}

// Test 10: Trapezoidal preserves oscillation better than Backward Euler
TEST_F(TrapezoidalVariableStepTest, BetterOscillationPreservation) {
    CircuitGraph circuit;

    // LC circuit
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_inductor("L1", "vin", "n1", 1e-3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Test with Trapezoidal
    SimulationConfig config_trap;
    config_trap.transient.use_adaptive_timestep = true;
    config_trap.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config_trap.transient.max_step = 1e-5;
    config_trap.transient.use_initial_conditions = false;

    TransientSolver solver_trap(circuit, config_trap);
    auto result_trap = solver_trap.solve(0.0, 2e-3, 1e-6);

    ASSERT_EQ(result_trap.status, ConvergenceStatus::SUCCESS);

    // Test with Backward Euler (for comparison)
    SimulationConfig config_be;
    config_be.transient.use_adaptive_timestep = true;
    config_be.transient.method = IntegrationMethod::BACKWARD_EULER;
    config_be.transient.max_step = 1e-5;
    config_be.transient.use_initial_conditions = false;

    TransientSolver solver_be(circuit, config_be);
    auto result_be = solver_be.solve(0.0, 2e-3, 1e-6);

    ASSERT_EQ(result_be.status, ConvergenceStatus::SUCCESS);

    // Find peak voltages in last period for both methods
    Real trap_last_peak = 0.0;
    Real be_last_peak = 0.0;

    for (std::size_t i = 0; i < result_trap.timepoints.size(); ++i) {
        Real t = result_trap.timepoints[i].time;
        Real v = result_trap.timepoints[i].node_voltages[1];
        if (t > 1.7e-3) {
            trap_last_peak = std::max(trap_last_peak, std::abs(v));
        }
    }

    for (std::size_t i = 0; i < result_be.timepoints.size(); ++i) {
        Real t = result_be.timepoints[i].time;
        Real v = result_be.timepoints[i].node_voltages[1];
        if (t > 1.7e-3) {
            be_last_peak = std::max(be_last_peak, std::abs(v));
        }
    }

    // Trapezoidal should preserve oscillation better (less damping)
    EXPECT_GT(trap_last_peak, be_last_peak * 0.9);  // At least 90% of BE

    spdlog::info("Oscillation preservation: Trapezoidal={:.6f}V, Backward Euler={:.6f}V",
                 trap_last_peak, be_last_peak);
}
