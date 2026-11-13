// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Test DC operating point for RL circuit
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <iostream>
#include <iomanip>
#include <spdlog/spdlog.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

int main() {
    spdlog::set_level(spdlog::level::debug);

    // RL Circuit: V=10V, R=100Ω, L=10mH
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 100.0);
    circuit.add_inductor("L1", "n2", "0", 10e-3);

    // Solve DC operating point
    DCSolver dc_solver(circuit);
    DenseVector solution;
    SolverStats stats;

    ConvergenceStatus status = dc_solver.solve(solution, stats);

    std::cout << std::fixed << std::setprecision(9);
    std::cout << "\n=== DC Operating Point ===" << std::endl;
    std::cout << "Status: " << (status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Iterations: " << stats.total_iterations << std::endl;
    std::cout << "\nSolution vector:" << std::endl;
    for (size_t i = 0; i < solution.size(); ++i) {
        std::cout << "  solution[" << i << "] = " << solution[i] << std::endl;
    }

    const auto& mna = dc_solver.get_mna_system();
    std::cout << "\nMNA system size: nodes=" << mna.num_nodes
              << ", branches=" << mna.num_branches
              << ", total=" << mna.size() << std::endl;

    std::cout << "\nNode voltages:" << std::endl;
    std::cout << "  V(0) = 0 (GND)" << std::endl;
    for (size_t i = 0; i < mna.num_nodes; ++i) {
        std::cout << "  V(" << (i+1) << ") = " << solution[i] << " V" << std::endl;
    }

    if (mna.num_branches > 0) {
        std::cout << "\nBranch currents:" << std::endl;
        for (size_t i = 0; i < mna.num_branches; ++i) {
            std::cout << "  I_branch[" << i << "] = " << solution[mna.num_nodes + i] << " A" << std::endl;
        }
    }

    // Expected values for RL circuit with L as short circuit:
    // V(n1) = 10V, V(n2) ≈ 0V (inductor is short), I = 10V/100Ω = 0.1A
    std::cout << "\n=== Expected Values ===" << std::endl;
    std::cout << "  V(n1) = 10.000000000 V" << std::endl;
    std::cout << "  V(n2) ≈ 0.000000000 V (inductor is short circuit in DC)" << std::endl;
    std::cout << "  I     ≈ 0.100000000 A" << std::endl;

    return 0;
}
