// BJT AC Complex Voltage Debug
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <iostream>
#include <iomanip>
#include <complex>

using namespace kirimaspice;

int main() {
    std::cout << "=== BJT Amplifier Complex Voltage Debug ===" << std::endl;

    // Build circuit (same as validation test)
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);             // DC only
    circuit.add_voltage_source("Vbase", "base", "0", 0.75, 0.005);  // AC=5mV
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    const NodeId node_base = circuit.get_node_id("base");
    const NodeId node_coll = circuit.get_node_id("coll");

    // Run AC analysis at 1 kHz
    SimulationConfig config;
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
    Complex v_base = freq_point.solution[node_base - 1];
    Complex v_coll = freq_point.solution[node_coll - 1];

    std::cout << std::fixed << std::setprecision(10);
    std::cout << "\n=== Complex Voltages at 1 kHz ===" << std::endl;
    std::cout << "V(base) = " << std::real(v_base) << " + j" << std::imag(v_base) << std::endl;
    std::cout << "        = " << std::abs(v_base) << " ∠ " << std::arg(v_base) * 180.0 / M_PI << "°" << std::endl;
    std::cout << std::endl;
    std::cout << "V(coll) = " << std::real(v_coll) << " + j" << std::imag(v_coll) << std::endl;
    std::cout << "        = " << std::abs(v_coll) << " ∠ " << std::arg(v_coll) * 180.0 / M_PI << "°" << std::endl;
    std::cout << std::endl;

    // Expected from ngspice
    std::cout << "=== Expected from ngspice ===" << std::endl;
    std::cout << "V(base) = 0.005 + j0 = 0.005 ∠ 0°" << std::endl;
    std::cout << "V(coll) = -0.020344 + j0 = 0.020344 ∠ 180°" << std::endl;
    std::cout << std::endl;

    // Gain
    Complex gain = v_coll / v_base;
    std::cout << "=== Gain Analysis ===" << std::endl;
    std::cout << "Gain (complex) = " << std::real(gain) << " + j" << std::imag(gain) << std::endl;
    std::cout << "Gain (magnitude) = " << std::abs(gain) << std::endl;
    std::cout << "Gain (dB) = " << 20.0 * std::log10(std::abs(gain)) << " dB" << std::endl;
    std::cout << "Gain (phase) = " << std::arg(gain) * 180.0 / M_PI << "°" << std::endl;
    std::cout << std::endl;
    std::cout << "Expected gain magnitude: -0.020344 / 0.005 = 4.07" << std::endl;
    std::cout << "Expected gain phase: 180° (inverted)" << std::endl;

    return 0;
}
