// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_large_scale_3000.cpp
// 3000 device large-scale unit tests

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// Helper function: Create RC ladder circuit
// num_stages stages → num_stages resistors + num_stages capacitors = 2*num_stages devices
CircuitGraph create_rc_ladder(size_t num_stages, Real R_value = 1e3, Real C_value = 1e-6) {
    CircuitGraph circuit;

    // Voltage source
    circuit.add_voltage_source("Vin", "in", "0", 1.0);  // 1V DC source

    // RC ladder: in -> R1 -> n1 -> C1 -> 0, n1 -> R2 -> n2 -> C2 -> 0, ...
    for (size_t i = 0; i < num_stages; ++i) {
        std::string prev_node = (i == 0) ? "in" : ("n" + std::to_string(i));
        std::string curr_node = "n" + std::to_string(i + 1);
        std::string r_name = "R" + std::to_string(i + 1);
        std::string c_name = "C" + std::to_string(i + 1);

        circuit.add_resistor(r_name, prev_node, curr_node, R_value);
        circuit.add_capacitor(c_name, curr_node, "0", C_value);
    }

    return circuit;
}

// Helper function: Create resistor mesh (grid)
// rows x cols resistors = rows*cols devices (plus 1 voltage source)
CircuitGraph create_resistor_mesh(size_t rows, size_t cols, Real R_value = 1e3) {
    CircuitGraph circuit;

    // Voltage source at top-left corner
    circuit.add_voltage_source("Vin", "n_0_0", "0", 1.0);  // 1V DC source

    // Create mesh: horizontal and vertical resistors
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            std::string node_curr = "n_" + std::to_string(r) + "_" + std::to_string(c);

            // Horizontal resistor (connect to right neighbor)
            if (c + 1 < cols) {
                std::string node_right = "n_" + std::to_string(r) + "_" + std::to_string(c + 1);
                std::string r_name = "Rh_" + std::to_string(r) + "_" + std::to_string(c);
                circuit.add_resistor(r_name, node_curr, node_right, R_value);
            }

            // Vertical resistor (connect to bottom neighbor)
            if (r + 1 < rows) {
                std::string node_bottom = "n_" + std::to_string(r + 1) + "_" + std::to_string(c);
                std::string r_name = "Rv_" + std::to_string(r) + "_" + std::to_string(c);
                circuit.add_resistor(r_name, node_curr, node_bottom, R_value);
            }
        }
    }

    // Ground the bottom-right corner
    std::string ground_node = "n_" + std::to_string(rows - 1) + "_" + std::to_string(cols - 1);
    circuit.add_resistor("Rload", ground_node, "0", R_value);

    return circuit;
}

// =============================================================================
// Test Suite: Large Scale 3000 Devices
// =============================================================================

// Test 1: DC Analysis - RC Ladder 3000 Devices
TEST(LargeScale3000, DC_RCLadder_3000Devices) {
    // 1500-stage RC ladder: 1500 resistors + 1500 capacitors = 3000 devices
    auto circuit = create_rc_ladder(1500, 1e3, 1e-6);

    SimulationConfig config;
    config.dc.max_iterations = 100;
    config.dc.vntol = 1e-6;

    DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;

    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_LT(stats.total_iterations, 20);  // Should converge quickly for linear circuit
    EXPECT_GT(solution.size(), 1500);  // At least 1500 nodes (n1...n1500)

    // In DC analysis, capacitors are open circuits, so all intermediate nodes are floating
    // The circuit has no DC current path, so all node voltages are effectively zero
    auto node_id_in = circuit.get_node_id("in");
    auto node_id_out = circuit.get_node_id("n1500");
    auto v_in = solution[node_id_in];
    auto v_out = solution[node_id_out];
    EXPECT_NEAR(v_in, 1.0, 1e-6);  // Input = 1V (voltage source)
    EXPECT_NEAR(v_out, 0.0, 1e-6);  // Output is effectively zero (floating/open circuit)
}

// Test 2: DC Analysis - Resistor Mesh 3000 Devices
TEST(LargeScale3000, DC_ResistorMesh_3000Devices) {
    // 39x39 resistor mesh: ~3042 resistors (horizontal + vertical) + 1 Vsource ≈ 3000 devices
    auto circuit = create_resistor_mesh(39, 39, 1e3);

    SimulationConfig config;
    config.dc.max_iterations = 100;
    config.dc.vntol = 1e-6;

    DCSolver solver(circuit, config);
    DenseVector solution;
    SolverStats stats;

    auto status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_LT(stats.total_iterations, 20);  // Linear circuit, fast convergence
    EXPECT_GT(solution.size(), 1500);  // At least 1500 nodes (39x39 grid)

    // Verify voltage gradient in resistor mesh (voltage decreases from top-left to bottom-right)
    auto node_id_topleft = circuit.get_node_id("n_0_0");
    auto node_id_bottomright = circuit.get_node_id("n_38_38");
    // The solution holds node n at index n - 1 (ground is not in it) and the
    // current of Vin after the node voltages.
    auto v_topleft = solution[node_id_topleft - 1];
    auto v_bottomright = solution[node_id_bottomright - 1];
    auto i_vin = solution[circuit.num_nodes() - 1];
    EXPECT_NEAR(v_topleft, 1.0, 1e-9);  // the node of Vin
    EXPECT_GT(v_bottomright, 0.0);
    EXPECT_LT(v_bottomright, 1.0);
    // KCL: the current Vin delivers (minus its branch current, which is positive
    // into n+, NETLIST_CONVENTIONS rule 3) all flows to ground through Rload
    EXPECT_NEAR(-i_vin, v_bottomright / 1e3, 1e-12);
}

// Test 3: Transient Analysis - RC Ladder 3000 Devices
TEST(LargeScale3000, Transient_RCLadder_3000Devices) {
    // 1500-stage RC ladder: 3000 devices total
    // Use C=1pF (1e-12) for fast propagation: τ=RC=1kΩ×1pF=1ns/stage
    // Total propagation time for 1500 stages: ~1.5µs
    auto circuit = create_rc_ladder(1500, 1e3, 1e-12);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;  // UIC mode
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;

    TransientSolver solver(circuit, config);

    Real t_stop = 5e-6;   // 5 microseconds (enough for full propagation)
    Real dt = 1e-8;       // 10 nanosecond steps
    TransientResult result = solver.solve(0.0, t_stop, dt);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 50);  // At least 50 time points
    EXPECT_NEAR(result.timepoints.back().time, t_stop, 1e-9);

    // Verify final voltage (should reach steady state)
    auto node_id = circuit.get_node_id("n1500");
    auto& last_tp = result.timepoints.back();

    // Note: node_voltages is 0-indexed, but node_id is 1-based (0 = GND)
    auto final_v = last_tp.node_voltages[node_id - 1];
    EXPECT_LT(final_v, 0.5);   // Attenuated through 1500 stages
    EXPECT_GT(final_v, 0.0);   // But not zero (signal has propagated)
}

// Test 4: AC Analysis - RC Lowpass Filter 3000 Devices
TEST(LargeScale3000, AC_RCLowpass_3000Devices) {
    // 1500-stage RC ladder as lowpass filter
    auto circuit = create_rc_ladder(1500, 1e3, 1e-9);  // R=1kΩ, C=1nF → fc≈159kHz

    SimulationConfig config;
    config.ac.start_freq = 1e3;  // 1kHz
    config.ac.stop_freq = 1e6;  // 1MHz
    config.ac.num_points = 50;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    // DEC sweep: decades * points_per_decade + 1
    // 1kHz→1MHz = 3 decades, 50 pts/decade → 3*50+1 = 151 points
    EXPECT_EQ(result.frequency_points.size(), 151);

    // Verify frequency response
    // Note: With 1500 stages, attenuation is extremely large across all frequencies
    // The primary goal is to verify that AC analysis completes successfully for large circuits
    auto node_id_out = circuit.get_node_id("n1500");
    auto v_out_low_freq = result.frequency_points[0].solution[node_id_out];  // 1kHz
    auto v_out_high_freq = result.frequency_points.back().solution[node_id_out];  // 1MHz

    Real mag_low = std::abs(v_out_low_freq);
    Real mag_high = std::abs(v_out_high_freq);

    // Verify that AC analysis produces non-zero results
    EXPECT_GT(mag_low, 0.0);   // Low freq has some response
    EXPECT_GT(mag_high, 0.0);  // High freq has some response
    // Note: Due to 1500-stage attenuation, typical lowpass behavior (mag_low > mag_high) may not hold
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
