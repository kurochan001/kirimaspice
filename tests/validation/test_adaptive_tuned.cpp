// tests/validation/test_adaptive_tuned.cpp
// Tuned adaptive timestep control
// Testing improved parameters: trtol=2.0, lte_abstol=1e-7, lte_reltol=1e-4
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
    // Create RC circuit
    // V1=5V, R1=1k, C1=10u, tau=10ms
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 5.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    // Configuration with tuned parameters
    SimulationConfig config;
    config.transient.use_initial_conditions = false;  // Start from DC operating point
    config.transient.use_adaptive_timestep = true;    // Enable adaptive timestep
    config.transient.method = IntegrationMethod::GEAR2;  // Start with BDF2
    config.transient.adaptive_order = true;  // Enable adaptive order control

    // Sprint 1 uses NEW DEFAULT PARAMETERS from types.hpp:
    // - trtol = 2.0 (reduced from hardcoded 10.0)
    // - lte_abstol = 1e-7 (tightened from 1e-6)
    // - lte_reltol = 1e-4 (tightened from 1e-3)
    // - max_order = 3 (increased from 2)
    // - Timestep multiplier: max 1.5 or 1.2 (reduced from 2.0)
    // - Order control thresholds: 0.2/1.5 (adjusted from 0.1/2.0)

    // Initial timestep: conservative start
    config.transient.initial_step = 1e-7;  // 0.1us (τ/100 for τ=10ms)
    config.transient.max_step = 1e-3;  // 1ms max step
    config.transient.min_step = 1e-12; // 1ps min step

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
    std::cout << "\n=== Phase 3.1 Sprint 1: Tuned Adaptive Control ===" << std::endl;
    std::cout << "Circuit: RC (R=1k, C=10u, tau=10ms)" << std::endl;
    std::cout << "Method: BDF2/BDF3 with adaptive timestep and order control" << std::endl;
    std::cout << "\nSprint 1 Improvements:" << std::endl;
    std::cout << "  - TRTOL: 10.0 → 2.0 (5x stricter acceptance)" << std::endl;
    std::cout << "  - LTE_ABSTOL: 1e-6 → 1e-7 (10x stricter)" << std::endl;
    std::cout << "  - LTE_RELTOL: 1e-3 → 1e-4 (10x stricter)" << std::endl;
    std::cout << "  - Max timestep increase: 2.0x → 1.5x/1.2x (conditional)" << std::endl;
    std::cout << "  - Order control: 0.1/2.0 → 0.2/1.5 (narrower window)" << std::endl;
    std::cout << "  - Max order: 2 → 3 (BDF3 enabled)" << std::endl;
    std::cout << "  - Initial step: 1e-6 → 1e-7 (10x smaller)" << std::endl;
    std::cout << "\nSimulation Results:" << std::endl;
    std::cout << "  Total steps: " << result.timepoints.size() << std::endl;
    std::cout << "  Total iterations: " << result.stats.total_iterations << std::endl;
    std::cout << "  Timestep rejects: " << result.stats.timestep_rejects << std::endl;
    std::cout << "  Simulation time: " << result.stats.total_time_seconds << " s" << std::endl;
    std::cout << std::endl;

    // ngspice reference values
    double tau = 1000.0 * 10e-6;  // 10ms = 0.01s
    std::vector<double> target_times = {tau, tau*2, tau*3, tau*4, tau*5};
    std::vector<double> ngspice_values = {3.228, 4.322, 4.751, 4.908, 4.966};

    std::cout << "Comparison at key timepoints (tau = 10ms):" << std::endl;
    std::cout << std::setw(8) << "Time" << " | ";
    std::cout << std::setw(10) << "ngspice" << " | ";
    std::cout << std::setw(10) << "kirimaspice" << " | ";
    std::cout << std::setw(10) << "Theory" << " | ";
    std::cout << std::setw(12) << "Err vs ng" << " | ";
    std::cout << std::setw(12) << "Err vs th" << std::endl;
    std::cout << std::string(80, '-') << std::endl;

    double max_error_ng = 0.0;
    double max_error_th = 0.0;

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

        max_error_ng = std::max(max_error_ng, error_vs_ngspice);
        max_error_th = std::max(max_error_th, error_vs_theory);

        std::cout << std::setw(7) << (target_t * 1e3) << "ms | ";
        std::cout << std::setw(9) << ngspice_v << "V | ";
        std::cout << std::setw(9) << v_out << "V | ";
        std::cout << std::setw(9) << theory_v << "V | ";
        std::cout << std::setw(10) << error_vs_ngspice << "% | ";
        std::cout << std::setw(10) << error_vs_theory << "%" << std::endl;
    }

    std::cout << std::string(80, '-') << std::endl;
    std::cout << "Maximum errors: vs ngspice = " << max_error_ng << "%, vs theory = " << max_error_th << "%" << std::endl;

    // Success criteria
    std::cout << "\n=== Sprint 1 Success Criteria ===" << std::endl;
    bool criterion1 = (max_error_ng < 5.0);  // Target: < 5% (down from 9.63%)
    bool criterion2 = (result.stats.timestep_rejects < 20);  // Target: < 20 (down from 29)
    bool criterion3 = (result.timepoints.size() >= 400);  // Should have enough resolution

    std::cout << "✓ Max error vs ngspice < 5%:    " << (criterion1 ? "PASS" : "FAIL")
              << " (" << max_error_ng << "%)" << std::endl;
    std::cout << "✓ Timestep rejects < 20:        " << (criterion2 ? "PASS" : "FAIL")
              << " (" << result.stats.timestep_rejects << ")" << std::endl;
    std::cout << "✓ Sufficient resolution (≥400): " << (criterion3 ? "PASS" : "FAIL")
              << " (" << result.timepoints.size() << ")" << std::endl;

    bool all_passed = criterion1 && criterion2 && criterion3;
    std::cout << "\nOverall: " << (all_passed ? "✅ PASS" : "❌ FAIL") << std::endl;

    // Write detailed output to file
    std::ofstream outfile("/tmp/kirimaspice_adaptive_tuned_output.txt");
    outfile << std::fixed << std::setprecision(9);
    outfile << "Time(s)\tV_out(V)\tTheory(V)\tError(%)\tTimestep(s)" << std::endl;

    for (size_t i = 0; i < result.timepoints.size(); ++i) {
        const auto& tp = result.timepoints[i];
        double v_out = tp.node_voltages[node_out - 1];
        double theory_v = rc_theory(tp.time, 5.0, 1000.0, 10e-6);
        double error = std::abs(v_out - theory_v) / theory_v * 100.0;

        // Calculate timestep (dt = t[i] - t[i-1])
        double dt = (i > 0) ? (tp.time - result.timepoints[i-1].time) : 0.0;

        outfile << tp.time << "\t" << v_out << "\t" << theory_v << "\t"
                << error << "\t" << dt << std::endl;
    }
    outfile.close();

    std::cout << "\nDetailed output written to /tmp/kirimaspice_adaptive_tuned_output.txt" << std::endl;

    return all_passed ? 0 : 1;
}
