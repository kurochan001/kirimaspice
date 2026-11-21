// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

namespace {

struct SimulationMetrics {
    Real final_error;
    Real mid_error;
    std::uint32_t iterations;
    std::uint32_t rejects;
};

SimulationMetrics simulate_metrics(IntegrationMethod method, Real timestep) {
    CircuitGraph circuit;
    circuit.add_voltage_source("VIN", "vin", "0", 5.0);
    circuit.add_resistor("R1", "vin", "vout", 1000.0);
    circuit.add_capacitor("C1", "vout", "0", 10e-6);

    SimulationConfig config;
    config.transient.use_initial_conditions = true; // ステップ応答
    config.transient.use_adaptive_timestep = false;
    config.transient.method = method;
    config.transient.adaptive_order = false;

    TransientSolver solver(circuit, config);
    Real t_start = 0.0;
    Real t_stop = 20e-3; // 20ms ≈ 2τ

    auto result = solver.solve(t_start, t_stop, timestep);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GE(result.timepoints.size(), 2);

    NodeId node_vout = circuit.get_node_id("vout");
    const auto& last_tp = result.timepoints.back();
    Real v_sim = last_tp.node_voltages[node_vout - 1];

    Real tau = 1000.0 * 10e-6; // 10ms
    Real v_theory = 5.0 * (1.0 - std::exp(-t_stop / tau));

    // Mid-point (t = 10 ms)
    Real t_mid = (t_start + t_stop) * 0.5;
    auto closest_mid = std::min_element(result.timepoints.begin(), result.timepoints.end(),
        [t_mid](const auto& a, const auto& b) {
            return std::abs(a.time - t_mid) < std::abs(b.time - t_mid);
        });

    Real v_mid_sim = closest_mid->node_voltages[node_vout - 1];
    Real v_mid_theory = 5.0 * (1.0 - std::exp(-t_mid / tau));

    SimulationMetrics metrics;
    metrics.final_error = std::abs(v_sim - v_theory);
    metrics.mid_error = std::abs(v_mid_sim - v_mid_theory);
    metrics.iterations = result.stats.total_iterations;
    metrics.rejects = result.stats.timestep_rejects;
    return metrics;
}

} // namespace

TEST(TransientTrBdf2, ReducesErrorRelativeToBackwardEuler) {
    Real timestep = 0.5e-3; // 0.5ms

    auto metrics_trbdf2 = simulate_metrics(IntegrationMethod::TR_BDF2, timestep);
    auto metrics_be = simulate_metrics(IntegrationMethod::BACKWARD_EULER, timestep);

    ASSERT_GT(metrics_be.final_error, 0.0);

    EXPECT_LT(metrics_trbdf2.final_error, metrics_be.final_error * 0.6)
        << "TR-BDF2 final error=" << metrics_trbdf2.final_error
        << ", BE final error=" << metrics_be.final_error;

    EXPECT_LT(metrics_trbdf2.mid_error, metrics_be.mid_error * 1.1)
        << "TR-BDF2 mid error=" << metrics_trbdf2.mid_error
        << ", BE mid error=" << metrics_be.mid_error;

    // TR-BDF2 is a two-stage method, so iterations are expected to be ~2x higher
    EXPECT_LT(metrics_trbdf2.iterations, metrics_be.iterations * 2.5)
        << "TR-BDF2 iterations=" << metrics_trbdf2.iterations
        << ", BE iterations=" << metrics_be.iterations;

    EXPECT_EQ(metrics_trbdf2.rejects, 0u);
    EXPECT_EQ(metrics_be.rejects, 0u);
}

namespace {

// v(out) at 1 ms of 1 kohm and 1 uF (tau = 1 ms) driven by PWL(0 0 1m -1),
// with fixed steps h under TR-BDF2
Real ramp_end_voltage(Real h) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 in 0 PWL(0 0 1m -1)\nR1 in out 1k\nC1 out 0 1u\n.options method=tr_bdf2\n.end\n");
    TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 1e-3, h);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    return result.timepoints.back().node_voltages.at(parsed.circuit->get_node_id("out") - 1);
}

}  // namespace

// The first stage of TR-BDF2 is the trapezoidal rule from t_n to
// t_n + gamma h, so the sources are taken at t_n + gamma h there, and the
// method is of second order for a source that changes with time. For the
// ramp v_in = -t/T into tau = T = 1 ms, v(out) = -(t/T - 1 + exp(-t/tau)),
// -exp(-1) V at 1 ms. The trapezoidal rule errs by 1.5e-5 V here with
// h = 10 us; the error of a second-order method falls by 4 when h halves.
TEST(TransientTrBdf2, SourcesOfTheFirstStageAreAtItsTime) {
    const Real exact = -std::exp(-1.0);
    const Real coarse = std::abs(ramp_end_voltage(10e-6) - exact);
    const Real fine = std::abs(ramp_end_voltage(5e-6) - exact);
    EXPECT_LT(coarse, 5e-5);
    EXPECT_GT(coarse / fine, 3.5);
}
