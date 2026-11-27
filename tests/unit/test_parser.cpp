// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_parser.cpp
#include <gtest/gtest.h>
#include "gtest_output.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/circuit/subcircuit.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/devices/capacitor.hpp"
#include "kirimaspice/devices/inductor.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/devices/current_source.hpp"
#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/mesfet.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <filesystem>
#include <fstream>
#include <fmt/format.h>

using namespace kirimaspice;

//==============================================================================
// ExpressionEvaluator Tests
//==============================================================================
TEST(ExpressionEvaluator, BasicArithmetic) {
    parser::ExpressionEvaluator eval;

    EXPECT_DOUBLE_EQ(eval.evaluate("2 + 3"), 5.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("10 - 4"), 6.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("5 * 6"), 30.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("20 / 4"), 5.0);
}

TEST(ExpressionEvaluator, ScientificFunctions) {
    parser::ExpressionEvaluator eval;

    EXPECT_NEAR(eval.evaluate("sqrt(16)"), 4.0, 1e-10);
    EXPECT_NEAR(eval.evaluate("pow(2, 3)"), 8.0, 1e-10);
    EXPECT_NEAR(eval.evaluate("exp(0)"), 1.0, 1e-10);
    EXPECT_NEAR(eval.evaluate("log(2.718281828)"), 1.0, 1e-6);
}

TEST(ExpressionEvaluator, VariableSubstitution) {
    parser::ExpressionEvaluator eval;

    eval.set_variable("R_val", 1000.0);
    eval.set_variable("C_val", 1e-6);

    EXPECT_DOUBLE_EQ(eval.evaluate("R_val * 2"), 2000.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("R_val + 500"), 1500.0);
    EXPECT_NEAR(eval.evaluate("R_val * C_val"), 1e-3, 1e-10);
}

TEST(ExpressionEvaluator, Constants) {
    parser::ExpressionEvaluator eval;

    EXPECT_NEAR(eval.evaluate("pi"), 3.14159265, 1e-6);
    EXPECT_NEAR(eval.evaluate("e"), 2.71828183, 1e-6);
}

//==============================================================================
// NetlistParser Tests
//==============================================================================
TEST(NetlistParser, SimpleResistor) {
    std::string netlist = R"(Simple Resistor Circuit
R1 in out 1k
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->title(), "Simple Resistor Circuit");
    EXPECT_EQ(circuit->num_nodes(), 3);  // in, out, 0 (GND)
    EXPECT_TRUE(circuit->has_node("in"));
    EXPECT_TRUE(circuit->has_node("out"));
    EXPECT_TRUE(circuit->has_node("0"));  // GND
}

TEST(NetlistParser, RCCircuit) {
    std::string netlist = R"(RC Circuit Test
R1 in out 1k
C1 out 0 1u
Vin in 0 DC 5
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 3);  // in, out, 0
    EXPECT_EQ(circuit->title(), "RC Circuit Test");
}

TEST(NetlistParser, RLCCircuit) {
    std::string netlist = R"(RLC Circuit Test
R1 in mid 100
L1 mid out 1m
C1 out 0 10n
Vin in 0 5
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 4);  // in, mid, out, 0
}

TEST(NetlistParser, CurrentSource) {
    std::string netlist = R"(Current Source Test
Idc 0 out DC 1m
R1 out 0 1k
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 2);  // out, 0
}

TEST(NetlistParser, UnitSuffixes) {
    std::string netlist = R"(Unit Suffix Test
R1 1 0 1k
R2 2 0 1MEG
R3 3 0 100
C1 4 0 1u
C2 5 0 10n
C3 6 0 100p
L1 7 0 1m
L2 8 0 100u
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 9);  // 1-8, 0
}

TEST(NetlistParser, UnitSuffixesWithTrailingUnits) {
    // Ensure prefixes are applied even when physical units follow (e.g., "10mA", "1kHz").
    std::string netlist = R"(
.title Trailing units should not break prefixes
.AC DEC 1 1kHz 10kHz
.STEP param I list 10mA 500uA
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // AC start/stop frequencies should respect the 'k' prefix even with 'Hz' suffix
    EXPECT_DOUBLE_EQ(result.config.ac.start_freq, 1e3);
    EXPECT_DOUBLE_EQ(result.config.ac.stop_freq, 1e4);

    ASSERT_EQ(result.config.step_configs.size(), 1);
    const auto& step = result.config.step_configs[0];
    ASSERT_EQ(step.list_values.size(), 2);
    EXPECT_DOUBLE_EQ(step.list_values[0], 10e-3);   // 10 mA
    EXPECT_DOUBLE_EQ(step.list_values[1], 500e-6);  // 500 uA
}

TEST(NetlistParser, CommentHandling) {
    std::string netlist = R"(Comment Test Circuit
* This is a comment line
R1 in out 1k  ; inline comment
* Another comment
C1 out 0 1u
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 3);  // in, out, 0
}

TEST(NetlistParser, EmptyLines) {
    std::string netlist = R"(Empty Lines Test

R1 in out 1k

C1 out 0 1u

.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 3);
}

TEST(NetlistParser, DCKeyword) {
    std::string netlist = R"(DC Keyword Test
Vin in 0 DC 5
Idc 0 out DC 1m
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 3);  // in, out, 0
}

TEST(NetlistParser, ScientificNotation) {
    std::string netlist = R"(Scientific Notation Test
R1 1 0 1e3
R2 2 0 1.5e6
C1 3 0 1e-6
C2 4 0 2.2e-12
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 5);  // 1-4, 0
}

TEST(NetlistParser, ExpressionEvaluation) {
    std::string netlist = R"(Expression Test
R1 in out {1000 * 2}
C1 out 0 {1e-6 / 2}
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_nodes(), 3);  // in, out, 0
}

TEST(NetlistParser, CaseInsensitivity) {
    std::string netlist = R"(Case Test
r1 in out 1K
C1 OUT 0 1U
VIN In 0 dc 5
.end
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    // Note: node names are case-sensitive, but element types and keywords are not
}

//==============================================================================
// Error Handling Tests
//==============================================================================
TEST(NetlistParser, MissingEndCard) {
    std::string netlist = R"(Missing END
R1 in out 1k
)";

    parser::NetlistParser parser;

    // .END is not needed: the parser reads every line, and .END itself is
    // ignored (REFERENCE_MANUAL, "Directives")
    std::unique_ptr<circuit::CircuitGraph> circuit;
    ASSERT_NO_THROW(circuit = parser.parse_string_legacy(netlist));
    ASSERT_NE(circuit, nullptr);
    auto resistor = std::dynamic_pointer_cast<devices::Resistor>(circuit->get_device("R1"));
    ASSERT_NE(resistor, nullptr);
    EXPECT_DOUBLE_EQ(resistor->resistance(), 1000.0);
}

TEST(NetlistParser, InvalidSyntax) {
    std::string netlist = R"(Invalid Syntax
R1 in
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string_legacy(netlist), parser::ParseError);
}

TEST(NetlistParser, EmptyNetlist) {
    std::string netlist = "";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string_legacy(netlist), std::exception);
}

//==============================================================================
// Node Management Tests
//==============================================================================
TEST(NetlistParser, NodeIDConsistency) {
    std::string netlist = R"(Node Test
R1 a b 1k
R2 b c 1k
R3 c a 1k
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);

    // Same node should have same ID
    NodeId id_a1 = circuit->get_node_id("a");
    NodeId id_b = circuit->get_node_id("b");
    NodeId id_c = circuit->get_node_id("c");

    EXPECT_NE(id_a1, id_b);
    EXPECT_NE(id_b, id_c);
    EXPECT_NE(id_c, id_a1);
}

TEST(NetlistParser, GroundNode) {
    std::string netlist = R"(Ground Test
R1 in 0 1k
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);

    // Ground node should have ID 0
    NodeId gnd_id = circuit->get_node_id("0");
    EXPECT_EQ(gnd_id, NODE_GND);
}

//==============================================================================
// .OPTION Directive Tests
//==============================================================================
TEST(NetlistParser, OptionIntegrationMethod) {
    std::string netlist = R"(
Test .OPTION method
.option method=gear2
V1 in 0 DC 10
R1 in out 1k
C1 out 0 1u
.tran 1u 5m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that integration method was set
    EXPECT_EQ(result.config.transient.method, IntegrationMethod::GEAR2);
}

TEST(NetlistParser, OptionErrorEstimation) {
    std::string netlist = R"(
Test .OPTION error_est
.option error_est=predictor_corrector
V1 in 0 DC 10
R1 in out 1k
C1 out 0 1u
.tran 1u 5m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that error estimation method was set
    EXPECT_EQ(result.config.transient.error_est, ErrorEstimation::PREDICTOR_CORRECTOR);
}

TEST(NetlistParser, OptionTolerances) {
    std::string netlist = R"(
Test .OPTION tolerances
.option trtol=5.0 chgtol=1e-15 abstol=1e-13 reltol=1e-4 vntol=1e-7
V1 in 0 DC 10
R1 in out 1k
C1 out 0 1u
.tran 1u 5m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that tolerances were set
    EXPECT_DOUBLE_EQ(result.config.transient.trtol, 5.0);
    EXPECT_DOUBLE_EQ(result.config.transient.chgtol, 1e-15);
    EXPECT_DOUBLE_EQ(result.config.transient.abstol, 1e-13);
    EXPECT_DOUBLE_EQ(result.config.dc.abstol, 1e-13);
    EXPECT_DOUBLE_EQ(result.config.transient.reltol, 1e-4);
    EXPECT_DOUBLE_EQ(result.config.dc.reltol, 1e-4);
    EXPECT_DOUBLE_EQ(result.config.transient.vntol, 1e-7);
    EXPECT_DOUBLE_EQ(result.config.dc.vntol, 1e-7);
}

TEST(NetlistParser, OptionMultipleParameters) {
    std::string netlist = R"(
Test .OPTION multiple parameters
.option method=trap error_est=combined trtol=3.0
V1 in 0 DC 10
R1 in out 1k
C1 out 0 1u
.tran 1u 5m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that all options were set
    EXPECT_EQ(result.config.transient.method, IntegrationMethod::TRAPEZOIDAL);
    EXPECT_EQ(result.config.transient.error_est, ErrorEstimation::COMBINED);
    EXPECT_DOUBLE_EQ(result.config.transient.trtol, 3.0);
}

TEST(NetlistParser, OptionSynonyms) {
    // Test that various synonyms work correctly
    std::string netlist1 = R"(
Test method synonyms
.option method=trap
V1 in 0 1
R1 in 0 1k
.end
)";

    parser::NetlistParser parser1;
    auto result1 = parser1.parse_string(netlist1);
    EXPECT_EQ(result1.config.transient.method, IntegrationMethod::TRAPEZOIDAL);

    std::string netlist2 = R"(
Test error_est synonyms
.option error_est=pc
V1 in 0 1
R1 in 0 1k
.end
)";

    parser::NetlistParser parser2;
    auto result2 = parser2.parse_string(netlist2);
    EXPECT_EQ(result2.config.transient.error_est, ErrorEstimation::PREDICTOR_CORRECTOR);

    std::string netlist3 = R"(
Test .OPTIONS plural
.options method=bdf2
V1 in 0 1
R1 in 0 1k
.end
)";

    parser::NetlistParser parser3;
    auto result3 = parser3.parse_string(netlist3);
    EXPECT_EQ(result3.config.transient.method, IntegrationMethod::GEAR2);
}

TEST(NetlistParser, OptionCaseInsensitive) {
    std::string netlist = R"(
Test case insensitivity
.OPTION METHOD=GEAR2 ERROR_EST=PHYSICAL
V1 in 0 DC 10
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that case-insensitive parsing works
    EXPECT_EQ(result.config.transient.method, IntegrationMethod::GEAR2);
    EXPECT_EQ(result.config.transient.error_est, ErrorEstimation::PHYSICAL);
}

// New SPICE-compatible options tests

TEST(NetlistParser, OptionIterationLimits) {
    std::string netlist = R"(
Test iteration limits (ITL1, ITL4)
.OPTION ITL1=200 ITL4=50
V1 in 0 DC 10
R1 in out 1k
.tran 1u 1m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that iteration limits were set
    EXPECT_EQ(result.config.dc.max_iterations, 200);
    EXPECT_EQ(result.config.transient.max_iterations, 50);
}

TEST(NetlistParser, OptionTemperature) {
    std::string netlist = R"(
Test temperature setting
.OPTION TEMP=50
V1 in 0 DC 10
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that temperature was set (50°C = 323.15K)
    EXPECT_DOUBLE_EQ(result.config.temperature, 323.15);
}

TEST(NetlistParser, OptionGmin) {
    std::string netlist = R"(
Test minimum conductance
.OPTION GMIN=1e-10
V1 in 0 DC 10
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that GMIN was set
    EXPECT_DOUBLE_EQ(result.config.gmin, 1e-10);
}

// A numeric .OPTIONS value takes a scale factor, as an element value does
// (safe_stod, safe_stoi): ngspice-45.2 reads reltol=10m as 0.01, abstol=1n as
// 1e-9, gmin=1f as 1e-15 and itl1=1k as 1000 (its option command after op,
// run 2026-11-28). Before, each was ignored with a warning. A value that is
// not a number, or not a whole number for an integer option, is still ignored.
TEST(NetlistParser, OptionValuesTakeScaleFactors) {
    parser::NetlistParser parser;
    auto result = parser.parse_string(
        "t\n.options reltol=10m abstol=1n gmin=1f itl1=1k itl4=2K vntol=1U\nV1 a 0 1\nR1 a 0 1k\n.end\n");
    EXPECT_DOUBLE_EQ(result.config.dc.reltol, 0.01);
    EXPECT_DOUBLE_EQ(result.config.transient.reltol, 0.01);
    EXPECT_DOUBLE_EQ(result.config.dc.abstol, 1e-9);
    EXPECT_DOUBLE_EQ(result.config.gmin, 1e-15);
    EXPECT_EQ(result.config.dc.max_iterations, 1000);
    EXPECT_EQ(result.config.transient.max_iterations, 2000);
    EXPECT_DOUBLE_EQ(result.config.dc.vntol, 1e-6);

    const SimulationConfig defaults;
    auto ignored = parser.parse_string("t\n.options reltol=abc itl1=1.5\nV1 a 0 1\nR1 a 0 1k\n.end\n");
    EXPECT_DOUBLE_EQ(ignored.config.dc.reltol, defaults.dc.reltol);
    EXPECT_EQ(ignored.config.dc.max_iterations, defaults.dc.max_iterations);
}

TEST(NetlistParser, OptionMaxord) {
    std::string netlist = R"(
Test maximum BDF order
.OPTION MAXORD=5
V1 in 0 DC 10
R1 in out 1k
.tran 1u 1m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that MAXORD was set
    EXPECT_EQ(result.config.transient.max_order, 5);
}

TEST(NetlistParser, OptionAllNew) {
    std::string netlist = R"(
Test all new options together
.OPTION ITL1=150 ITL4=30 TEMP=25 GMIN=1e-11 MAXORD=4
V1 in 0 DC 10
R1 in out 1k
.tran 1u 1m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check that all options were set correctly
    EXPECT_EQ(result.config.dc.max_iterations, 150);
    EXPECT_EQ(result.config.transient.max_iterations, 30);
    EXPECT_DOUBLE_EQ(result.config.temperature, 298.15); // 25°C in Kelvin
    EXPECT_DOUBLE_EQ(result.config.gmin, 1e-11);
    EXPECT_EQ(result.config.transient.max_order, 4);
}

//==============================================================================
// .INCLUDE Tests
//==============================================================================

// Helper function to create a temporary file with content
std::filesystem::path create_temp_file(const std::string& filename, const std::string& content) {
    std::filesystem::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    std::filesystem::create_directories(temp_dir);

    std::filesystem::path filepath = temp_dir / filename;
    std::ofstream file(filepath);
    file << content;
    file.close();

    return filepath;
}

// Test 1: Simple include (1 test)
TEST(NetlistParser, IncludeSimple) {
    // Create included file
    auto lib_file = create_temp_file("simple.lib", R"(
* Library file
.SUBCKT RESISTOR_DIV in out
R1 in out 1k
R2 out 0 1k
.ENDS
)");

    // Create main netlist that includes the library
    auto main_file = create_temp_file("main_simple.cir",
        fmt::format(R"(
Simple include test
.INCLUDE {}
V1 in 0 DC 10
X1 in out RESISTOR_DIV
.dc
.end
)", lib_file.string()));

    parser::NetlistParser parser;
    auto result = parser.parse_file(main_file.string());

    ASSERT_NE(result.circuit, nullptr);

    // Verify that subcircuit from included file was parsed
    const auto& registry = result.circuit->subcircuits();
    EXPECT_TRUE(registry.has_subcircuit("RESISTOR_DIV"));

    // Cleanup
    std::filesystem::remove(lib_file);
    std::filesystem::remove(main_file);
}

// .INC is the short form of .INCLUDE, in any case, as ngspice-47 reads it
// (a netlist that pulls in a resistor with .inc gives the same result as
// with .include). Without it the file was skipped without a message.
TEST(NetlistParser, IncludeShortForm) {
    auto lib_file = create_temp_file("short_form.lib", "R2 out 0 1k\n");
    auto main_file = create_temp_file("main_short_form.cir",
        fmt::format("Short form of include\nV1 in 0 DC 1\nR1 in out 1k\n.inc {}\n.end\n",
                    lib_file.string()));

    parser::NetlistParser parser;
    auto result = parser.parse_file(main_file.string());
    ASSERT_NE(result.circuit, nullptr);
    EXPECT_NO_THROW(result.circuit->get_device("R2"));

    std::filesystem::remove(lib_file);
    std::filesystem::remove(main_file);
}

// Test 2: Nested include (2 tests)
TEST(NetlistParser, IncludeNested2Levels) {
    // Create level 2 file (innermost)
    auto level2 = create_temp_file("level2.lib", R"(
* Level 2 library
.SUBCKT INNERMOST n1 n2
R_inner n1 n2 100
.ENDS
)");

    // Create level 1 file (includes level 2)
    auto level1 = create_temp_file("level1.lib",
        fmt::format(R"(
* Level 1 library
.INCLUDE {}
.SUBCKT MIDDLE n1 n2
X1 n1 n2 INNERMOST
.ENDS
)", level2.string()));

    // Create main file (includes level 1)
    auto main_file = create_temp_file("main_nested.cir",
        fmt::format(R"(
Nested include test
.INCLUDE {}
V1 in 0 DC 5
X1 in out MIDDLE
.dc
.end
)", level1.string()));

    parser::NetlistParser parser;
    auto result = parser.parse_file(main_file.string());

    ASSERT_NE(result.circuit, nullptr);

    const auto& registry = result.circuit->subcircuits();
    EXPECT_TRUE(registry.has_subcircuit("INNERMOST"));
    EXPECT_TRUE(registry.has_subcircuit("MIDDLE"));

    // Cleanup
    std::filesystem::remove(level2);
    std::filesystem::remove(level1);
    std::filesystem::remove(main_file);
}

TEST(NetlistParser, IncludeNested3Levels) {
    // Create level 3 file
    auto level3 = create_temp_file("level3.lib", R"(
.SUBCKT LEAF n1 n2
C1 n1 n2 1u
.ENDS
)");

    // Create level 2 file
    auto level2 = create_temp_file("level2b.lib",
        fmt::format(R"(
.INCLUDE {}
.SUBCKT BRANCH n1 n2
X1 n1 n2 LEAF
.ENDS
)", level3.string()));

    // Create level 1 file
    auto level1 = create_temp_file("level1b.lib",
        fmt::format(R"(
.INCLUDE {}
.SUBCKT TRUNK n1 n2
X1 n1 n2 BRANCH
.ENDS
)", level2.string()));

    // Create main file
    auto main_file = create_temp_file("main_nested3.cir",
        fmt::format(R"(
3-level nested include test
.INCLUDE {}
V1 in 0 DC 1
X1 in out TRUNK
.dc
.end
)", level1.string()));

    parser::NetlistParser parser;
    auto result = parser.parse_file(main_file.string());

    ASSERT_NE(result.circuit, nullptr);

    const auto& registry = result.circuit->subcircuits();
    EXPECT_TRUE(registry.has_subcircuit("LEAF"));
    EXPECT_TRUE(registry.has_subcircuit("BRANCH"));
    EXPECT_TRUE(registry.has_subcircuit("TRUNK"));

    // Cleanup
    std::filesystem::remove(level3);
    std::filesystem::remove(level2);
    std::filesystem::remove(level1);
    std::filesystem::remove(main_file);
}

// Test 3: Relative path (1 test)
TEST(NetlistParser, IncludeRelativePath) {
    std::filesystem::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    std::filesystem::path models_dir = temp_dir / "models";
    std::filesystem::create_directories(models_dir);

    // Create library in subdirectory
    auto lib_file = models_dir / "passive.lib";
    std::ofstream lib(lib_file);
    lib << R"(
.SUBCKT RC_FILTER in out
R1 in out 1k
C1 out 0 1n
.ENDS
)";
    lib.close();

    // Create main file with relative path
    auto main_file = temp_dir / "main_relative.cir";
    std::ofstream main(main_file);
    main << "Relative path test\n";         // Title line (first line)
    main << ".INCLUDE models/passive.lib\n";
    main << "V1 in 0 DC 0 AC 1\n";          // AC source requires DC value
    main << "X1 in out RC_FILTER\n";
    main << ".ac dec 10 1 1k\n";
    main << ".end\n";
    main.close();

    parser::NetlistParser parser;
    auto result = parser.parse_file(main_file.string());

    ASSERT_NE(result.circuit, nullptr);

    const auto& registry = result.circuit->subcircuits();
    EXPECT_TRUE(registry.has_subcircuit("RC_FILTER"));

    // Cleanup
    std::filesystem::remove(lib_file);
    std::filesystem::remove(main_file);
    std::filesystem::remove(models_dir);
}

// Test 4: Absolute path (1 test)
TEST(NetlistParser, IncludeAbsolutePath) {
    std::filesystem::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    std::filesystem::create_directories(temp_dir);

    // Create library with absolute path
    auto lib_file = temp_dir / "absolute.lib";
    std::ofstream lib(lib_file);
    lib << R"(
.SUBCKT AMP in out
V1 in 0 DC 1
R1 out 0 10k
.ENDS
)";
    lib.close();

    // Create main file using absolute path
    auto main_file = temp_dir / "main_absolute.cir";
    std::ofstream main(main_file);
    main << "Absolute path test\n";         // Title line (first line)
    main << ".INCLUDE " << lib_file.string() << "\n";
    main << "X1 in out AMP\n";
    main << ".dc\n";
    main << ".end\n";
    main.close();

    parser::NetlistParser parser;
    auto result = parser.parse_file(main_file.string());

    ASSERT_NE(result.circuit, nullptr);

    const auto& registry = result.circuit->subcircuits();
    EXPECT_TRUE(registry.has_subcircuit("AMP"));

    // Cleanup
    std::filesystem::remove(lib_file);
    std::filesystem::remove(main_file);
}

// Test 5: Error - File not found (1 test)
TEST(NetlistParser, IncludeFileNotFound) {
    auto main_file = create_temp_file("main_notfound.cir", R"(
File not found test
.INCLUDE /nonexistent/path/to/file.lib
V1 in 0 DC 1
.dc
.end
)");

    parser::NetlistParser parser;

    EXPECT_THROW({
        auto result = parser.parse_file(main_file.string());
    }, parser::ParseError);

    std::filesystem::remove(main_file);
}

// Test 6: Error - Circular include (1 test)
TEST(NetlistParser, IncludeCircular) {
    std::filesystem::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    std::filesystem::create_directories(temp_dir);

    auto file_a = temp_dir / "circular_a.lib";
    auto file_b = temp_dir / "circular_b.lib";

    // Create file A that includes B
    std::ofstream a(file_a);
    a << ".INCLUDE " << file_b.string() << "\n";
    a << ".SUBCKT CIRC_A n1 n2\n";
    a << "R1 n1 n2 1k\n";
    a << ".ENDS\n";
    a.close();

    // Create file B that includes A (circular!)
    std::ofstream b(file_b);
    b << ".INCLUDE " << file_a.string() << "\n";
    b << ".SUBCKT CIRC_B n1 n2\n";
    b << "C1 n1 n2 1u\n";
    b << ".ENDS\n";
    b.close();

    // Main file
    auto main_file = temp_dir / "main_circular.cir";
    std::ofstream main(main_file);
    main << "Circular test\n";         // Title line (first line)
    main << ".INCLUDE " << file_a.string() << "\n";
    main << "V1 in 0 DC 1\n";
    main << ".dc\n";
    main << ".end\n";
    main.close();

    parser::NetlistParser parser;

    EXPECT_THROW({
        auto result = parser.parse_file(main_file.string());
    }, parser::ParseError);

    // Cleanup
    std::filesystem::remove(file_a);
    std::filesystem::remove(file_b);
    std::filesystem::remove(main_file);
}

// Test 7: Error - Depth limit exceeded (1 test)
TEST(NetlistParser, IncludeDepthLimit) {
    std::filesystem::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    std::filesystem::create_directories(temp_dir);

    // Create 12 levels of includes (exceeds limit of 10)
    std::vector<std::filesystem::path> files;

    // First, create all file paths
    for (int i = 0; i <= 11; ++i) {
        files.push_back(temp_dir / fmt::format("depth{}.lib", i));
    }

    // Then create the files with correct include references
    for (int i = 0; i <= 11; ++i) {
        std::ofstream f(files[i]);
        if (i < 11) {
            f << ".INCLUDE " << files[i + 1].string() << "\n";
        }
        f << fmt::format(".SUBCKT DEPTH{} n1 n2\n", i);
        f << "R1 n1 n2 1k\n";
        f << ".ENDS\n";
        f.close();
    }

    // Main file includes depth0.lib (which triggers 12-level chain)
    auto main_file = temp_dir / "main_depth.cir";
    std::ofstream main(main_file);
    main << "Depth limit test\n";         // Title line (first line)
    main << ".INCLUDE " << files[0].string() << "\n";
    main << "V1 in 0 DC 1\n";
    main << ".dc\n";
    main << ".end\n";
    main.close();

    parser::NetlistParser parser;

    EXPECT_THROW({
        auto result = parser.parse_file(main_file.string());
    }, std::runtime_error);

    // Cleanup
    for (const auto& file : files) {
        std::filesystem::remove(file);
    }
    std::filesystem::remove(main_file);
}

//==============================================================================
// .IC/.NODESET Tests
//==============================================================================

// Test 1: .IC parsing
TEST(NetlistParser, InitialConditionIC) {
    std::string netlist = R"(
Test .IC directive
.IC V(out)=2.5 V(n1)=1.2 V(n2)=0.8
V1 in 0 DC 5
R1 in out 1k
C1 out 0 1u
.tran 1u 1m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check IC values
    EXPECT_EQ(result.config.ic.size(), 3);
    EXPECT_DOUBLE_EQ(result.config.ic["out"], 2.5);
    EXPECT_DOUBLE_EQ(result.config.ic["n1"], 1.2);
    EXPECT_DOUBLE_EQ(result.config.ic["n2"], 0.8);
}

// Test 2: .NODESET parsing
TEST(NetlistParser, InitialConditionNODESET) {
    std::string netlist = R"(
Test .NODESET directive
.NODESET V(out)=3.3 V(n1)=1.65
V1 in 0 DC 10
R1 in out 10k
R2 out 0 10k
.dc
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check NODESET values
    EXPECT_EQ(result.config.nodeset.size(), 2);
    EXPECT_DOUBLE_EQ(result.config.nodeset["out"], 3.3);
    EXPECT_DOUBLE_EQ(result.config.nodeset["n1"], 1.65);
}

// Test 3: Both .IC and .NODESET
TEST(NetlistParser, InitialConditionBoth) {
    std::string netlist = R"(
Test both .IC and .NODESET
.NODESET V(n1)=1.0
.IC V(out)=2.0
V1 in 0 DC 5
R1 in n1 1k
C1 n1 out 1u
R2 out 0 1k
.tran 1u 1m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check both
    EXPECT_EQ(result.config.ic.size(), 1);
    EXPECT_DOUBLE_EQ(result.config.ic["out"], 2.0);

    EXPECT_EQ(result.config.nodeset.size(), 1);
    EXPECT_DOUBLE_EQ(result.config.nodeset["n1"], 1.0);
}

// Test 4: .IC with unit suffixes
TEST(NetlistParser, InitialConditionWithUnits) {
    std::string netlist = R"(
Test .IC with units
.IC V(out)=2.5k V(n1)=1.2MEG V(n2)=800m
V1 in 0 DC 5
R1 in out 1k
.tran 1u 1m
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check IC values with unit conversion
    EXPECT_EQ(result.config.ic.size(), 3);
    EXPECT_DOUBLE_EQ(result.config.ic["out"], 2500.0);    // 2.5k = 2500
    EXPECT_DOUBLE_EQ(result.config.ic["n1"], 1.2e6);      // 1.2MEG = 1.2e6
    EXPECT_DOUBLE_EQ(result.config.ic["n2"], 0.8);        // 800m = 0.8
}

// ===========================================================================
// .PARAM/.FUNC Global Parameters Tests
// ===========================================================================

// Test 1: .PARAM with literal values
TEST(NetlistParser, ParamLiteralValues) {
    std::string netlist = R"(
* .PARAM literal values
.PARAM Vcc=5 R_load=1k C_filter=10u
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check global parameters
    EXPECT_EQ(result.config.global_params.size(), 3);
    EXPECT_DOUBLE_EQ(result.config.global_params["Vcc"], 5.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["R_load"], 1000.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["C_filter"], 10e-6);
}

// Test 2: .PARAM with expressions
TEST(NetlistParser, ParamExpressions) {
    std::string netlist = R"(
* .PARAM expressions
.PARAM Vcc=5
.PARAM Vbias={Vcc/2}
.PARAM tau={0.01}
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check parameters
    EXPECT_EQ(result.config.global_params.size(), 3);
    EXPECT_DOUBLE_EQ(result.config.global_params["Vcc"], 5.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["Vbias"], 2.5);  // Vcc/2
    EXPECT_DOUBLE_EQ(result.config.global_params["tau"], 0.01);
}

// Test 3: .PARAM multiple parameters in one line
TEST(NetlistParser, ParamMultipleInLine) {
    std::string netlist = R"(
* .PARAM multiple in line
.PARAM Vdd=3.3 Vss=0 Vref=1.2
V1 in 0 DC 3.3
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check all parameters
    EXPECT_EQ(result.config.global_params.size(), 3);
    EXPECT_DOUBLE_EQ(result.config.global_params["Vdd"], 3.3);
    EXPECT_DOUBLE_EQ(result.config.global_params["Vss"], 0.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["Vref"], 1.2);
}

// Test 4: .FUNC function definition
TEST(NetlistParser, FuncDefinition) {
    std::string netlist = R"(
* .FUNC definition
.FUNC RC(r, c) {r*c}
.FUNC parallel(r1, r2) {(r1*r2)/(r1+r2)}
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check functions are stored
    EXPECT_EQ(result.config.global_funcs.size(), 2);
    EXPECT_TRUE(result.config.global_funcs.find("RC") != result.config.global_funcs.end());
    EXPECT_TRUE(result.config.global_funcs.find("parallel") != result.config.global_funcs.end());

    // Check function data format: "args|expression"
    std::string rc_data = result.config.global_funcs["RC"];
    EXPECT_NE(rc_data.find("r, c"), std::string::npos);
    EXPECT_NE(rc_data.find("r*c"), std::string::npos);
}

// Test 4b: .FUNC with .PARAM - Function calls in parameter expressions
TEST(NetlistParser, FuncWithParamCalls) {
    std::string netlist = R"(
* .FUNC with .PARAM integration
.FUNC RC(r, c) {r*c}
.FUNC parallel(r1, r2) {(r1*r2)/(r1+r2)}
.PARAM R1=1000
.PARAM C1=1e-6
.PARAM tau={RC(R1, C1)}
.PARAM Rp={parallel(1000, 2000)}
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check functions are stored
    EXPECT_EQ(result.config.global_funcs.size(), 2);

    // Check parameters computed using functions
    EXPECT_EQ(result.config.global_params.size(), 4);
    EXPECT_DOUBLE_EQ(result.config.global_params["R1"], 1000.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["C1"], 1e-6);

    // tau = RC(1000, 1e-6) = 1000 * 1e-6 = 1e-3
    EXPECT_NEAR(result.config.global_params["tau"], 1e-3, 1e-9);

    // Rp = parallel(1000, 2000) = (1000*2000)/(1000+2000) = 2000000/3000 = 666.666...
    EXPECT_NEAR(result.config.global_params["Rp"], 2000000.0/3000.0, 1e-6);
}

// Test 4c: .FUNC with single argument
TEST(NetlistParser, FuncSingleArg) {
    std::string netlist = R"(
* .FUNC with single argument
.FUNC square(x) {x*x}
.PARAM val=5
.PARAM result={square(val)}
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check function is stored
    EXPECT_EQ(result.config.global_funcs.size(), 1);

    // Check parameter computed using function
    EXPECT_EQ(result.config.global_params.size(), 2);
    EXPECT_DOUBLE_EQ(result.config.global_params["val"], 5.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["result"], 25.0);  // square(5) = 25
}

// Test 4d: .FUNC with math functions
TEST(NetlistParser, FuncWithMathFunctions) {
    std::string netlist = R"(
* .FUNC with built-in math functions
.FUNC hypotenuse(a, b) {sqrt(a*a + b*b)}
.PARAM x=3
.PARAM y=4
.PARAM h={hypotenuse(x, y)}
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check parameter computed using function with math
    EXPECT_EQ(result.config.global_params.size(), 3);
    EXPECT_DOUBLE_EQ(result.config.global_params["x"], 3.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["y"], 4.0);
    EXPECT_NEAR(result.config.global_params["h"], 5.0, 1e-9);  // hypotenuse(3, 4) = 5
}

// Test 5: .PARAM with dependent parameters
TEST(NetlistParser, ParamDependent) {
    std::string netlist = R"(
* .PARAM dependent
.PARAM Ra=1k
.PARAM Rb=2k
.PARAM Rtotal={Ra+Rb}
.PARAM Rparallel={(Ra*Rb)/(Ra+Rb)}
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check parameters
    EXPECT_EQ(result.config.global_params.size(), 4);
    EXPECT_DOUBLE_EQ(result.config.global_params["Ra"], 1000.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["Rb"], 2000.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["Rtotal"], 3000.0);  // Ra+Rb
    EXPECT_NEAR(result.config.global_params["Rparallel"], 666.666, 1.0);  // (1k*2k)/(1k+2k)
}

// Test 6: .PARAM with scientific notation
TEST(NetlistParser, ParamScientific) {
    std::string netlist = R"(
* .PARAM scientific
.PARAM freq=1e6
.PARAM cap=1e-12
.PARAM omega={2*3.14159265359*freq}
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check parameters
    EXPECT_DOUBLE_EQ(result.config.global_params["freq"], 1e6);
    EXPECT_DOUBLE_EQ(result.config.global_params["cap"], 1e-12);
    EXPECT_NEAR(result.config.global_params["omega"], 6.28318530718e6, 1e3);  // 2*pi*1MHz
}

// Test 7: .PARAM expansion in device values (resistor)
TEST(NetlistParser, ParamExpansionResistor) {
    std::string netlist = R"(
* .PARAM expansion in resistor value
.PARAM R_value=1k
.PARAM R_double={R_value*2}
V1 in 0 DC 5
R1 in out {R_value}
R2 out 0 {R_double}
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check parameters
    EXPECT_DOUBLE_EQ(result.config.global_params["R_value"], 1000.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["R_double"], 2000.0);

    // Check circuit has correct number of devices
    EXPECT_EQ(result.circuit->num_devices(), 3);  // V1, R1, R2

    // Get resistor devices and check values
    auto devices = result.circuit->get_devices();

    // Find R1 and R2
    bool found_r1 = false, found_r2 = false;
    for (const auto& dev : devices) {
        if (dev->name() == "R1") {
            found_r1 = true;
            // Check R1 value is 1000 (R_value)
            auto resistor = std::dynamic_pointer_cast<devices::Resistor>(dev);
            ASSERT_NE(resistor, nullptr);
            EXPECT_DOUBLE_EQ(resistor->resistance(), 1000.0);
        } else if (dev->name() == "R2") {
            found_r2 = true;
            // Check R2 value is 2000 (R_double)
            auto resistor = std::dynamic_pointer_cast<devices::Resistor>(dev);
            ASSERT_NE(resistor, nullptr);
            EXPECT_DOUBLE_EQ(resistor->resistance(), 2000.0);
        }
    }

    EXPECT_TRUE(found_r1);
    EXPECT_TRUE(found_r2);
}

// Test 8: .PARAM expansion in device values (voltage source)
TEST(NetlistParser, ParamExpansionVoltageSource) {
    std::string netlist = R"(
* .PARAM expansion in voltage source
.PARAM Vcc=5.0
.PARAM Vbias={Vcc/2}
V1 vcc 0 DC {Vcc}
V2 vbias 0 DC {Vbias}
R1 vcc 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check parameters
    EXPECT_DOUBLE_EQ(result.config.global_params["Vcc"], 5.0);
    EXPECT_DOUBLE_EQ(result.config.global_params["Vbias"], 2.5);

    // Get voltage source devices
    auto devices = result.circuit->get_devices();

    bool found_v1 = false, found_v2 = false;
    for (const auto& dev : devices) {
        if (dev->name() == "V1") {
            found_v1 = true;
            auto vsource = std::dynamic_pointer_cast<devices::VoltageSource>(dev);
            ASSERT_NE(vsource, nullptr);
            EXPECT_DOUBLE_EQ(vsource->dc_value(), 5.0);  // Vcc
        } else if (dev->name() == "V2") {
            found_v2 = true;
            auto vsource = std::dynamic_pointer_cast<devices::VoltageSource>(dev);
            ASSERT_NE(vsource, nullptr);
            EXPECT_DOUBLE_EQ(vsource->dc_value(), 2.5);  // Vbias
        }
    }

    EXPECT_TRUE(found_v1);
    EXPECT_TRUE(found_v2);
}

// Test 9: .PARAM expansion with expressions
TEST(NetlistParser, ParamExpansionExpressions) {
    std::string netlist = R"(
* .PARAM expansion with complex expressions
.PARAM freq=1e6
.PARAM C_base=1e-12
.PARAM L_base=1e-6
.PARAM R_base=1000
V1 in 0 DC 1
R1 in n1 {R_base*2}
C1 n1 0 {C_base/2}
L1 in n2 {L_base*3}
R2 n2 0 {R_base}
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Get devices
    auto devices = result.circuit->get_devices();

    for (const auto& dev : devices) {
        if (dev->name() == "R1") {
            auto resistor = std::dynamic_pointer_cast<devices::Resistor>(dev);
            ASSERT_NE(resistor, nullptr);
            EXPECT_DOUBLE_EQ(resistor->resistance(), 2000.0);  // R_base*2
        } else if (dev->name() == "C1") {
            auto capacitor = std::dynamic_pointer_cast<devices::Capacitor>(dev);
            ASSERT_NE(capacitor, nullptr);
            EXPECT_DOUBLE_EQ(capacitor->capacitance(), 0.5e-12);  // C_base/2
        } else if (dev->name() == "L1") {
            auto inductor = std::dynamic_pointer_cast<devices::Inductor>(dev);
            ASSERT_NE(inductor, nullptr);
            EXPECT_DOUBLE_EQ(inductor->inductance(), 3e-6);  // L_base*3
        } else if (dev->name() == "R2") {
            auto resistor = std::dynamic_pointer_cast<devices::Resistor>(dev);
            ASSERT_NE(resistor, nullptr);
            EXPECT_DOUBLE_EQ(resistor->resistance(), 1000.0);  // R_base
        }
    }
}

//==============================================================================
// .MODEL Directive Tests
//==============================================================================

// Test 1: .MODEL BJT NPN model definition
TEST(NetlistParser, ModelBJT_NPN) {
    std::string netlist = R"(
* .MODEL BJT NPN test
.MODEL 2N3904 NPN(IS=1e-14 BF=200 N=1.0)
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check model is stored
    EXPECT_EQ(result.config.models.size(), 1);
    EXPECT_TRUE(result.config.models.find("2N3904") != result.config.models.end());

    const auto& model = result.config.models.at("2N3904");
    EXPECT_EQ(model.name, "2N3904");
    EXPECT_EQ(model.device_type, DeviceType::BJT_NPN);
    EXPECT_EQ(model.params.size(), 3);
    EXPECT_DOUBLE_EQ(model.params.at("is"), 1e-14);
    EXPECT_DOUBLE_EQ(model.params.at("bf"), 200.0);
    EXPECT_DOUBLE_EQ(model.params.at("n"), 1.0);
}

// Test 2: .MODEL with BJT device reference
TEST(NetlistParser, ModelBJT_DeviceReference) {
    std::string netlist = R"(
* .MODEL with BJT reference
.MODEL Q2N2222 NPN(IS=1e-14 BF=100 N=1.0)
Vcc vcc 0 DC 12
Q1 vcc base 0 Q2N2222
R1 vcc base 10k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check model
    EXPECT_EQ(result.config.models.size(), 1);
    EXPECT_TRUE(result.config.models.find("Q2N2222") != result.config.models.end());

    // Check circuit has BJT with model parameters
    EXPECT_EQ(result.circuit->num_devices(), 3);  // Vcc, Q1, R1
}

// Test 3: .MODEL PNP model
TEST(NetlistParser, ModelBJT_PNP) {
    std::string netlist = R"(
* .MODEL BJT PNP test
.MODEL 2N3906 PNP(IS=1e-14 BF=180 N=1.0)
V1 in 0 DC 5
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("2N3906");
    EXPECT_EQ(model.device_type, DeviceType::BJT_PNP);
    EXPECT_DOUBLE_EQ(model.params.at("bf"), 180.0);
}

// Test 4: .MODEL with parameters using expressions
TEST(NetlistParser, ModelWithExpressions) {
    std::string netlist = R"(
* .MODEL with parameter expressions
.PARAM Is_base=1e-14
.PARAM beta_gain=200
.MODEL MyBJT NPN(IS={Is_base} BF={beta_gain})
V1 in 0 DC 5
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("MyBJT");
    EXPECT_DOUBLE_EQ(model.params.at("is"), 1e-14);
    EXPECT_DOUBLE_EQ(model.params.at("bf"), 200.0);
}

// Test 5: Multiple models
TEST(NetlistParser, MultipleModels) {
    std::string netlist = R"(
* Multiple .MODEL definitions
.MODEL 2N3904 NPN(IS=1e-14 BF=200)
.MODEL 2N3906 PNP(IS=1e-14 BF=180)
.MODEL 1N4148 D(IS=2.52e-9 N=1.752)
V1 in 0 DC 5
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    EXPECT_EQ(result.config.models.size(), 3);
    EXPECT_TRUE(result.config.models.find("2N3904") != result.config.models.end());
    EXPECT_TRUE(result.config.models.find("2N3906") != result.config.models.end());
    EXPECT_TRUE(result.config.models.find("1N4148") != result.config.models.end());
}

// Test 6: .MODEL Diode model definition and reference
TEST(NetlistParser, ModelDiode_Definition) {
    std::string netlist = R"(
* .MODEL Diode test
.MODEL 1N4148 D(IS=2.52e-9 N=1.752 BV=100 IBV=100e-6)
V1 in 0 DC 5
R1 in out 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check model is stored
    EXPECT_EQ(result.config.models.size(), 1);
    EXPECT_TRUE(result.config.models.find("1N4148") != result.config.models.end());

    const auto& model = result.config.models.at("1N4148");
    EXPECT_EQ(model.name, "1N4148");
    EXPECT_EQ(model.device_type, DeviceType::DIODE);
    EXPECT_DOUBLE_EQ(model.params.at("is"), 2.52e-9);
    EXPECT_DOUBLE_EQ(model.params.at("n"), 1.752);
}

// Test 7: .MODEL Diode device reference
TEST(NetlistParser, ModelDiode_DeviceReference) {
    std::string netlist = R"(
* .MODEL Diode reference
.MODEL 1N4148 D(IS=2.52e-9 N=1.752)
V1 vcc 0 DC 5
D1 vcc out 1N4148
R1 out 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check model
    EXPECT_EQ(result.config.models.size(), 1);

    // Check circuit has diode with model parameters
    EXPECT_EQ(result.circuit->num_devices(), 3);  // V1, D1, R1
}

// Test 8: .MODEL MOSFET NMOS model definition and reference
TEST(NetlistParser, ModelMOSFET_NMOS) {
    std::string netlist = R"(
* .MODEL MOSFET NMOS test
.MODEL 2N7000 NMOS(VTO=2.0 KP=0.3 LAMBDA=0.05)
V1 vdd 0 DC 12
M1 vdd gate 0 2N7000
R1 vdd gate 100k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check model is stored
    EXPECT_EQ(result.config.models.size(), 1);
    EXPECT_TRUE(result.config.models.find("2N7000") != result.config.models.end());

    const auto& model = result.config.models.at("2N7000");
    EXPECT_EQ(model.name, "2N7000");
    EXPECT_EQ(model.device_type, DeviceType::MOSFET_N);
    EXPECT_DOUBLE_EQ(model.params.at("vto"), 2.0);
    EXPECT_DOUBLE_EQ(model.params.at("kp"), 0.3);

    // Check circuit has MOSFET
    EXPECT_EQ(result.circuit->num_devices(), 3);  // V1, M1, R1
}

// Test 9: .MODEL MOSFET PMOS model
TEST(NetlistParser, ModelMOSFET_PMOS) {
    std::string netlist = R"(
* .MODEL MOSFET PMOS test
.MODEL BS250 PMOS(VTO=-2.0 KP=0.15)
V1 vdd 0 DC 12
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("BS250");
    EXPECT_EQ(model.device_type, DeviceType::MOSFET_P);
    EXPECT_DOUBLE_EQ(model.params.at("vto"), -2.0);
}

// ============================================================================
// BSIM3 Parser Tests
// ============================================================================

// Test 1: .model BSIM3 NMOS level=8
TEST(NetlistParser, ModelBSIM3_NMOS_Level8) {
    std::string netlist = R"(
.title BSIM3 NMOS Test
.model NMOS_BSIM3 NMOS level=8 tox=1.5e-8 vth0=0.7 u0=670 vsat=8e4
+ k1=0.5 pclm=1.3 kf=0 tnom=27
M1 2 1 0 0 NMOS_BSIM3 W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check model type
    const auto& model = result.config.models.at("NMOS_BSIM3");
    EXPECT_EQ(model.device_type, DeviceType::MOSFET_BSIM3_N);

    // Check parameters
    EXPECT_DOUBLE_EQ(model.params.at("tox"), 1.5e-8);
    EXPECT_DOUBLE_EQ(model.params.at("vth0"), 0.7);
    EXPECT_DOUBLE_EQ(model.params.at("u0"), 670.0);
    EXPECT_DOUBLE_EQ(model.params.at("vsat"), 8e4);
    EXPECT_DOUBLE_EQ(model.params.at("k1"), 0.5);
    EXPECT_DOUBLE_EQ(model.params.at("pclm"), 1.3);
    EXPECT_DOUBLE_EQ(model.params.at("kf"), 0.0);
    EXPECT_DOUBLE_EQ(model.params.at("tnom"), 27.0);
}

// Test 2: .model BSIM3 PMOS level=49
TEST(NetlistParser, ModelBSIM3_PMOS_Level49) {
    std::string netlist = R"(
.title BSIM3 PMOS Test
.model PMOS_BSIM3 PMOS level=49 tox=1.5e-8 vth0=-0.7 u0=250 vsat=7e4
+ k1=0.6 pclm=1.0 kf=1e-28 tnom=27
M1 2 1 3 3 PMOS_BSIM3 W=20u L=0.5u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check model type
    const auto& model = result.config.models.at("PMOS_BSIM3");
    EXPECT_EQ(model.device_type, DeviceType::MOSFET_BSIM3_P);

    // Check parameters
    EXPECT_DOUBLE_EQ(model.params.at("tox"), 1.5e-8);
    EXPECT_DOUBLE_EQ(model.params.at("vth0"), -0.7);
    EXPECT_DOUBLE_EQ(model.params.at("u0"), 250.0);
    EXPECT_DOUBLE_EQ(model.params.at("vsat"), 7e4);
}

// Test 3: BSIM3 with comprehensive parameters
TEST(NetlistParser, ModelBSIM3_ComprehensiveParameters) {
    std::string netlist = R"(
.title BSIM3 Comprehensive Test
.model N1 NMOS level=8
+ tox=1.5e-8 toxm=1.5e-8 vth0=0.7 u0=670 vsat=8e4
+ k1=0.5 k2=0.0 k3=80 pclm=1.3
+ lint=0 wint=0 dlc=0 dwc=0
+ cgso=1e-10 cgdo=1e-10 cgbo=0
+ cj=5e-4 mj=0.5 pb=1.0
+ kf=0 af=1.0 ef=1.0
+ ute=-1.5 kt1=-0.11 kt2=0.022
+ tnom=27
M1 2 1 0 0 N1 W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("N1");
    EXPECT_EQ(model.device_type, DeviceType::MOSFET_BSIM3_N);

    // Verify key parameters
    EXPECT_DOUBLE_EQ(model.params.at("tox"), 1.5e-8);
    EXPECT_DOUBLE_EQ(model.params.at("toxm"), 1.5e-8);
    EXPECT_DOUBLE_EQ(model.params.at("vth0"), 0.7);
    EXPECT_DOUBLE_EQ(model.params.at("cgso"), 1e-10);
    EXPECT_DOUBLE_EQ(model.params.at("cgdo"), 1e-10);
    EXPECT_DOUBLE_EQ(model.params.at("cj"), 5e-4);
    EXPECT_DOUBLE_EQ(model.params.at("ute"), -1.5);
    EXPECT_DOUBLE_EQ(model.params.at("kt1"), -0.11);
}

// Test 4: Multiple BSIM3 models (NMOS and PMOS)
TEST(NetlistParser, ModelBSIM3_MultipleModels) {
    std::string netlist = R"(
.title BSIM3 Multiple Models Test
.model NMOS_MODEL NMOS level=8 tox=1.5e-8 vth0=0.7 u0=670
.model PMOS_MODEL PMOS level=8 tox=1.5e-8 vth0=-0.7 u0=250
M1 2 1 0 0 NMOS_MODEL W=10u L=1u
M2 3 1 2 2 PMOS_MODEL W=20u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Check NMOS model
    const auto& nmos = result.config.models.at("NMOS_MODEL");
    EXPECT_EQ(nmos.device_type, DeviceType::MOSFET_BSIM3_N);
    EXPECT_DOUBLE_EQ(nmos.params.at("vth0"), 0.7);
    EXPECT_DOUBLE_EQ(nmos.params.at("u0"), 670.0);

    // Check PMOS model
    const auto& pmos = result.config.models.at("PMOS_MODEL");
    EXPECT_EQ(pmos.device_type, DeviceType::MOSFET_BSIM3_P);
    EXPECT_DOUBLE_EQ(pmos.params.at("vth0"), -0.7);
    EXPECT_DOUBLE_EQ(pmos.params.at("u0"), 250.0);
}

// Test 5: BSIM3 with VTO alias for VTH0
TEST(NetlistParser, ModelBSIM3_VtoAlias) {
    std::string netlist = R"(
.title BSIM3 VTO Alias Test
.model N1 NMOS level=8 tox=1.5e-8 vto=0.65 u0=670
M1 2 1 0 0 N1 W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("N1");
    EXPECT_EQ(model.device_type, DeviceType::MOSFET_BSIM3_N);
    EXPECT_DOUBLE_EQ(model.params.at("vto"), 0.65);
}

// Test 6: BSIM3 temperature parameter conversion (TNOM in Celsius)
TEST(NetlistParser, ModelBSIM3_TemperatureConversion) {
    std::string netlist = R"(
.title BSIM3 Temperature Conversion Test
.model N1 NMOS level=8 tox=1.5e-8 vth0=0.7 tnom=25
M1 2 1 0 0 N1 W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("N1");
    // TNOM should be stored in Celsius (will be converted to Kelvin in BSIM3Params mapping)
    EXPECT_DOUBLE_EQ(model.params.at("tnom"), 25.0);
}

// Test 7: BSIM3 with geometry parameters
TEST(NetlistParser, ModelBSIM3_GeometryParameters) {
    std::string netlist = R"(
.title BSIM3 Geometry Test
.model N1 NMOS level=8 tox=1.5e-8 vth0=0.7
+ lint=10n wint=5n
+ ll=1e-15 lw=2e-15 lwl=3e-21
+ wl=1e-15 ww=2e-15 wwl=3e-21
M1 2 1 0 0 N1 W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("N1");
    EXPECT_DOUBLE_EQ(model.params.at("lint"), 10e-9);
    EXPECT_DOUBLE_EQ(model.params.at("wint"), 5e-9);
    EXPECT_DOUBLE_EQ(model.params.at("ll"), 1e-15);
    EXPECT_DOUBLE_EQ(model.params.at("lwl"), 3e-21);
}

// Test 8: BSIM3 model reference in M element
TEST(NetlistParser, BSIM3_MElementReference) {
    std::string netlist = R"(
.title BSIM3 M Element Reference
.model MYMODEL NMOS level=8 tox=1.5e-8 vth0=0.7 u0=670
M1 2 1 0 0 MYMODEL W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Verify model exists
    EXPECT_TRUE(result.config.models.find("MYMODEL") != result.config.models.end());

    // Verify device was created (check circuit graph)
    // Note: This is a parser test, detailed device verification is in integration tests
}

// Test 9: Unrecognized BSIM3 parameter warning
TEST(NetlistParser, ModelBSIM3_UnrecognizedParameter) {
    std::string netlist = R"(
.title BSIM3 Unrecognized Parameter Test
.model N1 NMOS level=8 tox=1.5e-8 vth0=0.7 unknown_param=123
M1 2 1 0 0 N1 W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Should parse successfully despite unknown parameter
    ASSERT_NE(result.circuit, nullptr);

    const auto& model = result.config.models.at("N1");
    EXPECT_EQ(model.device_type, DeviceType::MOSFET_BSIM3_N);
    // Unknown parameter should still be in params map
    EXPECT_TRUE(model.params.find("unknown_param") != model.params.end());
}

// Test 10: BSIM3 with instance parameters on M line
TEST(NetlistParser, ModelBSIM3_InstanceParameters) {
    std::string netlist = R"(
.title BSIM3 Instance Parameters Test
.model N1 NMOS level=8 tox=1.5e-8 vth0=0.7 u0=670
M1 2 1 0 0 N1 W=5u L=0.5u
M2 3 1 0 0 N1 W=20u L=2u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Both devices should use the same model but with different W/L
    // (Detailed verification in integration tests)
}

//==============================================================================
// .STEP Parameter Sweep Tests
//==============================================================================

// Test 1: .STEP LIN (Linear sweep)
TEST(NetlistParser, StepLinearSweep) {
    std::string netlist = R"(
.title Linear Parameter Sweep Test
* Linear sweep: R from 1k to 10k with 1k steps
.STEP param R 1k 10k 1k
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 1);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "R");
    EXPECT_EQ(step.sweep_type, StepType::LIN);
    EXPECT_DOUBLE_EQ(step.start_value, 1000.0);
    EXPECT_DOUBLE_EQ(step.stop_value, 10000.0);
    EXPECT_DOUBLE_EQ(step.step_value, 1000.0);

    // Check generated points
    auto points = step.generate_points();
    ASSERT_EQ(points.size(), 10);
    EXPECT_DOUBLE_EQ(points[0], 1000.0);
    EXPECT_DOUBLE_EQ(points[9], 10000.0);
}

// Test 2: .STEP DEC (Decade logarithmic sweep)
TEST(NetlistParser, StepDecadeSweep) {
    std::string netlist = R"(
.title Decade Logarithmic Sweep Test
* Decade sweep: C from 1pF to 1uF with 10 points per decade
.STEP dec param C 1p 1u 10
C1 1 0 1p
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 1);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "C");
    EXPECT_EQ(step.sweep_type, StepType::DEC);
    EXPECT_DOUBLE_EQ(step.start_value, 1e-12);
    EXPECT_DOUBLE_EQ(step.stop_value, 1e-6);
    EXPECT_DOUBLE_EQ(step.step_value, 10.0);

    // Check generated points (6 decades, 10 points/decade = 61 points)
    auto points = step.generate_points();
    EXPECT_EQ(points.size(), 61);
    EXPECT_NEAR(points[0], 1e-12, 1e-15);
    EXPECT_NEAR(points[60], 1e-6, 1e-9);
}

// Test 3: .STEP OCT (Octave logarithmic sweep)
TEST(NetlistParser, StepOctaveSweep) {
    std::string netlist = R"(
.title Octave Logarithmic Sweep Test
* Octave sweep: L from 1nH to 1mH with 5 points per octave
.STEP oct param L 1n 1m 5
L1 1 2 1n
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 1);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "L");
    EXPECT_EQ(step.sweep_type, StepType::OCT);
    EXPECT_DOUBLE_EQ(step.start_value, 1e-9);
    EXPECT_DOUBLE_EQ(step.stop_value, 1e-3);
    EXPECT_DOUBLE_EQ(step.step_value, 5.0);

    // Check generated points
    auto points = step.generate_points();
    EXPECT_GT(points.size(), 0);
    EXPECT_NEAR(points[0], 1e-9, 1e-12);
    EXPECT_NEAR(points.back(), 1e-3, 1e-6);
}

// Test 4: .STEP with case-insensitive keywords
TEST(NetlistParser, StepCaseInsensitive) {
    std::string netlist = R"(
.title Case Insensitive STEP Test
.STEP DEC PARAM R 1k 10k 10
.STEP OCT ParaM C 1p 1u 5
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 2);

    EXPECT_EQ(result.config.step_configs[0].sweep_type, StepType::DEC);
    EXPECT_EQ(result.config.step_configs[1].sweep_type, StepType::OCT);
}

// Test 5: .STEP LIST
TEST(NetlistParser, StepList) {
    std::string netlist = R"(
.title LIST Sweep Test
.STEP param R list 1k 2k 5k 10k
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 1);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.param_name, "R");
    EXPECT_EQ(step.sweep_type, StepType::LIST);

    // Check list values
    ASSERT_EQ(step.list_values.size(), 4);
    EXPECT_DOUBLE_EQ(step.list_values[0], 1000.0);
    EXPECT_DOUBLE_EQ(step.list_values[1], 2000.0);
    EXPECT_DOUBLE_EQ(step.list_values[2], 5000.0);
    EXPECT_DOUBLE_EQ(step.list_values[3], 10000.0);

    // Check generated points
    auto points = step.generate_points();
    ASSERT_EQ(points.size(), 4);
    EXPECT_DOUBLE_EQ(points[0], 1000.0);
    EXPECT_DOUBLE_EQ(points[3], 10000.0);
}

// Test 6: .STEP error - zero step size (LIN)
TEST(NetlistParser, StepErrorZeroStep) {
    std::string netlist = R"(
.title Zero Step Error Test
.STEP param R 1k 10k 0
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 7: .STEP error - wrong step direction
TEST(NetlistParser, StepErrorWrongDirection) {
    std::string netlist = R"(
.title Wrong Direction Error Test
* start < stop but negative step
.STEP param R 1k 10k -1k
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 8: .STEP error - negative values for DEC
TEST(NetlistParser, StepErrorNegativeDEC) {
    std::string netlist = R"(
.title Negative DEC Error Test
.STEP dec param R -1k 10k 10
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 9: .STEP error - invalid points per decade
TEST(NetlistParser, StepErrorInvalidPointsPerDecade) {
    std::string netlist = R"(
.title Invalid Points Per Decade Test
.STEP dec param R 1k 10k 0
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 10: Multiple .STEP directives (preparation)
TEST(NetlistParser, MultipleStepDirectives) {
    std::string netlist = R"(
.title Multiple STEP Test
.STEP param R 1k 10k 1k
.STEP dec param C 1p 1u 10
.STEP oct param L 1n 1m 5
R1 1 0 1k
C1 1 0 1p
L1 1 2 1n
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 3);

    EXPECT_EQ(result.config.step_configs[0].param_name, "R");
    EXPECT_EQ(result.config.step_configs[0].sweep_type, StepType::LIN);

    EXPECT_EQ(result.config.step_configs[1].param_name, "C");
    EXPECT_EQ(result.config.step_configs[1].sweep_type, StepType::DEC);

    EXPECT_EQ(result.config.step_configs[2].param_name, "L");
    EXPECT_EQ(result.config.step_configs[2].sweep_type, StepType::OCT);
}

// Test 11: .STEP LIST with unit suffixes
TEST(NetlistParser, StepList_UnitSuffixes) {
    std::string netlist = R"(
.title LIST with Unit Suffixes
.STEP param C list 1p 10p 100p 1n 10n
C1 1 0 1p
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 1);

    const auto& step = result.config.step_configs[0];
    EXPECT_EQ(step.sweep_type, StepType::LIST);

    ASSERT_EQ(step.list_values.size(), 5);
    EXPECT_DOUBLE_EQ(step.list_values[0], 1e-12);
    EXPECT_DOUBLE_EQ(step.list_values[1], 10e-12);
    EXPECT_DOUBLE_EQ(step.list_values[2], 100e-12);
    EXPECT_DOUBLE_EQ(step.list_values[3], 1e-9);
    EXPECT_DOUBLE_EQ(step.list_values[4], 10e-9);
}

// Test 12: .STEP LIST error - no values provided
TEST(NetlistParser, StepList_ErrorNoValues) {
    std::string netlist = R"(
.title LIST Error - No Values
.STEP param R list
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 13: Multiple .STEP with LIST
TEST(NetlistParser, MultipleStepWithList) {
    std::string netlist = R"(
.title Multiple STEP with LIST
.STEP param R list 1k 2k 5k
.STEP param C list 1p 10p 100p
R1 1 0 1k
C1 1 0 1p
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.step_configs.size(), 2);

    EXPECT_EQ(result.config.step_configs[0].sweep_type, StepType::LIST);
    EXPECT_EQ(result.config.step_configs[0].list_values.size(), 3);

    EXPECT_EQ(result.config.step_configs[1].sweep_type, StepType::LIST);
    EXPECT_EQ(result.config.step_configs[1].list_values.size(), 3);
}

// ============================================================================
// Temperature Sweep (.TEMP) Parser Tests
// ============================================================================

TEST(NetlistParser, TempBasicSyntax) {
    std::string netlist = R"(
.title Temperature Sweep Test
.TEMP -40 0 27 85 125
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_TRUE(result.config.temp_sweep.enabled);
    ASSERT_EQ(result.config.temp_sweep.temperatures.size(), 5);

    // Verify conversion from Celsius to Kelvin
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[0], -40.0 + 273.15);  // 233.15K
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[1], 0.0 + 273.15);     // 273.15K
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[2], 27.0 + 273.15);    // 300.15K
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[3], 85.0 + 273.15);    // 358.15K
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[4], 125.0 + 273.15);   // 398.15K
}

TEST(NetlistParser, TempSingleValue) {
    std::string netlist = R"(
.title Temperature Sweep Single Point
.TEMP 100
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.temp_sweep.enabled);
    ASSERT_EQ(result.config.temp_sweep.temperatures.size(), 1);
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[0], 100.0 + 273.15);  // 373.15K
}

TEST(NetlistParser, TempWithUnitSuffix) {
    std::string netlist = R"(
.title Temperature Sweep with Units
.TEMP -40 27 100
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.temp_sweep.enabled);
    ASSERT_EQ(result.config.temp_sweep.temperatures.size(), 3);

    // All values should be in Kelvin
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[0], 233.15);   // -40°C
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[1], 300.15);   // 27°C
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[2], 373.15);   // 100°C
}

TEST(NetlistParser, TempFractionalValues) {
    std::string netlist = R"(
.title Temperature Sweep with Fractional Values
.TEMP 26.85 27.0 27.15
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.temp_sweep.enabled);
    ASSERT_EQ(result.config.temp_sweep.temperatures.size(), 3);

    EXPECT_NEAR(result.config.temp_sweep.temperatures[0], 26.85 + 273.15, 1e-10);
    EXPECT_NEAR(result.config.temp_sweep.temperatures[1], 27.0 + 273.15, 1e-10);
    EXPECT_NEAR(result.config.temp_sweep.temperatures[2], 27.15 + 273.15, 1e-10);
}

TEST(NetlistParser, TempCaseInsensitive) {
    std::string netlist = R"(
.title Temperature Sweep Case Insensitive
.temp -40 27 125
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.temp_sweep.enabled);
    ASSERT_EQ(result.config.temp_sweep.temperatures.size(), 3);

    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[0], 233.15);
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[1], 300.15);
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[2], 398.15);
}

TEST(NetlistParser, TempErrorNoArguments) {
    std::string netlist = R"(
.title Temperature Sweep No Arguments
.TEMP
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        parser.parse_string(netlist);
    }, parser::ParseError);
}

TEST(NetlistParser, TempErrorBelowAbsoluteZero) {
    std::string netlist = R"(
.title Temperature Sweep Below Absolute Zero
.TEMP -300
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        parser.parse_string(netlist);
    }, parser::ParseError);
}

TEST(NetlistParser, TempMultipleDirectives) {
    // Last .TEMP directive should override previous ones
    std::string netlist = R"(
.title Temperature Sweep Multiple Directives
.TEMP 0 27
.TEMP 85 125 150
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.temp_sweep.enabled);
    ASSERT_EQ(result.config.temp_sweep.temperatures.size(), 3);  // Last directive wins

    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[0], 85.0 + 273.15);
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[1], 125.0 + 273.15);
    EXPECT_DOUBLE_EQ(result.config.temp_sweep.temperatures[2], 150.0 + 273.15);
}

// ============================================================================
// .DC Directive Parser Tests
// ============================================================================

// Test: Parse 1D .DC directive
TEST(NetlistParser, DCSweep1D) {
    std::string netlist = R"(
Simple DC Sweep Test
V1 in 0 DC 0
R1 in 0 1k
.DC V1 0 10 1
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Verify DC sweep configuration
    ASSERT_TRUE(result.config.dc_sweep_enabled);
    EXPECT_EQ(result.config.dc_sweep.source_name, "V1");
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.start_value, 0.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.stop_value, 10.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.step_value, 1.0);
    EXPECT_FALSE(result.config.dc_sweep.nested_sweep);  // No nested sweep
    EXPECT_EQ(result.config.dc_sweep.num_points(), 11);  // 0, 1, ..., 10
}

// Test: Parse 2D .DC directive
TEST(NetlistParser, DCSweep2D) {
    std::string netlist = R"(
2D DC Sweep Test
V1 in1 0 DC 0
V2 in2 0 DC 0
R1 in1 out 1k
R2 out in2 1k
.DC V1 0 5 0.5 V2 0 3 1
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Verify primary sweep
    ASSERT_TRUE(result.config.dc_sweep_enabled);
    EXPECT_EQ(result.config.dc_sweep.source_name, "V1");
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.start_value, 0.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.stop_value, 5.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.step_value, 0.5);
    EXPECT_EQ(result.config.dc_sweep.num_points(), 11);  // 0, 0.5, 1, ..., 5

    // Verify nested sweep
    ASSERT_TRUE(result.config.dc_sweep.nested_sweep != nullptr);
    EXPECT_EQ(result.config.dc_sweep.nested_sweep->source_name, "V2");
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.nested_sweep->start_value, 0.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.nested_sweep->stop_value, 3.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.nested_sweep->step_value, 1.0);
    EXPECT_EQ(result.config.dc_sweep.nested_sweep->num_points(), 4);  // 0, 1, 2, 3
    EXPECT_FALSE(result.config.dc_sweep.nested_sweep->nested_sweep);  // No 3D sweep
}

// Test: Parse .DC with unit suffixes
TEST(NetlistParser, DCSweepWithUnits) {
    std::string netlist = R"(
DC Sweep with Units
V1 in 0 DC 0
R1 in 0 1k
.DC V1 0 10m 1m
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.dc_sweep_enabled);
    EXPECT_EQ(result.config.dc_sweep.source_name, "V1");
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.start_value, 0.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.stop_value, 0.010);   // 10m = 0.010
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.step_value, 0.001);   // 1m = 0.001
    EXPECT_EQ(result.config.dc_sweep.num_points(), 11);
}

// Test: Incomplete .DC directive is skipped (backward compatibility)
TEST(NetlistParser, DCSweepErrorTooFewArgs) {
    std::string netlist = R"(
DC Sweep Incomplete Test
V1 in 0 DC 0
R1 in 0 1k
.DC V1 0 10
.DC
.END
)";

    parser::NetlistParser parser;
    // Should not throw, just log warnings and skip incomplete directives
    EXPECT_NO_THROW(parser.parse_string(netlist));

    auto result = parser.parse_string(netlist);
    // DC sweep should not be enabled (incomplete directive)
    EXPECT_FALSE(result.config.dc_sweep_enabled);
}

// Test: Error - .DC with zero step
TEST(NetlistParser, DCSweepErrorZeroStep) {
    std::string netlist = R"(
DC Sweep Zero Step Error
V1 in 0 DC 0
R1 in 0 1k
.DC V1 0 10 0
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), parser::ParseError);
}

//==============================================================================
// Behavioral Source (B-element) Tests
//==============================================================================

// Test: Basic B-element voltage source with simple expression
TEST(NetlistParser, BehavioralVoltageSourceSimple) {
    std::string netlist = R"(
B-element Simple Test
V1 in 0 DC 1
R1 out 0 1k
B1 out 0 V={2*V(in)}
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 3);  // V1, R1, B1
}

// Test: B-element current source with TIME expression
TEST(NetlistParser, BehavioralCurrentSourceWithTime) {
    std::string netlist = R"(
B-element Current Source Test
R1 out 0 1k
B1 out 0 I={sin(2*pi*1000*TIME)}
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 2);  // R1, B1
}

// Test: B-element with braces around expression
TEST(NetlistParser, BehavioralSourceWithBraces) {
    std::string netlist = R"(
B-element Braces Test
V1 in 0 DC 5
R1 out 0 1k
B1 out 0 V={V(in)*0.5}
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 3);  // V1, R1, B1
}

// Test: Error - B-element missing V= or I= specifier
TEST(NetlistParser, BehavioralSourceMissingSpecifier) {
    std::string netlist = R"(
B-element Error Test
B1 out 0 1+2
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), parser::ParseError);
}

// Test: Error - B-element with invalid syntax
TEST(NetlistParser, BehavioralSourceInvalidSyntax) {
    std::string netlist = R"(
B-element Invalid Syntax
B1 out
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), parser::ParseError);
}

//==============================================================================
// Behavioral Source Integration Tests
//==============================================================================

// Test: B-element voltage follower with V(node)
TEST(NetlistParser, BehavioralVoltageFollower) {
    std::string netlist = R"(
B-element Voltage Follower Test
V1 in 0 DC 5
R1 in 0 1k
B1 out 0 V={V(in)}
R2 out 0 1k
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 4);  // V1, R1, B1, R2

    // Check that B1 is created correctly
    auto devices = result.circuit->get_devices();
    bool found_behavioral = false;
    for (const auto& dev : devices) {
        if (dev->name() == "B1") {
            found_behavioral = true;
            EXPECT_EQ(dev->type(), DeviceType::BEHAVIORAL_SOURCE);
            break;
        }
    }
    EXPECT_TRUE(found_behavioral);
}

// Test: B-element amplifier with gain
TEST(NetlistParser, BehavioralAmplifier) {
    std::string netlist = R"(
B-element Amplifier Test
V1 in 0 DC 1
R1 in 0 1k
B1 out 0 V={2*V(in)}
R2 out 0 1k
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 4);  // V1, R1, B1, R2
}

// Test: B-element with TIME-dependent expression
TEST(NetlistParser, BehavioralTimeDependentSource) {
    std::string netlist = R"(
B-element Time-Dependent Test
R1 out 0 1k
B1 out 0 V={sin(2*pi*1000*TIME)}
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 2);  // R1, B1
}

// Test: B-element with combined V(node) and TIME
TEST(NetlistParser, BehavioralCombinedExpression) {
    std::string netlist = R"(
B-element Combined Expression Test
V1 in 0 DC 1
R1 in 0 1k
B1 out 0 V={V(in)*sin(2*pi*1000*TIME)}
R2 out 0 1k
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 4);  // V1, R1, B1, R2
}

// Test: B-element current source with expression
TEST(NetlistParser, BehavioralCurrentSource) {
    std::string netlist = R"(
B-element Current Source Test
R1 out 0 1k
B1 out 0 I={0.001*TIME}
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_devices().size(), 2);  // R1, B1
}

//==============================================================================
// Conditional Directives Tests
//==============================================================================

// Note: .IF/.ELIF/.ELSE/.ENDIF preprocessing is implemented but not yet integrated
// into the main parser. These tests verify the preprocessing logic.

// Test: Basic .IF/.ENDIF with true condition
TEST(ConditionalDirectives, BasicIfTrue) {
    std::string netlist = R"(
Test Circuit
.PARAM USE_R1=1
.IF USE_R1
R1 in out 1k
.ENDIF
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist, {{"USE_R1", 1.0}});

    ASSERT_NE(result.circuit, nullptr);
    // Note: Conditional preprocessing not yet integrated
    // This test documents the expected behavior
}

// Test: Basic .IF/.ENDIF with false condition
TEST(ConditionalDirectives, BasicIfFalse) {
    std::string netlist = R"(
Test Circuit
.PARAM USE_R1=0
.IF USE_R1
R1 in out 1k
.ENDIF
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist, {{"USE_R1", 0.0}});

    ASSERT_NE(result.circuit, nullptr);
    // Note: Conditional preprocessing not yet integrated
    // This test documents the expected behavior
}

// Test: .IF/.ELSE/.ENDIF
TEST(ConditionalDirectives, IfElse) {
    std::string netlist = R"(
Test Circuit
.PARAM FAST_MODE=1
.IF FAST_MODE
R1 in out 100
.ELSE
R1 in out 1k
.ENDIF
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist, {{"FAST_MODE", 1.0}});

    ASSERT_NE(result.circuit, nullptr);
    // Note: Conditional preprocessing not yet integrated
}

// Test: .IF/.ELIF/.ELSE/.ENDIF
TEST(ConditionalDirectives, IfElifElse) {
    std::string netlist = R"(
Test Circuit
.PARAM MODE=2
.IF MODE==2
R1 in out 500
.ELIF MODE==1
R1 in out 100
.ELSE
R1 in out 1k
.ENDIF
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist, {{"MODE", 2.0}});

    ASSERT_NE(result.circuit, nullptr);
    // Note: Conditional preprocessing not yet integrated
}

//==============================================================================
// .LIB Section Selection Tests
//==============================================================================

TEST(LibSectionSelection, BasicSectionSelection) {
    namespace fs = std::filesystem;

    // Create temporary library file with multiple sections
    fs::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    fs::path lib_file = temp_dir / "test_lib.lib";

    {
        std::ofstream lib(lib_file);
        lib << ".LIB typical\n";
        lib << "R_typical in out 1k\n";
        lib << ".ENDL\n";
        lib << "\n";
        lib << ".LIB fast\n";
        lib << "R_fast in out 500\n";
        lib << ".ENDL\n";
        lib << "\n";
        lib << ".LIB slow\n";
        lib << "R_slow in out 2k\n";
        lib << ".ENDL\n";
    }

    // Test selecting 'typical' section
    std::string netlist = fmt::format(R"(
Test Circuit
V1 in 0 DC 5
.LIB {} typical
.END
)", lib_file.string());

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    auto devices = result.circuit->get_devices();

    // Should have V1 and R_typical (not R_fast or R_slow)
    EXPECT_EQ(devices.size(), 2);

    bool found_r_typical = false;
    for (const auto& dev : devices) {
        if (dev->name() == "R_typical") {
            found_r_typical = true;
            EXPECT_EQ(dev->type(), kirimaspice::DeviceType::RESISTOR);
        }
    }
    EXPECT_TRUE(found_r_typical);

    // Cleanup
    fs::remove(lib_file);
}

TEST(LibSectionSelection, WithoutSectionIncludesEntireFile) {
    namespace fs = std::filesystem;

    // Create temporary library file
    fs::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    fs::path lib_file = temp_dir / "test_lib2.lib";

    {
        std::ofstream lib(lib_file);
        lib << "R1 in out 1k\n";
        lib << "C1 out 0 1u\n";
    }

    // Test without section - should include entire file
    std::string netlist = fmt::format(R"(
Test Circuit
V1 in 0 DC 5
.LIB {}
.END
)", lib_file.string());

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    auto devices = result.circuit->get_devices();

    // Should have V1, R1, and C1
    EXPECT_EQ(devices.size(), 3);

    // Cleanup
    fs::remove(lib_file);
}

TEST(LibSectionSelection, SectionNotFoundThrows) {
    namespace fs = std::filesystem;

    // Create temporary library file
    fs::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    fs::path lib_file = temp_dir / "test_lib3.lib";

    {
        std::ofstream lib(lib_file);
        lib << ".LIB typical\n";
        lib << "R1 in out 1k\n";
        lib << ".ENDL\n";
    }

    // Test selecting non-existent section
    std::string netlist = fmt::format(R"(
Test Circuit
.LIB {} fast
.END
)", lib_file.string());

    parser::NetlistParser p;
    EXPECT_THROW(p.parse_string(netlist), parser::ParseError);

    // Cleanup
    fs::remove(lib_file);
}

TEST(LibSectionSelection, NestedLibraryFiles) {
    namespace fs = std::filesystem;

    fs::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    fs::path lib1 = temp_dir / "lib1.lib";
    fs::path lib2 = temp_dir / "lib2.lib";

    // Create lib2 (inner library)
    {
        std::ofstream lib(lib2);
        lib << ".LIB inner\n";
        lib << "C1 out 0 1u\n";
        lib << ".ENDL\n";
    }

    // Create lib1 (outer library)
    {
        std::ofstream lib(lib1);
        lib << ".LIB outer\n";
        lib << "R1 in out 1k\n";
        lib << fmt::format(".LIB {} inner\n", lib2.string());
        lib << ".ENDL\n";
    }

    // Test nested library loading
    std::string netlist = fmt::format(R"(
Test Circuit
V1 in 0 DC 5
.LIB {} outer
.END
)", lib1.string());

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    auto devices = result.circuit->get_devices();

    // Should have V1, R1, and C1
    EXPECT_EQ(devices.size(), 3);

    // Cleanup
    fs::remove(lib1);
    fs::remove(lib2);
}

TEST(LibSectionSelection, CaseInsensitiveSectionName) {
    namespace fs = std::filesystem;

    fs::path temp_dir = kirimaspice::test::test_output_path("x").parent_path();
    fs::path lib_file = temp_dir / "test_lib4.lib";

    {
        std::ofstream lib(lib_file);
        lib << ".LIB TyPiCaL\n";
        lib << "R1 in out 1k\n";
        lib << ".ENDL\n";
    }

    // Test with different case
    std::string netlist = fmt::format(R"(
Test Circuit
V1 in 0 DC 5
.LIB {} typical
.END
)", lib_file.string());

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    auto devices = result.circuit->get_devices();
    EXPECT_EQ(devices.size(), 2);  // V1 and R1

    // Cleanup
    fs::remove(lib_file);
}
//==============================================================================
// .CONTROL/.ENDC Tests
//==============================================================================

TEST(ControlBlock, BasicControlBlock) {
    std::string netlist = R"(
Basic Control Block Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
dc V1 0 10 1
print v(out)
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_TRUE(result.config.control_block.has_value());

    const auto& block = result.config.control_block.value();
    EXPECT_TRUE(block.enabled);
    EXPECT_EQ(block.commands.size(), 2);
    EXPECT_EQ(block.commands[0].raw_command, "dc V1 0 10 1");
    EXPECT_EQ(block.commands[1].raw_command, "print v(out)");
}

TEST(ControlBlock, MultipleCommands) {
    std::string netlist = R"(
Multiple Commands Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
dc V1 0 10 1
ac dec 10 1 1meg
tran 1n 100n
print v(out)
echo "Analysis complete"
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& block = result.config.control_block.value();
    EXPECT_EQ(block.commands.size(), 5);
}

TEST(ControlBlock, EmptyControlBlock) {
    std::string netlist = R"(
Empty Control Block Test
V1 in 0 DC 5
.CONTROL
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& block = result.config.control_block.value();
    EXPECT_TRUE(block.enabled);
    EXPECT_EQ(block.commands.size(), 0);
}

TEST(ControlBlock, NestedControlBlocks) {
    std::string netlist = R"(
Nested Control Blocks Test
V1 in 0 DC 5
.CONTROL
dc V1 0 10 1
.CONTROL
ac dec 10 1 1meg
.ENDC
.ENDC
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), parser::ParseError);
}

TEST(ControlBlock, MultipleControlBlocks) {
    std::string netlist = R"(
Multiple Control Blocks Test
V1 in 0 DC 5
.CONTROL
dc V1 0 10 1
.ENDC
.CONTROL
ac dec 10 1 1meg
.ENDC
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), parser::ParseError);
}

TEST(ControlBlock, UnclosedControlBlock) {
    std::string netlist = R"(
Unclosed Control Block Test
V1 in 0 DC 5
.CONTROL
dc V1 0 10 1
print v(out)
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), parser::ParseError);
}

TEST(ControlBlock, EndcWithoutControl) {
    std::string netlist = R"(
ENDC Without CONTROL Test
V1 in 0 DC 5
.ENDC
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), parser::ParseError);
}

TEST(ControlBlock, NoControlBlock) {
    std::string netlist = R"(
No Control Block Test
V1 in 0 DC 5
R1 in out 1k
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_FALSE(result.config.control_block.has_value());
}

//==============================================================================
// .CONTROL/.ENDC Command Parser Tests
//==============================================================================

TEST(ControlBlockParser, DcCommand) {
    std::string netlist = R"(
DC Command Test
V1 in 0 DC 5
.CONTROL
dc V1 0 10 1
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& block = result.config.control_block.value();
    ASSERT_EQ(block.commands.size(), 1);

    const auto& cmd = block.commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::DC);
    ASSERT_EQ(cmd.args.size(), 4);
    EXPECT_EQ(cmd.args[0], "V1");
    EXPECT_EQ(cmd.args[1], "0");
    EXPECT_EQ(cmd.args[2], "10");
    EXPECT_EQ(cmd.args[3], "1");
}

TEST(ControlBlockParser, AcCommand) {
    std::string netlist = R"(
AC Command Test
V1 in 0 DC 0 AC 1
.CONTROL
ac dec 10 1 1meg
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& cmd = result.config.control_block->commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::AC);
    ASSERT_EQ(cmd.args.size(), 4);
    EXPECT_EQ(cmd.args[0], "dec");
    EXPECT_EQ(cmd.args[1], "10");
    EXPECT_EQ(cmd.args[2], "1");
    EXPECT_EQ(cmd.args[3], "1meg");
}

TEST(ControlBlockParser, TranCommand) {
    std::string netlist = R"(
TRAN Command Test
V1 in 0 DC 5
.CONTROL
tran 1n 100n
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& cmd = result.config.control_block->commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::TRAN);
    ASSERT_EQ(cmd.args.size(), 2);
    EXPECT_EQ(cmd.args[0], "1n");
    EXPECT_EQ(cmd.args[1], "100n");
}

TEST(ControlBlockParser, PrintCommand) {
    std::string netlist = R"(
PRINT Command Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
print v(in) v(out) i(V1)
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& cmd = result.config.control_block->commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::PRINT);
    ASSERT_EQ(cmd.args.size(), 3);
    EXPECT_EQ(cmd.args[0], "v(in)");
    EXPECT_EQ(cmd.args[1], "v(out)");
    EXPECT_EQ(cmd.args[2], "i(V1)");
}

TEST(ControlBlockParser, EchoCommand) {
    std::string netlist = R"(
ECHO Command Test
V1 in 0 DC 5
.CONTROL
echo Simulation complete
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& cmd = result.config.control_block->commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::ECHO);
    ASSERT_EQ(cmd.args.size(), 1);
    EXPECT_EQ(cmd.args[0], "Simulation complete");
}

TEST(ControlBlockParser, LetCommand) {
    std::string netlist = R"(
LET Command Test
V1 in 0 DC 5
.CONTROL
let x = 5
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& cmd = result.config.control_block->commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::LET);
    ASSERT_EQ(cmd.args.size(), 3);
    EXPECT_EQ(cmd.args[0], "x");
    EXPECT_EQ(cmd.args[1], "=");
    EXPECT_EQ(cmd.args[2], "5");
}

TEST(ControlBlockParser, MeasCommand) {
    std::string netlist = R"(
MEAS Command Test
V1 in 0 DC 5
.CONTROL
meas tran vmax max v(out)
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& cmd = result.config.control_block->commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::MEAS);
    ASSERT_EQ(cmd.args.size(), 4);
    EXPECT_EQ(cmd.args[0], "tran");
    EXPECT_EQ(cmd.args[1], "vmax");
    EXPECT_EQ(cmd.args[2], "max");
    EXPECT_EQ(cmd.args[3], "v(out)");
}

TEST(ControlBlockParser, CaseInsensitiveCommands) {
    std::string netlist = R"(
Case Insensitive Test
V1 in 0 DC 5
.CONTROL
DC V1 0 10 1
Ac DEC 10 1 1meg
TRAN 1n 100n
Print v(out)
Echo Done
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;
    EXPECT_EQ(commands.size(), 5);
    EXPECT_EQ(commands[0].type, ControlCommandType::DC);
    EXPECT_EQ(commands[1].type, ControlCommandType::AC);
    EXPECT_EQ(commands[2].type, ControlCommandType::TRAN);
    EXPECT_EQ(commands[3].type, ControlCommandType::PRINT);
    EXPECT_EQ(commands[4].type, ControlCommandType::ECHO);
}

TEST(ControlBlockParser, MixedCommands) {
    std::string netlist = R"(
Mixed Commands Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
dc V1 0 10 1
print v(out)
let vmax = 10
tran 1n 100n
meas tran vpeak max v(out)
echo Analysis complete
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;
    EXPECT_EQ(commands.size(), 6);
    EXPECT_EQ(commands[0].type, ControlCommandType::DC);
    EXPECT_EQ(commands[1].type, ControlCommandType::PRINT);
    EXPECT_EQ(commands[2].type, ControlCommandType::LET);
    EXPECT_EQ(commands[3].type, ControlCommandType::TRAN);
    EXPECT_EQ(commands[4].type, ControlCommandType::MEAS);
    EXPECT_EQ(commands[5].type, ControlCommandType::ECHO);
}

//==============================================================================
// .CONTROL/.ENDC Command Execution Tests
//==============================================================================

TEST(ControlBlockExecution, EchoCommand) {
    std::string netlist = R"(
ECHO Command Execution Test
V1 in 0 DC 5
.CONTROL
echo Test message
.ENDC
.END
)";

    // Capture log output by parsing (execution happens during parse)
    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& cmd = result.config.control_block->commands[0];
    EXPECT_EQ(cmd.type, ControlCommandType::ECHO);
    EXPECT_EQ(cmd.args[0], "Test message");
}

TEST(ControlBlockExecution, LetCommandSimple) {
    std::string netlist = R"(
LET Command Execution Test
V1 in 0 DC 5
.CONTROL
let x = 10
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& block = result.config.control_block.value();

    // Check that variable was stored
    ASSERT_TRUE(block.variables.count("x") > 0);
    EXPECT_DOUBLE_EQ(block.variables.at("x"), 10.0);
}

TEST(ControlBlockExecution, LetCommandExpression) {
    std::string netlist = R"(
LET Expression Test
V1 in 0 DC 5
.CONTROL
let x = 5 + 3
let y = x * 2
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& block = result.config.control_block.value();

    // Check both variables
    ASSERT_TRUE(block.variables.count("x") > 0);
    ASSERT_TRUE(block.variables.count("y") > 0);
    EXPECT_DOUBLE_EQ(block.variables.at("x"), 8.0);
    EXPECT_DOUBLE_EQ(block.variables.at("y"), 16.0);
}

TEST(ControlBlockExecution, MultipleCommands) {
    std::string netlist = R"(
Multiple Command Execution Test
V1 in 0 DC 5
.CONTROL
echo Starting
let a = 5
let b = 10
let c = a + b
echo Done
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& block = result.config.control_block.value();

    // Check variables
    EXPECT_DOUBLE_EQ(block.variables.at("a"), 5.0);
    EXPECT_DOUBLE_EQ(block.variables.at("b"), 10.0);
    EXPECT_DOUBLE_EQ(block.variables.at("c"), 15.0);
}

TEST(ControlBlockExecution, LetCommandWithFunctions) {
    std::string netlist = R"(
LET with Functions Test
V1 in 0 DC 5
.CONTROL
let x = sin(0)
let y = cos(0)
let z = sqrt(4)
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& block = result.config.control_block.value();

    EXPECT_NEAR(block.variables.at("x"), 0.0, 1e-10);
    EXPECT_NEAR(block.variables.at("y"), 1.0, 1e-10);
    EXPECT_NEAR(block.variables.at("z"), 2.0, 1e-10);
}

//==============================================================================
// Analysis Command Execution Tests
//==============================================================================

TEST(ControlBlockAnalysis, DcCommandExecution) {
    std::string netlist = R"(
DC Command Execution Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
dc V1 0 10 1
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that DC sweep config was updated
    EXPECT_TRUE(result.config.dc_sweep_enabled);
    EXPECT_EQ(result.config.dc_sweep.source_name, "V1");
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.start_value, 0.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.stop_value, 10.0);
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.step_value, 1.0);
}

TEST(ControlBlockAnalysis, AcCommandExecution) {
    std::string netlist = R"(
AC Command Execution Test
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1u
.CONTROL
ac dec 10 1 1meg
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that AC config was updated
    EXPECT_EQ(result.config.ac.sweep_type, SimulationConfig::AC::SweepType::DEC);
    EXPECT_EQ(result.config.ac.num_points, 10u);
    EXPECT_DOUBLE_EQ(result.config.ac.start_freq, 1.0);
    EXPECT_DOUBLE_EQ(result.config.ac.stop_freq, 1e6);
}

TEST(ControlBlockAnalysis, TranCommandExecution) {
    std::string netlist = R"(
TRAN Command Execution Test
V1 in 0 DC 0 PULSE(0 5 0 1n 1n 50n 100n)
R1 in out 1k
C1 out 0 1u
.CONTROL
tran 1n 100n
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that transient config was updated
    EXPECT_DOUBLE_EQ(result.config.transient.initial_step, 1e-9);
    EXPECT_DOUBLE_EQ(result.config.transient.max_step, 1e-9);
    EXPECT_DOUBLE_EQ(result.config.transient.end_time, 100e-9);
    EXPECT_DOUBLE_EQ(result.config.transient.start_time, 0.0);
}

TEST(ControlBlockAnalysis, TranCommandWithOptionalArgs) {
    std::string netlist = R"(
TRAN with Optional Args Test
V1 in 0 DC 0 PULSE(0 5 0 1n 1n 50n 100n)
R1 in out 1k
C1 out 0 1u
.CONTROL
tran 1n 100n 10n 0.5n
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that transient config was updated with all args
    EXPECT_DOUBLE_EQ(result.config.transient.initial_step, 1e-9);
    EXPECT_DOUBLE_EQ(result.config.transient.end_time, 100e-9);
    EXPECT_DOUBLE_EQ(result.config.transient.start_time, 10e-9);
    EXPECT_DOUBLE_EQ(result.config.transient.max_step, 0.5e-9);  // tmax overrides initial tstep
}

TEST(ControlBlockAnalysis, PrintCommandExecution) {
    std::string netlist = R"(
PRINT Command Execution Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
print v(out) v(in)
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that output signals were configured
    EXPECT_FALSE(result.config.output.save_all);
    ASSERT_EQ(result.config.output.save_signals.size(), 2u);
    EXPECT_EQ(result.config.output.save_signals[0], "v(out)");
    EXPECT_EQ(result.config.output.save_signals[1], "v(in)");
}

TEST(ControlBlockAnalysis, PrintCommandWithAnalysisType) {
    std::string netlist = R"(
PRINT with Analysis Type Test
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1u
.CONTROL
ac dec 10 1 1meg
print ac v(out)
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that analysis type is skipped and only signals are saved
    EXPECT_FALSE(result.config.output.save_all);
    ASSERT_EQ(result.config.output.save_signals.size(), 1u);
    EXPECT_EQ(result.config.output.save_signals[0], "v(out)");
}

TEST(ControlBlockAnalysis, MultipleCommands) {
    std::string netlist = R"(
Multiple Analysis Commands Test
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1u
.CONTROL
dc V1 0 5 0.5
ac dec 10 1 1meg
tran 1n 100n
print v(out) v(in)
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check DC config
    EXPECT_TRUE(result.config.dc_sweep_enabled);
    EXPECT_EQ(result.config.dc_sweep.source_name, "V1");
    EXPECT_DOUBLE_EQ(result.config.dc_sweep.step_value, 0.5);

    // Check AC config
    EXPECT_EQ(result.config.ac.sweep_type, SimulationConfig::AC::SweepType::DEC);

    // Check Transient config
    EXPECT_DOUBLE_EQ(result.config.transient.end_time, 100e-9);

    // Check output config
    EXPECT_EQ(result.config.output.save_signals.size(), 2u);
}

// IF/ELSE/END Control Flow Tests

TEST(ControlBlockControlFlow, IfCommandParsing) {
    std::string netlist = R"(
IF Command Parsing Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
if 1 > 0
  echo Condition is true
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;

    // Should have 3 commands: IF, ECHO, END
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].type, ControlCommandType::IF);
    EXPECT_EQ(commands[1].type, ControlCommandType::ECHO);
    EXPECT_EQ(commands[2].type, ControlCommandType::END);

    // Check IF args (should be "1 > 0" split into tokens)
    EXPECT_GE(commands[0].args.size(), 3u);
}

TEST(ControlBlockControlFlow, IfElseCommandParsing) {
    std::string netlist = R"(
IF-ELSE Command Parsing Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
if 1 < 0
  echo Condition is true
else
  echo Condition is false
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;

    // Should have 5 commands: IF, ECHO, ELSE, ECHO, END
    ASSERT_EQ(commands.size(), 5u);
    EXPECT_EQ(commands[0].type, ControlCommandType::IF);
    EXPECT_EQ(commands[1].type, ControlCommandType::ECHO);
    EXPECT_EQ(commands[2].type, ControlCommandType::ELSE);
    EXPECT_EQ(commands[3].type, ControlCommandType::ECHO);
    EXPECT_EQ(commands[4].type, ControlCommandType::END);
}

TEST(ControlBlockControlFlow, IfTrueExecution) {
    std::string netlist = R"(
IF True Execution Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let x = 0
if 1 > 0
  let x = 10
end
let y = x
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Variable x should be set to 10 (IF condition true)
    ASSERT_TRUE(vars.count("x") > 0);
    EXPECT_DOUBLE_EQ(vars.at("x"), 10.0);

    // Variable y should equal x (10)
    ASSERT_TRUE(vars.count("y") > 0);
    EXPECT_DOUBLE_EQ(vars.at("y"), 10.0);
}

TEST(ControlBlockControlFlow, IfFalseExecution) {
    std::string netlist = R"(
IF False Execution Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let x = 0
if 1 < 0
  let x = 10
end
let y = x
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Variable x should remain 0 (IF condition false)
    ASSERT_TRUE(vars.count("x") > 0);
    EXPECT_DOUBLE_EQ(vars.at("x"), 0.0);

    // Variable y should equal x (0)
    ASSERT_TRUE(vars.count("y") > 0);
    EXPECT_DOUBLE_EQ(vars.at("y"), 0.0);
}

TEST(ControlBlockControlFlow, IfElseTrueBranch) {
    std::string netlist = R"(
IF-ELSE True Branch Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let result = 0
if 2 + 2 == 4
  let result = 100
else
  let result = 200
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Should execute IF branch (result = 100)
    ASSERT_TRUE(vars.count("result") > 0);
    EXPECT_DOUBLE_EQ(vars.at("result"), 100.0);
}

TEST(ControlBlockControlFlow, IfElseFalseBranch) {
    std::string netlist = R"(
IF-ELSE False Branch Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let result = 0
if 2 + 2 == 5
  let result = 100
else
  let result = 200
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Should execute ELSE branch (result = 200)
    ASSERT_TRUE(vars.count("result") > 0);
    EXPECT_DOUBLE_EQ(vars.at("result"), 200.0);
}

TEST(ControlBlockControlFlow, NestedIfStatements) {
    std::string netlist = R"(
Nested IF Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let result = 0
if 1 > 0
  if 2 > 1
    let result = 42
  end
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Both conditions true, should set result to 42
    ASSERT_TRUE(vars.count("result") > 0);
    EXPECT_DOUBLE_EQ(vars.at("result"), 42.0);
}

// FOREACH Loop Tests

TEST(ControlBlockControlFlow, ForeachCommandParsing) {
    std::string netlist = R"(
FOREACH Command Parsing Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
foreach value 1 2 3
  echo Value is value
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;

    // Should have 3 commands: FOREACH, ECHO, END
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].type, ControlCommandType::FOREACH);
    EXPECT_EQ(commands[1].type, ControlCommandType::ECHO);
    EXPECT_EQ(commands[2].type, ControlCommandType::END);

    // Check FOREACH args: var name + values
    ASSERT_GE(commands[0].args.size(), 4u);  // value, 1, 2, 3
    EXPECT_EQ(commands[0].args[0], "value");
}

TEST(ControlBlockControlFlow, ForeachSimpleLoop) {
    std::string netlist = R"(
FOREACH Simple Loop Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let total = 0
foreach x 1 2 3 4 5
  let total = total + x
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Total should be 1 + 2 + 3 + 4 + 5 = 15
    ASSERT_TRUE(vars.count("total") > 0);
    EXPECT_DOUBLE_EQ(vars.at("total"), 15.0);

    // Loop variable x should have last value (5)
    ASSERT_TRUE(vars.count("x") > 0);
    EXPECT_DOUBLE_EQ(vars.at("x"), 5.0);
}

TEST(ControlBlockControlFlow, ForeachWithUnits) {
    std::string netlist = R"(
FOREACH with Units Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let total = 0
foreach r 1k 2k 3k
  let total = total + r
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Total should be 1k + 2k + 3k = 6000
    ASSERT_TRUE(vars.count("total") > 0);
    EXPECT_DOUBLE_EQ(vars.at("total"), 6000.0);
}

TEST(ControlBlockControlFlow, ForeachWithIfCondition) {
    std::string netlist = R"(
FOREACH with IF Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let count = 0
foreach x 1 2 3 4 5 6 7 8 9 10
  if x > 5
    let count = count + 1
  end
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Count should be 5 (values 6, 7, 8, 9, 10)
    ASSERT_TRUE(vars.count("count") > 0);
    EXPECT_DOUBLE_EQ(vars.at("count"), 5.0);
}

TEST(ControlBlockControlFlow, NestedForeachLoops) {
    std::string netlist = R"(
Nested FOREACH Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let total = 0
foreach i 1 2 3
  foreach j 10 20
    let total = total + i * j
  end
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Total should be:
    // i=1: 1*10 + 1*20 = 30
    // i=2: 2*10 + 2*20 = 60
    // i=3: 3*10 + 3*20 = 90
    // Grand Total: 180
    ASSERT_TRUE(vars.count("total") > 0);
    EXPECT_DOUBLE_EQ(vars.at("total"), 180.0);
}

//==============================================================================
// WHILE/REPEAT Loop Tests
//==============================================================================

TEST(ControlBlockControlFlow, WhileCommandParsing) {
    std::string netlist = R"(
WHILE Command Parsing Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
while counter < 5
  echo Iteration
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;

    // Should have 3 commands: WHILE, ECHO, END
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].type, ControlCommandType::WHILE);
    EXPECT_EQ(commands[1].type, ControlCommandType::ECHO);
    EXPECT_EQ(commands[2].type, ControlCommandType::END);

    // Check WHILE args: condition expression
    ASSERT_GE(commands[0].args.size(), 1u);
    EXPECT_EQ(commands[0].args[0], "counter");
}

TEST(ControlBlockControlFlow, WhileSimpleLoop) {
    std::string netlist = R"(
WHILE Simple Loop Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let counter = 0
let total = 0
while counter < 5
  let total = total + counter
  let counter = counter + 1
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Total should be 0 + 1 + 2 + 3 + 4 = 10
    ASSERT_TRUE(vars.count("total") > 0);
    EXPECT_DOUBLE_EQ(vars.at("total"), 10.0);

    // Counter should be 5 (exit condition)
    ASSERT_TRUE(vars.count("counter") > 0);
    EXPECT_DOUBLE_EQ(vars.at("counter"), 5.0);
}

TEST(ControlBlockControlFlow, WhileWithCondition) {
    std::string netlist = R"(
WHILE with Condition Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let x = 100
let count = 0
while x > 1
  let x = x / 2
  let count = count + 1
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // x should be halved until <= 1: 100 -> 50 -> 25 -> 12.5 -> 6.25 -> 3.125 -> 1.5625 -> 0.78125
    // Count should be 7
    ASSERT_TRUE(vars.count("count") > 0);
    EXPECT_DOUBLE_EQ(vars.at("count"), 7.0);

    // x should be <= 1
    ASSERT_TRUE(vars.count("x") > 0);
    EXPECT_LT(vars.at("x"), 1.0);
}

TEST(ControlBlockControlFlow, WhileWithIfCondition) {
    std::string netlist = R"(
WHILE with IF Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let i = 0
let even_sum = 0
while i < 10
  if i % 2 == 0
    let even_sum = even_sum + i
  end
  let i = i + 1
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Even sum should be 0 + 2 + 4 + 6 + 8 = 20
    ASSERT_TRUE(vars.count("even_sum") > 0);
    EXPECT_DOUBLE_EQ(vars.at("even_sum"), 20.0);
}

TEST(ControlBlockControlFlow, RepeatCommandParsing) {
    std::string netlist = R"(
REPEAT Command Parsing Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
repeat 5
  echo Iteration
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;

    // Should have 3 commands: REPEAT, ECHO, END
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].type, ControlCommandType::REPEAT);
    EXPECT_EQ(commands[1].type, ControlCommandType::ECHO);
    EXPECT_EQ(commands[2].type, ControlCommandType::END);

    // Check REPEAT args: count
    ASSERT_EQ(commands[0].args.size(), 1u);
    EXPECT_EQ(commands[0].args[0], "5");
}

TEST(ControlBlockControlFlow, RepeatSimpleLoop) {
    std::string netlist = R"(
REPEAT Simple Loop Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let counter = 0
repeat 7
  let counter = counter + 1
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Counter should be 7 (repeated 7 times)
    ASSERT_TRUE(vars.count("counter") > 0);
    EXPECT_DOUBLE_EQ(vars.at("counter"), 7.0);
}

TEST(ControlBlockControlFlow, RepeatWithCalculation) {
    std::string netlist = R"(
REPEAT with Calculation Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let total = 1
repeat 5
  let total = total * 2
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Total should be 1 * 2^5 = 32
    ASSERT_TRUE(vars.count("total") > 0);
    EXPECT_DOUBLE_EQ(vars.at("total"), 32.0);
}

TEST(ControlBlockControlFlow, NestedWhileRepeat) {
    std::string netlist = R"(
Nested WHILE and REPEAT Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let total = 0
let i = 0
while i < 3
  repeat 2
    let total = total + 1
  end
  let i = i + 1
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Total should be 3 * 2 = 6 (3 while iterations, each with 2 repeat iterations)
    ASSERT_TRUE(vars.count("total") > 0);
    EXPECT_DOUBLE_EQ(vars.at("total"), 6.0);
}

TEST(ControlBlockControlFlow, NestedRepeatWhile) {
    std::string netlist = R"(
Nested REPEAT and WHILE Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
let grand_total = 0
repeat 2
  let j = 0
  while j < 4
    let grand_total = grand_total + j
    let j = j + 1
  end
end
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& vars = result.config.control_block->variables;

    // Each WHILE adds 0 + 1 + 2 + 3 = 6
    // REPEAT does this 2 times: 6 * 2 = 12
    ASSERT_TRUE(vars.count("grand_total") > 0);
    EXPECT_DOUBLE_EQ(vars.at("grand_total"), 12.0);
}

//==============================================================================
// ALTER Command Tests
//==============================================================================

TEST(ControlBlockAlterCommand, AlterCommandParsing) {
    std::string netlist = R"(
ALTER Command Parsing Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
alter R1 resistance=2k
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());
    const auto& commands = result.config.control_block->commands;

    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands[0].type, ControlCommandType::ALTER);
    EXPECT_EQ(commands[0].args.size(), 2);
    EXPECT_EQ(commands[0].args[0], "R1");
    EXPECT_EQ(commands[0].args[1], "resistance=2k");
}

TEST(ControlBlockAlterCommand, AlterResistorValue) {
    std::string netlist = R"(
ALTER Resistor Value Test
V1 in 0 DC 5
R1 in out 1k
R2 out 0 2k
.CONTROL
alter R1 resistance=3k
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());

    // Get R1 device and check if parameter was changed
    auto r1 = result.circuit->get_device("R1");
    ASSERT_NE(r1, nullptr);

    // After alter command execution, R1 should have resistance=3k
    auto resistance = r1->get_parameter("resistance");
    ASSERT_TRUE(resistance.has_value());
    EXPECT_DOUBLE_EQ(resistance.value(), 3000.0);
}

TEST(ControlBlockAlterCommand, AlterMultipleDevices) {
    std::string netlist = R"(
ALTER Multiple Devices Test
V1 in 0 DC 5
R1 in n1 1k
R2 n1 out 2k
C1 out 0 1u
.CONTROL
alter R1 resistance=5k
alter R2 resistance=10k
alter C1 capacitance=2u
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());

    // Check R1
    auto r1 = result.circuit->get_device("R1");
    ASSERT_NE(r1, nullptr);
    auto r1_resistance = r1->get_parameter("resistance");
    ASSERT_TRUE(r1_resistance.has_value());
    EXPECT_DOUBLE_EQ(r1_resistance.value(), 5000.0);

    // Check R2
    auto r2 = result.circuit->get_device("R2");
    ASSERT_NE(r2, nullptr);
    auto r2_resistance = r2->get_parameter("resistance");
    ASSERT_TRUE(r2_resistance.has_value());
    EXPECT_DOUBLE_EQ(r2_resistance.value(), 10000.0);

    // Check C1
    auto c1 = result.circuit->get_device("C1");
    ASSERT_NE(c1, nullptr);
    auto c1_capacitance = c1->get_parameter("capacitance");
    ASSERT_TRUE(c1_capacitance.has_value());
    EXPECT_DOUBLE_EQ(c1_capacitance.value(), 2e-6);
}

TEST(ControlBlockAlterCommand, AlterInvalidDevice) {
    std::string netlist = R"(
ALTER Invalid Device Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
alter R999 resistance=3k
.ENDC
.END
)";

    parser::NetlistParser parser;
    // Should not throw, but log an error
    EXPECT_NO_THROW({
        auto result = parser.parse_string(netlist);
    });
}

TEST(ControlBlockAlterCommand, AlterInvalidSyntax) {
    std::string netlist = R"(
ALTER Invalid Syntax Test
V1 in 0 DC 5
R1 in out 1k
.CONTROL
alter R1 resistance 3k
.ENDC
.END
)";

    parser::NetlistParser parser;
    // Should not throw, but log an error (missing '=')
    EXPECT_NO_THROW({
        auto result = parser.parse_string(netlist);
    });
}

TEST(ControlBlockAlterCommand, AlterWithUnits) {
    std::string netlist = R"(
ALTER With Units Test
V1 in 0 DC 5
R1 in out 1k
C1 out 0 1u
L1 out 0 1m
.CONTROL
alter R1 resistance=4.7k
alter C1 capacitance=100n
alter L1 inductance=10m
.ENDC
.END
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.control_block.has_value());

    // Check R1
    auto r1 = result.circuit->get_device("R1");
    ASSERT_NE(r1, nullptr);
    auto r1_resistance = r1->get_parameter("resistance");
    ASSERT_TRUE(r1_resistance.has_value());
    EXPECT_DOUBLE_EQ(r1_resistance.value(), 4700.0);

    // Check C1
    auto c1 = result.circuit->get_device("C1");
    ASSERT_NE(c1, nullptr);
    auto c1_capacitance = c1->get_parameter("capacitance");
    ASSERT_TRUE(c1_capacitance.has_value());
    EXPECT_DOUBLE_EQ(c1_capacitance.value(), 100e-9);

    // Check L1
    auto l1 = result.circuit->get_device("L1");
    ASSERT_NE(l1, nullptr);
    auto l1_inductance = l1->get_parameter("inductance");
    ASSERT_TRUE(l1_inductance.has_value());
    EXPECT_DOUBLE_EQ(l1_inductance.value(), 10e-3);
}

// Note: Variable expansion in ALTER command (e.g., alter R1 resistance={r})
// is a complex feature that requires parser-level support for expression substitution
// in command arguments. This is beyond the scope of and will be
// addressed in a future phase when full expression substitution is implemented.

// Model names are compared without regard to case, as in SPICE: an element
// finds its model whatever the case of the name in the element line and in
// the .MODEL line.
TEST(NetlistParser, ModelNamesIgnoreCase) {
    std::string netlist = R"(
Model names in mixed case
.model dmod D(IS=1e-9 N=2)
.model QMod NPN(IS=2e-15 BF=150)
.model nch NMOS(LEVEL=1 VTO=0.5 KP=100u)
V1 in 0 DC 1
D1 in 0 DMOD
Q1 in in 0 qmod
M1 in in 0 0 NCH W=10u L=1u
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    auto diode = std::dynamic_pointer_cast<devices::Diode>(result.circuit->get_device("D1"));
    ASSERT_NE(diode, nullptr);
    EXPECT_DOUBLE_EQ(diode->saturation_current(), 1e-9);
    EXPECT_DOUBLE_EQ(diode->emission_coefficient(), 2.0);

    auto bjt = std::dynamic_pointer_cast<devices::BJT_NPN>(result.circuit->get_device("Q1"));
    ASSERT_NE(bjt, nullptr);
    EXPECT_DOUBLE_EQ(bjt->saturation_current(), 2e-15);
    EXPECT_DOUBLE_EQ(bjt->forward_beta(), 150.0);

    auto mosfet = std::dynamic_pointer_cast<devices::MOSFET_N>(result.circuit->get_device("M1"));
    ASSERT_NE(mosfet, nullptr);
    EXPECT_DOUBLE_EQ(mosfet->threshold_voltage(), 0.5);
    EXPECT_DOUBLE_EQ(mosfet->process_transconductance(), 100e-6);
}

// A .MODEL whose name differs from an earlier one only in case defines it
// again: the first definition is used, as in ngspice-47 (run 2026-11-21).
TEST(NetlistParser, ModelRedefinitionIgnoresCase) {
    std::string netlist = R"(
Model redefined with other case
.model dm D(IS=1e-9)
.MODEL DM D(IS=3e-9)
V1 in 0 DC 1
D1 in 0 dm
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    EXPECT_EQ(result.config.models.size(), 1u);
    auto diode = std::dynamic_pointer_cast<devices::Diode>(result.circuit->get_device("D1"));
    ASSERT_NE(diode, nullptr);
    EXPECT_DOUBLE_EQ(diode->saturation_current(), 1e-9);
}

// A .MODEL line applies to the whole netlist wherever it stands, so elements
// may come before the models they use.
TEST(NetlistParser, ModelDefinedAfterElement) {
    std::string netlist = R"(
Models after the elements
V1 in 0 DC 1
D1 in 0 DMOD
Q1 in in 0 QMOD
M1 in in 0 0 NCH W=10u L=1u
M2 in in 0 NCH W=10u L=1u
.MODEL DMOD D(IS=1e-9 N=2)
.MODEL QMOD NPN(IS=2e-15 BF=150)
.MODEL NCH NMOS(LEVEL=1 VTO=0.5 KP=100u)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    auto diode = std::dynamic_pointer_cast<devices::Diode>(result.circuit->get_device("D1"));
    ASSERT_NE(diode, nullptr);
    EXPECT_DOUBLE_EQ(diode->saturation_current(), 1e-9);
    EXPECT_DOUBLE_EQ(diode->emission_coefficient(), 2.0);

    auto bjt = std::dynamic_pointer_cast<devices::BJT_NPN>(result.circuit->get_device("Q1"));
    ASSERT_NE(bjt, nullptr);
    EXPECT_DOUBLE_EQ(bjt->saturation_current(), 2e-15);
    EXPECT_DOUBLE_EQ(bjt->forward_beta(), 150.0);

    for (const char* name : {"M1", "M2"}) {
        auto mosfet = std::dynamic_pointer_cast<devices::MOSFET_N>(result.circuit->get_device(name));
        ASSERT_NE(mosfet, nullptr) << name;
        EXPECT_DOUBLE_EQ(mosfet->threshold_voltage(), 0.5) << name;
        EXPECT_DOUBLE_EQ(mosfet->process_transconductance(), 100e-6) << name;
    }
}

// A model that is never defined is an error, reported at the element's line.
TEST(NetlistParser, UndefinedDiodeModelIsAnError) {
    std::string netlist = R"(
Undefined diode model
V1 in 0 DC 1
D1 in 0 NOSUCH
.end
)";

    parser::NetlistParser parser;
    try {
        parser.parse_string(netlist);
        FAIL() << "expected a ParseError";
    } catch (const parser::ParseError& e) {
        EXPECT_EQ(e.line(), 3u);
        EXPECT_NE(std::string(e.what()).find("NOSUCH"), std::string::npos) << e.what();
    }
}

// A parameter assignment whose value is a brace expression, NAME={expr},
// is one token, on element lines as on .SUBCKT lines.
TEST(NetlistParser, BraceExpressionAfterEqualsIsOneToken) {
    std::string netlist = R"(
Brace expressions after name=
.PARAM ISAT=1e-9 RT=3k
.SUBCKT DIV a b RTOP={RT}
R1 a b {RTOP}
R2 b 0 1k
.ENDS
V1 in 0 DC 5
R1 in a 1k
D1 a 0 IS={ISAT} N=2
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    auto diode = std::dynamic_pointer_cast<devices::Diode>(result.circuit->get_device("D1"));
    ASSERT_NE(diode, nullptr);
    EXPECT_DOUBLE_EQ(diode->saturation_current(), 1e-9);
    EXPECT_DOUBLE_EQ(diode->emission_coefficient(), 2.0);

    const auto& div = result.circuit->subcircuits().get_subcircuit("DIV");
    EXPECT_DOUBLE_EQ(div.default_params.at("RTOP"), 3000.0);
}

// B1 0 out I={0.001*V(in)} written with braces: with V(in) = 1 V the source
// drives 1 mA into 1 kOhm, so V(out) = 1 V.
TEST(NetlistParser, BehavioralCurrentSourceWithBraces) {
    std::string netlist = R"(
B current source with braces
V1 in 0 DC 1
R1 in 0 1k
B1 0 out I={0.001*V(in)}
R2 out 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    const NodeId out = result.circuit->get_node_id("out");
    EXPECT_NEAR(solution[out - 1], 1.0, 1e-9);
}

// NETLIST_CONVENTIONS rule 2: a source is an AC input only if its line has
// AC; AC alone means magnitude 1; the same holds inside subcircuits.
TEST(NetlistParser, AcInputOnlyWithTheKeywordAc) {
    std::string netlist = R"(
AC inputs
.SUBCKT SRC p
VS p 0 DC 3
.ENDS
.SUBCKT SRCAC p
VA p 0 DC 0 AC 0.5 90
.ENDS
V1 a 0 DC 5
V2 b 0 DC 0 AC
V3 c 0 DC 0 AC 2 45
I1 0 d DC 1m
I2 0 e DC 0 AC
X1 f SRC
X2 g SRCAC
R1 a 0 1k
R2 b 0 1k
R3 c 0 1k
R4 d 0 1k
R5 e 0 1k
R6 f 0 1k
R7 g 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);
    result.circuit->expand_all_subcircuits();

    auto vsrc = [&](const std::string& name) {
        auto v = std::dynamic_pointer_cast<devices::VoltageSource>(result.circuit->get_device(name));
        EXPECT_NE(v, nullptr) << name;
        return v;
    };
    auto isrc = [&](const std::string& name) {
        auto i = std::dynamic_pointer_cast<devices::CurrentSource>(result.circuit->get_device(name));
        EXPECT_NE(i, nullptr) << name;
        return i;
    };
    EXPECT_DOUBLE_EQ(vsrc("V1")->ac_magnitude(), 0.0);
    EXPECT_DOUBLE_EQ(vsrc("V2")->ac_magnitude(), 1.0);
    EXPECT_DOUBLE_EQ(vsrc("V3")->ac_magnitude(), 2.0);
    EXPECT_DOUBLE_EQ(vsrc("V3")->ac_phase(), 45.0);
    EXPECT_DOUBLE_EQ(isrc("I1")->ac_magnitude(), 0.0);
    EXPECT_DOUBLE_EQ(isrc("I2")->ac_magnitude(), 1.0);
    EXPECT_DOUBLE_EQ(vsrc("X1.VS")->ac_magnitude(), 0.0);
    EXPECT_DOUBLE_EQ(vsrc("X2.VA")->ac_magnitude(), 0.5);
    EXPECT_DOUBLE_EQ(vsrc("X2.VA")->ac_phase(), 90.0);
}

// NETLIST_CONVENTIONS rule 5: gnd in any case is ground at the top level, as
// it is inside subcircuits.
TEST(NetlistParser, GndInAnyCaseIsGround) {
    std::string netlist = R"(
gnd names
V1 a gnd DC 1
R1 a GND 1k
R2 a Gnd 1k
R3 a 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.circuit->get_node_id("gnd"), NODE_GND);
    EXPECT_EQ(result.circuit->get_node_id("GND"), NODE_GND);
    EXPECT_TRUE(result.circuit->has_node("Gnd"));
    EXPECT_EQ(result.circuit->num_nodes(), 2u);  // 0 and a
}

// NETLIST_CONVENTIONS rule 3: the current of V, E and H sources is positive
// into n+ through the source, and F and H use that current. With V1 = 1 V
// across 1 kOhm, ngspice-47 gave i(V1) = -1 mA, v(f) = -3 V for F1 0 f V1 3,
// v(h) = -0.1 V for H1 h 0 V1 100, and i(E1) = -2 mA for E1 driving 2 V into
// 1 kOhm; these follow from the rule with Ohm's law.
TEST(NetlistParser, SourceCurrentsFollowNgspiceSign) {
    std::string netlist = R"(
Source current sign
V1 in 0 DC 1
R1 in 0 1k
E1 e 0 in 0 2
Re e 0 1k
F1 0 f V1 3
Rf f 0 1k
H1 h 0 V1 100
Rh h 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    auto v = [&](const std::string& node) { return solution[result.circuit->get_node_id(node) - 1]; };
    EXPECT_NEAR(v("f"), -3.0, 1e-9);
    EXPECT_NEAR(v("h"), -0.1, 1e-9);
    EXPECT_NEAR(solver.get_branch_current("V1").value(), -1e-3, 1e-12);
    EXPECT_NEAR(solver.get_branch_current("E1").value(), -2e-3, 1e-12);
    // H drives -0.1 V into 1 kOhm, so 0.1 mA flows into its n+
    EXPECT_NEAR(solver.get_branch_current("H1").value(), 1e-4, 1e-12);
}

// A MOSFET card's LEVEL selects the device; a LEVEL that no device has is an
// error at the card, as in ngspice-47 ("Error on line 4"), not level 1.
TEST(ModelLevel, UnknownLevelIsAnError) {
    for (const char* level : {"2", "99", "1.5"}) {
        parser::NetlistParser parser;
        try {
            parser.parse_string(std::string("level\nM1 d g 0 0 m\n.model m nmos level=") + level +
                                "\n.end\n");
            FAIL() << "LEVEL=" << level << " was accepted";
        } catch (const parser::ParseError& e) {
            EXPECT_EQ(e.line(), 3u) << level;
        }
    }
    parser::NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_string("level\nM1 d g 0 0 m\n.model m nmos level=1\n.end\n"));
}

// A PMF card without VTO is the p-channel depletion device the PMF keyword
// gives, VTO = +2 V (MESFET::default_vto), not the n-channel default -2 V:
// parse_mesfet_device. ngspice-47 (run 2026-11-21) conducts at VGS = 0 with
// such a card: I(VD) = 2.407 mA for .model pm PMF beta=1e-3 lambda=0 b=0.3
// alpha=2 at VDS = -1 V, as an NMF card mirrored.
TEST(ModelLevel, PmfCardWithoutVtoIsDepletion) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "pmf\n.model pm PMF beta=1e-3 lambda=0\n.model nm NMF beta=1e-3 lambda=0\n"
        "VD d 0 -1\nVG g 0 0\nZ1 d g 0 pm\nVD2 d2 0 -1\nZ2 d2 g 0 PMF beta=1e-3 lambda=0\n"
        "Z3 n g 0 nm\nR3 n 0 1k\n.op\n.end\n");
    const auto z1 = std::dynamic_pointer_cast<devices::MESFET_P>(parsed.circuit->get_device("Z1"));
    const auto z2 = std::dynamic_pointer_cast<devices::MESFET_P>(parsed.circuit->get_device("Z2"));
    const auto z3 = std::dynamic_pointer_cast<devices::MESFET_N>(parsed.circuit->get_device("Z3"));
    ASSERT_NE(z1, nullptr);
    ASSERT_NE(z2, nullptr);
    ASSERT_NE(z3, nullptr);
    EXPECT_DOUBLE_EQ(z1->vto(), 2.0);
    EXPECT_DOUBLE_EQ(z2->vto(), 2.0);
    EXPECT_DOUBLE_EQ(z3->vto(), -2.0);
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    // The card and the keyword give the same device, and it conducts
    const Real card = solver.get_branch_current("VD").value();
    const Real keyword = solver.get_branch_current("VD2").value();
    EXPECT_GT(keyword, 1e-4);
    EXPECT_NEAR(card, keyword, 1e-15);
}
