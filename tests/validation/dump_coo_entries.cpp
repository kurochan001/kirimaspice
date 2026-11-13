// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Dump ALL COO matrix entries in order (not aggregated)
// Goal: See exact stamping sequence

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <iostream>
#include <iomanip>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

int main() {
    std::cout << "=== Dumping ALL COO Entries (BJT without Rb) ===\n\n";

    // Build minimal BJT circuit (NO base resistor)
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0, 0.0);
    circuit.add_voltage_source("Vin", "in", "0", 0.7, 0.01, 0.0);
    circuit.add_resistor("Rc", "vcc", "out", 1000.0);
    circuit.add_bjt_npn("Q1", "out", "in", "0", 1e-14, 100.0, 1.0);

    NodeId in_node = circuit.get_node_id("in");
    NodeId out_node = circuit.get_node_id("out");

    // Run DC analysis
    SimulationConfig config;
    DCSolver dc_solver(circuit, config);
    DenseVector dc_solution;
    SolverStats stats;

    if (dc_solver.solve(dc_solution, stats) != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC analysis failed\n";
        return 1;
    }

    std::cout << "Node mapping:\n";
    std::cout << "  in (base) = " << in_node << " → idx " << (in_node - 1) << "\n";
    std::cout << "  out (coll) = " << out_node << " → idx " << (out_node - 1) << "\n\n";

    // Build AC system
    std::size_t num_nodes = dc_solution.size();
    std::size_t num_branches = 2;
    std::size_t system_size = num_nodes + num_branches;

    ComplexMNASystem sys;
    sys.num_nodes = num_nodes;
    sys.num_branches = num_branches;
    sys.Y.rows.reserve(system_size * 10);
    sys.Y.cols.reserve(system_size * 10);
    sys.Y.values.reserve(system_size * 10);
    sys.I.resize(system_size, Complex(0, 0));

    Real omega = 2.0 * M_PI * 1000.0;

    // Stamp devices ONE BY ONE and print entries after each
    auto& devices = circuit.get_devices();

    std::size_t entry_count = 0;

    for (std::size_t dev_idx = 0; dev_idx < devices.size(); dev_idx++) {
        const auto& device = devices[dev_idx];

        std::size_t entries_before = sys.Y.rows.size();

        device->stamp_ac(sys, omega, dc_solution);

        std::size_t entries_after = sys.Y.rows.size();
        std::size_t new_entries = entries_after - entries_before;

        std::cout << "Device " << dev_idx << ": " << device->name()
                  << " (type=" << static_cast<int>(device->type()) << ")\n";
        std::cout << "  Added " << new_entries << " entries:\n";

        for (std::size_t k = entries_before; k < entries_after; k++) {
            std::uint32_t i = sys.Y.rows[k];
            std::uint32_t j = sys.Y.cols[k];
            Complex val = sys.Y.values[k];

            std::cout << "    [" << entry_count++ << "] Y[" << i << "," << j << "] = "
                      << std::scientific << std::setprecision(6)
                      << std::real(val) << " + j" << std::imag(val);

            // Highlight collector-base entries
            if (i == (out_node - 1) && j == (in_node - 1)) {
                std::cout << "  🔴 COLLECTOR-BASE";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "=== Summary: All Y[" << (out_node - 1) << "," << (in_node - 1) << "] entries ===\n";
    for (std::size_t k = 0; k < sys.Y.rows.size(); k++) {
        if (sys.Y.rows[k] == (out_node - 1) && sys.Y.cols[k] == (in_node - 1)) {
            std::cout << "Entry [" << k << "]: Y[" << sys.Y.rows[k] << "," << sys.Y.cols[k] << "] = "
                      << std::scientific << std::setprecision(6)
                      << std::real(sys.Y.values[k]) << " + j" << std::imag(sys.Y.values[k]) << "\n";
        }
    }

    return 0;
}
