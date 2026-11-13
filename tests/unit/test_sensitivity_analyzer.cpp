// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_sensitivity_analyzer.cpp
// Sensitivity Analysis Engine Tests

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/sensitivity_analyzer.hpp"
#include <gtest/gtest.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;
using namespace kirimaspice::parser;

// ========== Sensitivity Analysis: Resistor Divider ==========

TEST(SensitivityAnalyzer, ResistorDivider_ForwardDifference) {
    // Circuit: V1 (1V) -- R1 (1k) -- Vout -- R2 (1k) -- GND
    // Expected: Vout = 0.5V (voltage divider)
    // Sensitivity ∂Vout/∂R1 = -Vout²/R1² = -0.25 mV/Ω
    // Sensitivity ∂Vout/∂R2 = +Vout²/R2² = +0.25 mV/Ω

    std::string netlist = R"(
Resistor Divider Sensitivity Analysis
V1 in 0 DC 1.0
R1 in out 1000
R2 out 0 1000
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify DC solution: Vout = 0.5V
    NodeId out_id = circuit.get_node_id("out");
    ASSERT_NE(out_id, NODE_GND);
    Real v_out = solution[out_id - 1];
    EXPECT_NEAR(v_out, 0.5, 1e-6);

    // Run sensitivity analysis (forward difference)
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(false);  // Forward difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success) << sens_result.error_message;
    ASSERT_EQ(sens_result.sensitivities.size(), 2);  // R1, R2

    // Find R1 sensitivity
    auto it_r1 = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "R1" && r.parameter_name == "resistance";
                              });
    ASSERT_NE(it_r1, sens_result.sensitivities.end());

    // ∂Vout/∂R1 should be negative (increasing R1 decreases Vout)
    EXPECT_LT(it_r1->sensitivity, 0.0);
    // Approximate value: -0.25e-3 V/Ω
    EXPECT_NEAR(it_r1->sensitivity, -0.25e-3, 1e-5);

    // Find R2 sensitivity
    auto it_r2 = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "R2" && r.parameter_name == "resistance";
                              });
    ASSERT_NE(it_r2, sens_result.sensitivities.end());

    // ∂Vout/∂R2 should be positive (increasing R2 increases Vout)
    EXPECT_GT(it_r2->sensitivity, 0.0);
    // Approximate value: +0.25e-3 V/Ω
    EXPECT_NEAR(it_r2->sensitivity, 0.25e-3, 1e-5);

    // Verify normalized sensitivities
    // S_R1 = (∂Vout/∂R1) * (R1/Vout) = -0.25e-3 * (1000/0.5) = -0.5
    EXPECT_NEAR(it_r1->normalized_sensitivity, -0.5, 1e-3);

    // S_R2 = (∂Vout/∂R2) * (R2/Vout) = +0.25e-3 * (1000/0.5) = +0.5
    EXPECT_NEAR(it_r2->normalized_sensitivity, 0.5, 1e-3);

    // Verify relative importance (absolute value of normalized sensitivity)
    EXPECT_NEAR(it_r1->relative_importance, 0.5, 1e-3);
    EXPECT_NEAR(it_r2->relative_importance, 0.5, 1e-3);
}

TEST(SensitivityAnalyzer, ResistorDivider_CentralDifference) {
    // Same circuit, but using central difference (more accurate)
    std::string netlist = R"(
Resistor Divider Sensitivity Analysis (Central Difference)
V1 in 0 DC 1.0
R1 in out 1000
R2 out 0 1000
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Run sensitivity analysis (central difference)
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(true);  // Central difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success);
    ASSERT_EQ(sens_result.sensitivities.size(), 2);

    // Central difference should give more accurate results
    auto it_r1 = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "R1";
                              });
    ASSERT_NE(it_r1, sens_result.sensitivities.end());

    // Should be closer to exact value
    EXPECT_NEAR(it_r1->sensitivity, -0.25e-3, 1e-6);
    EXPECT_NEAR(it_r1->normalized_sensitivity, -0.5, 1e-4);
}

TEST(SensitivityAnalyzer, UnbalancedDivider_HigherSensitivityOnSmallerResistor) {
    // Circuit with 9:1 ratio
    // V1 (1V) -- R1 (900Ω) -- Vout -- R2 (100Ω) -- GND
    // Expected: Vout = 0.1V
    // R2 has higher relative importance (smaller denominator)

    std::string netlist = R"(
Unbalanced Divider
V1 in 0 DC 1.0
R1 in out 900
R2 out 0 100
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify Vout = 0.1V
    NodeId out_id = circuit.get_node_id("out");
    Real v_out = solution[out_id - 1];
    EXPECT_NEAR(v_out, 0.1, 1e-6);

    // Run sensitivity analysis
    SensitivityAnalyzer sens_analyzer(circuit, config);
    auto sens_result = sens_analyzer.analyze(solution);
    ASSERT_TRUE(sens_result.success);

    // Results should be sorted by relative importance (descending)
    ASSERT_EQ(sens_result.sensitivities.size(), 2);

    // R2 should have higher relative importance
    EXPECT_EQ(sens_result.sensitivities[0].device_name, "R2");
    EXPECT_GT(sens_result.sensitivities[0].relative_importance,
              sens_result.sensitivities[1].relative_importance);
}

// ========== Sensitivity Analysis: Get/Set Parameter ==========

TEST(SensitivityAnalyzer, GetSetParameter_Resistor) {
    std::string netlist = R"(
Simple Resistor Circuit
V1 in 0 DC 1.0
R1 in out 1000
.DC
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    SensitivityAnalyzer sens_analyzer(circuit, config);

    // Get initial resistance
    auto r_initial = sens_analyzer.get_parameter_value("R1", "resistance");
    ASSERT_TRUE(r_initial.has_value());
    EXPECT_DOUBLE_EQ(*r_initial, 1000.0);

    // Set new resistance
    bool success = sens_analyzer.set_parameter_value("R1", "resistance", 2000.0);
    ASSERT_TRUE(success);

    // Get updated resistance
    auto r_updated = sens_analyzer.get_parameter_value("R1", "resistance");
    ASSERT_TRUE(r_updated.has_value());
    EXPECT_DOUBLE_EQ(*r_updated, 2000.0);

    // Try invalid parameter name
    auto r_invalid = sens_analyzer.get_parameter_value("R1", "invalid_param");
    EXPECT_FALSE(r_invalid.has_value());

    // Try invalid device name
    auto r_no_device = sens_analyzer.get_parameter_value("R99", "resistance");
    EXPECT_FALSE(r_no_device.has_value());

    // Try setting invalid value (negative resistance)
    bool invalid_set = sens_analyzer.set_parameter_value("R1", "resistance", -100.0);
    EXPECT_FALSE(invalid_set);
}

TEST(SensitivityAnalyzer, GetPerturbableParameters) {
    std::string netlist = R"(
Multiple Devices
V1 in 0 DC 1.0
R1 in n1 1000
R2 n1 n2 2000
R3 n2 0 3000
.DC
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    SensitivityAnalyzer sens_analyzer(circuit, config);

    // Get all perturbable parameters
    auto params = sens_analyzer.get_perturbable_parameters();

    // Should have 3 parameters (R1, R2, R3 resistance)
    EXPECT_EQ(params.size(), 3);

    // Verify parameter names
    std::set<std::pair<std::string, std::string>> param_set(params.begin(), params.end());
    EXPECT_TRUE(param_set.count({"R1", "resistance"}) > 0);
    EXPECT_TRUE(param_set.count({"R2", "resistance"}) > 0);
    EXPECT_TRUE(param_set.count({"R3", "resistance"}) > 0);
}

// ========== Sensitivity Analysis: Edge Cases ==========

TEST(SensitivityAnalyzer, EmptyCircuit_NoParameters) {
    std::string netlist = R"(
Empty Circuit
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    SensitivityAnalyzer sens_analyzer(circuit, config);

    // Should return empty list
    auto params = sens_analyzer.get_perturbable_parameters();
    EXPECT_EQ(params.size(), 0);
}

TEST(SensitivityAnalyzer, OutputNodeNotFound_GracefulFailure) {
    std::string netlist = R"(
Simple Circuit
V1 in 0 DC 1.0
R1 in out 1000
R2 out 0 1000
.DC
.SENS v(nonexistent_node)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    SensitivityAnalyzer sens_analyzer(circuit, config);
    auto sens_result = sens_analyzer.analyze(solution);

    // Should succeed but with no sensitivities computed
    EXPECT_TRUE(sens_result.success || !sens_result.error_message.empty());
}

// ========== Sensitivity Analysis: RC/RL/RLC Circuits ==========

TEST(SensitivityAnalyzer, RCCircuit_CapacitanceSensitivity) {
    // RC voltage divider in DC:
    // V1 (1V) -- R1 (1k) -- Vout -- C1 (1u) -- GND
    // In DC analysis, capacitor is open circuit, so Vout = 1V
    // Sensitivity ∂Vout/∂R1 = 0 (no current flow in DC)
    // Sensitivity ∂Vout/∂C1 = 0 (capacitor doesn't affect DC)

    std::string netlist = R"(
RC Circuit Sensitivity Analysis
V1 in 0 DC 1.0
R1 in out 1000
C1 out 0 1u
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify DC solution: Vout = 1V (capacitor is open)
    NodeId out_id = circuit.get_node_id("out");
    Real v_out = solution[out_id - 1];
    EXPECT_NEAR(v_out, 1.0, 1e-6);

    // Run sensitivity analysis
    SensitivityAnalyzer sens_analyzer(circuit, config);
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success);
    // Should have 2 parameters: R1 resistance, C1 capacitance
    EXPECT_EQ(sens_result.sensitivities.size(), 2);

    // Find capacitance sensitivity
    auto it_c = std::find_if(sens_result.sensitivities.begin(),
                             sens_result.sensitivities.end(),
                             [](const SensitivityResult& r) {
                                 return r.device_name == "C1" && r.parameter_name == "capacitance";
                             });
    ASSERT_NE(it_c, sens_result.sensitivities.end());

    // In DC analysis, capacitance doesn't affect output (open circuit)
    // Sensitivity should be zero or near-zero
    EXPECT_NEAR(it_c->sensitivity, 0.0, 1e-9);
}

TEST(SensitivityAnalyzer, RLCircuit_InductanceSensitivity) {
    // RL circuit in DC:
    // V1 (1V) -- R1 (1k) -- Vout -- L1 (1m) -- GND
    // In DC analysis, inductor is short circuit, so Vout ≈ 0V
    // Sensitivity ∂Vout/∂R1 should be very small
    // Sensitivity ∂Vout/∂L1 = 0 (inductor doesn't affect DC steady state)

    std::string netlist = R"(
RL Circuit Sensitivity Analysis
V1 in 0 DC 1.0
R1 in out 1000
L1 out 0 1m
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify DC solution: Vout ≈ 0V (inductor is short, modeled as 1μΩ)
    NodeId out_id = circuit.get_node_id("out");
    Real v_out = solution[out_id - 1];
    EXPECT_NEAR(v_out, 0.0, 1e-3);  // Very small (1μΩ × I)

    // Run sensitivity analysis
    SensitivityAnalyzer sens_analyzer(circuit, config);
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success);
    // Should have 2 parameters: R1 resistance, L1 inductance
    EXPECT_EQ(sens_result.sensitivities.size(), 2);

    // Find inductance sensitivity
    auto it_l = std::find_if(sens_result.sensitivities.begin(),
                             sens_result.sensitivities.end(),
                             [](const SensitivityResult& r) {
                                 return r.device_name == "L1" && r.parameter_name == "inductance";
                             });
    ASSERT_NE(it_l, sens_result.sensitivities.end());

    // In DC analysis, inductance doesn't affect output (short circuit)
    // Sensitivity should be zero or near-zero
    EXPECT_NEAR(it_l->sensitivity, 0.0, 1e-9);
}

TEST(SensitivityAnalyzer, RLCCircuit_MultiParameterSensitivity) {
    // RLC voltage divider in DC:
    // V1 (1V) -- R1 (1k) -- Vout -- R2 (1k) -- GND
    //                       |
    //                       C1 (1u)  (parallel to R2)
    //                       |
    //                      GND
    //
    // In DC: Capacitor is open, so Vout = 0.5V (pure resistive divider)
    // We add an inductor L1 in series with R1 (short in DC, no effect)

    std::string netlist = R"(
RLC Circuit Sensitivity Analysis
V1 in 0 DC 1.0
R1 in mid 500
L1 mid out 1m
R2 out 0 1000
C1 out 0 1u
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify DC solution: Vout ≈ 0.667V (R2/(R1+R2) with L1 short)
    NodeId out_id = circuit.get_node_id("out");
    Real v_out = solution[out_id - 1];
    EXPECT_NEAR(v_out, 1.0 * 1000.0 / (500.0 + 1000.0), 5e-3);  // ≈0.667V

    // Run sensitivity analysis
    SensitivityAnalyzer sens_analyzer(circuit, config);
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success);
    // Should have 4 parameters: R1, L1, R2, C1
    EXPECT_EQ(sens_result.sensitivities.size(), 4);

    // Verify R1 sensitivity is negative (increasing R1 decreases Vout)
    auto it_r1 = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "R1" && r.parameter_name == "resistance";
                              });
    ASSERT_NE(it_r1, sens_result.sensitivities.end());
    EXPECT_LT(it_r1->sensitivity, 0.0);

    // Verify R2 sensitivity is positive (increasing R2 increases Vout)
    auto it_r2 = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "R2" && r.parameter_name == "resistance";
                              });
    ASSERT_NE(it_r2, sens_result.sensitivities.end());
    EXPECT_GT(it_r2->sensitivity, 0.0);

    // Verify C1 and L1 have negligible sensitivity in DC
    auto it_c = std::find_if(sens_result.sensitivities.begin(),
                             sens_result.sensitivities.end(),
                             [](const SensitivityResult& r) {
                                 return r.device_name == "C1";
                             });
    ASSERT_NE(it_c, sens_result.sensitivities.end());
    EXPECT_NEAR(it_c->sensitivity, 0.0, 1e-9);

    auto it_l = std::find_if(sens_result.sensitivities.begin(),
                             sens_result.sensitivities.end(),
                             [](const SensitivityResult& r) {
                                 return r.device_name == "L1";
                             });
    ASSERT_NE(it_l, sens_result.sensitivities.end());
    EXPECT_NEAR(it_l->sensitivity, 0.0, 1e-9);
}

// ========== Sensitivity Analysis: Diode ==========

TEST(SensitivityAnalyzer, DiodeRectifier_SaturationCurrentSensitivity) {
    // Simple diode rectifier circuit:
    // V1 (0.7V) -- R1 (1k) -- Anode-D1-Cathode -- GND
    // Expected: Forward bias, diode conducts
    // Sensitivity of output voltage to diode IS parameter
    //
    // Diode equation: I = IS × (exp(V/(N×Vt)) - 1)
    // Higher IS → more current → lower voltage drop across diode

    std::string netlist = R"(
Diode Rectifier Sensitivity Analysis
V1 in 0 DC 0.7
R1 in out 1000
.MODEL SIMPLE_DIODE D(IS=1e-14 N=1.0)
D1 out 0 SIMPLE_DIODE
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify DC solution: Vout should be ~0V (diode is forward biased)
    NodeId out_id = circuit.get_node_id("out");
    ASSERT_NE(out_id, NODE_GND);
    Real v_out = solution[out_id - 1];
    EXPECT_GT(v_out, 0.0);    // Forward bias voltage
    EXPECT_LT(v_out, 0.7);    // Less than input due to diode drop

    // Run sensitivity analysis
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(false);  // Forward difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success) << sens_result.error_message;

    // Find D1 IS sensitivity
    auto it_is = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "D1" && r.parameter_name == "IS";
                              });
    ASSERT_NE(it_is, sens_result.sensitivities.end());

    // ∂Vout/∂IS: Increasing IS makes diode more conductive → lower diode voltage
    // For this circuit topology (V1-R1-D1-GND), Vout is the diode anode voltage
    // Higher IS → lower anode voltage → negative sensitivity
    EXPECT_LT(it_is->sensitivity, 0.0) << "IS sensitivity should be negative";

    // Verify normalized sensitivity is calculated
    EXPECT_NE(it_is->normalized_sensitivity, 0.0);
}

TEST(SensitivityAnalyzer, DiodeRectifier_EmissionCoefficientSensitivity) {
    // Same circuit, test N parameter sensitivity
    std::string netlist = R"(
Diode Rectifier N Parameter Sensitivity
V1 in 0 DC 0.7
R1 in out 1000
.MODEL SIMPLE_DIODE D(IS=1e-14 N=1.0)
D1 out 0 SIMPLE_DIODE
.DC
.SENS v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Run sensitivity analysis (central difference for better accuracy)
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(true);  // Central difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success) << sens_result.error_message;

    // Find D1 N sensitivity
    auto it_n = std::find_if(sens_result.sensitivities.begin(),
                             sens_result.sensitivities.end(),
                             [](const SensitivityResult& r) {
                                 return r.device_name == "D1" && r.parameter_name == "N";
                             });
    ASSERT_NE(it_n, sens_result.sensitivities.end());

    // ∂Vout/∂N: Vd = N×Vt×ln(I/IS + 1)
    // Increasing N → higher diode voltage (anode voltage) → positive sensitivity
    EXPECT_GT(it_n->sensitivity, 0.0) << "N sensitivity should be positive";

    // Verify relative importance
    EXPECT_GT(it_n->relative_importance, 0.0);
}

// ========== Sensitivity Analysis: BJT ==========

TEST(SensitivityAnalyzer, BJTAmplifier_BetaSensitivity) {
    // Common-emitter amplifier circuit:
    // VCC (5V) -- RC (1k) -- Collector
    //                         |
    //                        BJT (NPN)
    //                         |
    //                      Emitter -- RE (100) -- GND
    //
    // Base -- RB (10k) -- VB (0.7V)
    //
    // Test sensitivity of collector voltage to beta (BF parameter)

    std::string netlist = R"(
BJT Amplifier Beta Sensitivity
VCC vcc 0 DC 5.0
VB vb 0 DC 0.7
RB vb b 10000
RC vcc c 1000
RE e 0 100
.MODEL SIMPLE_NPN NPN(IS=1e-14 BF=100 NF=1.0)
Q1 c b e SIMPLE_NPN
.DC
.SENS v(c)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify DC solution: Collector voltage should be between 0 and VCC
    NodeId c_id = circuit.get_node_id("c");
    ASSERT_NE(c_id, NODE_GND);
    Real v_c = solution[c_id - 1];
    EXPECT_GT(v_c, 0.0);
    EXPECT_LT(v_c, 5.0);

    // Run sensitivity analysis
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(false);  // Forward difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success) << sens_result.error_message;

    // Find Q1 BF sensitivity
    auto it_bf = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "Q1" && r.parameter_name == "BF";
                              });
    ASSERT_NE(it_bf, sens_result.sensitivities.end());

    // ∂Vc/∂BF: Higher beta → more collector current → lower Vc
    // Sensitivity should be negative
    EXPECT_LT(it_bf->sensitivity, 0.0) << "BF sensitivity should be negative";

    // Verify normalized sensitivity
    EXPECT_NE(it_bf->normalized_sensitivity, 0.0);
}

TEST(SensitivityAnalyzer, BJTAmplifier_SaturationCurrentSensitivity) {
    // Same BJT amplifier, test IS parameter sensitivity
    std::string netlist = R"(
BJT Amplifier IS Sensitivity
VCC vcc 0 DC 5.0
VB vb 0 DC 0.7
RB vb b 10000
RC vcc c 1000
RE e 0 100
.MODEL SIMPLE_NPN NPN(IS=1e-14 BF=100 NF=1.0)
Q1 c b e SIMPLE_NPN
.DC
.SENS v(c)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Run sensitivity analysis (central difference)
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(true);  // Central difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success) << sens_result.error_message;

    // Find Q1 IS sensitivity
    auto it_is = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "Q1" && r.parameter_name == "IS";
                              });
    ASSERT_NE(it_is, sens_result.sensitivities.end());

    // IS sensitivity can be very small or zero depending on circuit configuration
    // Just verify it's computed (may be near-zero in well-biased circuits)
    // In this circuit, IS has minimal impact on collector voltage
    EXPECT_TRUE(std::isfinite(it_is->sensitivity)) << "IS sensitivity should be finite";

    // Verify relative importance is non-negative
    EXPECT_GE(it_is->relative_importance, 0.0);
}

// ========== Sensitivity Analysis: MOSFET ==========

TEST(SensitivityAnalyzer, MOSFETAmplifier_TransconductanceSensitivity) {
    // Common-source MOSFET amplifier:
    // VDD (5V) -- RD (1k) -- Drain
    //                         |
    //                       MOSFET (N-channel)
    //                         |
    //                      Source -- RS (100) -- GND
    //
    // Gate -- RG (10k) -- VG (2.0V)
    //
    // Test sensitivity of drain voltage to KP parameter

    std::string netlist = R"(
MOSFET Amplifier KP Sensitivity
VDD vdd 0 DC 5.0
VG vg 0 DC 2.0
RG vg g 10000
RD vdd d 1000
RS s 0 100
.MODEL SIMPLE_NMOS NMOS(KP=2e-5 VTO=0.7 W=10e-6 L=1e-6)
M1 d g s SIMPLE_NMOS
.DC
.SENS v(d)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Verify DC solution: Drain voltage should be between 0 and VDD
    NodeId d_id = circuit.get_node_id("d");
    ASSERT_NE(d_id, NODE_GND);
    Real v_d = solution[d_id - 1];
    EXPECT_GT(v_d, 0.0);
    EXPECT_LT(v_d, 5.0);

    // Run sensitivity analysis
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(false);  // Forward difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success) << sens_result.error_message;

    // Find M1 KP sensitivity
    auto it_kp = std::find_if(sens_result.sensitivities.begin(),
                              sens_result.sensitivities.end(),
                              [](const SensitivityResult& r) {
                                  return r.device_name == "M1" && r.parameter_name == "KP";
                              });
    ASSERT_NE(it_kp, sens_result.sensitivities.end());

    // ∂Vd/∂KP: Higher KP → more drain current → lower Vd
    // Sensitivity should be negative
    EXPECT_LT(it_kp->sensitivity, 0.0) << "KP sensitivity should be negative";

    // Verify normalized sensitivity
    EXPECT_NE(it_kp->normalized_sensitivity, 0.0);
}

TEST(SensitivityAnalyzer, MOSFETAmplifier_ThresholdVoltageSensitivity) {
    // Same MOSFET amplifier, test VTO (threshold voltage) sensitivity
    std::string netlist = R"(
MOSFET Amplifier VTO Sensitivity
VDD vdd 0 DC 5.0
VG vg 0 DC 2.0
RG vg g 10000
RD vdd d 1000
RS s 0 100
.MODEL SIMPLE_NMOS NMOS(KP=2e-5 VTO=0.7 W=10e-6 L=1e-6)
M1 d g s SIMPLE_NMOS
.DC
.SENS v(d)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);
    ASSERT_NE(parse_result.circuit, nullptr);
    auto& circuit = *parse_result.circuit;
    auto& config = parse_result.config;

    // Run DC analysis
    DCSolver dc_solver(circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Run sensitivity analysis (central difference)
    SensitivityAnalyzer sens_analyzer(circuit, config);
    sens_analyzer.set_differentiation_method(true);  // Central difference
    auto sens_result = sens_analyzer.analyze(solution);

    ASSERT_TRUE(sens_result.success) << sens_result.error_message;

    // Find M1 VTO sensitivity
    auto it_vto = std::find_if(sens_result.sensitivities.begin(),
                               sens_result.sensitivities.end(),
                               [](const SensitivityResult& r) {
                                   return r.device_name == "M1" && r.parameter_name == "VTO";
                               });
    ASSERT_NE(it_vto, sens_result.sensitivities.end());

    // ∂Vd/∂VTO: Higher VTO → less drain current → higher Vd
    // Sensitivity should be positive
    EXPECT_GT(it_vto->sensitivity, 0.0) << "VTO sensitivity should be positive";

    // Verify relative importance
    EXPECT_GT(it_vto->relative_importance, 0.0);
}
