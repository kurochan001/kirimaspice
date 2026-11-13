// tests/validation/test_bdf2_fixed.cpp
// BDF2 with FIXED timestep - fair comparison with ngspice
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;

// RC circuit theoretical response
double rc_theory(double t, double V0, double R, double C) {
    double tau = R * C;
    return V0 * (1.0 - std::exp(-t / tau));
}

int main() {
    // Create RC circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 5.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    // Configuration with BDF2 + FIXED timestep (like original test)
    SimulationConfig config;
    config.transient.use_initial_conditions = false;
    config.transient.use_adaptive_timestep = false;  // FIXED timestep
    config.transient.method = IntegrationMethod::GEAR2;  // Use BDF2
    config.transient.adaptive_order = false;  // No order control

    // Run transient analysis
    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 50e-3, 0.5e-3);  // 0 to 50ms, 0.5ms step

    if (result.status != ConvergenceStatus::SUCCESS) {
        std::cerr << "Simulation failed!" << std::endl;
        return 1;
    }

    // Extract node ID
    NodeId node_out = circuit.get_node_id("out");

    // Print results
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n=== BDF2 Fixed Timestep (0.5ms) Results ===" << std::endl;
    std::cout << "Total steps: " << result.timepoints.size() << std::endl;
    std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;
    std::cout << "\nComparison with ngspice:\n" << std::endl;

    // Target times
    double tau = 0.01;  // 10ms
    std::vector<double> target_times = {tau, tau*2, tau*3, tau*4, tau*5};
    std::vector<double> ngspice_values = {3.228, 4.322, 4.751, 4.908, 4.966};

    std::cout << std::setw(8) << "Time" << " | ";
    std::cout << std::setw(10) << "ngspice" << " | ";
    std::cout << std::setw(10) << "kirimaspice" << " | ";
    std::cout << std::setw(10) << "Theory" << " | ";
    std::cout << std::setw(10) << "Err ng" << " | ";
    std::cout << std::setw(10) << "Err th" << std::endl;
    std::cout << std::string(70, '-') << std::endl;

    for (size_t i = 0; i < target_times.size(); ++i) {
        double target_t = target_times[i];

        // Find closest timepoint
        size_t closest_idx = 0;
        double min_diff = std::abs(result.timepoints[0].time - target_t);
        for (size_t j = 1; j < result.timepoints.size(); ++j) {
            double diff = std::abs(result.timepoints[j].time - target_t);
            if (diff < min_diff) {
                min_diff = diff;
                closest_idx = j;
            }
        }

        const auto& tp = result.timepoints[closest_idx];
        double v_out = tp.node_voltages[node_out - 1];
        double theory_v = rc_theory(tp.time, 5.0, 1000.0, 10e-6);
        double ngspice_v = ngspice_values[i];

        double error_ng = std::abs(v_out - ngspice_v) / ngspice_v * 100.0;
        double error_th = std::abs(v_out - theory_v) / theory_v * 100.0;

        std::cout << std::setw(7) << (target_t * 1e3) << "ms | ";
        std::cout << std::setw(9) << ngspice_v << "V | ";
        std::cout << std::setw(9) << v_out << "V | ";
        std::cout << std::setw(9) << theory_v << "V | ";
        std::cout << std::setw(8) << error_ng << "% | ";
        std::cout << std::setw(8) << error_th << "%" << std::endl;
    }

    return 0;
}
