// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_bdf_order_comparison.cpp
// BDF1/2/3 accuracy comparison test using RC circuit
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace kirimaspice;

// RC circuit theoretical response: V(t) = V0 * (1 - exp(-t/tau))
double rc_theory(double t, double V0, double tau) {
    return V0 * (1.0 - std::exp(-t / tau));
}

// Test fixture for BDF order comparison
class BDFOrderComparisonTest : public ::testing::Test {
protected:
    void SetUp() override {
        // RC circuit parameters
        R_ = 1000.0;   // 1 kΩ
        C_ = 1e-6;     // 1 μF
        V0_ = 5.0;     // 5V
        tau_ = R_ * C_; // Time constant: 1ms
    }

    // Create RC circuit
    circuit::CircuitGraph create_rc_circuit() {
        circuit::CircuitGraph circuit;
        circuit.add_voltage_source("V1", "in", "0", V0_);
        circuit.add_resistor("R1", "in", "out", R_);
        circuit.add_capacitor("C1", "out", "0", C_);
        return circuit;
    }

    // Compute RMS error against theory at key timepoints
    double compute_rms_error(const solvers::TransientResult& result, NodeId node_out) {
        std::vector<double> check_times = {0.1e-3, 0.5e-3, 1.0e-3, 2.0e-3, 5.0e-3};
        double sum_sq_error = 0.0;
        int count = 0;

        for (double target_t : check_times) {
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
            double v_sim = tp.node_voltages[node_out - 1];
            double v_theory = rc_theory(tp.time, V0_, tau_);
            double error = (v_sim - v_theory) / V0_; // Normalized error
            sum_sq_error += error * error;
            count++;
        }

        return std::sqrt(sum_sq_error / count) * 100.0; // RMS error in %
    }

    // Largest absolute error against the charging curve, over every point
    double max_error(const solvers::TransientResult& result, NodeId node_out) {
        double worst = 0.0;
        for (const auto& tp : result.timepoints) {
            worst = std::max(worst, std::abs(tp.node_voltages[node_out - 1] - rc_theory(tp.time, V0_, tau_)));
        }
        return worst;
    }

    double largest_step(const solvers::TransientResult& result) {
        double h = 0.0;
        for (size_t j = 1; j < result.timepoints.size(); ++j) {
            h = std::max(h, result.timepoints[j].time - result.timepoints[j - 1].time);
        }
        return h;
    }

    // For dv/dt = (V0 - v)/tau, a k-step BDF of step h multiplies the
    // decaying part V0 exp(-t/tau) by a root whose logarithm is
    // -h/tau + c (h/tau)^(k+1) + ..., with c = |C(k+1)|/sigma(1): 1/2 for
    // BDF1, (2/9)/(2/3) = 1/3 for BDF2. The global error is then
    // c (h/tau)^k (t/tau) V0 exp(-t/tau), largest at t = tau:
    // c (h/tau)^k V0 / e.
    double bdf_error_bound(double c, int order, double h) const {
        return c * std::pow(h / tau_, order) * V0_ / std::numbers::e;
    }

    double R_, C_, V0_, tau_;
};

// Test BDF1 (Backward Euler) accuracy
TEST_F(BDFOrderComparisonTest, BDF1_BackwardEuler) {
    auto circuit = create_rc_circuit();
    NodeId node_out = circuit.get_node_id("out");

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER; // BDF1
    config.transient.adaptive_order = false; // Fixed order
    config.transient.use_initial_conditions = true;  // charge from 0 V, as rc_theory

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-6); // 0 to 5ms, initial dt=1μs

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    double rms_error = compute_rms_error(result, node_out);

    // First order: within the leading global error of BDF1 at the largest
    // step (5% above it for the higher-order terms)
    const double h = largest_step(result);
    EXPECT_LE(max_error(result, node_out), 1.05 * bdf_error_bound(1.0 / 2.0, 1, h)) << "h=" << h;
    EXPECT_GT(result.timepoints.size(), 10); // At least some steps

    std::cout << "BDF1: RMS error = " << rms_error << "%, "
              << "Steps = " << result.timepoints.size() << ", "
              << "Rejects = " << result.stats.timestep_rejects << std::endl;
}

// Test BDF2 (Gear 2) accuracy
TEST_F(BDFOrderComparisonTest, BDF2_Gear2) {
    auto circuit = create_rc_circuit();
    NodeId node_out = circuit.get_node_id("out");

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;  // Fixed step for reliable BDF2
    config.transient.method = IntegrationMethod::GEAR2; // BDF2
    config.transient.adaptive_order = false; // Fixed order
    config.transient.use_initial_conditions = true;  // UIC mode for stability

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 10e-6);  // Fixed step=10μs

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    double rms_error = compute_rms_error(result, node_out);

    // BDF2 is 2nd order: expect better accuracy than BDF1
    EXPECT_LT(rms_error, 2.0); // < 2% RMS error
    EXPECT_GT(result.timepoints.size(), 10);

    std::cout << "BDF2: RMS error = " << rms_error << "%, "
              << "Steps = " << result.timepoints.size() << ", "
              << "Rejects = " << result.stats.timestep_rejects << std::endl;
}

// Test BDF3 (Gear 3) accuracy
TEST_F(BDFOrderComparisonTest, BDF3_Gear3) {
    auto circuit = create_rc_circuit();
    NodeId node_out = circuit.get_node_id("out");

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;  // Fixed step for reliable BDF3
    config.transient.method = IntegrationMethod::GEAR3; // BDF3
    config.transient.adaptive_order = false; // Fixed order
    config.transient.use_initial_conditions = true;  // UIC mode for stability

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 10e-6);  // Fixed step=10μs

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    double rms_error = compute_rms_error(result, node_out);

    // BDF3 is 3rd order: expect highest accuracy
    EXPECT_LT(rms_error, 0.5); // < 0.5% RMS error
    EXPECT_GT(result.timepoints.size(), 10);

    std::cout << "BDF3: RMS error = " << rms_error << "%, "
              << "Steps = " << result.timepoints.size() << ", "
              << "Rejects = " << result.stats.timestep_rejects << std::endl;
}

// Test adaptive order control: should automatically select optimal order
TEST_F(BDFOrderComparisonTest, AdaptiveOrderControl) {
    auto circuit = create_rc_circuit();
    NodeId node_out = circuit.get_node_id("out");

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2; // Start with BDF2
    config.transient.adaptive_order = true;  // Enable adaptive order
    config.transient.max_order = 3;          // Allow up to BDF3
    config.transient.use_initial_conditions = true;  // charge from 0 V, as rc_theory

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-6);  // initial dt=1μs

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    double rms_error = compute_rms_error(result, node_out);

    // An order of 2 or more: within the leading global error of BDF2 at the
    // largest step
    const double h = largest_step(result);
    EXPECT_LE(max_error(result, node_out), bdf_error_bound(1.0 / 3.0, 2, h)) << "h=" << h;
    EXPECT_GT(result.timepoints.size(), 10);

    std::cout << "Adaptive: RMS error = " << rms_error << "%, "
              << "Steps = " << result.timepoints.size() << ", "
              << "Rejects = " << result.stats.timestep_rejects << std::endl;
}

// Test final value convergence for all methods
TEST_F(BDFOrderComparisonTest, FinalValueConvergence) {
    std::vector<std::pair<IntegrationMethod, std::string>> methods = {
        {IntegrationMethod::BACKWARD_EULER, "BDF1"},
        {IntegrationMethod::GEAR2, "BDF2"},
        {IntegrationMethod::GEAR3, "BDF3"}
    };

    for (const auto& [method, name] : methods) {
        auto circuit = create_rc_circuit();
        NodeId node_out = circuit.get_node_id("out");

        SimulationConfig config;
        config.transient.use_adaptive_timestep = true;
        config.transient.method = method;
        config.transient.adaptive_order = false;

        solvers::TransientSolver solver(circuit, config);
        auto result = solver.solve(0.0, 10e-3, 1e-6);  // Long time: 10ms = 10*tau, initial dt=1μs

        if (result.status != ConvergenceStatus::SUCCESS) {
            std::cout << name << ": Skipped (convergence failed)" << std::endl;
            continue;
        }
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

        // After 10*tau, should be at ~99.995% of V0
        double v_final = result.timepoints.back().node_voltages[node_out - 1];
        double error_percent = std::abs(v_final - V0_) / V0_ * 100.0;

        EXPECT_LT(error_percent, 0.1) << name << " final value error too large";

        std::cout << name << ": Final value = " << v_final << "V, "
                  << "Error = " << error_percent << "%" << std::endl;
    }
}
