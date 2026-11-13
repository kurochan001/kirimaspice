// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/debug/debug_cmos_inverter.cpp
// Debug CMOS inverter singular matrix issue
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

int main() {
    // Set log level to trace for maximum detail
    spdlog::set_level(spdlog::level::trace);

    std::cout << "=== CMOS Inverter Debug (Vin=0V) ===" << std::endl;

    CircuitGraph circuit;
    circuit.add_voltage_source("VDD", "vdd", "0", 5.0);
    circuit.add_voltage_source("Vin", "in", "0", 0.0);  // Low input
    circuit.add_mosfet_p("Mp", "out", "in", "vdd");
    circuit.add_mosfet_n("Mn", "out", "in", "0");
    circuit.add_resistor("Rload", "out", "0", 1e3);  // 1kΩ load

    std::cout << "\nCircuit structure:" << std::endl;
    std::cout << "  Nodes: " << circuit.num_nodes() << std::endl;
    std::cout << "  Devices: " << circuit.num_devices() << std::endl;

    std::cout << "\nNode IDs:" << std::endl;
    std::cout << "  vdd: " << circuit.get_node_id("vdd") << std::endl;
    std::cout << "  in: " << circuit.get_node_id("in") << std::endl;
    std::cout << "  out: " << circuit.get_node_id("out") << std::endl;

    std::cout << "\nAttempting DC solve..." << std::endl;
    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    std::cout << "\nResult: ";
    if (status == ConvergenceStatus::SUCCESS) {
        std::cout << "SUCCESS" << std::endl;
        std::cout << "Solution:" << std::endl;
        for (size_t i = 0; i < solution.size(); ++i) {
            std::cout << "  Node " << (i+1) << ": " << solution[i] << " V" << std::endl;
        }
    } else if (status == ConvergenceStatus::MAX_ITERATIONS) {
        std::cout << "FAILED - Max iterations" << std::endl;
    } else if (status == ConvergenceStatus::NUMERICAL_ERROR) {
        std::cout << "FAILED - Numerical error (singular matrix)" << std::endl;
    } else {
        std::cout << "FAILED - Unknown error" << std::endl;
    }

    return (status == ConvergenceStatus::SUCCESS) ? 0 : 1;
}
