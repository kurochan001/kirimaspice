// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_optimizer_tran_objective.cpp
// The transient objective of .OPTIMIZE reads the voltage of the node it
// names. TimePoint::node_voltages leaves ground out, so node k is element
// k - 1, as in the DC and AC objectives.

#include <gtest/gtest.h>

#include "kirimaspice/analysis/circuit_optimizer.hpp"

#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::analysis;

// A chain of three resistors from a 5 V source: in -R1- mid -R2- out -R3- 0.
// With R2 = R3 = 1k, v(mid) = 5 * 2k / (R1 + 2k) (the voltage divider), and
// the final point of a transient of this resistive circuit is that value.
// The objective of the result is v(mid) at the optimal R1, whatever R1 the
// search stops at.
TEST(OptimizerTranObjective, ReadsTheNamedNode) {
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vin", "in", "0", 5.0);
    circuit.add_resistor("R1", "in", "mid", 1000.0);
    circuit.add_resistor("R2", "mid", "out", 1000.0);
    circuit.add_resistor("R3", "out", "0", 1000.0);

    SimulationConfig config;
    config.optimizer.enabled = true;
    config.optimizer.algorithm = OptimizerConfig::Algorithm::NELDERMEAD;
    config.optimizer.objective = OptimizerConfig::Objective::MINIMIZE;
    config.optimizer.objective_measure = "v(mid)";
    config.optimizer.analysis_type = OptimizerConfig::AnalysisType::TRAN;
    config.optimizer.tran_start = 0.0;
    config.optimizer.tran_stop = 1e-6;
    config.optimizer.tran_step = 1e-7;
    config.optimizer.max_evaluations = 10;

    OptimizerConfig::OptimizationParameter r1;
    r1.parameter_name = "R1.resistance";
    r1.initial_value = 1000.0;
    r1.lower_bound = 500.0;
    r1.upper_bound = 4000.0;
    config.optimizer.parameters.push_back(r1);

    CircuitOptimizer optimizer(circuit, config);
    const OptimizerResult result = optimizer.optimize();

    ASSERT_TRUE(result.success);
    const Real r1_opt = result.optimal_parameters.at("R1.resistance");
    const Real v_mid = 5.0 * 2000.0 / (r1_opt + 2000.0);
    EXPECT_NEAR(result.optimal_value, v_mid, 1e-6 * v_mid) << "R1 = " << r1_opt;
}
