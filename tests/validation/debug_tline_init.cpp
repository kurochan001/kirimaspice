// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// debug_tline_init.cpp - Debug T-line initialization
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <iostream>
#include <iomanip>

using namespace kirimaspice;

int main() {
    std::string netlist = R"(T-line Debug
Vin 1 0 DC 1.0
Rs 1 2 50
T1 2 0 3 0 Z0=50 TD=5e-9
Rload 3 0 50
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    std::cout << "=== Circuit Created ===" << std::endl;
    std::cout << "Nodes: " << circuit->num_nodes() << std::endl;
    std::cout << "Devices: " << circuit->num_devices() << std::endl;

    // Run DC analysis first
    std::cout << "\n=== DC Analysis ===" << std::endl;
    solvers::DCSolver dc_solver(*circuit);
    DenseVector dc_solution;
    SolverStats dc_stats;
    auto dc_status = dc_solver.solve(dc_solution, dc_stats);

    std::cout << "DC Status: " << (dc_status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "DC Solution:" << std::endl;
    for (size_t i = 0; i < dc_solution.size(); ++i) {
        std::cout << "  Variable " << i << ": " << dc_solution[i] << std::endl;
    }

    // Run transient analysis
    std::cout << "\n=== Transient Analysis ===" << std::endl;
    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;
    config.transient.use_initial_conditions = false;  // Start from DC operating point

    solvers::TransientSolver solver(*circuit, config);
    auto result = solver.solve(0.0, 10e-9, 1e-10);

    std::cout << "Transient Status: " << (result.status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Timepoints: " << result.timepoints.size() << std::endl;

    // Print first few timepoints
    std::cout << "\nFirst 10 timepoints:" << std::endl;
    std::cout << std::setw(15) << "Time(ns)"
              << std::setw(15) << "V(1)"
              << std::setw(15) << "V(2)"
              << std::setw(15) << "V(3)" << std::endl;

    for (size_t i = 0; i < std::min(size_t(10), result.timepoints.size()); ++i) {
        const auto& tp = result.timepoints[i];
        std::cout << std::setw(15) << std::fixed << std::setprecision(3) << tp.time * 1e9
                  << std::setw(15) << std::setprecision(6) << tp.node_voltages[0]
                  << std::setw(15) << tp.node_voltages[1]
                  << std::setw(15) << tp.node_voltages[2] << std::endl;
    }

    // Print last few timepoints
    std::cout << "\nLast 5 timepoints:" << std::endl;
    size_t start = result.timepoints.size() > 5 ? result.timepoints.size() - 5 : 0;
    for (size_t i = start; i < result.timepoints.size(); ++i) {
        const auto& tp = result.timepoints[i];
        std::cout << std::setw(15) << std::fixed << std::setprecision(3) << tp.time * 1e9
                  << std::setw(15) << std::setprecision(6) << tp.node_voltages[0]
                  << std::setw(15) << tp.node_voltages[1]
                  << std::setw(15) << tp.node_voltages[2] << std::endl;
    }

    return 0;
}
