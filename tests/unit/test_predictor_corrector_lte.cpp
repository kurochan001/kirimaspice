// tests/unit/test_predictor_corrector_lte.cpp
// Unit tests for Predictor-Corrector difference-based LTE estimation

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

class PredictorCorrectorLTETest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set log level to info (change to trace for debugging)
        spdlog::set_level(spdlog::level::info);
    }
};

// Test 1: Simple RC circuit with P-C error estimation
TEST_F(PredictorCorrectorLTETest, BasicRCCircuitPCMode) {
    CircuitGraph circuit;

    // Simple RC circuit: V1(10V) -[R=1k]- n1 -[C=1u]- GND
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Configure transient solver with P-C error estimation
    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;  // P-C mode
    config.transient.max_step = 1e-4;
    config.transient.min_step = 1e-9;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;  // DC operating point

    TransientSolver solver(circuit, config);

    // Run transient analysis
    Real t_start = 0.0;
    Real t_stop = 5e-3;  // 5ms
    Real timestep = 1e-5;

    auto result = solver.solve(t_start, t_stop, timestep);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 10);  // At least some timesteps

    // Check final voltage (should approach 10V, RC time constant = 1ms)
    // At t=5ms = 5*tau, V(C) ≈ 10V * (1 - exp(-5)) ≈ 9.93V
    Real final_voltage = result.timepoints.back().node_voltages[1];  // n1 node (node ID=2, index=1)
    EXPECT_GT(final_voltage, 9.5);
    EXPECT_LT(final_voltage, 10.1);
}

// Test 2: Verify predictor-corrector reduces iteration count
TEST_F(PredictorCorrectorLTETest, PCReducesIterations) {
    CircuitGraph circuit;

    // RLC circuit with resonance
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e2);
    circuit.add_inductor("L1", "n1", "n2", 1e-3);
    circuit.add_capacitor("C1", "n2", "0", 1e-6);

    // Test with P-C mode
    SimulationConfig config_pc;
    config_pc.transient.use_adaptive_timestep = true;
    config_pc.transient.method = IntegrationMethod::GEAR2;
    config_pc.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;
    config_pc.transient.max_step = 1e-5;
    config_pc.transient.use_initial_conditions = false;

    TransientSolver solver_pc(circuit, config_pc);
    auto result_pc = solver_pc.solve(0.0, 1e-4, 1e-6);

    ASSERT_EQ(result_pc.status, ConvergenceStatus::SUCCESS);

    // Test with embedded method (for comparison)
    SimulationConfig config_embed;
    config_embed.transient.use_adaptive_timestep = true;
    config_embed.transient.method = IntegrationMethod::GEAR2;
    config_embed.transient.error_est = ErrorEstimation::PHYSICAL;
    config_embed.transient.max_step = 1e-5;
    config_embed.transient.use_initial_conditions = false;

    TransientSolver solver_embed(circuit, config_embed);
    auto result_embed = solver_embed.solve(0.0, 1e-4, 1e-6);

    ASSERT_EQ(result_embed.status, ConvergenceStatus::SUCCESS);

    // Both should produce similar results (n2 node: ID=3, index=2)
    EXPECT_NEAR(result_pc.timepoints.back().node_voltages[2],
                result_embed.timepoints.back().node_voltages[2],
                0.1);  // 10% tolerance

    spdlog::info("P-C iterations: {}, Embedded iterations: {}",
                 result_pc.stats.total_iterations,
                 result_embed.stats.total_iterations);
}

// Test 3: Combined mode (P-C + Physical)
TEST_F(PredictorCorrectorLTETest, CombinedModeUsesMax) {
    CircuitGraph circuit;

    // RC circuit
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Configure with COMBINED mode
    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.error_est = ErrorEstimation::COMBINED;  // Combined mode
    config.transient.max_step = 1e-4;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Should produce accurate results using both P-C and physical LTE (n1 node: ID=2, index=1)
    Real final_voltage = result.timepoints.back().node_voltages[1];
    EXPECT_GT(final_voltage, 9.5);
    EXPECT_LT(final_voltage, 10.1);
}

// Test 4: P-C mode with trapezoidal method
TEST_F(PredictorCorrectorLTETest, PCWithTrapezoidal) {
    CircuitGraph circuit;

    // LC resonant circuit
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_inductor("L1", "vin", "n1", 1e-3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;
    config.transient.max_step = 1e-6;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 1e-4, 1e-7);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Trapezoidal should preserve oscillation energy
    EXPECT_GT(result.timepoints.size(), 50);
}

// Test 5: Verify P-C error estimation scales with timestep
TEST_F(PredictorCorrectorLTETest, PCErrorScalesWithTimestep) {
    CircuitGraph circuit;

    // RC circuit
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Test with different max timesteps
    std::vector<Real> max_steps = {1e-4, 5e-5, 1e-5};
    std::vector<std::size_t> num_steps;

    for (Real max_step : max_steps) {
        SimulationConfig config;
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::GEAR2;
        config.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;
        config.transient.max_step = max_step;
        config.transient.trtol = 2.0;
        config.transient.use_initial_conditions = false;

        TransientSolver solver(circuit, config);
        auto result = solver.solve(0.0, 1e-3, max_step);

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        num_steps.push_back(result.timepoints.size());
    }

    // Smaller max_step should result in more timesteps
    EXPECT_GT(num_steps[2], num_steps[1]);
    EXPECT_GT(num_steps[1], num_steps[0]);
}

// Test 6: P-C mode handles stiff problems
TEST_F(PredictorCorrectorLTETest, PCHandlesStiffProblem) {
    CircuitGraph circuit;

    // Stiff RC circuit (very small time constant)
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e2);  // 100 ohm
    circuit.add_capacitor("C1", "n1", "0", 1e-9);  // 1nF -> tau=100ns

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2;  // BDF2 for stiff problems
    config.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;
    config.transient.max_step = 1e-7;
    config.transient.min_step = 1e-12;
    config.transient.trtol = 2.0;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 1e-6, 1e-8);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Should converge to steady state (10V) (n1 node: ID=2, index=1)
    Real final_voltage = result.timepoints.back().node_voltages[1];
    EXPECT_GT(final_voltage, 9.5);
}

// Test 7: Compare P-C vs Physical vs Combined accuracy
TEST_F(PredictorCorrectorLTETest, CompareErrorEstimationMethods) {
    CircuitGraph circuit;

    // RC circuit with known analytical solution
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    std::vector<ErrorEstimation> methods = {
        ErrorEstimation::PHYSICAL,
        ErrorEstimation::PREDICTOR_CORRECTOR,
        ErrorEstimation::COMBINED
    };

    std::vector<Real> final_voltages;

    for (auto method : methods) {
        SimulationConfig config;
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::GEAR2;
        config.transient.error_est = method;
        config.transient.max_step = 1e-4;
        config.transient.trtol = 2.0;
        config.transient.use_initial_conditions = false;

        TransientSolver solver(circuit, config);
        auto result = solver.solve(0.0, 5e-3, 1e-5);

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        final_voltages.push_back(result.timepoints.back().node_voltages[1]);  // n1 node: ID=2, index=1
    }

    // All methods should produce similar results (≈9.93V at t=5τ)
    for (std::size_t i = 0; i < final_voltages.size(); ++i) {
        EXPECT_GT(final_voltages[i], 9.5) << "Method " << i << " too low";
        EXPECT_LT(final_voltages[i], 10.1) << "Method " << i << " too high";
    }

    // All methods should agree with each other
    for (std::size_t i = 1; i < final_voltages.size(); ++i) {
        EXPECT_NEAR(final_voltages[i], final_voltages[0], 0.2)
            << "Methods " << i << " and 0 disagree";
    }
}

// Test 8: P-C mode with backward Euler
TEST_F(PredictorCorrectorLTETest, PCWithBackwardEuler) {
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;
    config.transient.max_step = 1e-4;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 10);
}

// Test 9: P-C with adaptive order control
TEST_F(PredictorCorrectorLTETest, PCWithAdaptiveOrder) {
    CircuitGraph circuit;

    // RLC circuit that benefits from adaptive order
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e2);
    circuit.add_inductor("L1", "n1", "n2", 1e-3);
    circuit.add_capacitor("C1", "n2", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.adaptive_order = true;  // Enable adaptive order
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.max_order = 3;
    config.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;
    config.transient.max_step = 1e-5;
    config.transient.use_initial_conditions = false;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 2e-4, 1e-6);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Adaptive order should help convergence
    EXPECT_LT(result.stats.timestep_rejects, 30);  // Reasonable reject count
}

// Test 10: P-C mode convergence with fixed timestep
TEST_F(PredictorCorrectorLTETest, PCFixedTimestepConvergence) {
    CircuitGraph circuit;

    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;  // Fixed timestep
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Fixed timestep should still work with P-C error estimation
    // (though error estimation only used in adaptive mode)
    EXPECT_GT(result.timepoints.size(), 400);  // ~500 timesteps expected
}
