// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_rlc_uic.cpp
// RLC series circuit with UIC (Use Initial Conditions)
// Observes underdamped oscillation in step response
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace kirimaspice;

int main() {
    // RLC series circuit parameters (underdamped: ζ < 1)
    double R = 10.0;       // 10 Ω
    double L = 1e-3;       // 1 mH
    double C = 10e-6;      // 10 μF
    double V0 = 5.0;       // 5V source

    // Calculate circuit characteristics
    double omega_0 = 1.0 / std::sqrt(L * C);  // Natural frequency (rad/s)
    double f0 = omega_0 / (2.0 * M_PI);       // Natural frequency (Hz)
    double zeta = (R / 2.0) * std::sqrt(C / L); // Damping ratio
    double omega_d = omega_0 * std::sqrt(1.0 - zeta * zeta); // Damped frequency
    double f_d = omega_d / (2.0 * M_PI);      // Damped frequency (Hz)
    double T_d = 1.0 / f_d;                   // Damped period (s)

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n=== RLC Series Circuit with UIC ===" << std::endl;
    std::cout << "R = " << R << " Ω" << std::endl;
    std::cout << "L = " << L * 1e3 << " mH" << std::endl;
    std::cout << "C = " << C * 1e6 << " μF" << std::endl;
    std::cout << "V0 = " << V0 << " V" << std::endl;
    std::cout << "\nCircuit characteristics:" << std::endl;
    std::cout << "  Natural frequency f₀ = " << f0 << " Hz" << std::endl;
    std::cout << "  Damping ratio ζ = " << zeta << std::endl;
    std::cout << "  Damped frequency f_d = " << f_d << " Hz" << std::endl;
    std::cout << "  Damped period T_d = " << T_d * 1e6 << " μs" << std::endl;

    if (zeta >= 1.0) {
        std::cout << "  Warning: Overdamped (ζ ≥ 1), no oscillation expected" << std::endl;
    } else {
        std::cout << "  Underdamped (ζ < 1), oscillation expected!" << std::endl;
    }

    // Create RLC circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", V0);
    circuit.add_resistor("R1", "in", "n1", R);
    circuit.add_inductor("L1", "n1", "out", L);
    circuit.add_capacitor("C1", "out", "0", C);

    NodeId node_out = circuit.get_node_id("out");

    // Simulation configuration with UIC
    SimulationConfig config;
    config.transient.use_initial_conditions = true;  // UIC: Start from zero
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;  // Use Trapezoidal for oscillations
    config.transient.adaptive_order = false;
    config.transient.initial_step = 1e-9;  // Very small initial step

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 2e-3, 1e-9);  // 0 to 2ms (> 3 periods)

    if (result.status != ConvergenceStatus::SUCCESS) {
        std::cerr << "Simulation failed!" << std::endl;
        return 1;
    }

    std::cout << "\n=== Simulation Results ===" << std::endl;
    std::cout << "Steps: " << result.timepoints.size() << std::endl;
    std::cout << "Iterations: " << result.stats.total_iterations << std::endl;
    std::cout << "Rejects: " << result.stats.timestep_rejects << std::endl;
    std::cout << "Time: " << result.stats.total_time_seconds << " s" << std::endl;

    // Find peaks and valleys (oscillation analysis)
    std::vector<std::pair<double, double>> peaks;   // {time, voltage}
    std::vector<std::pair<double, double>> valleys; // {time, voltage}

    for (size_t i = 1; i < result.timepoints.size() - 1; ++i) {
        double v_prev = result.timepoints[i-1].node_voltages[node_out - 1];
        double v_curr = result.timepoints[i].node_voltages[node_out - 1];
        double v_next = result.timepoints[i+1].node_voltages[node_out - 1];

        // Peak: v_curr > v_prev and v_curr > v_next
        if (v_curr > v_prev && v_curr > v_next && v_curr > V0 * 0.9) {  // Above 90% of final value
            double t = result.timepoints[i].time;
            peaks.push_back({t, v_curr});
        }

        // Valley: v_curr < v_prev and v_curr < v_next
        if (v_curr < v_prev && v_curr < v_next && v_curr < V0 * 1.1) {  // Below 110% of final value
            double t = result.timepoints[i].time;
            valleys.push_back({t, v_curr});
        }
    }

    std::cout << "\n=== Oscillation Analysis ===" << std::endl;
    std::cout << "Peaks found: " << peaks.size() << std::endl;
    std::cout << "Valleys found: " << valleys.size() << std::endl;

    if (peaks.size() >= 2) {
        std::cout << "\nFirst few peaks:" << std::endl;
        std::cout << std::setw(10) << "Time" << " | " << std::setw(10) << "Voltage" << std::endl;
        std::cout << std::string(25, '-') << std::endl;
        for (size_t i = 0; i < std::min(size_t(5), peaks.size()); ++i) {
            std::cout << std::setw(8) << peaks[i].first * 1e6 << "μs | ";
            std::cout << std::setw(10) << peaks[i].second << "V" << std::endl;
        }

        // Measure period from peaks
        double period_measured = peaks[1].first - peaks[0].first;
        double freq_measured = 1.0 / period_measured;
        std::cout << "\nMeasured damped period: " << period_measured * 1e6 << " μs" << std::endl;
        std::cout << "Measured damped frequency: " << freq_measured << " Hz" << std::endl;
        std::cout << "Expected damped frequency: " << f_d << " Hz" << std::endl;
        std::cout << "Frequency error: " << std::abs(freq_measured - f_d) / f_d * 100 << "%" << std::endl;
    }

    if (valleys.size() >= 1) {
        std::cout << "\nFirst few valleys:" << std::endl;
        std::cout << std::setw(10) << "Time" << " | " << std::setw(10) << "Voltage" << std::endl;
        std::cout << std::string(25, '-') << std::endl;
        for (size_t i = 0; i < std::min(size_t(5), valleys.size()); ++i) {
            std::cout << std::setw(8) << valleys[i].first * 1e6 << "μs | ";
            std::cout << std::setw(10) << valleys[i].second << "V" << std::endl;
        }
    }

    // Final value (should approach V0)
    double v_final = result.timepoints.back().node_voltages[node_out - 1];
    std::cout << "\nFinal voltage (t=" << result.timepoints.back().time * 1e3 << "ms): ";
    std::cout << v_final << "V" << std::endl;
    std::cout << "Expected steady-state: " << V0 << "V" << std::endl;
    std::cout << "Error: " << std::abs(v_final - V0) / V0 * 100 << "%" << std::endl;

    if (peaks.size() >= 2 && valleys.size() >= 1) {
        std::cout << "\n✅ SUCCESS: Underdamped oscillation observed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n⚠️  WARNING: Oscillation not clearly detected" << std::endl;
        return 1;
    }
}
