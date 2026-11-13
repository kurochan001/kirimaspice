// tests/unit/test_multistage_rc.cpp
// Multi-stage RC filter integration test for complex circuits
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <string>

using namespace kirimaspice;

// Test fixture for multi-stage RC filters
class MultistageRCTest : public ::testing::Test {
protected:
    // Create N-stage RC lowpass filter
    // Each stage: R-C to ground
    // Nodes: in -> n1 -> n2 -> ... -> nN (output)
    circuit::CircuitGraph create_multistage_rc(int stages, double R, double C, double V0) {
        circuit::CircuitGraph circuit;
        circuit.add_voltage_source("V1", "in", "0", V0);

        std::string prev_node = "in";
        for (int i = 1; i <= stages; ++i) {
            std::string curr_node = "n" + std::to_string(i);
            std::string r_name = "R" + std::to_string(i);
            std::string c_name = "C" + std::to_string(i);

            circuit.add_resistor(r_name, prev_node, curr_node, R);
            circuit.add_capacitor(c_name, curr_node, "0", C);

            prev_node = curr_node;
        }

        return circuit;
    }

    // Estimate output node ID for N-stage filter
    NodeId get_output_node(const circuit::CircuitGraph& circuit, int stages) {
        std::string output_node_name = "n" + std::to_string(stages);
        return circuit.get_node_id(output_node_name);
    }
};

// Test single-stage RC filter (baseline)
TEST_F(MultistageRCTest, SingleStage_Baseline) {
    double R = 1000.0;   // 1 kΩ
    double C = 1e-6;     // 1 μF
    double V0 = 5.0;     // 5V
    double tau = R * C;  // 1ms

    auto circuit = create_multistage_rc(1, R, C, V0);
    NodeId node_out = get_output_node(circuit, 1);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2;

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 1e-6);  // initial dt=1μs // 5*tau

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // At t = 5*tau, V ≈ V0 * (1 - exp(-5)) = V0 * 0.9933
    double v_final = result.timepoints.back().node_voltages[node_out - 1];
    double v_expected = V0 * (1.0 - std::exp(-5.0));

    EXPECT_NEAR(v_final, v_expected, 0.1) << "Single-stage RC final value";

    std::cout << "1-stage: Final=" << v_final << "V, Expected=" << v_expected << "V" << std::endl;
}

// Test 2-stage RC filter
TEST_F(MultistageRCTest, TwoStage_CascadedResponse) {
    double R = 1000.0;   // 1 kΩ
    double C = 1e-6;     // 1 μF
    double V0 = 5.0;     // 5V

    auto circuit = create_multistage_rc(2, R, C, V0);
    NodeId node_out = get_output_node(circuit, 2);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2;

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 10e-3, 1e-6);  // initial dt=1μs

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // 2-stage filter has slower response than single stage
    // Final value should still approach V0
    double v_final = result.timepoints.back().node_voltages[node_out - 1];

    EXPECT_GT(v_final, V0 * 0.8) << "2-stage should reach at least 80% after 10ms";
    EXPECT_LT(v_final, V0 * 1.01) << "2-stage should not exceed V0";

    std::cout << "2-stage: Final=" << v_final << "V, Steps=" << result.timepoints.size() << std::endl;
}

// Test 3-stage RC filter
TEST_F(MultistageRCTest, ThreeStage_SlowerResponse) {
    double R = 1000.0;   // 1 kΩ
    double C = 1e-6;     // 1 μF
    double V0 = 5.0;     // 5V

    auto circuit = create_multistage_rc(3, R, C, V0);
    NodeId node_out = get_output_node(circuit, 3);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2;

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 15e-3, 1e-6);  // initial dt=1μs

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // 3-stage filter is even slower
    double v_final = result.timepoints.back().node_voltages[node_out - 1];

    EXPECT_GT(v_final, V0 * 0.7) << "3-stage should reach at least 70% after 15ms";
    EXPECT_LT(v_final, V0 * 1.01) << "3-stage should not exceed V0";

    std::cout << "3-stage: Final=" << v_final << "V, Steps=" << result.timepoints.size() << std::endl;
}

// Test 4-stage RC filter (larger system)
TEST_F(MultistageRCTest, FourStage_LargerSystem) {
    double R = 1000.0;   // 1 kΩ
    double C = 1e-6;     // 1 μF
    double V0 = 5.0;     // 5V

    auto circuit = create_multistage_rc(4, R, C, V0);
    NodeId node_out = get_output_node(circuit, 4);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR3; // Use higher order for efficiency

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 20e-3, 1e-6);  // initial dt=1μs

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    double v_final = result.timepoints.back().node_voltages[node_out - 1];

    EXPECT_GT(v_final, V0 * 0.5) << "4-stage should reach at least 50% after 20ms";
    EXPECT_LT(v_final, V0 * 1.01) << "4-stage should not exceed V0";

    std::cout << "4-stage: Final=" << v_final << "V, Steps=" << result.timepoints.size()
              << ", Iterations=" << result.stats.total_iterations << std::endl;
}

// Test comparison: More stages = slower response
TEST_F(MultistageRCTest, CompareStages_IncreasedDelay) {
    double R = 1000.0;   // 1 kΩ
    double C = 1e-6;     // 1 μF
    double V0 = 5.0;     // 5V
    double t_end = 10e-3; // Common simulation time
    double threshold = V0 * 0.3; // 30% of final value

    std::vector<int> stages = {1, 2, 3};
    std::vector<double> times_to_threshold;

    for (int n : stages) {
        auto circuit = create_multistage_rc(n, R, C, V0);
        NodeId node_out = get_output_node(circuit, n);

        SimulationConfig config;
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::GEAR2;
        config.transient.adaptive_order = true;  // Enable adaptive order for stability
        config.transient.use_initial_conditions = true;  // UIC mode for stability

        solvers::TransientSolver solver(circuit, config);
        auto result = solver.solve(0.0, t_end, 1e-6);  // initial dt=1μs

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

        // Find time when output crosses threshold
        double t_cross = -1.0;
        for (const auto& tp : result.timepoints) {
            if (tp.node_voltages[node_out - 1] >= threshold) {
                t_cross = tp.time;
                break;
            }
        }

        ASSERT_GT(t_cross, 0) << n << "-stage should cross threshold";
        times_to_threshold.push_back(t_cross);

        std::cout << n << "-stage: Time to 30% = " << t_cross * 1e3 << " ms" << std::endl;
    }

    // More stages should take longer to reach threshold
    EXPECT_LT(times_to_threshold[0], times_to_threshold[1])
        << "2-stage should be slower than 1-stage";
    EXPECT_LT(times_to_threshold[1], times_to_threshold[2])
        << "3-stage should be slower than 2-stage";
}

// Test stability with different time constants per stage
TEST_F(MultistageRCTest, MixedTimeConstants_Stable) {
    double V0 = 5.0;

    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", V0);

    // Stage 1: Fast (τ = 0.1ms)
    circuit.add_resistor("R1", "in", "n1", 100.0);
    circuit.add_capacitor("C1", "n1", "0", 1e-6);

    // Stage 2: Medium (τ = 1ms)
    circuit.add_resistor("R2", "n1", "n2", 1000.0);
    circuit.add_capacitor("C2", "n2", "0", 1e-6);

    // Stage 3: Slow (τ = 10ms)
    circuit.add_resistor("R3", "n2", "n3", 10000.0);
    circuit.add_capacitor("C3", "n3", "0", 1e-6);

    NodeId node_out = circuit.get_node_id("n3");

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.adaptive_order = true; // Adaptive for stiff system

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 50e-3, 10e-9);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Should be stable despite mixed time constants
    EXPECT_EQ(result.stats.timestep_rejects, 0)
        << "Mixed time constants should be handled without rejects";

    double v_final = result.timepoints.back().node_voltages[node_out - 1];
    EXPECT_GT(v_final, V0 * 0.5) << "Should reach at least 50% after 50ms";

    std::cout << "Mixed τ (0.1, 1, 10 ms): Final=" << v_final << "V, "
              << "Steps=" << result.timepoints.size() << ", "
              << "Rejects=" << result.stats.timestep_rejects << std::endl;
}

// Test 5-stage filter (stress test for larger systems)
TEST_F(MultistageRCTest, FiveStage_StressTest) {
    double R = 100.0;    // 100 Ω (smaller for faster simulation)
    double C = 1e-6;     // 1 μF
    double V0 = 10.0;    // 10V

    auto circuit = create_multistage_rc(5, R, C, V0);
    NodeId node_out = get_output_node(circuit, 5);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR3;
    config.transient.adaptive_order = true;
    config.transient.max_order = 3;

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 5e-3, 10e-9);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Should handle 5-stage system without issues
    EXPECT_GT(result.timepoints.size(), 10) << "Should have reasonable number of steps";
    EXPECT_LT(result.stats.timestep_rejects, result.timepoints.size() * 0.2)
        << "Rejects should be < 20% of total steps";

    double v_final = result.timepoints.back().node_voltages[node_out - 1];
    EXPECT_GT(v_final, 0.0) << "Output should be positive";
    EXPECT_LT(v_final, V0 * 1.01) << "Output should not exceed source voltage";

    std::cout << "5-stage stress test: Final=" << v_final << "V, "
              << "Steps=" << result.timepoints.size() << ", "
              << "Iterations=" << result.stats.total_iterations << ", "
              << "Rejects=" << result.stats.timestep_rejects << std::endl;
}
