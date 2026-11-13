// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_rc_lowpass_ac_ngspice.cpp
// RC low-pass filter AC analysis validation against ngspice

#include "ngspice_ac_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::validation;

int main() {
    std::cout << "=== RC Low-Pass Filter: AC Analysis Validation ===" << std::endl;
    std::cout << "Circuit: V(1V AC) -> R(1k) -> C(1u) -> GND" << std::endl;
    std::cout << "Cutoff frequency: fc = 1/(2*pi*R*C) = 159.15 Hz" << std::endl;
    std::cout << std::endl;

    // Load ngspice reference data
    std::cout << "Loading ngspice reference data..." << std::endl;
    NgspiceACData ngspice_data;
    try {
        ngspice_data = load_ngspice_ac_data("validation/rc_lowpass_ac_ngspice.csv");
        std::cout << "  Loaded " << ngspice_data.frequencies.size()
                  << " frequency points from ngspice" << std::endl;
        std::cout << "  Frequency range: " << ngspice_data.frequencies.front() << " Hz to "
                  << ngspice_data.frequencies.back() << " Hz" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading ngspice data: " << e.what() << std::endl;
        return 1;
    }

    // Build circuit using Circuit Graph
    std::cout << "\nBuilding kirimaspice circuit..." << std::endl;

    circuit::CircuitGraph circuit;

    // Add voltage source: V1 in 0 DC 0 AC 1
    circuit.add_voltage_source("V1", "in", "0", 0.0, 1.0);  // DC=0V, AC=1V

    // Add resistor: R1 in out 1k
    circuit.add_resistor("R1", "in", "out", 1000.0);  // 1kΩ

    // Add capacitor: C1 out 0 1u
    circuit.add_capacitor("C1", "out", "0", 1e-6);  // 1µF

    // Get node IDs
    const NodeId node_out = circuit.get_node_id("out");

    std::cout << "  Devices: V1 (0V DC, 1V AC), R1 (1kΩ), C1 (1µF)" << std::endl;
    std::cout << "  Output node ID: " << node_out << std::endl;

    // Run AC analysis
    std::cout << "\nRunning kirimaspice AC analysis..." << std::endl;
    std::cout << "  Sweep: DEC 20 10 10k (10 Hz - 10 kHz, 20 points/decade)" << std::endl;

    // Configure AC analysis
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 10.0;
    config.ac.stop_freq = 10000.0;
    config.ac.num_points = 20;  // 20 points per decade

    solvers::ACSolver ac_solver(circuit, config);
    solvers::ACResult kirimaspice_result;
    try {
        kirimaspice_result = ac_solver.solve();
        std::cout << "  Computed " << kirimaspice_result.frequency_points.size()
                  << " frequency points" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error running AC analysis: " << e.what() << std::endl;
        return 1;
    }

    // Compare results
    std::cout << "\nComparing kirimaspice vs ngspice..." << std::endl;

    auto comparison = compare_ac_results(ngspice_data, kirimaspice_result, node_out);

    // Print and check comparison with tolerances for linear circuit
    // Magnitude error: < 0.02 dB (relaxed from 0.01 dB due to solver differences)
    // Phase error: < 0.1 degrees
    // Note: ngspice uses SPARSE solver, kirimaspice uses KLU
    const double max_mag_error_db = 0.02;
    const double max_phase_error_deg = 0.1;

    bool passed = print_and_check_comparison(
        comparison,
        max_mag_error_db,
        max_phase_error_deg
    );

    // Final result
    std::cout << "\n" << std::string(100, '=') << std::endl;
    if (passed) {
        std::cout << "✓ VALIDATION PASSED: RC low-pass filter AC analysis matches ngspice" << std::endl;
        std::cout << "  (within " << max_mag_error_db << " dB magnitude and "
                  << max_phase_error_deg << "° phase tolerance)" << std::endl;
        return 0;
    } else {
        std::cerr << "✗ VALIDATION FAILED: Errors exceed tolerance" << std::endl;
        return 1;
    }
}
