// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_adaptive_physical.cpp
// Physical quantity-based error estimation
// Testing charge (Q) and flux (Φ) based adaptive timestep control
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

    // Configuration with Physical quantity-based error estimation
    SimulationConfig config;
    config.transient.use_initial_conditions = true;  // UIC mode: step response from 0V
    config.transient.use_adaptive_timestep = true;    // Enable adaptive timestep
    config.transient.method = IntegrationMethod::GEAR2;  // Start with BDF2
    config.transient.adaptive_order = true;  // Enable adaptive order control

    // Sprint 2 uses physical quantity-based error estimation:
    // - Capacitor: Charge Q = C * V (predictor-corrector method)
    // - Inductor: Flux Φ = L * I (predictor-corrector method)
    // - LTE = max(LTE_voltage, LTE_physical)
    // - CHGTOL = 1e-14 Coulombs (ngspice default)

    // Parameters from Sprint 1 (proven effective)
    config.transient.initial_step = 1e-7;  // 0.1us
    config.transient.max_step = 1e-3;  // 1ms max step
    config.transient.min_step = 1e-12; // 1ps min step
    config.transient.trtol = 2.0;  // Truncation tolerance
    config.transient.lte_abstol = 1e-6;  // Voltage LTE (absolute) - ngspice VNTOL
    config.transient.lte_reltol = 1e-4;  // Voltage LTE (relative) - 0.01% (10x stricter than ngspice)
    config.transient.chgtol = 1e-14;  // Charge tolerance (ngspice default)

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
    std::cout << "\n=== Phase 3.1 Sprint 2: Physical Quantity-Based Error Estimation ===" << std::endl;
    std::cout << "Circuit: RC (R=1k, C=10u, tau=10ms)" << std::endl;
    std::cout << "Method: BDF2/BDF3 with adaptive timestep and order control" << std::endl;
    std::cout << "\nSprint 2 Improvements:" << std::endl;
    std::cout << "  - Physical LTE: Charge Q = C * V (predictor-corrector)" << std::endl;
    std::cout << "  - CHGTOL: 1e-14 Coulombs (ngspice default)" << std::endl;
    std::cout << "  - Combined LTE: max(voltage-based, physical-based)" << std::endl;
    std::cout << "  - Preserves charge conservation for capacitors" << std::endl;
    std::cout << "  - Preserves flux conservation for inductors" << std::endl;
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

    // Success criteria (Sprint 2 target: < 2% error)
    std::cout << "\n=== Sprint 2 Success Criteria ===" << std::endl;
    bool criterion1 = (max_error_ng < 2.0);  // Target: < 2% (down from 6.07% in Sprint 1)
    bool criterion2 = (result.stats.timestep_rejects < 20);  // Target: < 20
    bool criterion3 = (result.timepoints.size() >= 400);  // Should have enough resolution

    std::cout << "✓ Max error vs ngspice < 2%:    " << (criterion1 ? "PASS" : "FAIL")
              << " (" << max_error_ng << "%)" << std::endl;
    std::cout << "✓ Timestep rejects < 20:        " << (criterion2 ? "PASS" : "FAIL")
              << " (" << result.stats.timestep_rejects << ")" << std::endl;
    std::cout << "✓ Sufficient resolution (≥400): " << (criterion3 ? "PASS" : "FAIL")
              << " (" << result.timepoints.size() << ")" << std::endl;

    bool all_passed = criterion1 && criterion2 && criterion3;
    std::cout << "\nOverall: " << (all_passed ? "✅ PASS" : "❌ FAIL") << std::endl;

    // Comparison with Sprint 1
    std::cout << "\n=== Sprint 1 vs Sprint 2 Comparison ===" << std::endl;
    double sprint1_error = 6.07;  // Sprint 1 result
    int sprint1_rejects = 15;     // Sprint 1 result
    double improvement = ((sprint1_error - max_error_ng) / sprint1_error) * 100.0;

    std::cout << "Max error:       Sprint 1: " << sprint1_error << "% → Sprint 2: " << max_error_ng << "%";
    if (max_error_ng < sprint1_error) {
        std::cout << " (✅ " << improvement << "% improvement)" << std::endl;
    } else {
        std::cout << " (⚠️ " << -improvement << "% regression)" << std::endl;
    }

    std::cout << "Timestep rejects: Sprint 1: " << sprint1_rejects << " → Sprint 2: " << result.stats.timestep_rejects;
    if (result.stats.timestep_rejects <= sprint1_rejects) {
        std::cout << " (✅ same or better)" << std::endl;
    } else {
        std::cout << " (⚠️ more rejects)" << std::endl;
    }

    // Write detailed output to file
    std::ofstream outfile("/tmp/kirimaspice_adaptive_physical_output.txt");
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

    std::cout << "\nDetailed output written to /tmp/kirimaspice_adaptive_physical_output.txt" << std::endl;

    return all_passed ? 0 : 1;
}
