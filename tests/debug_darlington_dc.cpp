// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Debug program to identify singular matrix column in Darlington amplifier
// This helps diagnose why DC operating point fails to converge

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/types.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <iomanip>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// Build the exact Darlington amplifier that fails
CircuitGraph build_darlington_amplifier() {
    CircuitGraph circuit;

    // Power supply
    circuit.add_voltage_source("Vcc", "vcc", "0", 12.0, 0.0);  // 12V DC

    // Input signal (AC only)
    circuit.add_voltage_source("Vin", "in", "0", 0.0, 0.01);  // 0V DC, 10mV AC

    // Input coupling
    circuit.add_capacitor("Cin", "in", "base1", 10e-6);

    // Bias network
    circuit.add_resistor("R1", "vcc", "base1", 100e3);
    circuit.add_resistor("R2", "base1", "0", 22e3);

    // Darlington pair: Q1 drives Q2
    circuit.add_bjt_npn("Q1", "vcc", "base1", "e1");       // Q1: collector to Vcc
    circuit.add_bjt_npn("Q2", "c2", "e1", "e2");           // Q2: base from Q1 emitter

    // Collector resistor (for Q2)
    circuit.add_resistor("Rc", "vcc", "c2", 4.7e3);

    // Emitter resistor with bypass
    circuit.add_resistor("Re", "e2", "0", 1e3);
    circuit.add_capacitor("Ce", "e2", "0", 100e-6);

    // Output
    circuit.add_capacitor("Cout", "c2", "out", 10e-6);
    circuit.add_resistor("Rload", "out", "0", 10e3);

    return circuit;
}

int main() {
    // Enable debug logging
    spdlog::set_level(spdlog::level::debug);

    std::cout << "\n========================================\n";
    std::cout << "Darlington Amplifier DC Debug\n";
    std::cout << "========================================\n\n";

    // Build circuit
    auto circuit = build_darlington_amplifier();

    // Print node mapping
    std::cout << "Node Mapping:\n";
    std::cout << std::setw(15) << "Node Name" << std::setw(10) << "NodeId" << std::setw(15) << "MNA Index\n";
    std::cout << std::string(40, '-') << "\n";

    std::vector<std::string> node_names = {
        "0", "vcc", "in", "base1", "e1", "c2", "e2", "out"
    };

    for (const auto& name : node_names) {
        try {
            NodeId id = circuit.get_node_id(name);
            int mna_idx = (id == 0) ? -1 : (id - 1);  // GND is not in MNA system
            std::cout << std::setw(15) << name
                     << std::setw(10) << id
                     << std::setw(15) << mna_idx << "\n";
        } catch (...) {
            // Node might not exist
        }
    }
    std::cout << "\n";

    // Circuit info
    std::cout << "Circuit Info:\n";
    std::cout << "  Total nodes: " << circuit.num_nodes() << "\n";
    std::cout << "  MNA nodes (excl GND): " << (circuit.num_nodes() - 1) << "\n";
    std::cout << "  Devices: " << circuit.get_devices().size() << "\n";
    std::cout << "\n";

    // Count branches (voltage sources)
    uint32_t num_branches = 0;
    for (const auto& device : circuit.get_devices()) {
        if (device->type() == DeviceType::VSOURCE) {
            num_branches++;
        }
    }
    std::cout << "  Voltage sources (branches): " << num_branches << "\n";
    std::cout << "  MNA system size: " << (circuit.num_nodes() - 1 + num_branches) << "\n";
    std::cout << "\n";

    // Darlington topology analysis
    std::cout << "Darlington Pair Topology:\n";
    std::cout << "  Q1: collector=vcc, base=base1, emitter=e1\n";
    std::cout << "  Q2: collector=c2, base=e1 (Q1 emitter), emitter=e2\n";
    std::cout << "  Coupling: Q1.emitter (e1) = Q2.base (e1)\n";
    std::cout << "  Note: Q1 collector directly to Vcc (no resistor)\n";
    std::cout << "\n";

    // Try to solve DC
    std::cout << "Attempting DC solve...\n";
    std::cout << std::string(40, '=') << "\n";

    DCSolver solver(circuit);

    DenseVector solution;
    SolverStats stats;

    ConvergenceStatus status = solver.solve(solution, stats);

    std::cout << std::string(40, '=') << "\n";
    std::cout << "\nDC Solve Result:\n";
    std::cout << "  Status: " << static_cast<int>(status) << " (0=SUCCESS, 1=MAX_ITER, 2=SINGULAR, 3=NUM_ERROR)\n";
    std::cout << "  Iterations: " << stats.total_iterations << "\n";
    std::cout << "  Total time: " << stats.total_time_seconds << " s\n";
    std::cout << "\n";

    if (status == ConvergenceStatus::SUCCESS) {
        std::cout << "✅ SUCCESS! DC operating point converged.\n\n";
        std::cout << "Solution vector:\n";
        for (size_t i = 0; i < solution.size(); ++i) {
            std::cout << "  x[" << i << "] = " << solution[i] << "\n";
        }

        // Decode node voltages
        std::cout << "\nNode Voltages:\n";
        try {
            NodeId vcc_id = circuit.get_node_id("vcc");
            NodeId base1_id = circuit.get_node_id("base1");
            NodeId e1_id = circuit.get_node_id("e1");
            NodeId c2_id = circuit.get_node_id("c2");
            NodeId e2_id = circuit.get_node_id("e2");

            std::cout << "  Vcc    = " << solution[vcc_id - 1] << " V\n";
            std::cout << "  base1  = " << solution[base1_id - 1] << " V\n";
            std::cout << "  e1     = " << solution[e1_id - 1] << " V (Q1 emitter = Q2 base)\n";
            std::cout << "  c2     = " << solution[c2_id - 1] << " V (Q2 collector)\n";
            std::cout << "  e2     = " << solution[e2_id - 1] << " V (Q2 emitter)\n";
            std::cout << "\n";
            std::cout << "  Q1: Vbe1 = " << (solution[base1_id - 1] - solution[e1_id - 1]) << " V\n";
            std::cout << "  Q2: Vbe2 = " << (solution[e1_id - 1] - solution[e2_id - 1]) << " V\n";
            std::cout << "  Q2: Vce2 = " << (solution[c2_id - 1] - solution[e2_id - 1]) << " V\n";
        } catch (...) {
            std::cout << "  (Error decoding node voltages)\n";
        }
    } else {
        std::cout << "❌ FAILED! DC operating point did not converge.\n";
        std::cout << "\nThis confirms the issue. Check logs above for:\n";
        std::cout << "  - 'Matrix is singular at column: N'\n";
        std::cout << "  - Then map column N to node using table above\n";
        std::cout << "\nFor Darlington pair:\n";
        std::cout << "  - Column 3 might be e1 (Q1 emitter = Q2 base)\n";
        std::cout << "  - This is the coupling node between Q1 and Q2\n";
        std::cout << "  - Special initialization may be needed\n";
    }

    std::cout << "\n========================================\n";
    return (status == ConvergenceStatus::SUCCESS) ? 0 : 1;
}
