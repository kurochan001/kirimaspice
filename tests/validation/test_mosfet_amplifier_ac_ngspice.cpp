// tests/validation/test_mosfet_amplifier_ac_ngspice.cpp
// MOSFET common-source amplifier AC analysis validation against ngspice

#include "ngspice_ac_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::validation;

int main() {
    std::cout << "=== MOSFET Common-Source Amplifier AC Analysis Validation ===" << std::endl;
    std::cout << "Circuit: VDD(5V) -> RD(10k) -> MOSFET Drain" << std::endl;
    std::cout << "         VGS(2.0V DC + 10mV AC) -> MOSFET Gate" << std::endl;
    std::cout << "         MOSFET Source -> GND" << std::endl;
    std::cout << "MOSFET model: VTO=0.7, KP=2e-5, W=10u, L=1u" << std::endl;
    std::cout << std::endl;

    // Load ngspice reference data
    std::cout << "Loading ngspice reference data..." << std::endl;
    NgspiceACData ngspice_data;
    try {
        ngspice_data = load_ngspice_ac_data("validation/mosfet_amplifier_ac_ngspice.csv");
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

    // Power supply: VDD vdd 0 DC 5.0
    circuit.add_voltage_source("VDD", "vdd", "0", 5.0);

    // Gate voltage with AC: VGS vgs 0 DC 2.0 AC 0.01
    circuit.add_voltage_source("VGS", "vgs", "0", 2.0, 0.01);  // DC=2.0V, AC=10mV

    // Drain resistor: RD vdd d 10k
    circuit.add_resistor("RD", "vdd", "d", 10000.0);  // 10kΩ

    // MOSFET: M1 d vgs 0 (Drain Gate Source)
    // KP=2e-5, W=10u, L=1u, VTO=0.7
    circuit.add_mosfet_n("M1", "d", "vgs", "0", 2e-5, 10e-6, 1e-6, 0.7);

    // Get node IDs
    const NodeId node_d = circuit.get_node_id("d");

    std::cout << "  Devices: VDD (5V), VGS (2.0V DC + 10mV AC), RD (10kΩ)" << std::endl;
    std::cout << "           M1 (VTO=0.7, KP=2e-5, W=10u, L=1u)" << std::endl;
    std::cout << "  Drain node ID: " << node_d << std::endl;

    // Run AC analysis
    std::cout << "\nRunning kirimaspice AC analysis..." << std::endl;
    std::cout << "  Sweep: DEC 20 10 100k (10 Hz - 100 kHz, 20 points/decade)" << std::endl;

    // Configure AC analysis
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 10.0;      // 10 Hz
    config.ac.stop_freq = 100000.0;   // 100 kHz
    config.ac.num_points = 20;        // 20 points per decade

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

    auto comparison = compare_ac_results(ngspice_data, kirimaspice_result, node_d);

    // Print and check comparison with tolerances for nonlinear device
    // Magnitude error: < 0.5 dB (after fixing duplicate condition bug)
    // Phase error: < 2.0 degrees
    // Note: MOSFET small-signal parameters depend on DC operating point
    const double max_mag_error_db = 0.5;
    const double max_phase_error_deg = 2.0;

    bool passed = print_and_check_comparison(
        comparison,
        max_mag_error_db,
        max_phase_error_deg
    );

    // Final result
    std::cout << "\n" << std::string(100, '=') << std::endl;
    if (passed) {
        std::cout << "✓ VALIDATION PASSED: MOSFET amplifier AC analysis matches ngspice" << std::endl;
        std::cout << "  (within " << max_mag_error_db << " dB magnitude and "
                  << max_phase_error_deg << "° phase tolerance)" << std::endl;
        return 0;
    } else {
        std::cerr << "✗ VALIDATION FAILED: Errors exceed tolerance" << std::endl;
        return 1;
    }
}
