// Debug program to identify singular matrix column in BJT 2-stage amplifier
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

// Build the exact same 2-stage BJT amplifier that fails
CircuitGraph build_bjt_2stage_amplifier() {
    CircuitGraph circuit;

    // Power supply
    circuit.add_voltage_source("Vcc", "vcc", "0", 12.0, 0.0);  // 12V DC

    // Input signal (AC only)
    circuit.add_voltage_source("Vin", "in", "0", 0.0, 0.01);  // 0V DC, 10mV AC

    // Stage 1: Common-emitter amplifier
    circuit.add_capacitor("Cin", "in", "base1", 10e-6);         // Input coupling
    circuit.add_resistor("R1_1", "vcc", "base1", 100e3);        // Bias
    circuit.add_resistor("R2_1", "base1", "0", 22e3);           // Bias
    circuit.add_resistor("Rc1", "vcc", "c1", 4.7e3);            // Collector
    circuit.add_resistor("Re1", "e1", "0", 1e3);                // Emitter
    circuit.add_capacitor("Ce1", "e1", "0", 100e-6);            // Bypass
    circuit.add_bjt_npn("Q1", "c1", "base1", "e1");             // NPN

    // Stage 2: Common-emitter amplifier
    circuit.add_capacitor("Cc", "c1", "base2", 10e-6);          // Coupling
    circuit.add_resistor("R1_2", "vcc", "base2", 100e3);        // Bias
    circuit.add_resistor("R2_2", "base2", "0", 22e3);           // Bias
    circuit.add_resistor("Rc2", "vcc", "c2", 4.7e3);            // Collector
    circuit.add_resistor("Re2", "e2", "0", 1e3);                // Emitter
    circuit.add_capacitor("Ce2", "e2", "0", 100e-6);            // Bypass
    circuit.add_bjt_npn("Q2", "c2", "base2", "e2");             // NPN

    // Output
    circuit.add_capacitor("Cout", "c2", "out", 10e-6);          // Output coupling
    circuit.add_resistor("Rload", "out", "0", 10e3);            // Load

    return circuit;
}

int main() {
    // Enable debug logging
    spdlog::set_level(spdlog::level::debug);

    std::cout << "\n========================================\n";
    std::cout << "BJT 2-Stage Amplifier DC Debug\n";
    std::cout << "========================================\n\n";

    // Build circuit
    auto circuit = build_bjt_2stage_amplifier();

    // Print node mapping
    std::cout << "Node Mapping:\n";
    std::cout << std::setw(15) << "Node Name" << std::setw(10) << "NodeId" << std::setw(15) << "MNA Index\n";
    std::cout << std::string(40, '-') << "\n";

    std::vector<std::string> node_names = {
        "0", "vcc", "in", "base1", "c1", "e1", "base2", "c2", "e2", "out"
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

    // Try to solve DC
    std::cout << "Attempting DC solve...\n";
    std::cout << std::string(40, '=') << "\n";

    DCSolver solver(circuit);

    DenseVector solution;
    SolverStats stats;

    ConvergenceStatus status = solver.solve(solution, stats);

    std::cout << std::string(40, '=') << "\n";
    std::cout << "\nDC Solve Result:\n";
    std::cout << "  Status: " << static_cast<int>(status) << " (0=SUCCESS, 1=MAX_ITER, 2=NUM_ERROR)\n";
    std::cout << "  Iterations: " << stats.total_iterations << "\n";
    std::cout << "  Total time: " << stats.total_time_seconds << " s\n";
    std::cout << "\n";

    if (status == ConvergenceStatus::SUCCESS) {
        std::cout << "✅ SUCCESS! DC operating point converged.\n\n";
        std::cout << "Solution vector:\n";
        for (size_t i = 0; i < solution.size(); ++i) {
            std::cout << "  x[" << i << "] = " << solution[i] << "\n";
        }
    } else {
        std::cout << "❌ FAILED! DC operating point did not converge.\n";
        std::cout << "\nThis confirms the issue. Check logs above for:\n";
        std::cout << "  - 'Matrix is singular at column: N'\n";
        std::cout << "  - Then map column N to node using table above\n";
        std::cout << "\nFor example:\n";
        std::cout << "  - Column 0 → vcc (MNA index 0)\n";
        std::cout << "  - Column 1 → in (MNA index 1)\n";
        std::cout << "  - Column 2 → base1 (MNA index 2)\n";
        std::cout << "  - Column 3 → c1 (MNA index 3)\n";
        std::cout << "  - Column 4 → e1 (MNA index 4)\n";
        std::cout << "  - etc...\n";
    }

    std::cout << "\n========================================\n";
    return (status == ConvergenceStatus::SUCCESS) ? 0 : 1;
}
