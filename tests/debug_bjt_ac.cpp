// Debug program for BJT amplifier AC analysis
// Outputs actual complex values to compare with ngspice

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <iostream>
#include <iomanip>
#include <complex>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// Build common-emitter amplifier (same as test)
CircuitGraph build_bjt_amplifier() {
    CircuitGraph circuit;

    // Power supply
    circuit.add_voltage_source("Vcc", "vcc", "0", 12.0, 0.0);  // 12V DC

    // Input signal (AC only)
    circuit.add_voltage_source("Vin", "in", "0", 0.0, 1.0);  // 0V DC, 1V AC

    // Input coupling capacitor
    circuit.add_capacitor("Cin", "in", "base", 10e-6);  // 10µF

    // Bias network
    circuit.add_resistor("R1", "vcc", "base", 47000.0);  // 47kΩ
    circuit.add_resistor("R2", "base", "0", 10000.0);    // 10kΩ

    // Emitter resistor with bypass capacitor
    circuit.add_resistor("Re", "emitter", "0", 1000.0);  // 1kΩ
    circuit.add_capacitor("Ce", "emitter", "0", 100e-6); // 100µF

    // Collector resistor
    circuit.add_resistor("Rc", "vcc", "collector", 4700.0);  // 4.7kΩ

    // Output coupling capacitor
    circuit.add_capacitor("Cout", "collector", "out", 10e-6);  // 10µF

    // Load resistor
    circuit.add_resistor("Rload", "out", "0", 10000.0);  // 10kΩ

    // BJT (NPN, β=100, default parameters)
    circuit.add_bjt_npn("Q1", "collector", "base", "emitter");

    return circuit;
}

int main() {
    std::cout << "\n========================================\n";
    std::cout << "BJT Amplifier AC Analysis Debug\n";
    std::cout << "========================================\n\n";

    auto circuit = build_bjt_amplifier();

    // Node IDs
    NodeId node_in = circuit.get_node_id("in");
    NodeId node_base = circuit.get_node_id("base");
    NodeId node_emitter = circuit.get_node_id("emitter");
    NodeId node_collector = circuit.get_node_id("collector");
    NodeId node_out = circuit.get_node_id("out");

    std::cout << "Node Mapping:\n";
    std::cout << "  in (input):       NodeId " << node_in << " -> MNA index " << (node_in - 1) << "\n";
    std::cout << "  base:             NodeId " << node_base << " -> MNA index " << (node_base - 1) << "\n";
    std::cout << "  emitter:          NodeId " << node_emitter << " -> MNA index " << (node_emitter - 1) << "\n";
    std::cout << "  collector:        NodeId " << node_collector << " -> MNA index " << (node_collector - 1) << "\n";
    std::cout << "  out (output):     NodeId " << node_out << " -> MNA index " << (node_out - 1) << "\n";
    std::cout << "\n";

    // Run AC analysis (first 3 frequency points for debugging)
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 10.0;      // 10 Hz
    config.ac.stop_freq = 100.0;      // 100 Hz (reduced for debugging)
    config.ac.num_points = 20;        // 20 points/decade

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    std::cout << "AC Analysis Results:\n";
    std::cout << "Input AC magnitude: 1.0 V (specified in Vin)\n";
    std::cout << "\n";

    std::cout << std::setw(12) << "Freq [Hz]"
              << std::setw(20) << "V(out) Real"
              << std::setw(20) << "V(out) Imag"
              << std::setw(20) << "|V(out)|"
              << std::setw(20) << "∠V(out) [deg]"
              << std::setw(20) << "Gain [V/V]"
              << std::setw(20) << "Gain [dB]\n";
    std::cout << std::string(132, '-') << "\n";

    for (size_t i = 0; i < std::min(result.frequency_points.size(), size_t(10)); ++i) {
        const auto& fp = result.frequency_points[i];
        std::complex<double> v_out = fp.solution[node_out - 1];

        double magnitude = std::abs(v_out);
        double phase_deg = std::arg(v_out) * 180.0 / M_PI;
        double gain_db = 20.0 * std::log10(magnitude);  // Gain relative to 1V input

        std::cout << std::setw(12) << std::fixed << std::setprecision(2) << fp.frequency
                  << std::setw(20) << std::scientific << std::setprecision(6) << v_out.real()
                  << std::setw(20) << v_out.imag()
                  << std::setw(20) << std::fixed << std::setprecision(6) << magnitude
                  << std::setw(20) << std::setprecision(2) << phase_deg
                  << std::setw(20) << std::setprecision(4) << magnitude
                  << std::setw(20) << std::setprecision(2) << gain_db << "\n";
    }

    std::cout << "\n";
    std::cout << "Expected behavior (from ngspice):\n";
    std::cout << "  - Gain should be ~10-30 dB (voltage gain ~3-30 V/V)\n";
    std::cout << "  - Phase should be around -180° (inverting amplifier)\n";
    std::cout << "\n";
    std::cout << "If actual values are very different:\n";
    std::cout << "  - Check BJT small-signal model (Gm, Cπ, Cμ)\n";
    std::cout << "  - Check AC MNA stamping (bjt_npn.cpp::stamp_ac)\n";
    std::cout << "  - Compare DC operating point\n";
    std::cout << "\n========================================\n";

    return 0;
}
