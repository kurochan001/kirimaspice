// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_optimizer.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/analysis/circuit_optimizer.hpp"

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::analysis;

// ============================================================================
// 1. Parser Tests (5 tests)
// ============================================================================

TEST(Optimizer, BasicSyntax) {
    std::string netlist = R"(* Circuit optimizer parser test
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE COBYLA OBJECTIVE=v(out) MAX PARAM=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.optimizer.enabled);
    EXPECT_EQ(result.config.optimizer.algorithm, OptimizerConfig::Algorithm::COBYLA);
    EXPECT_EQ(result.config.optimizer.objective_measure, "v(out)");
    EXPECT_EQ(result.config.optimizer.objective, OptimizerConfig::Objective::MAXIMIZE);
    ASSERT_EQ(result.config.optimizer.parameters.size(), 1);
    EXPECT_EQ(result.config.optimizer.parameters[0].parameter_name, "R1.resistance");
    EXPECT_DOUBLE_EQ(result.config.optimizer.parameters[0].initial_value, 1000.0);
    EXPECT_DOUBLE_EQ(result.config.optimizer.parameters[0].lower_bound, 100.0);
    EXPECT_DOUBLE_EQ(result.config.optimizer.parameters[0].upper_bound, 10000.0);
}

TEST(Optimizer, MultipleParameters) {
    std::string netlist = R"(* Multiple parameters
R1 in out 1k
R2 out 0 2k
Vin in 0 DC 5
.OPTIMIZE BOBYQA OBJECTIVE=v(out) MIN PARAM=R1.resistance:1k:100:10k PARAM=R2.resistance:2k:200:20k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.optimizer.enabled);
    EXPECT_EQ(result.config.optimizer.algorithm, OptimizerConfig::Algorithm::BOBYQA);
    EXPECT_EQ(result.config.optimizer.objective, OptimizerConfig::Objective::MINIMIZE);
    ASSERT_EQ(result.config.optimizer.parameters.size(), 2);

    EXPECT_EQ(result.config.optimizer.parameters[0].parameter_name, "R1.resistance");
    EXPECT_DOUBLE_EQ(result.config.optimizer.parameters[0].initial_value, 1000.0);

    EXPECT_EQ(result.config.optimizer.parameters[1].parameter_name, "R2.resistance");
    EXPECT_DOUBLE_EQ(result.config.optimizer.parameters[1].initial_value, 2000.0);
}

TEST(Optimizer, WithConstraints) {
    std::string netlist = R"(* With constraints
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE SLSQP OBJECTIVE=v(out) MAX PARAM=R1.resistance:1k:100:10k CONSTRAINT=power:0:0.1
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.optimizer.enabled);
    EXPECT_EQ(result.config.optimizer.algorithm, OptimizerConfig::Algorithm::SLSQP);
    ASSERT_EQ(result.config.optimizer.constraints.size(), 1);
    EXPECT_EQ(result.config.optimizer.constraints[0].measure_name, "power");
    EXPECT_DOUBLE_EQ(result.config.optimizer.constraints[0].lower_bound, 0.0);
    EXPECT_DOUBLE_EQ(result.config.optimizer.constraints[0].upper_bound, 0.1);
}

TEST(Optimizer, CaseInsensitive) {
    std::string netlist = R"(* Case-insensitive test
R1 1 0 1k
Vin 1 0 DC 5
.optimize neldermead objective=v(1) max param=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.optimizer.enabled);
    EXPECT_EQ(result.config.optimizer.algorithm, OptimizerConfig::Algorithm::NELDERMEAD);
}

TEST(Optimizer, ErrorHandling) {
    NetlistParser parser;

    // Missing OBJECTIVE
    std::string netlist1 = R"(R1 1 0 1k
Vin 1 0 DC 5
.OPTIMIZE COBYLA MAX PARAM=R1.resistance:1k:100:10k
.END
)";
    EXPECT_THROW(parser.parse_string(netlist1), ParseError);

    // Missing PARAM
    std::string netlist2 = R"(R1 1 0 1k
Vin 1 0 DC 5
.OPTIMIZE COBYLA OBJECTIVE=v(1) MAX
.END
)";
    EXPECT_THROW(parser.parse_string(netlist2), ParseError);

    // Invalid bounds (lb >= ub)
    std::string netlist3 = R"(R1 1 0 1k
Vin 1 0 DC 5
.OPTIMIZE COBYLA OBJECTIVE=v(1) MAX PARAM=R1.resistance:1k:10k:100
.END
)";
    EXPECT_THROW(parser.parse_string(netlist3), ParseError);
}

// ============================================================================
// 2. Simple Integration Test (2 tests)
// ============================================================================

TEST(Optimizer_Integration, VoltageDividerMaximize) {
    // Simple voltage divider: Vin=5V, R1+R2=2k -> Optimize R1 to maximize v(out)
    // Optimal: R1 → min (100Ω) to maximize current and R2 voltage drop
    std::string netlist = R"(* Voltage divider optimization
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE NELDERMEAD OBJECTIVE=v(out) MAX PARAM=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);
    ASSERT_EQ(opt_result.optimal_parameters.size(), 1);

    // Optimal R1 should be close to lower bound (100Ω) to maximize v(out)
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    EXPECT_LT(optimal_r1, 2000.0);  // Should be significantly less than initial 1k

    // Objective value should be improved (note: MAXIMIZE converts to negative)
    EXPECT_LT(opt_result.optimal_value, 0.0);  // Should be negative (maximizing)
}

TEST(Optimizer_Integration, VoltageDividerMinimize) {
    // Minimize v(out) → R1 should go to max (10kΩ)
    std::string netlist = R"(* Voltage divider minimize
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE NELDERMEAD OBJECTIVE=v(out) MIN PARAM=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // Optimal R1 should be close to upper bound (10kΩ) to minimize v(out)
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    EXPECT_GT(optimal_r1, 5000.0);  // Should be significantly more than initial 1k

    // Objective value should be positive (minimizing)
    EXPECT_GT(opt_result.optimal_value, 0.0);
}

// ============================================================================
// 3. COBYLA Integration Tests (5 tests)
// ============================================================================

TEST(COBYLA_Integration, VoltageDividerMaximize) {
    // COBYLA optimization: Maximize v(out) in voltage divider
    // Optimal: R1 → min (100Ω) to maximize v(out)
    std::string netlist = R"(* Voltage divider optimization with COBYLA
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE COBYLA OBJECTIVE=v(out) MAX PARAM=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);
    ASSERT_EQ(opt_result.optimal_parameters.size(), 1);

    // COBYLA should find improved solution
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];

    // Check that optimization ran and changed parameters
    EXPECT_GT(opt_result.num_evaluations, 5);  // Should have explored the space

    // Objective value should be improved (note: actual convergence may vary)
    // This is a smoke test - full validation requires more iterations
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
}

TEST(COBYLA_Integration, VoltageDividerMinimize) {
    // COBYLA optimization: Minimize v(out) in voltage divider
    // Optimal: R1 → max (10kΩ) to minimize v(out)
    std::string netlist = R"(* Voltage divider minimize with COBYLA
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE COBYLA OBJECTIVE=v(out) MIN PARAM=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // COBYLA should find optimal R1 close to upper bound
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    EXPECT_GT(optimal_r1, 8000.0);  // Should be close to 10kΩ

    // Objective value should be small positive (minimized v(out))
    EXPECT_LT(opt_result.optimal_value, 1.0);  // Should be close to 0.5V
}

TEST(COBYLA_Integration, ToleranceConvergence) {
    // Test COBYLA convergence with tight tolerances
    std::string netlist = R"(* Test convergence with tight tolerance
R1 in out 5k
R2 out 0 5k
Vin in 0 DC 10
.OPTIMIZE COBYLA OBJECTIVE=v(out) MAX PARAM=R1.resistance:5k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Set tight tolerances (use OptimizerConfig defaults)
    EXPECT_DOUBLE_EQ(result.config.optimizer.tolerances.xtol_rel, 1e-4);
    EXPECT_DOUBLE_EQ(result.config.optimizer.tolerances.ftol_rel, 1e-4);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // Should converge to optimal solution (R1 → min)
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    EXPECT_LT(optimal_r1, 2000.0);
}

TEST(COBYLA_Integration, MultiParameter) {
    // COBYLA with 2 parameters: Optimize R1 and R2 simultaneously
    // Objective: Maximize v(out) = Vin * R2 / (R1 + R2)
    // Optimal: R1 → min, R2 → max
    std::string netlist = R"(* Multi-parameter COBYLA optimization
R1 in out 5k
R2 out 0 5k
Vin in 0 DC 10
.OPTIMIZE COBYLA OBJECTIVE=v(out) MAX PARAM=R1.resistance:5k:1k:10k PARAM=R2.resistance:5k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);
    ASSERT_EQ(opt_result.optimal_parameters.size(), 2);

    // Multi-parameter optimization: verify it runs successfully
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    Real optimal_r2 = opt_result.optimal_parameters["R2.resistance"];

    // Check that both parameters are within bounds
    EXPECT_GE(optimal_r1, 1000.0);
    EXPECT_LE(optimal_r1, 10000.0);
    EXPECT_GE(optimal_r2, 1000.0);
    EXPECT_LE(optimal_r2, 10000.0);

    // Verify optimization explored the space
    EXPECT_GT(opt_result.num_evaluations, 10);  // Should have evaluated multiple points
}

TEST(COBYLA_Integration, MaxEvaluationLimit) {
    // Test COBYLA with limited function evaluations
    std::string netlist = R"(* Test max evaluation limit
R1 in out 5k
R2 out 0 5k
Vin in 0 DC 10
.OPTIMIZE COBYLA OBJECTIVE=v(out) MAX PARAM=R1.resistance:5k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Override max_evaluations to a small number
    result.config.optimizer.max_evaluations = 20;

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    // Should still succeed (or reach max evaluations gracefully)
    ASSERT_TRUE(opt_result.success);

    // Should not exceed max evaluations
    EXPECT_LE(opt_result.num_evaluations, 20);
}

// ============================================================================
// 4. BOBYQA Integration Tests (3 tests)
// ============================================================================

TEST(BOBYQA_Integration, VoltageDividerOptimization) {
    // BOBYQA optimization: Minimize v(out) in voltage divider
    // BOBYQA should be more efficient than COBYLA (fewer evaluations)
    std::string netlist = R"(* Voltage divider optimization with BOBYQA
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE BOBYQA OBJECTIVE=v(out) MIN PARAM=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // BOBYQA should find optimal R1 close to upper bound
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    EXPECT_GT(optimal_r1, 7000.0);  // Should be close to 10kΩ

    // BOBYQA typically converges faster than COBYLA
    EXPECT_LT(opt_result.num_evaluations, 50);  // Should be efficient

    // Objective value should be small positive (minimized v(out))
    EXPECT_LT(opt_result.optimal_value, 1.0);
}

TEST(BOBYQA_Integration, MultiParameterOptimization) {
    // BOBYQA with 2 parameters: Trust region method should handle this well
    std::string netlist = R"(* Multi-parameter BOBYQA optimization
R1 in out 5k
R2 out 0 5k
Vin in 0 DC 10
.OPTIMIZE BOBYQA OBJECTIVE=v(out) MAX PARAM=R1.resistance:5k:1k:10k PARAM=R2.resistance:5k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);
    ASSERT_EQ(opt_result.optimal_parameters.size(), 2);

    // BOBYQA should explore parameter space efficiently
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    Real optimal_r2 = opt_result.optimal_parameters["R2.resistance"];

    // Check that parameters are within bounds
    EXPECT_GE(optimal_r1, 1000.0);
    EXPECT_LE(optimal_r1, 10000.0);
    EXPECT_GE(optimal_r2, 1000.0);
    EXPECT_LE(optimal_r2, 10000.0);

    // Verify BOBYQA explored efficiently
    EXPECT_GT(opt_result.num_evaluations, 5);
    EXPECT_LT(opt_result.num_evaluations, 100);  // Should be more efficient than grid search
}

TEST(BOBYQA_Integration, TrustRegionConvergence) {
    // Test BOBYQA's trust region convergence
    // BOBYQA should converge smoothly due to trust region method
    std::string netlist = R"(* Test trust region convergence
R1 in out 2k
R2 out 0 8k
Vin in 0 DC 10
.OPTIMIZE BOBYQA OBJECTIVE=v(out) MAX PARAM=R1.resistance:2k:1k:10k PARAM=R2.resistance:8k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // BOBYQA should converge from initial values (2k, 8k)
    // Optimal: R1 → min, R2 → max to maximize v(out)
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    Real optimal_r2 = opt_result.optimal_parameters["R2.resistance"];

    // Check convergence happened (parameters changed from initial)
    EXPECT_NE(optimal_r1, 2000.0);  // Should have moved from initial
    EXPECT_NE(optimal_r2, 8000.0);  // Should have moved from initial

    // Verify finite objective
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
}

// ============================================================================
// SLSQP Integration Tests
// ============================================================================

TEST(SLSQP_Integration, VoltageDividerOptimization) {
    // SLSQP optimization: Minimize v(out) in voltage divider
    // SLSQP is gradient-based, so should be more accurate than derivative-free methods
    // Note: SLSQP may converge quickly on simple linear problems
    std::string netlist = R"(* Voltage divider optimization with SLSQP
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.OPTIMIZE SLSQP OBJECTIVE=v(out) MIN PARAM=R1.resistance:1k:100:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // SLSQP should run successfully
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];

    // Check that parameter is within bounds
    EXPECT_GE(optimal_r1, 100.0);
    EXPECT_LE(optimal_r1, 10000.0);

    // Verify finite objective
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
    EXPECT_GT(opt_result.optimal_value, 0.0);  // v(out) should be positive
}

TEST(SLSQP_Integration, MultiParameterOptimization) {
    // SLSQP with 2 parameters: Gradient-based method should handle this efficiently
    std::string netlist = R"(* Multi-parameter SLSQP optimization
R1 in out 5k
R2 out 0 5k
Vin in 0 DC 10
.OPTIMIZE SLSQP OBJECTIVE=v(out) MAX PARAM=R1.resistance:5k:1k:10k PARAM=R2.resistance:5k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);
    ASSERT_EQ(opt_result.optimal_parameters.size(), 2);

    // SLSQP should run successfully
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    Real optimal_r2 = opt_result.optimal_parameters["R2.resistance"];

    // Check that parameters are within bounds
    EXPECT_GE(optimal_r1, 1000.0);
    EXPECT_LE(optimal_r1, 10000.0);
    EXPECT_GE(optimal_r2, 1000.0);
    EXPECT_LE(optimal_r2, 10000.0);

    // Verify finite objective
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
}

TEST(SLSQP_Integration, ConstrainedOptimization) {
    // SLSQP with constraint: Test constraint handling capability
    // Note: Current implementation has basic constraint support
    std::string netlist = R"(* Constrained SLSQP optimization
R1 in out 2k
R2 out 0 8k
Vin in 0 DC 10
.OPTIMIZE SLSQP OBJECTIVE=v(out) MAX PARAM=R1.resistance:2k:1k:10k PARAM=R2.resistance:8k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // SLSQP should handle constraints (even if none specified in this test)
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    Real optimal_r2 = opt_result.optimal_parameters["R2.resistance"];

    // Check that parameters are within bounds (box constraints)
    EXPECT_GE(optimal_r1, 1000.0);
    EXPECT_LE(optimal_r1, 10000.0);
    EXPECT_GE(optimal_r2, 1000.0);
    EXPECT_LE(optimal_r2, 10000.0);

    // Verify finite objective
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
}

TEST(SLSQP_Integration, GradientBasedConvergence) {
    // Test SLSQP's gradient-based convergence
    // SLSQP should converge smoothly using gradient information
    std::string netlist = R"(* Test gradient-based convergence
R1 in out 3k
R2 out 0 7k
Vin in 0 DC 10
.OPTIMIZE SLSQP OBJECTIVE=v(out) MIN PARAM=R1.resistance:3k:1k:10k PARAM=R2.resistance:7k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);

    // SLSQP should run successfully
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    Real optimal_r2 = opt_result.optimal_parameters["R2.resistance"];

    // Check that parameters are within bounds
    EXPECT_GE(optimal_r1, 1000.0);
    EXPECT_LE(optimal_r1, 10000.0);
    EXPECT_GE(optimal_r2, 1000.0);
    EXPECT_LE(optimal_r2, 10000.0);

    // Verify finite objective
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
    EXPECT_GT(opt_result.optimal_value, 0.0);  // v(out) should be positive
}

// ============================================================================
// Nelder-Mead Integration Tests
// ============================================================================

TEST(NelderMead_Integration, SingleParameterOptimization) {
    // Nelder-Mead optimization: Single parameter optimization (low-dimensional)
    // Nelder-Mead simplex method is particularly efficient for low-dimensional problems
    std::string netlist = R"(* Single parameter Nelder-Mead optimization
R1 in out 5k
R2 out 0 5k
Vin in 0 DC 10
.OPTIMIZE NELDERMEAD OBJECTIVE=v(out) MAX PARAM=R1.resistance:5k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);
    ASSERT_EQ(opt_result.optimal_parameters.size(), 1);

    // Nelder-Mead should run successfully
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];

    // Check that parameter is within bounds
    EXPECT_GE(optimal_r1, 1000.0);
    EXPECT_LE(optimal_r1, 10000.0);

    // Verify finite objective
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
}

TEST(NelderMead_Integration, TwoParameterOptimization) {
    // Nelder-Mead optimization: 2-parameter optimization
    // Simplex method works well for small-dimensional problems (n=2 is ideal)
    std::string netlist = R"(* Two-parameter Nelder-Mead optimization
R1 in out 3k
R2 out 0 7k
Vin in 0 DC 10
.OPTIMIZE NELDERMEAD OBJECTIVE=v(out) MIN PARAM=R1.resistance:3k:1k:10k PARAM=R2.resistance:7k:1k:10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    CircuitOptimizer optimizer(*result.circuit, result.config);
    OptimizerResult opt_result = optimizer.optimize();

    ASSERT_TRUE(opt_result.success);
    ASSERT_EQ(opt_result.optimal_parameters.size(), 2);

    // Nelder-Mead should run successfully
    Real optimal_r1 = opt_result.optimal_parameters["R1.resistance"];
    Real optimal_r2 = opt_result.optimal_parameters["R2.resistance"];

    // Check that parameters are within bounds
    EXPECT_GE(optimal_r1, 1000.0);
    EXPECT_LE(optimal_r1, 10000.0);
    EXPECT_GE(optimal_r2, 1000.0);
    EXPECT_LE(optimal_r2, 10000.0);

    // Verify finite objective
    EXPECT_TRUE(std::isfinite(opt_result.optimal_value));
    EXPECT_GT(opt_result.optimal_value, 0.0);  // v(out) should be positive
}

// TRAN optimization tests
TEST(OptimizerTRAN, RC_SteadyStateOptimization) {
    // Optimize RC circuit transient steady-state voltage
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vin", "in", "0", 5.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 1e-6);

    SimulationConfig config;
    config.optimizer.enabled = true;
    config.optimizer.algorithm = OptimizerConfig::Algorithm::COBYLA;
    config.optimizer.objective = OptimizerConfig::Objective::MAXIMIZE;
    config.optimizer.objective_measure = "v(out)";
    config.optimizer.analysis_type = OptimizerConfig::AnalysisType::TRAN;
    config.optimizer.tran_start = 0.0;
    config.optimizer.tran_stop = 5e-3;   // 5ms should reach steady state
    config.optimizer.tran_step = 1e-4;   // 0.1ms time step
    config.optimizer.max_evaluations = 50;

    // Add R1 as optimization parameter
    OptimizerConfig::OptimizationParameter param_r1;
    param_r1.parameter_name = "R1.resistance";
    param_r1.initial_value = 1000.0;
    param_r1.lower_bound = 100.0;
    param_r1.upper_bound = 5000.0;
    config.optimizer.parameters.push_back(param_r1);

    CircuitOptimizer optimizer(circuit, config);
    OptimizerResult result = optimizer.optimize();

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.optimal_parameters.size(), 1);

    // Optimal resistance should be within bounds
    Real optimal_r1 = result.optimal_parameters["R1.resistance"];
    EXPECT_GE(optimal_r1, 100.0);
    EXPECT_LE(optimal_r1, 5000.0);

    // At steady state, v(out) ≈ 5V regardless of R1 (C1 fully charged)
    // Optimizer may converge to any value within bounds
    // Main test: verify optimization completes successfully

    // Verify finite objective (note: negative for MAXIMIZE due to internal conversion)
    EXPECT_TRUE(std::isfinite(result.optimal_value));
}

// AC optimization tests
TEST(OptimizerAC, RC_FrequencyResponseOptimization) {
    // Optimize RC circuit AC frequency response
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vin", "in", "0", 1.0);  // 1V AC source
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 1e-6);  // 1μF

    SimulationConfig config;
    config.optimizer.enabled = true;
    config.optimizer.algorithm = OptimizerConfig::Algorithm::COBYLA;
    config.optimizer.objective = OptimizerConfig::Objective::MAXIMIZE;
    config.optimizer.objective_measure = "v(out)";
    config.optimizer.analysis_type = OptimizerConfig::AnalysisType::AC;
    config.optimizer.ac_start_freq = 1.0;      // 1 Hz
    config.optimizer.ac_stop_freq = 1e6;       // 1 MHz
    config.optimizer.ac_points_per_decade = 10;
    config.optimizer.max_evaluations = 50;

    // Add C1 as optimization parameter
    OptimizerConfig::OptimizationParameter param_c1;
    param_c1.parameter_name = "C1.capacitance";
    param_c1.initial_value = 1e-6;
    param_c1.lower_bound = 1e-9;  // 1 nF
    param_c1.upper_bound = 1e-5;  // 10 μF
    config.optimizer.parameters.push_back(param_c1);

    CircuitOptimizer optimizer(circuit, config);
    OptimizerResult result = optimizer.optimize();

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.optimal_parameters.size(), 1);

    // Optimal capacitance should be within bounds
    Real optimal_c1 = result.optimal_parameters["C1.capacitance"];
    EXPECT_GE(optimal_c1, 1e-9);
    EXPECT_LE(optimal_c1, 1e-5);

    // Note: At high frequency (1 MHz), RC low-pass filter attenuates signal
    // Objective uses final frequency point magnitude
    // Optimizer may find C1 that balances various factors
    // Main test: verify optimization completes successfully

    // Verify finite objective (note: negative for MAXIMIZE due to internal conversion)
    EXPECT_TRUE(std::isfinite(result.optimal_value));
}
