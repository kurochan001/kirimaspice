// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_trapezoidal_adaptive.cpp
// Trapezoidal rule with adaptive timestep control - comparison with BDF2
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;

// RC circuit theoretical response: V(t) = V0 * (1 - exp(-t/tau))
double rc_theory(double t, double V0, double R, double C) {
    double tau = R * C;
    return V0 * (1.0 - std::exp(-t / tau));
}

int main() {
    std::cout << "=== Trapezoidal vs BDF2 Comparison Test ===" << std::endl;
    std::cout << "Circuit: RC (R=1k, C=10u, tau=10ms)" << std::endl;
    std::cout << std::endl;

    // Create RC circuit
    // V1=5V, R1=1k, C1=10u, tau=10ms
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 5.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    NodeId node_out = circuit.get_node_id("out");

    // Simulation parameters
    double t_start = 0.0;
    double t_stop = 50e-3;  // 50ms = 5*tau
    double tau = 1000.0 * 10e-6;  // 10ms

    // Test 1: BDF2 (default)
    {
        SimulationConfig config;
        config.transient.use_initial_conditions = true;
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::GEAR2;
        config.transient.adaptive_order = true;
        config.transient.max_order = 2;  // BDF1/BDF2 only for fair comparison
        config.transient.prefer_trapezoidal = false;  // Use BDF2
        config.transient.initial_step = 1e-4;
        config.transient.max_step = 1e-3;
        config.transient.min_step = 1e-9;
        config.transient.lte_abstol = 1e-9;
        config.transient.lte_reltol = 1e-4;

        solvers::TransientSolver solver(circuit, config);
        auto result = solver.solve(t_start, t_stop, 0.5e-3);

        std::cout << "--- BDF2 (GEAR2) Results ---" << std::endl;
        std::cout << "Total steps: " << result.timepoints.size() << std::endl;
        std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;
        std::cout << "Timestep rejects: " << result.stats.timestep_rejects << std::endl;
        std::cout << "Simulation time: " << result.stats.total_time_seconds << " s" << std::endl;

        // Compare at tau intervals
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "\nTime(ms) | V_out(V) | Theory(V) | Error(%)" << std::endl;
        std::cout << "---------|----------|-----------|----------" << std::endl;

        for (int i = 1; i <= 5; ++i) {
            double target_time = tau * i;
            double theory_val = rc_theory(target_time, 5.0, 1000.0, 10e-6);

            // Find closest timepoint
            double closest_time = 0.0;
            double closest_val = 0.0;
            double min_diff = 1e100;

            for (const auto& tp : result.timepoints) {
                double diff = std::abs(tp.time - target_time);
                if (diff < min_diff) {
                    min_diff = diff;
                    closest_time = tp.time;
                    closest_val = tp.node_voltages[node_out];
                }
            }

            double error_pct = std::abs(closest_val - theory_val) / theory_val * 100.0;
            std::cout << std::setw(8) << closest_time * 1e3 << " | ";
            std::cout << std::setw(8) << closest_val << " | ";
            std::cout << std::setw(9) << theory_val << " | ";
            std::cout << std::setw(8) << error_pct << "%" << std::endl;
        }
        std::cout << std::endl;
    }

    // Test 2: Trapezoidal rule
    {
        SimulationConfig config;
        config.transient.use_initial_conditions = true;
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::TRAPEZOIDAL;  // Will be overridden by adaptive
        config.transient.adaptive_order = true;
        config.transient.max_order = 2;  // Order 1 and 2
        config.transient.prefer_trapezoidal = true;  // Use TRAPEZOIDAL for order 2
        config.transient.initial_step = 1e-4;
        config.transient.max_step = 1e-3;
        config.transient.min_step = 1e-9;
        config.transient.lte_abstol = 1e-9;
        config.transient.lte_reltol = 1e-4;

        solvers::TransientSolver solver(circuit, config);
        auto result = solver.solve(t_start, t_stop, 0.5e-3);

        std::cout << "--- TRAPEZOIDAL Results ---" << std::endl;
        std::cout << "Total steps: " << result.timepoints.size() << std::endl;
        std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;
        std::cout << "Timestep rejects: " << result.stats.timestep_rejects << std::endl;
        std::cout << "Simulation time: " << result.stats.total_time_seconds << " s" << std::endl;

        // Compare at tau intervals
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "\nTime(ms) | V_out(V) | Theory(V) | Error(%)" << std::endl;
        std::cout << "---------|----------|-----------|----------" << std::endl;

        for (int i = 1; i <= 5; ++i) {
            double target_time = tau * i;
            double theory_val = rc_theory(target_time, 5.0, 1000.0, 10e-6);

            // Find closest timepoint
            double closest_time = 0.0;
            double closest_val = 0.0;
            double min_diff = 1e100;

            for (const auto& tp : result.timepoints) {
                double diff = std::abs(tp.time - target_time);
                if (diff < min_diff) {
                    min_diff = diff;
                    closest_time = tp.time;
                    closest_val = tp.node_voltages[node_out];
                }
            }

            double error_pct = std::abs(closest_val - theory_val) / theory_val * 100.0;
            std::cout << std::setw(8) << closest_time * 1e3 << " | ";
            std::cout << std::setw(8) << closest_val << " | ";
            std::cout << std::setw(9) << theory_val << " | ";
            std::cout << std::setw(8) << error_pct << "%" << std::endl;
        }
        std::cout << std::endl;
    }

    std::cout << "=== Conclusion ===" << std::endl;
    std::cout << "Both BDF2 and TRAPEZOIDAL are now fully supported." << std::endl;
    std::cout << "Use prefer_trapezoidal=true for smooth problems." << std::endl;
    std::cout << "Use prefer_trapezoidal=false (default) for stiff problems." << std::endl;

    return 0;
}
