// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Detailed debugging for RL circuit step response
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <iostream>
#include <iomanip>
#include <spdlog/spdlog.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

int main() {
    spdlog::set_level(spdlog::level::debug);

    std::cout << std::fixed << std::setprecision(6);

    // RL Circuit: V=10V, R=100Ω, L=10mH
    // τ = L/R = 10mH/100Ω = 0.1ms = 100μs
    // At t=1μs: v(n2) should be ≈ 10V (step response)

    std::cout << "\n=== RL Circuit Step Response Debug ===\n" << std::endl;
    std::cout << "Circuit parameters:" << std::endl;
    std::cout << "  V1 = 10V" << std::endl;
    std::cout << "  R1 = 100Ω (G = 0.01 S)" << std::endl;
    std::cout << "  L1 = 10mH (0.01 H)" << std::endl;
    std::cout << "  τ = L/R = 100μs" << std::endl;

    std::cout << "\nTheoretical values at t=1μs:" << std::endl;
    std::cout << "  V_L(1μs) = 10 × e^(-1/100) ≈ 9.900V" << std::endl;
    std::cout << "  I(1μs) = (10/100) × (1 - e^(-1/100)) ≈ 0.001A" << std::endl;

    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 100.0);
    circuit.add_inductor("L1", "n2", "0", 10e-3);  // 10mH

    std::cout << "\nCircuit topology:" << std::endl;
    std::cout << "  V1: n1 - 0 (10V)" << std::endl;
    std::cout << "  R1: n1 - n2 (100Ω)" << std::endl;
    std::cout << "  L1: n2 - 0 (10mH)" << std::endl;
    std::cout << "  Nodes: 0 (GND), 1 (n1), 2 (n2)" << std::endl;

    // Configure UIC mode + fixed timestep
    SimulationConfig config;
    config.transient.use_initial_conditions = true;  // UIC for step response
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;

    std::cout << "\n=== Test 1: Single step (0 → 1μs) ===" << std::endl;

    TransientSolver solver(circuit, config);
    Real t_start = 0.0;
    Real t_stop = 1e-6;  // 1μs
    Real timestep = 1e-6;

    TransientResult result = solver.solve(t_start, t_stop, timestep);

    std::cout << "\nResult:" << std::endl;
    std::cout << "  Status: " << (result.status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "  Timepoints: " << result.timepoints.size() << std::endl;

    if (result.timepoints.size() >= 2) {
        const auto& tp0 = result.timepoints[0];
        const auto& tp1 = result.timepoints[1];

        std::cout << "\nTimepoint 0 (t=0):" << std::endl;
        std::cout << "  time = " << tp0.time * 1e6 << " μs" << std::endl;
        std::cout << "  V(n1) = " << tp0.node_voltages[0] << " V" << std::endl;
        std::cout << "  V(n2) = " << tp0.node_voltages[1] << " V" << std::endl;
        if (tp0.branch_currents.size() > 0) {
            std::cout << "  I(V1) = " << tp0.branch_currents[0] << " A" << std::endl;
        }

        std::cout << "\nTimepoint 1 (t=1μs):" << std::endl;
        std::cout << "  time = " << tp1.time * 1e6 << " μs" << std::endl;
        std::cout << "  V(n1) = " << tp1.node_voltages[0] << " V" << std::endl;
        std::cout << "  V(n2) = " << tp1.node_voltages[1] << " V" << std::endl;
        if (tp1.branch_currents.size() > 0) {
            std::cout << "  I(V1) = " << tp1.branch_currents[0] << " A" << std::endl;
        }

        Real v_n2 = tp1.node_voltages[1];
        Real expected_v_n2 = 9.900;
        Real error = std::abs(v_n2 - expected_v_n2);
        Real rel_error = error / expected_v_n2 * 100.0;

        std::cout << "\nComparison:" << std::endl;
        std::cout << "  Expected V(n2) = " << expected_v_n2 << " V" << std::endl;
        std::cout << "  Actual V(n2)   = " << v_n2 << " V" << std::endl;
        std::cout << "  Error          = " << error << " V (" << rel_error << "%)" << std::endl;

        if (rel_error > 1.0) {
            std::cout << "\n⚠ WARNING: Large error detected!" << std::endl;
            std::cout << "Actual value is " << (v_n2 / expected_v_n2) << "x of expected" << std::endl;
        }
    }

    // Test with multiple steps to see the transient behavior
    std::cout << "\n=== Test 2: Multiple steps (0 → 10μs, dt=1μs) ===" << std::endl;

    TransientSolver solver2(circuit, config);
    TransientResult result2 = solver2.solve(0.0, 10e-6, 1e-6);

    std::cout << "\nTimepoint summary:" << std::endl;
    std::cout << "Time(μs)\tV(n2)(V)\tTheory(V)\tError(%)" << std::endl;

    for (size_t i = 0; i < result2.timepoints.size() && i < 11; ++i) {
        const auto& tp = result2.timepoints[i];
        Real t_us = tp.time * 1e6;
        Real v_n2 = tp.node_voltages[1];

        // Theory: V_L(t) = 10 × e^(-t/τ), τ = 100μs
        Real theory = 10.0 * std::exp(-t_us / 100.0);
        Real error_pct = std::abs(v_n2 - theory) / theory * 100.0;

        std::cout << t_us << "\t\t" << v_n2 << "\t\t"
                  << theory << "\t\t" << error_pct << std::endl;
    }

    return 0;
}
