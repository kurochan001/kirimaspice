// tests/validation/test_adaptive_rl_debug.cpp
// Detailed debugging of RL adaptive timestep issue

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <iostream>
#include <iomanip>
#include <spdlog/spdlog.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

int main() {
    // Set logging level to trace for detailed output
    spdlog::set_level(spdlog::level::trace);

    // RL Circuit: V=10V, R=100Ω, L=10mH
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 100.0);
    circuit.add_inductor("L1", "n2", "0", 10e-3);  // 10mH

    std::cout << "=== RL Circuit Adaptive Timestep Debug ===" << std::endl;
    std::cout << "Circuit: V=10V, R=100Ω, L=10mH, τ=100μs" << std::endl;
    std::cout << std::endl;

    // Configure adaptive timestep with very small initial step
    SimulationConfig config;
    config.transient.use_initial_conditions = true;  // UIC mode
    config.transient.use_adaptive_timestep = true;
    config.transient.initial_step = 1e-6;     // 1μs
    config.transient.max_step = 10e-6;        // Max 10μs (very conservative)
    config.transient.min_step = 1e-9;         // Min 1ns
    config.transient.lte_abstol = 1e-6;       // 1μV (strict)
    config.transient.lte_reltol = 0.001;      // 0.1% (strict)
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;

    std::cout << "Adaptive timestep configuration:" << std::endl;
    std::cout << "  Initial step: " << config.transient.initial_step * 1e6 << " μs" << std::endl;
    std::cout << "  Max step: " << config.transient.max_step * 1e6 << " μs" << std::endl;
    std::cout << "  LTE abstol: " << config.transient.lte_abstol << " V" << std::endl;
    std::cout << "  LTE reltol: " << config.transient.lte_reltol << std::endl;
    std::cout << std::endl;

    // Run short simulation (0 to 10μs) to see first few timesteps
    TransientSolver solver(circuit, config);
    TransientResult result = solver.solve(0.0, 10e-6, 0.0);

    std::cout << std::endl;
    std::cout << "=== Simulation Results ===" << std::endl;
    std::cout << "Status: " << (result.status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Timepoints: " << result.timepoints.size() << std::endl;
    std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;
    std::cout << "Timestep rejects: " << result.stats.timestep_rejects << std::endl;
    std::cout << std::endl;

    // Print all timepoints
    std::cout << "All timepoints:" << std::endl;
    std::cout << std::fixed << std::setprecision(9);
    std::cout << std::setw(5) << "Step"
              << std::setw(15) << "Time(μs)"
              << std::setw(15) << "v(n1)(V)"
              << std::setw(15) << "v(n2)(V)"
              << std::setw(15) << "i(V1)(A)"
              << std::endl;
    std::cout << std::string(65, '-') << std::endl;

    for (size_t i = 0; i < result.timepoints.size(); ++i) {
        const auto& tp = result.timepoints[i];
        Real time_us = tp.time * 1e6;
        Real v_n1 = tp.node_voltages[0];  // Node n1
        Real v_n2 = tp.node_voltages[1];  // Node n2 (voltage across inductor)
        Real i_v1 = tp.branch_currents[0];  // Current through V1

        std::cout << std::setw(5) << i
                  << std::setw(15) << time_us
                  << std::setw(15) << v_n1
                  << std::setw(15) << v_n2
                  << std::setw(15) << i_v1
                  << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Theoretical Values ===" << std::endl;
    std::cout << "For RL step response: V_L(t) = V0 * exp(-R*t/L)" << std::endl;
    std::cout << "Expected V_L at key times:" << std::endl;
    std::cout << "  t=0μs:   V_L = 10.000V (inductor blocks current)" << std::endl;
    std::cout << "  t=1μs:   V_L = 9.900V  (99% of V0)" << std::endl;
    std::cout << "  t=10μs:  V_L = 9.048V  (90% of V0)" << std::endl;
    std::cout << "  t=100μs: V_L = 3.679V  (37% of V0)" << std::endl;
    std::cout << std::endl;

    // Check first non-zero timepoint
    if (result.timepoints.size() > 1) {
        const auto& tp1 = result.timepoints[1];
        Real v_n2_1 = tp1.node_voltages[1];
        std::cout << "First non-zero timepoint:" << std::endl;
        std::cout << "  Time: " << tp1.time * 1e6 << " μs" << std::endl;
        std::cout << "  v(n2): " << v_n2_1 << " V" << std::endl;

        // Check if this is close to steady-state (wrong!)
        if (v_n2_1 < 1.0) {
            std::cout << "  ⚠️  WARNING: v(n2) < 1V at first timestep!" << std::endl;
            std::cout << "  This is close to steady-state, not transient response." << std::endl;
            std::cout << "  Expected: v(n2) ≈ 9.9V (very close to V0)" << std::endl;
        }
    }

    return 0;
}
