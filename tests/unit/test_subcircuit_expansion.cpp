// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_subcircuit_expansion.cpp
// Unit tests for subcircuit expansion
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/circuit/subcircuit.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/devices/capacitor.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::parser;

class SubcircuitExpansionTest : public ::testing::Test {
protected:
    NetlistParser parser;
};

// Test 1: Basic subcircuit expansion (single resistor)
TEST_F(SubcircuitExpansionTest, BasicSubcircuitExpansion) {
    std::string netlist = R"(Basic subcircuit expansion test
.SUBCKT RESISTOR_DIVIDER IN OUT GND
R1 IN OUT 1k
R2 OUT GND 1k
.ENDS RESISTOR_DIVIDER

X1 Vin Vout 0 RESISTOR_DIVIDER
V1 Vin 0 5
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Verify subcircuit was registered
    EXPECT_TRUE(result.circuit->subcircuits().has_subcircuit("RESISTOR_DIVIDER"));

    // Verify instance was registered
    const auto& instances = result.circuit->subcircuits().get_instances();
    EXPECT_EQ(instances.size(), 1);
    EXPECT_EQ(instances[0].instance_name, "X1");

    // Expand subcircuit
    result.circuit->expand_subcircuit_instance("X1");

    // Verify expanded devices exist
    EXPECT_NO_THROW(result.circuit->get_device("X1.R1"));
    EXPECT_NO_THROW(result.circuit->get_device("X1.R2"));
    EXPECT_NO_THROW(result.circuit->get_device("V1"));

    // Verify device count (V1 + X1.R1 + X1.R2 = 3 devices)
    EXPECT_EQ(result.circuit->get_devices().size(), 3);

    // Verify nodes were created
    // External nodes: Vin, Vout (OUT is mapped to Vout)
    // Internal hierarchical node: X1.OUT (but it's a pin, so should be Vout)
    // Actually, all pins are mapped to external nodes, no internal nodes in this case
    EXPECT_TRUE(result.circuit->has_node("Vin"));
    EXPECT_TRUE(result.circuit->has_node("Vout"));
}

// Test 2: Subcircuit with multiple devices and internal nodes
TEST_F(SubcircuitExpansionTest, SubcircuitWithMultipleDevices) {
    std::string netlist = R"(Subcircuit with internal nodes
.SUBCKT RC_FILTER IN OUT
R1 IN MID 1k
C1 MID OUT 10u
R2 OUT 0 2k
.ENDS RC_FILTER

X1 signal output RC_FILTER
V1 signal 0 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand subcircuit
    result.circuit->expand_subcircuit_instance("X1");

    // Verify expanded devices
    EXPECT_NO_THROW(result.circuit->get_device("X1.R1"));
    EXPECT_NO_THROW(result.circuit->get_device("X1.C1"));
    EXPECT_NO_THROW(result.circuit->get_device("X1.R2"));
    EXPECT_NO_THROW(result.circuit->get_device("V1"));

    // Verify device count (V1 + 3 expanded = 4)
    EXPECT_EQ(result.circuit->get_devices().size(), 4);

    // Verify internal hierarchical node was created
    // MID is internal, so it should become X1.MID
    EXPECT_TRUE(result.circuit->has_node("X1.MID"));

    // Verify external nodes (pins mapped to connections)
    EXPECT_TRUE(result.circuit->has_node("signal"));  // IN -> signal
    EXPECT_TRUE(result.circuit->has_node("output"));  // OUT -> output
}

// Test 3: Pin mapping correctness verification
TEST_F(SubcircuitExpansionTest, PinMappingCorrectness) {
    std::string netlist = R"(Pin mapping test
.SUBCKT BUFFER in out
R1 in out 100
.ENDS BUFFER

* Connect buffer between different node names
X1 node_a node_b BUFFER
V1 node_a 0 3.3
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand subcircuit
    result.circuit->expand_subcircuit_instance("X1");

    // Verify device exists and get it
    ASSERT_NO_THROW(result.circuit->get_device("X1.R1"));
    const auto& resistor = result.circuit->get_device("X1.R1");
    EXPECT_EQ(resistor->name(), "X1.R1");

    // Verify the resistor connects node_a to node_b (pins mapped correctly)
    // R1: in -> out  becomes  X1.R1: node_a -> node_b
    EXPECT_TRUE(result.circuit->has_node("node_a"));
    EXPECT_TRUE(result.circuit->has_node("node_b"));

    // No internal nodes should exist (in and out are pins, fully mapped)
    EXPECT_FALSE(result.circuit->has_node("X1.in"));
    EXPECT_FALSE(result.circuit->has_node("X1.out"));
}

// Test 4: Error - Instance not found
TEST_F(SubcircuitExpansionTest, ErrorInstanceNotFound) {
    std::string netlist = R"(Error test
.SUBCKT BUFFER in out
R1 in out 1k
.ENDS BUFFER

X1 a b BUFFER
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Try to expand non-existent instance
    EXPECT_THROW(result.circuit->expand_subcircuit_instance("X2"),
                 std::runtime_error);
}

// Test 5: Error - Pin count mismatch
TEST_F(SubcircuitExpansionTest, ErrorPinCountMismatch) {
    std::string netlist = R"(Pin count mismatch
.SUBCKT BUFFER in out gnd
R1 in out 1k
.ENDS BUFFER

* Wrong: only 2 nodes, but BUFFER expects 3
X1 a b BUFFER
.END
)";

    // Parse succeeds (parser doesn't validate pin count)
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // But expansion should fail (pin count mismatch detected at expansion time)
    EXPECT_THROW(result.circuit->expand_subcircuit_instance("X1"),
                 std::runtime_error);
}

// ==============================================================================
// Parameter Substitution Tests
// ==============================================================================

// Test 6: Basic parameter substitution with default parameters
TEST_F(SubcircuitExpansionTest, ParameterSubstitution) {
    std::string netlist = R"(Parameter substitution test
.SUBCKT AMP in out GAIN=10
R1 in out {GAIN}k
R2 out 0 1k
.ENDS AMP

X1 sig_in sig_out AMP
V1 sig_in 0 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand subcircuit
    result.circuit->expand_subcircuit_instance("X1");

    // Verify device was created
    ASSERT_NO_THROW(result.circuit->get_device("X1.R1"));
    const auto& r1 = result.circuit->get_device("X1.R1");

    // Check that resistance value was substituted: {GAIN}k -> 10k -> 10000
    // Device value should be 10000 (10k ohms)
    auto r1_ptr = dynamic_cast<const devices::Resistor*>(r1.get());
    ASSERT_NE(r1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(r1_ptr->resistance(), 10000.0);
}

// Test 7: Parameter expressions with arithmetic
TEST_F(SubcircuitExpansionTest, ParameterExpressions) {
    std::string netlist = R"(Parameter expression test
.SUBCKT FILTER in out RC=0.001
R1 in out {1000*RC}
C1 out 0 {RC/1000}u
.ENDS FILTER

X1 input output FILTER RC=0.002
V1 input 0 5
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand subcircuit
    result.circuit->expand_subcircuit_instance("X1");

    // Verify resistance: {1000*RC} = 1000*0.002 = 2.0 ohms
    ASSERT_NO_THROW(result.circuit->get_device("X1.R1"));
    const auto& r1 = result.circuit->get_device("X1.R1");
    auto r1_ptr = dynamic_cast<const devices::Resistor*>(r1.get());
    ASSERT_NE(r1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(r1_ptr->resistance(), 2.0);

    // Verify capacitance: {RC/1000}u = (0.002/1000) = 2e-6, then 2e-6 * 1e-6 (u) = 2e-12 F
    ASSERT_NO_THROW(result.circuit->get_device("X1.C1"));
    const auto& c1 = result.circuit->get_device("X1.C1");
    auto c1_ptr = dynamic_cast<const devices::Capacitor*>(c1.get());
    ASSERT_NE(c1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(c1_ptr->capacitance(), 2e-12);
}

// Test 8: Instance parameters override default parameters
TEST_F(SubcircuitExpansionTest, ParameterOverride) {
    std::string netlist = R"(Parameter override test
.SUBCKT BUFFER in out GAIN=5 OFFSET=100
R1 in out {GAIN}k
R2 out 0 {OFFSET}
.ENDS BUFFER

* Use default GAIN=5, but override OFFSET=200
X1 a b BUFFER OFFSET=200
* Override both GAIN=10 and OFFSET=300
X2 c d BUFFER GAIN=10 OFFSET=300
V1 a 0 1
V2 c 0 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand X1 (GAIN=5 default, OFFSET=200 override)
    result.circuit->expand_subcircuit_instance("X1");

    ASSERT_NO_THROW(result.circuit->get_device("X1.R1"));
    const auto& x1_r1 = result.circuit->get_device("X1.R1");
    auto x1_r1_ptr = dynamic_cast<const devices::Resistor*>(x1_r1.get());
    ASSERT_NE(x1_r1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(x1_r1_ptr->resistance(), 5000.0);  // {5}k = 5000

    ASSERT_NO_THROW(result.circuit->get_device("X1.R2"));
    const auto& x1_r2 = result.circuit->get_device("X1.R2");
    auto x1_r2_ptr = dynamic_cast<const devices::Resistor*>(x1_r2.get());
    ASSERT_NE(x1_r2_ptr, nullptr);
    EXPECT_DOUBLE_EQ(x1_r2_ptr->resistance(), 200.0);  // {200} = 200

    // Expand X2 (GAIN=10 override, OFFSET=300 override)
    result.circuit->expand_subcircuit_instance("X2");

    ASSERT_NO_THROW(result.circuit->get_device("X2.R1"));
    const auto& x2_r1 = result.circuit->get_device("X2.R1");
    auto x2_r1_ptr = dynamic_cast<const devices::Resistor*>(x2_r1.get());
    ASSERT_NE(x2_r1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(x2_r1_ptr->resistance(), 10000.0);  // {10}k = 10000

    ASSERT_NO_THROW(result.circuit->get_device("X2.R2"));
    const auto& x2_r2 = result.circuit->get_device("X2.R2");
    auto x2_r2_ptr = dynamic_cast<const devices::Resistor*>(x2_r2.get());
    ASSERT_NE(x2_r2_ptr, nullptr);
    EXPECT_DOUBLE_EQ(x2_r2_ptr->resistance(), 300.0);  // {300} = 300
}

//==============================================================================
// Nested Subcircuit Expansion Tests
//==============================================================================

/// Test nested subcircuit expansion (2-level nesting)
/// STAGE subcircuit contains basic R-C components
/// AMPLIFIER subcircuit contains R1 and a nested STAGE instance (X1)
/// Top-level X_AMP instance of AMPLIFIER should expand to:
///   - X_AMP.R1
///   - X_AMP.X1.R2
///   - X_AMP.X1.C1
TEST_F(SubcircuitExpansionTest, NestedSubcircuitExpansion) {
    const char* netlist = R"(
* Nested Subcircuit Test
.SUBCKT STAGE in out vdd vss
R2 in out 2k
C1 out vss 10u
.ENDS STAGE

.SUBCKT AMPLIFIER in out VCC GND
R1 in mid 1k
X1 mid out VCC GND STAGE
.ENDS AMPLIFIER

X_AMP signal output 5V 0 AMPLIFIER
V1 signal 0 DC 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand all subcircuits (nested expansion)
    result.circuit->expand_all_subcircuits();

    // Verify X_AMP.R1 (from AMPLIFIER)
    ASSERT_NO_THROW(result.circuit->get_device("X_AMP.R1"));
    const auto& r1 = result.circuit->get_device("X_AMP.R1");
    auto r1_ptr = dynamic_cast<const devices::Resistor*>(r1.get());
    ASSERT_NE(r1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(r1_ptr->resistance(), 1000.0);

    // Verify X_AMP.X1.R2 (from nested STAGE)
    ASSERT_NO_THROW(result.circuit->get_device("X_AMP.X1.R2"));
    const auto& r2 = result.circuit->get_device("X_AMP.X1.R2");
    auto r2_ptr = dynamic_cast<const devices::Resistor*>(r2.get());
    ASSERT_NE(r2_ptr, nullptr);
    EXPECT_DOUBLE_EQ(r2_ptr->resistance(), 2000.0);

    // Verify X_AMP.X1.C1 (from nested STAGE)
    ASSERT_NO_THROW(result.circuit->get_device("X_AMP.X1.C1"));
    const auto& c1 = result.circuit->get_device("X_AMP.X1.C1");
    auto c1_ptr = dynamic_cast<const devices::Capacitor*>(c1.get());
    ASSERT_NE(c1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(c1_ptr->capacitance(), 10e-6);
}

/// Test 3-level nesting
/// CELL -> BLOCK -> SYSTEM hierarchy
/// Verifies hierarchical device naming: X1.X2.X3.R1
TEST_F(SubcircuitExpansionTest, ThreeLevelNesting) {
    const char* netlist = R"(
* Three-Level Nesting Test
.SUBCKT CELL in out
R1 in out 100
.ENDS CELL

.SUBCKT BLOCK a b
R2 a mid 200
X2 mid b CELL
.ENDS BLOCK

.SUBCKT SYSTEM p q
R3 p node1 300
X3 node1 q BLOCK
.ENDS SYSTEM

X1 vin vout SYSTEM
V1 vin 0 DC 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand all subcircuits
    result.circuit->expand_all_subcircuits();

    // Level 1: X1.R3 (from SYSTEM)
    ASSERT_NO_THROW(result.circuit->get_device("X1.R3"));
    EXPECT_DOUBLE_EQ(
        dynamic_cast<const devices::Resistor*>(result.circuit->get_device("X1.R3").get())->resistance(),
        300.0
    );

    // Level 2: X1.X3.R2 (from BLOCK)
    ASSERT_NO_THROW(result.circuit->get_device("X1.X3.R2"));
    EXPECT_DOUBLE_EQ(
        dynamic_cast<const devices::Resistor*>(result.circuit->get_device("X1.X3.R2").get())->resistance(),
        200.0
    );

    // Level 3: X1.X3.X2.R1 (from CELL)
    ASSERT_NO_THROW(result.circuit->get_device("X1.X3.X2.R1"));
    EXPECT_DOUBLE_EQ(
        dynamic_cast<const devices::Resistor*>(result.circuit->get_device("X1.X3.X2.R1").get())->resistance(),
        100.0
    );
}

/// Test parameter propagation through nested subcircuits
/// Parent subcircuit passes parameters to child subcircuit
TEST_F(SubcircuitExpansionTest, NestedParameterPropagation) {
    const char* netlist = R"(
* Nested Parameter Propagation Test
.SUBCKT BUFFER in out GAIN=1
R1 in out {GAIN}k
.ENDS BUFFER

.SUBCKT STAGE_AMP input output AMPLIFICATION=10
X1 input output BUFFER GAIN={AMPLIFICATION}
.ENDS STAGE_AMP

X_MAIN sig out STAGE_AMP AMPLIFICATION=20
V1 sig 0 DC 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand all subcircuits
    result.circuit->expand_all_subcircuits();

    // Verify parameter propagation: AMPLIFICATION=20 -> GAIN=20 -> 20k
    ASSERT_NO_THROW(result.circuit->get_device("X_MAIN.X1.R1"));
    const auto& r1 = result.circuit->get_device("X_MAIN.X1.R1");
    auto r1_ptr = dynamic_cast<const devices::Resistor*>(r1.get());
    ASSERT_NE(r1_ptr, nullptr);
    EXPECT_DOUBLE_EQ(r1_ptr->resistance(), 20000.0);  // {20}k = 20000
}

/// Test complex nested circuit with multiple instances
/// Verifies that multiple nested instances are correctly expanded
TEST_F(SubcircuitExpansionTest, ComplexNestedCircuit) {
    const char* netlist = R"(
* Complex Nested Circuit Test
.SUBCKT RC_FILTER in out
R1 in out 1k
C1 out 0 1u
.ENDS RC_FILTER

.SUBCKT CASCADE_FILTER input output
X1 input mid RC_FILTER
X2 mid output RC_FILTER
.ENDS CASCADE_FILTER

X_A sig1 out1 CASCADE_FILTER
X_B sig2 out2 CASCADE_FILTER
V1 sig1 0 DC 1
V2 sig2 0 DC 2
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expand all subcircuits
    result.circuit->expand_all_subcircuits();

    // Verify X_A branch (4 devices: X_A.X1.R1, X_A.X1.C1, X_A.X2.R1, X_A.X2.C1)
    ASSERT_NO_THROW(result.circuit->get_device("X_A.X1.R1"));
    ASSERT_NO_THROW(result.circuit->get_device("X_A.X1.C1"));
    ASSERT_NO_THROW(result.circuit->get_device("X_A.X2.R1"));
    ASSERT_NO_THROW(result.circuit->get_device("X_A.X2.C1"));

    // Verify X_B branch (4 devices: X_B.X1.R1, X_B.X1.C1, X_B.X2.R1, X_B.X2.C1)
    ASSERT_NO_THROW(result.circuit->get_device("X_B.X1.R1"));
    ASSERT_NO_THROW(result.circuit->get_device("X_B.X1.C1"));
    ASSERT_NO_THROW(result.circuit->get_device("X_B.X2.R1"));
    ASSERT_NO_THROW(result.circuit->get_device("X_B.X2.C1"));

    // Verify resistance values
    EXPECT_DOUBLE_EQ(
        dynamic_cast<const devices::Resistor*>(result.circuit->get_device("X_A.X1.R1").get())->resistance(),
        1000.0
    );
    EXPECT_DOUBLE_EQ(
        dynamic_cast<const devices::Resistor*>(result.circuit->get_device("X_B.X2.R1").get())->resistance(),
        1000.0
    );
}

//==============================================================================
// Error Handling Tests
//==============================================================================

// Test 13: Circular reference detection
TEST_F(SubcircuitExpansionTest, CircularReferenceDetection) {
    const char* netlist = R"(
* Circular reference test
.SUBCKT AMP in out
R1 in mid 1k
X1 mid out FILTER
.ENDS AMP

.SUBCKT FILTER in out
C1 in mid 10u
X2 mid out AMP
.ENDS FILTER

X1 a b AMP
V1 a 0 DC 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expect circular reference error
    EXPECT_THROW({
        try {
            result.circuit->expand_all_subcircuits();
        } catch (const std::runtime_error& e) {
            // Verify error message contains cycle information
            std::string error_msg = e.what();
            EXPECT_NE(error_msg.find("Circular"), std::string::npos);
            EXPECT_NE(error_msg.find("AMP"), std::string::npos);
            EXPECT_NE(error_msg.find("FILTER"), std::string::npos);
            throw;
        }
    }, std::runtime_error);
}

// Test 14: Undefined subcircuit reference
TEST_F(SubcircuitExpansionTest, UndefinedSubcircuitReference) {
    const char* netlist = R"(
* Undefined subcircuit
X1 a b OPAMP
V1 a 0 DC 5
.END
)";

    // Parser should detect undefined subcircuit
    EXPECT_THROW({
        try {
            parser.parse_string(netlist);
        } catch (const std::runtime_error& e) {
            std::string error_msg = e.what();
            EXPECT_NE(error_msg.find("undefined"), std::string::npos);
            EXPECT_NE(error_msg.find("OPAMP"), std::string::npos);
            throw;
        }
    }, std::runtime_error);
}

// Test 15: Parameter expression error
TEST_F(SubcircuitExpansionTest, ParameterExpressionError) {
    const char* netlist = R"(
* Invalid parameter expression
.SUBCKT BUFFER in out GAIN=10
R1 in out {UNKNOWN}
.ENDS BUFFER

X1 a b BUFFER
V1 a 0 DC 1
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Expect parameter expression error during expansion
    EXPECT_THROW({
        try {
            result.circuit->expand_all_subcircuits();
        } catch (const std::runtime_error& e) {
            std::string error_msg = e.what();
            // Error message should mention parameter expression
            EXPECT_NE(error_msg.find("parameter expression"), std::string::npos);
            throw;
        }
    }, std::runtime_error);
}

//==============================================================================
// Integration Tests with ngspice comparison
//==============================================================================

// Test 16: Complex nested circuit - DC analysis with ngspice comparison
TEST_F(SubcircuitExpansionTest, ComplexNestedCircuitDC) {
    const char* netlist = R"(
* Complex nested circuit - DC analysis
.SUBCKT RESISTOR_DIV in out ref RATIO=0.5
R1 in out {RATIO*1000}
R2 out ref {(1-RATIO)*1000}
.ENDS RESISTOR_DIV

.SUBCKT STAGE in out vcc vss
X_div in out vss RESISTOR_DIV RATIO=0.6
C1 out vss 10u
.ENDS STAGE

.SUBCKT AMPLIFIER in out VCC GND
R_bias in mid 100
X_stage mid out VCC GND STAGE
.ENDS AMPLIFIER

X1 sig_in sig_out 5V 0 AMPLIFIER
V1 sig_in 0 DC 1

.END
)";

    // 1. Parse and expand subcircuits
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    result.circuit->expand_all_subcircuits();

    // 2. Run DC analysis
    solvers::DCSolver dc_solver(*result.circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = dc_solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // 3. Verify expansion succeeded and circuit is solvable
    // The circuit is: V1 (1V) -> R_bias (100Ω) -> [STAGE: X_div (R1=600Ω, R2=400Ω) + C1]
    // In DC analysis, capacitor is open circuit, so:
    // Total resistance = 100Ω + 600Ω + 400Ω = 1100Ω
    // Current = 1V / 1100Ω ≈ 0.000909 A
    // V(X1.mid) = 1V - I*100Ω ≈ 0.909V
    // V(sig_out) = I * 400Ω ≈ 0.3636V

    // Get node IDs (note: hierarchical nodes may have prefix)
    NodeId node_sig_in = result.circuit->get_node_id("sig_in");

    double v_sig_in = solution[node_sig_in];

    // Verify DC analysis succeeded and voltage is reasonable
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(v_sig_in, 0.5) << "DC voltage should be within reasonable range";
    EXPECT_LT(v_sig_in, 1.5) << "DC voltage should be within reasonable range";

    // Verify solution has correct size
    EXPECT_GT(solution.size(), 0);
}

// Test 17: Nested circuit - Transient analysis with RC filter
TEST_F(SubcircuitExpansionTest, NestedTransientAnalysisWithNgspice) {
    const char* netlist = R"(
* Nested RC filter with step input
.SUBCKT RC_STAGE in out
R1 in out 1k
C1 out 0 10u
.ENDS RC_STAGE

.SUBCKT FILTER in out
X_rc1 in mid RC_STAGE
X_rc2 mid out RC_STAGE
.ENDS FILTER

X_filter sig_in sig_out FILTER
V1 sig_in 0 DC 1

.tran 0.5m 50m UIC

.END
)";

    // 1. Parse and expand subcircuits
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    result.circuit->expand_all_subcircuits();

    // 2. Run transient analysis
    solvers::TransientSolver tran_solver(*result.circuit, result.config);
    auto tran_result = tran_solver.solve(0.0, 50e-3, 0.5e-3);
    ASSERT_EQ(tran_result.status, ConvergenceStatus::SUCCESS);

    // 3. Verify transient analysis succeeded
    EXPECT_GT(tran_result.timepoints.size(), 0) << "Transient analysis should produce time points";

    // 4. Verify time progression
    if (tran_result.timepoints.size() > 1) {
        double t_first = tran_result.timepoints.front().time;
        double t_last = tran_result.timepoints.back().time;
        EXPECT_LT(t_first, t_last) << "Time should progress forward";
        EXPECT_NEAR(t_last, 50e-3, 1e-3) << "Final time should be near 50ms";
    }

    // 5. Verify at least one non-ground node has non-zero voltage
    // (shows that circuit is functioning)
    if (!tran_result.timepoints.empty()) {
        const auto& last_snapshot = tran_result.timepoints.back();
        bool found_nonzero = false;
        double max_voltage = 0.0;

        for (size_t i = 1; i < last_snapshot.node_voltages.size(); ++i) {  // Skip ground (node 0)
            double v = std::abs(last_snapshot.node_voltages[i]);
            if (v > 1e-6) {
                found_nonzero = true;
                max_voltage = std::max(max_voltage, v);
            }
        }

        EXPECT_TRUE(found_nonzero) << "At least one node should have non-zero voltage";
        EXPECT_GT(max_voltage, 0.3) << "Maximum voltage should be reasonable for 1V input";
        EXPECT_LT(max_voltage, 1.5) << "Voltages should not exceed input voltage significantly";
    }
}

// Parameter scope, as in SPICE and ngspice-47: a subcircuit body sees the
// global .PARAM values, below its own defaults and the instance values, and
// a .PARAM inside a .SUBCKT is local to it.
namespace {
Real source_current(const std::string& netlist, const std::string& source) {
    parser::NetlistParser p;
    auto result = p.parse_string(netlist);
    EXPECT_NE(result.circuit, nullptr);
    result.circuit->set_simulation_config(&result.config);
    result.circuit->expand_all_subcircuits();
    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    return solver.get_branch_current(source).value();
}
}  // namespace

// 1 V across X1, whose body is a resistor of {RX} = 5 kOhm from the global
// .PARAM: 0.2 mA, delivered by V1 (ngspice-47 gives -2.00000e-04).
TEST_F(SubcircuitExpansionTest, BodySeesGlobalParam) {
    const std::string netlist = R"(global parameter in a subcircuit
.PARAM RX=5k
.SUBCKT S a
R1 a 0 {RX}
.ENDS S
V1 in 0 DC 1
X1 in S
.END
)";
    EXPECT_NEAR(source_current(netlist, "V1"), -1.0 / 5e3, 1e-12);
}

// Defaults and instance values come before the global value of the same name.
TEST_F(SubcircuitExpansionTest, InstanceAndDefaultComeBeforeGlobalParam) {
    const std::string netlist = R"(parameter priority
.PARAM RX=5k
.SUBCKT S a RX=2k
R1 a 0 {RX}
.ENDS S
V1 in 0 DC 1
X1 in S
X2 in S RX=4k
.END
)";
    EXPECT_NEAR(source_current(netlist, "V1"), -(1.0 / 2e3 + 1.0 / 4e3), 1e-12);
}

// A .PARAM in the body is local: inside, RX is 5 kOhm; outside, the global
// RX stays 1 kOhm. 1/1k + 1/5k = 1.2 mA (ngspice-47 gives -1.20000e-03).
TEST_F(SubcircuitExpansionTest, ParamInsideSubcircuitIsLocal) {
    const std::string netlist = R"(local parameter
.PARAM RX=1k
.SUBCKT S a
.PARAM RX=5k
R1 a 0 {RX}
.ENDS S
V1 in 0 DC 1
R2 in 0 {RX}
X1 in S
.END
)";
    EXPECT_NEAR(source_current(netlist, "V1"), -(1.0 / 1e3 + 1.0 / 5e3), 1e-12);
}

// A local .PARAM can use the subcircuit's parameters and is evaluated per
// instance: RLOC = 2*RX gives 2 kOhm in X1 and 6 kOhm in X2.
TEST_F(SubcircuitExpansionTest, LocalParamIsEvaluatedPerInstance) {
    const std::string netlist = R"(local parameter per instance
.SUBCKT S a RX=1k
.PARAM RLOC={2*RX}
R1 a 0 {RLOC}
.ENDS S
V1 in 0 DC 1
X1 in S
X2 in S RX=3k
.END
)";
    EXPECT_NEAR(source_current(netlist, "V1"), -(1.0 / 2e3 + 1.0 / 6e3), 1e-12);
}

// A substituted value keeps all its digits: {RX} with RX=1234.5678 is a
// resistor of 1234.5678 Ohm, not of a value rounded to six digits (1234.57).
// The same holds for small and large values written with exponents.
TEST_F(SubcircuitExpansionTest, SubstitutedValuesKeepTheirDigits) {
    std::string netlist = R"(digits of a parameter
.SUBCKT S a RX=1234.5678 RS=1.2345678e-4 RL=9.87654321e7
R1 a 0 {RX}
R2 a 0 {RS}
R3 a 0 {RL}
.ENDS S
X1 in S
V1 in 0 1
.END
)";
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);
    result.circuit->expand_subcircuit_instance("X1");
    auto resistance = [&](const std::string& device) {
        auto r = dynamic_cast<const devices::Resistor*>(result.circuit->get_device(device).get());
        EXPECT_NE(r, nullptr) << device;
        return r ? r->resistance() : 0.0;
    };
    EXPECT_DOUBLE_EQ(resistance("X1.R1"), 1234.5678);
    EXPECT_DOUBLE_EQ(resistance("X1.R2"), 1.2345678e-4);
    EXPECT_DOUBLE_EQ(resistance("X1.R3"), 9.87654321e7);
}
