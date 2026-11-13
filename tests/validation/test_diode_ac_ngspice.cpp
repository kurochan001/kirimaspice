// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_diode_ac_ngspice.cpp
// Diode AC analysis validation against ngspice

#include "ngspice_ac_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::validation;

int main() {
    std::cout << "=== Diode AC Analysis Validation ===" << std::endl;
    std::cout << "Circuit: V(2V DC + 1V AC) -> R(1k) -> Diode -> GND" << std::endl;
    std::cout << "Diode model: IS=1e-14, N=1.0" << std::endl;
    std::cout << std::endl;

    // Load ngspice reference data
    std::cout << "Loading ngspice reference data..." << std::endl;
    NgspiceACData ngspice_data;
    try {
        ngspice_data = load_ngspice_ac_data("validation/diode_ac_ngspice.csv");
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

    // Add DC voltage source with AC component: Vdc in 0 DC 2.0 AC 1.0
    circuit.add_voltage_source("Vdc", "in", "0", 2.0, 1.0);  // DC=2V, AC=1V

    // Add resistor: R1 in out 1k
    circuit.add_resistor("R1", "in", "out", 1000.0);  // 1kΩ

    // Add diode: D1 out 0 (IS=1e-14, N=1.0)
    circuit.add_diode("D1", "out", "0", 1e-14, 1.0);

    // Get node IDs
    const NodeId node_out = circuit.get_node_id("out");

    std::cout << "  Devices: Vdc (2V DC + 1V AC), R1 (1kΩ), D1 (IS=1e-14, N=1.0)" << std::endl;
    std::cout << "  Output node ID: " << node_out << std::endl;

    // Run AC analysis
    std::cout << "\nRunning kirimaspice AC analysis..." << std::endl;
    std::cout << "  Sweep: DEC 10 1k 10k (1 kHz - 10 kHz, 10 points/decade)" << std::endl;

    // Configure AC analysis
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 1000.0;    // 1 kHz
    config.ac.stop_freq = 10000.0;    // 10 kHz
    config.ac.num_points = 10;        // 10 points per decade

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

    // Print and check comparison with relaxed tolerances for nonlinear device
    // Magnitude error: < 0.1 dB (nonlinear device, DC operating point differences)
    // Phase error: < 1.0 degrees
    // Note: Diode small-signal parameters depend on DC operating point
    const double max_mag_error_db = 0.1;
    const double max_phase_error_deg = 1.0;

    bool passed = print_and_check_comparison(
        comparison,
        max_mag_error_db,
        max_phase_error_deg
    );

    // Final result
    std::cout << "\n" << std::string(100, '=') << std::endl;
    if (passed) {
        std::cout << "✓ VALIDATION PASSED: Diode AC analysis matches ngspice" << std::endl;
        std::cout << "  (within " << max_mag_error_db << " dB magnitude and "
                  << max_phase_error_deg << "° phase tolerance)" << std::endl;
        return 0;
    } else {
        std::cerr << "✗ VALIDATION FAILED: Errors exceed tolerance" << std::endl;
        return 1;
    }
}
