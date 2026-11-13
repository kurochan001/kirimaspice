// Check DC operating point with Vbase = 0.75V
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <iostream>
#include <iomanip>

using namespace kirimaspice;

int main() {
    std::cout << "=== Checking DC Operating Point with Vbase = 0.75V ===\n\n";

    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 0.75, 0.005);
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    const NodeId node_coll = circuit.get_node_id("coll");
    const NodeId node_b = circuit.get_node_id("b");

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1001.0;
    config.ac.num_points = 2;

    solvers::ACSolver ac_solver(circuit, config);
    solvers::ACResult result = ac_solver.solve();

    std::cout << "AC Analysis @ 1 kHz:\n";
    std::cout << std::scientific << std::setprecision(6);

    const auto& fp = result.frequency_points[0];
    Complex v_coll = fp.solution[node_coll - 1];
    Complex v_b = fp.solution[node_b - 1];

    std::cout << "V(b)    = " << std::real(v_b) << " + j" << std::imag(v_b) << "\n";
    std::cout << "V(coll) = " << std::real(v_coll) << " + j" << std::imag(v_coll) << "\n\n";

    std::cout << "Phase(coll) = " << (std::arg(v_coll) * 180.0 / M_PI) << "°\n\n";

    if (std::real(v_coll) < 0) {
        std::cout << "✓ V(coll) is NEGATIVE (180° phase) - CORRECT!\n";
    } else {
        std::cout << "⚠️  V(coll) is POSITIVE (0° phase) - WRONG!\n";
    }

    return 0;
}
