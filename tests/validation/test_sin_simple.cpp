// tests/validation/test_sin_simple.cpp
// Ultra-simple test: SIN voltage source with resistor load only

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;

int main() {
    std::cout << "=== Ultra-Simple SIN Test (Resistor Load Only) ===\n";
    std::cout << "Circuit: V1(SIN 0V 5V 1Hz) -> R1(1k)\n";
    std::cout << "Expected: V(out) should follow 5*sin(2*pi*1*t)\n\n";

    // Create simple circuit: SIN source with resistor load
    circuit::CircuitGraph circuit;
    circuit.add_sin_voltage_source("V1", "out", "0",
                                   0.0,   // offset
                                   5.0,   // amplitude
                                   1.0,   // frequency (1Hz for easy debugging)
                                   0.0,   // delay
                                   0.0,   // damping
                                   0.0);  // phase
    circuit.add_resistor("R1", "out", "0", 1000.0);

    NodeId node_out = circuit.get_node_id("out");

    // Simulation configuration (fixed timestep for predictability)
    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = false;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;

    // Simulate 1 period (1 second)
    double t_stop = 1.0;
    double timestep = 0.1;  // 0.1s steps

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, t_stop, timestep);

    if (result.status != ConvergenceStatus::SUCCESS) {
        std::cerr << "ERROR: Simulation failed\n";
        return 1;
    }

    std::cout << "Simulation completed!\n";
    std::cout << "Total timepoints: " << result.timepoints.size() << "\n\n";

    // Print all timepoints
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Time(s) | V_out(V) | Expected(V) | Error(V)" << std::endl;
    std::cout << "--------|----------|-------------|----------" << std::endl;

    double max_error = 0.0;
    for (const auto& tp : result.timepoints) {
        double v_out = tp.node_voltages[node_out - 1];  // NodeId starts from 1, vector index from 0
        double v_expected = 5.0 * std::sin(2.0 * M_PI * 1.0 * tp.time);
        double error = std::abs(v_out - v_expected);
        max_error = std::max(max_error, error);

        std::cout << std::setw(7) << tp.time << " | ";
        std::cout << std::setw(8) << v_out << " | ";
        std::cout << std::setw(11) << v_expected << " | ";
        std::cout << std::setw(8) << error << std::endl;
    }

    std::cout << "\nMaximum error: " << max_error << " V\n";

    if (max_error < 0.01) {
        std::cout << "✓ Test PASSED\n";
        return 0;
    } else {
        std::cout << "✗ Test FAILED - error too large\n";
        return 1;
    }
}
