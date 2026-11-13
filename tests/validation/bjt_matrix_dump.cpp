// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// BJT AC MNA Matrix Dump
// Outputs the complex admittance matrix Y and RHS vector I for comparison with ngspice

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <complex>

using namespace kirimaspice;

void print_complex_matrix(const ComplexSparseMatrix& Y, const ComplexDenseVector& I,
                          std::size_t n, const std::string& filename) {
    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << "Failed to open " << filename << std::endl;
        return;
    }

    ofs << std::scientific << std::setprecision(12);

    // Print matrix dimensions
    ofs << "# Complex MNA System: " << n << "x" << n << std::endl;
    ofs << "# Format: row col real imag" << std::endl;
    ofs << std::endl;

    // Convert to dense for easier printing
    ofs << "# Y Matrix (Admittance)" << std::endl;
    for (std::size_t i = 0; i < n; i++) {
        for (std::size_t j = 0; j < n; j++) {
            Complex val(0.0, 0.0);
            // Find entry in COO format
            for (std::size_t k = 0; k < Y.rows.size(); k++) {
                if (Y.rows[k] == static_cast<std::uint32_t>(i) &&
                    Y.cols[k] == static_cast<std::uint32_t>(j)) {
                    val += Y.values[k];
                }
            }
            if (std::abs(val) > 1e-20) {
                ofs << i << " " << j << " " << std::real(val) << " " << std::imag(val) << std::endl;
            }
        }
    }

    ofs << std::endl;
    ofs << "# I Vector (RHS)" << std::endl;
    for (std::size_t i = 0; i < I.size(); i++) {
        if (std::abs(I[i]) > 1e-20) {
            ofs << i << " " << std::real(I[i]) << " " << std::imag(I[i]) << std::endl;
        }
    }

    ofs.close();
    std::cout << "Matrix dumped to " << filename << std::endl;
}

int main() {
    std::cout << "=== BJT Amplifier AC MNA Matrix Dump ===" << std::endl;

    // Build circuit (same as validation test)
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 0.75, 0.005);  // AC=5mV
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    std::cout << "Circuit built successfully" << std::endl;

    // Run DC analysis
    std::cout << "\nComputing DC operating point..." << std::endl;
    SimulationConfig config;
    solvers::DCSolver dc_solver(circuit, config);
    DenseVector dc_solution;
    SolverStats dc_stats;
    ConvergenceStatus dc_status = dc_solver.solve(dc_solution, dc_stats);

    if (dc_status != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC analysis failed!" << std::endl;
        return 1;
    }

    std::cout << "DC solution computed" << std::endl;

    // Configure AC analysis for single frequency (1 kHz)
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1001.0;
    config.ac.num_points = 2;

    std::cout << "\nBuilding AC solver..." << std::endl;
    solvers::ACSolver ac_solver(circuit, config);

    // We need to access internal MNA system - let's run solve and intercept
    // For now, let's manually build the AC system
    std::cout << "\nBuilding AC MNA system at 1 kHz..." << std::endl;

    // Get circuit info
    const std::size_t num_nodes = circuit.num_nodes();

    // Count voltage sources (need branch currents)
    std::uint32_t num_branches = 0;
    for (const auto& device : circuit.get_devices()) {
        if (device->type() == DeviceType::VSOURCE) {
            num_branches++;
        }
    }

    const std::size_t system_size = num_nodes + num_branches;

    std::cout << "System size: " << num_nodes << " nodes + "
              << num_branches << " branches = " << system_size << std::endl;

    // Manually build the system (we need to replicate ACSolver logic)
    ComplexMNASystem sys;
    sys.num_nodes = num_nodes;
    sys.num_branches = num_branches;
    sys.Y.rows.clear();
    sys.Y.cols.clear();
    sys.Y.values.clear();
    sys.I.resize(system_size, Complex(0.0, 0.0));

    // Stamp all devices at omega = 2*pi*1000
    const Real omega = 2.0 * M_PI * 1000.0;

    std::cout << "\nStamping devices at ω = " << omega << " rad/s..." << std::endl;

    // Stamp each device
    for (const auto& device : circuit.get_devices()) {
        device->stamp_ac(sys, omega, dc_solution);
        std::cout << "  Stamped: " << device->name() << std::endl;
    }

    std::cout << "\nMNA matrix assembled:" << std::endl;
    std::cout << "  Non-zero entries in Y: " << sys.Y.rows.size() << std::endl;
    std::cout << "  RHS vector size: " << sys.I.size() << std::endl;

    // Dump to file
    print_complex_matrix(sys.Y, sys.I, system_size, "bjt_ac_matrix_kirimaspice.txt");

    // Print node mapping
    std::cout << "\nNode mapping:" << std::endl;
    std::cout << "  base: " << circuit.get_node_id("base") << std::endl;
    std::cout << "  b:    " << circuit.get_node_id("b") << std::endl;
    std::cout << "  vcc:  " << circuit.get_node_id("vcc") << std::endl;
    std::cout << "  coll: " << circuit.get_node_id("coll") << std::endl;
    std::cout << "  GND:  " << 0 << std::endl;

    return 0;
}
