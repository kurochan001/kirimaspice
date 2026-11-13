#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <vector>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

TEST(TransientAdaptive, PhysicalLteUnlocksStepGrowth) {
    CircuitGraph circuit;
    circuit.add_voltage_source("VIN", "vin", "0", 1.0);
    circuit.add_resistor("R1", "vin", "vout", 1000.0);
    circuit.add_capacitor("C1", "vout", "0", 1e-6);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = true;
    config.transient.initial_step = 1e-7;  // 十分小さい初期ステップ
    config.transient.max_step = 1e-3;
    config.transient.min_step = 1e-9;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.trtol = 2.0;

    TransientSolver solver(circuit, config);

    Real t_start = 0.0;
    Real t_stop = 2e-3; // 2ms (約2τ)

    auto result = solver.solve(t_start, t_stop, config.transient.initial_step);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 25u);

    std::vector<Real> timesteps;
    timesteps.reserve(result.timepoints.size() - 1);
    for (size_t i = 1; i < result.timepoints.size(); ++i) {
        Real dt = result.timepoints[i].time - result.timepoints[i - 1].time;
        timesteps.push_back(dt);
    }

    ASSERT_FALSE(timesteps.empty());

    bool observed_growth = false;
    for (size_t i = 1; i < timesteps.size(); ++i) {
        // history_count が十分大きくなると 1.1 を超える伸びが許可される
        if (i >= 20 && timesteps[i - 1] > 0.0) {
            Real ratio = timesteps[i] / timesteps[i - 1];
            if (ratio > 1.11) {
                observed_growth = true;
                break;
            }
        }
    }

    EXPECT_TRUE(observed_growth);
}

