// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_adaptive_rl.cpp
// Test adaptive timestep on RL circuit and compare with ngspice

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <spdlog/spdlog.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

int main() {
    // Set logging level to trace for detailed debugging
    spdlog::set_level(spdlog::level::trace);

    // RL Circuit: V=10V, R=100Ω, L=10mH
    // τ = L/R = 10mH / 100Ω = 0.1ms
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 100.0);      // 100Ω
    circuit.add_inductor("L1", "n2", "0", 10e-3);       // 10mH

    Real t_stop = 0.5e-3;  // 0.5ms

    // Test 1: Fixed timestep (for comparison)
    {
        std::cout << "=== Fixed Timestep (10μs) ===" << std::endl;
        SimulationConfig config;
        config.transient.use_initial_conditions = true;  // UIC mode for step response
        config.transient.method = IntegrationMethod::TRAPEZOIDAL;
        TransientSolver solver_fixed(circuit, config);
        TransientResult result = solver_fixed.solve(0.0, t_stop, 10e-6);

        std::cout << "Status: " << (result.status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "Timepoints: " << result.timepoints.size() << std::endl;
        std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;

        // Save to file
        std::ofstream out("tests/validation/results/rl_adaptive_fixed.txt");
        out << std::fixed << std::setprecision(9);
        out << "# RL Circuit - Fixed Timestep (10μs)" << std::endl;
        out << "# time\tv(n1)\tv(n2)" << std::endl;
        for (const auto& tp : result.timepoints) {
            out << tp.time << "\t"
                << tp.node_voltages[0] << "\t"  // v(n1)
                << tp.node_voltages[1] << std::endl;  // v(n2)
        }
        out.close();
        std::cout << "Results saved to: tests/validation/results/rl_adaptive_fixed.txt" << std::endl;
    }

    // Test 2: Adaptive timestep (BDF2/BDF3 + adaptive order, matching RC circuit test)
    {
        std::cout << "\n=== Adaptive Timestep (BDF2/BDF3) ===" << std::endl;
        SimulationConfig config;
        config.transient.use_initial_conditions = true;  // UIC mode for step response
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::GEAR2;  // Start with BDF2
        config.transient.adaptive_order = true;  // Enable adaptive order control
        config.transient.max_order = 3;  // Allow up to BDF3
        config.transient.initial_step = 1e-6;     // Start with 1μs
        config.transient.max_step = 50e-6;        // Max 50μs
        config.transient.min_step = 1e-9;         // Min 1ns
        config.transient.lte_abstol = 1e-9;       // 1nV (tighter)
        config.transient.lte_reltol = 1e-4;       // 0.01% (tighter)

        TransientSolver solver_adaptive(circuit, config);
        TransientResult result = solver_adaptive.solve(0.0, t_stop, 0.0);

        std::cout << "Status: " << (result.status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "Timepoints: " << result.timepoints.size() << std::endl;
        std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;
        std::cout << "Timestep rejects: " << result.stats.timestep_rejects << std::endl;

        // Save to file
        std::ofstream out("tests/validation/results/rl_adaptive_adaptive.txt");
        out << std::fixed << std::setprecision(9);
        out << "# RL Circuit - Adaptive Timestep" << std::endl;
        out << "# time\tv(n1)\tv(n2)" << std::endl;
        for (const auto& tp : result.timepoints) {
            out << tp.time << "\t"
                << tp.node_voltages[0] << "\t"  // v(n1)
                << tp.node_voltages[1] << std::endl;  // v(n2)
        }
        out.close();
        std::cout << "Results saved to: tests/validation/results/rl_adaptive_adaptive.txt" << std::endl;

        // Print first and last few timepoints
        std::cout << "\nFirst 5 timepoints:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(5), result.timepoints.size()); ++i) {
            const auto& tp = result.timepoints[i];
            std::cout << "  t=" << tp.time*1e6 << "μs, v(n2)=" << tp.node_voltages[1] << "V" << std::endl;
        }

        std::cout << "\nLast 5 timepoints:" << std::endl;
        size_t start = result.timepoints.size() > 5 ? result.timepoints.size() - 5 : 0;
        for (size_t i = start; i < result.timepoints.size(); ++i) {
            const auto& tp = result.timepoints[i];
            std::cout << "  t=" << tp.time*1e6 << "μs, v(n2)=" << tp.node_voltages[1] << "V" << std::endl;
        }
    }

    return 0;
}
