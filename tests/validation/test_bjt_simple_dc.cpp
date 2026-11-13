// Simple BJT DC test to debug collector voltage issue
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>

using namespace kirimaspice;

int main() {
    std::cout << "=== BJT Simple DC Test ===\n";
    std::cout << "Circuit: VCC(5V) -> Rc(1k) -> Coll; Base=0.75V via Rb(1kΩ); Emit=GND\n\n";

    circuit::CircuitGraph circuit;

    // Power supply
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);

    // Base voltage
    circuit.add_voltage_source("Vbase", "base", "0", 0.75);

    // Base resistor
    circuit.add_resistor("Rb", "base", "b", 1000.0);

    // Collector resistor
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);

    // BJT: Collector Base Emitter
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    NodeId node_base = circuit.get_node_id("base");
    NodeId node_b = circuit.get_node_id("b");
    NodeId node_coll = circuit.get_node_id("coll");
    NodeId node_vcc = circuit.get_node_id("vcc");

    std::cout << "Node IDs: base=" << node_base
              << ", b=" << node_b
              << ", coll=" << node_coll
              << ", vcc=" << node_vcc << "\n\n";

    // DC solver
    SimulationConfig config;
    config.dc.max_iterations = 100;
    config.dc.abstol = 1e-10;
    config.dc.reltol = 1e-6;
    config.dc.vntol = 1e-6;

    solvers::DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    if (status != ConvergenceStatus::SUCCESS) {
        std::cerr << "ERROR: DC solve failed\n";
        return 1;
    }

    std::cout << "DC Solution (converged in " << stats.total_iterations << " iterations):\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "  V_base  = " << solution[node_base - 1] << " V\n";
    std::cout << "  V_b     = " << solution[node_b - 1] << " V\n";
    std::cout << "  V_coll  = " << solution[node_coll - 1] << " V\n";
    std::cout << "  V_vcc   = " << solution[node_vcc - 1] << " V\n";

    // Calculate currents
    double v_base = solution[node_base - 1];
    double v_b = solution[node_b - 1];
    double v_coll = solution[node_coll - 1];
    double v_vcc = solution[node_vcc - 1];

    double i_rb = (v_base - v_b) / 1000.0;  // Current through Rb
    double i_rc = (v_vcc - v_coll) / 1000.0;  // Current through Rc (= Ic)

    std::cout << "\nCalculated currents:\n";
    std::cout << "  I_Rb    = " << i_rb * 1000.0 << " mA\n";
    std::cout << "  I_Rc(Ic)= " << i_rc * 1000.0 << " mA\n";

    std::cout << "\nExpected (from ngspice with Rb=1k):\n";
    std::cout << "  V_b     ≈ 0.696 V\n";
    std::cout << "  V_coll  ≈ 0.174 V\n";

    return 0;
}
