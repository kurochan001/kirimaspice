// Test Trapezoidal fixed timestep with UIC (Use Initial Conditions)
#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <spdlog/spdlog.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

TEST(TrapFixedUIC, CompareFixedAndAdaptive) {
    spdlog::set_level(spdlog::level::info);

    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "n1", 1e3);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Test with adaptive timestep and UIC
    SimulationConfig config_adaptive;
    config_adaptive.transient.use_adaptive_timestep = true;
    config_adaptive.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config_adaptive.transient.max_step = 1e-4;
    config_adaptive.transient.trtol = 2.0;
    config_adaptive.transient.use_initial_conditions = true;  // UIC mode

    TransientSolver solver_adaptive(circuit, config_adaptive);
    auto result_adaptive = solver_adaptive.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result_adaptive.status, ConvergenceStatus::SUCCESS);

    // Test with fixed timestep and UIC
    SimulationConfig config_fixed;
    config_fixed.transient.use_adaptive_timestep = false;  // Fixed
    config_fixed.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config_fixed.transient.use_initial_conditions = true;  // UIC mode

    TransientSolver solver_fixed(circuit, config_fixed);
    auto result_fixed = solver_fixed.solve(0.0, 5e-3, 1e-5);

    ASSERT_EQ(result_fixed.status, ConvergenceStatus::SUCCESS);

    // Both should produce similar final results
    Real voltage_adaptive = result_adaptive.timepoints.back().node_voltages[1];
    Real voltage_fixed = result_fixed.timepoints.back().node_voltages[1];

    spdlog::info("Adaptive: {} steps, V_final={:.6f}V",
                 result_adaptive.timepoints.size(), voltage_adaptive);
    spdlog::info("Fixed: {} steps, V_final={:.6f}V",
                 result_fixed.timepoints.size(), voltage_fixed);

    EXPECT_NEAR(voltage_adaptive, voltage_fixed, 0.1);  // Within 0.1V

    // Both should be near analytical solution: V(5τ) ≈ 9.93V
    EXPECT_GT(voltage_adaptive, 9.5);
    EXPECT_LT(voltage_adaptive, 10.1);
    EXPECT_GT(voltage_fixed, 9.5);
    EXPECT_LT(voltage_fixed, 10.1);
}
