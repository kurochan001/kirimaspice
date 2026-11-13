// tests/validation/test_bdf_adaptive.cpp
// BDF2/BDF3 with adaptive timestep control - comparison with ngspice
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cmath>

using namespace kirimaspice;

// RC circuit theoretical response: V(t) = V0 * (1 - exp(-t/tau))
double rc_theory(double t, double V0, double R, double C) {
    double tau = R * C;
    return V0 * (1.0 - std::exp(-t / tau));
}

int main() {
    // Create RC circuit (same as validation)
    // V1=5V, R1=1k, C1=10u, tau=10ms
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 5.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    // Configuration with BDF2/BDF3 + adaptive timestep + adaptive order
    SimulationConfig config;
    config.transient.use_initial_conditions = true;  // UIC mode for step response
    config.transient.use_adaptive_timestep = true;   // Enable adaptive timestep
    config.transient.method = IntegrationMethod::GEAR2;  // Start with BDF2
    config.transient.adaptive_order = true;  // Enable adaptive order control
    config.transient.max_order = 3;  // Allow up to BDF3
    config.transient.initial_step = 1e-4;  // 100us initial step (larger)
    config.transient.max_step = 1e-3;  // 1ms max step
    config.transient.min_step = 1e-9;  // 1ns min step
    config.transient.lte_abstol = 1e-9;  // LTE absolute tolerance (tighter)
    config.transient.lte_reltol = 1e-4;  // LTE relative tolerance (tighter)

    // Run transient analysis
    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 50e-3, 0.5e-3);  // 0 to 50ms

    if (result.status != ConvergenceStatus::SUCCESS) {
        std::cerr << "Simulation failed!" << std::endl;
        return 1;
    }

    // Extract node ID for 'out'
    NodeId node_out = circuit.get_node_id("out");

    // Print header
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "=== BDF2/BDF3 Adaptive Timestep Results ===" << std::endl;
    std::cout << "Circuit: RC (R=1k, C=10u, tau=10ms)" << std::endl;
    std::cout << "Method: BDF2/BDF3 with adaptive timestep and order control" << std::endl;
    std::cout << "Total steps: " << result.timepoints.size() << std::endl;
    std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;
    std::cout << "Timestep rejects: " << result.stats.timestep_rejects << std::endl;
    std::cout << "Simulation time: " << result.stats.total_time_seconds << " s" << std::endl;
    std::cout << std::endl;

    // Output comparison at key timepoints
    std::cout << "Time(ms) | V_out(V) | Theory(V) | Error(%) | Timestep" << std::endl;
    std::cout << "---------|----------|-----------|----------|----------" << std::endl;

    // Target times: tau, 2*tau, 3*tau, 4*tau, 5*tau
    double tau = 1000.0 * 10e-6;  // 10ms = 0.01s
    std::vector<double> target_times = {tau, tau*2, tau*3, tau*4, tau*5};

    // ngspice reference values (from previous validation)
    std::vector<double> ngspice_values = {3.228, 4.322, 4.751, 4.908, 4.966};

    std::cout << "\nComparison at key timepoints (tau = 10ms):" << std::endl;
    std::cout << std::setw(8) << "Time" << " | ";
    std::cout << std::setw(10) << "ngspice" << " | ";
    std::cout << std::setw(10) << "kirimaspice" << " | ";
    std::cout << std::setw(10) << "Theory" << " | ";
    std::cout << std::setw(12) << "Err vs ng" << " | ";
    std::cout << std::setw(12) << "Err vs th" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

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

        double error_vs_ngspice = std::abs(v_out - ngspice_v) / ngspice_v * 100.0;
        double error_vs_theory = std::abs(v_out - theory_v) / theory_v * 100.0;

        std::cout << std::setw(7) << (target_t * 1e3) << "ms | ";
        std::cout << std::setw(9) << ngspice_v << "V | ";
        std::cout << std::setw(9) << v_out << "V | ";
        std::cout << std::setw(9) << theory_v << "V | ";
        std::cout << std::setw(10) << error_vs_ngspice << "% | ";
        std::cout << std::setw(10) << error_vs_theory << "%" << std::endl;
    }

    // Write detailed output to file
    std::ofstream outfile("/tmp/kirimaspice_bdf_adaptive_output.txt");
    outfile << std::fixed << std::setprecision(9);
    outfile << "Time(s)\tV_out(V)\tTheory(V)\tError(%)" << std::endl;

    for (const auto& tp : result.timepoints) {
        double v_out = tp.node_voltages[node_out - 1];
        double theory_v = rc_theory(tp.time, 5.0, 1000.0, 10e-6);
        double error = std::abs(v_out - theory_v) / theory_v * 100.0;

        outfile << tp.time << "\t" << v_out << "\t" << theory_v << "\t" << error << std::endl;
    }
    outfile.close();

    std::cout << "\nDetailed output written to /tmp/kirimaspice_bdf_adaptive_output.txt" << std::endl;

    return 0;
}
