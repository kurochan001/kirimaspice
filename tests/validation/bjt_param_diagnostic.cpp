// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// BJT AC Analysis Diagnostic
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <iostream>
#include <iomanip>

using namespace kirimaspice;

int main() {
    std::cout << "=== BJT Amplifier AC Analysis Diagnostic ===" << std::endl;

    // Build circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 0.75, 0.005);
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    // Get node IDs
    const NodeId node_base = circuit.get_node_id("base");
    const NodeId node_b = circuit.get_node_id("b");
    const NodeId node_vcc = circuit.get_node_id("vcc");
    const NodeId node_coll = circuit.get_node_id("coll");

    std::cout << "Node IDs: base=" << node_base << ", b=" << node_b
              << ", vcc=" << node_vcc << ", coll=" << node_coll << std::endl;

    // Run DC analysis
    std::cout << "\n=== DC Analysis ===" << std::endl;
    SimulationConfig config;
    solvers::DCSolver dc_solver(circuit, config);

    DenseVector dc_solution;
    SolverStats dc_stats;
    ConvergenceStatus dc_status = dc_solver.solve(dc_solution, dc_stats);

    if (dc_status != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC analysis failed to converge!" << std::endl;
        return 1;
    }

    // Print DC voltages
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "V(base) = " << dc_solution[node_base - 1] << " V" << std::endl;
    std::cout << "V(b)    = " << dc_solution[node_b - 1] << " V" << std::endl;
    std::cout << "V(vcc)  = " << dc_solution[node_vcc - 1] << " V" << std::endl;
    std::cout << "V(coll) = " << dc_solution[node_coll - 1] << " V" << std::endl;

    Real vbe = dc_solution[node_b - 1];  // Base voltage (emitter at ground)
    Real vbc = dc_solution[node_b - 1] - dc_solution[node_coll - 1];
    std::cout << "Vbe = " << vbe << " V" << std::endl;
    std::cout << "Vbc = " << vbc << " V" << std::endl;

    // Calculate small-signal parameters manually
    const Real IS = 1e-14;
    const Real BF = 100.0;
    const Real NF = 1.0;
    const Real VT = 0.026;  // Thermal voltage at 300K

    Real ic = IS * std::exp(vbe / (NF * VT));
    Real ib = ic / BF;
    Real gm = ic / (NF * VT);
    Real gpi = ib / (NF * VT);

    std::cout << "\n=== Calculated Small-Signal Parameters ===" << std::endl;
    std::cout << std::scientific << std::setprecision(6);
    std::cout << "Ic  = " << ic << " A" << std::endl;
    std::cout << "Ib  = " << ib << " A" << std::endl;
    std::cout << "gm  = " << gm << " S" << std::endl;
    std::cout << "gπ  = " << gpi << " S" << std::endl;

    // Run AC analysis at one frequency
    std::cout << "\n=== AC Analysis (1 kHz) ===" << std::endl;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1001.0;
    config.ac.num_points = 2;

    solvers::ACSolver ac_solver(circuit, config);
    auto ac_result = ac_solver.solve();

    if (ac_result.frequency_points.empty()) {
        std::cerr << "AC analysis failed!" << std::endl;
        return 1;
    }

    auto& freq_point = ac_result.frequency_points[0];
    Complex v_coll = freq_point.solution[node_coll - 1];
    Complex v_base = freq_point.solution[node_base - 1];

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "V(base) = " << std::abs(v_base) << " ∠ "
              << std::arg(v_base) * 180.0 / M_PI << "°" << std::endl;
    std::cout << "V(coll) = " << std::abs(v_coll) << " ∠ "
              << std::arg(v_coll) * 180.0 / M_PI << "°" << std::endl;

    Real gain = std::abs(v_coll) / std::abs(v_base);
    std::cout << "Gain = " << gain << " (" << 20.0 * std::log10(gain) << " dB)" << std::endl;

    std::cout << "\n=== Expected from ngspice ===" << std::endl;
    std::cout << "gm (ngspice)  = 1.867969e-01 S" << std::endl;
    std::cout << "gπ (ngspice)  = 1.870187e-03 S" << std::endl;
    std::cout << "gμ (ngspice)  = 2.218430e-04 S" << std::endl;
    std::cout << "V(coll) magnitude = 0.020344 V" << std::endl;
    std::cout << "Expected gain = 0.020344 / 0.005 = 4.07" << std::endl;

    return 0;
}
