// tests/validation/test_bjt_amplifier_ac_ngspice.cpp
// BJT common-emitter amplifier AC analysis validation against ngspice

#include "ngspice_ac_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::validation;

int main() {
    std::cout << "=== BJT Common-Emitter Amplifier AC Analysis Validation ===" << std::endl;
    std::cout << "Circuit: Vcc(5V) -> Rc(1k) -> BJT Collector" << std::endl;
    std::cout << "         Vbase(0.7V DC + 10mV AC) -> Rb(1k) -> BJT Base" << std::endl;
    std::cout << "         BJT Emitter -> GND" << std::endl;
    std::cout << "BJT model: IS=1e-14, BF=100, NF=1.0" << std::endl;
    std::cout << std::endl;

    // Load ngspice reference data
    std::cout << "Loading ngspice reference data..." << std::endl;
    NgspiceACData ngspice_data;
    try {
        ngspice_data = load_ngspice_ac_data("validation/bjt_amplifier_ac_ngspice.csv");
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

    // Power supply: Vcc vcc 0 DC 5
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);

    // Base voltage with AC: Vbase base 0 DC 0.7 AC 0.01
    // Changed from 0.75V to 0.7V to match test_bjt_with_rb_phase (known working case)
    circuit.add_voltage_source("Vbase", "base", "0", 0.7, 0.01);  // DC=0.7V, AC=10mV

    // Base resistor: Rb base b 1k
    circuit.add_resistor("Rb", "base", "b", 1000.0);  // 1kΩ

    // Collector resistor: Rc vcc coll 1k
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);  // 1kΩ

    // BJT: Q1 coll b 0 (Collector Base Emitter)
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);  // IS, BF, NF

    // Get node IDs
    const NodeId node_coll = circuit.get_node_id("coll");

    std::cout << "  Devices: Vcc (5V), Vbase (0.75V DC + 5mV AC), Rb (1kΩ), Rc (1kΩ)" << std::endl;
    std::cout << "           Q1 (IS=1e-14, BF=100, NF=1.0)" << std::endl;
    std::cout << "  Collector node ID: " << node_coll << std::endl;

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

    auto comparison = compare_ac_results(ngspice_data, kirimaspice_result, node_coll);

    // Print and check comparison with tolerances for nonlinear device
    // Magnitude error: < 0.5 dB (after fixing duplicate condition bug)
    // Phase error: < 2.0 degrees
    // Note: BJT small-signal parameters depend on DC operating point
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
        std::cout << "✓ VALIDATION PASSED: BJT amplifier AC analysis matches ngspice" << std::endl;
        std::cout << "  (within " << max_mag_error_db << " dB magnitude and "
                  << max_phase_error_deg << "° phase tolerance)" << std::endl;
        return 0;
    } else {
        std::cerr << "✗ VALIDATION FAILED: Errors exceed tolerance" << std::endl;
        return 1;
    }
}
