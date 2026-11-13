// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_sin_voltage_source.cpp
// Simple test for SIN voltage source with RC circuit

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;

int main() {
    std::cout << "=== SIN Voltage Source Test ===\n";
    std::cout << "Circuit: V1(SIN 0V 5V 60Hz) -> R1(1k) -> C1(10uF)\n";
    std::cout << std::endl;

    // Create RC circuit with SIN voltage source
    // V1: SIN(0V 5V 60Hz) - offset=0V, amplitude=5V, frequency=60Hz
    // R1: 1k ohm
    // C1: 10 uF
    // Time constant: tau = RC = 1000 * 10e-6 = 10ms
    // Period: T = 1/60 = 16.67ms

    circuit::CircuitGraph circuit;
    circuit.add_sin_voltage_source("V1", "in", "0",
                                   0.0,   // offset
                                   5.0,   // amplitude
                                   60.0,  // frequency (Hz)
                                   0.0,   // delay
                                   0.0,   // damping
                                   0.0);  // phase (degrees)
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    NodeId node_in = circuit.get_node_id("in");
    NodeId node_out = circuit.get_node_id("out");

    // Simulation configuration
    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = false;  // Use fixed timestep for simplicity
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;

    // Simulate for 3 periods (50ms)
    double t_start = 0.0;
    double t_stop = 50e-3;  // 50ms = 3 periods
    double timestep = 0.5e-3;  // 0.5ms

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(t_start, t_stop, timestep);

    if (result.status != ConvergenceStatus::SUCCESS) {
        std::cerr << "ERROR: Simulation failed with status "
                  << static_cast<int>(result.status) << std::endl;
        return 1;
    }

    std::cout << "Simulation completed successfully!\n";
    std::cout << "Total time points: " << result.timepoints.size() << std::endl;
    std::cout << "Total iterations: " << result.stats.total_iterations << std::endl;
    std::cout << std::endl;

    // Print some time points to verify SIN behavior
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Time(ms) | V_in(V) | V_out(V) | Expected V_in(V)" << std::endl;
    std::cout << "---------|---------|----------|-----------------" << std::endl;

    for (size_t i = 0; i < result.timepoints.size(); i += 5) {
        const auto& tp = result.timepoints[i];
        double t_ms = tp.time * 1000.0;
        double v_in = tp.node_voltages[node_in];
        double v_out = tp.node_voltages[node_out];

        // Expected input voltage: V(t) = 0 + 5 * sin(2*pi*60*t)
        double omega = 2.0 * M_PI * 60.0;
        double v_in_expected = 5.0 * std::sin(omega * tp.time);

        std::cout << std::setw(8) << t_ms << " | ";
        std::cout << std::setw(7) << v_in << " | ";
        std::cout << std::setw(8) << v_out << " | ";
        std::cout << std::setw(15) << v_in_expected << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Verification ===\n";

    // Check if input voltage matches expected SIN waveform
    bool all_match = true;
    double max_error = 0.0;

    for (const auto& tp : result.timepoints) {
        double omega = 2.0 * M_PI * 60.0;
        double v_in_expected = 5.0 * std::sin(omega * tp.time);
        double v_in_actual = tp.node_voltages[node_in - 1];  // NodeId starts from 1, vector index from 0
        double error = std::abs(v_in_actual - v_in_expected);

        max_error = std::max(max_error, error);

        if (error > 0.01) {  // 10mV tolerance
            all_match = false;
        }
    }

    std::cout << "Maximum error in V_in: " << max_error * 1000.0 << " mV\n";

    if (all_match) {
        std::cout << "✓ SIN voltage source working correctly!\n";
        return 0;
    } else {
        std::cout << "✗ SIN voltage source has errors exceeding tolerance\n";
        return 1;
    }
}
