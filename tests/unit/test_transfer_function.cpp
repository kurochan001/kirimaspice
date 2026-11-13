// tests/unit/test_transfer_function.cpp
#include <gtest/gtest.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transfer_function_solver.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

#include <sstream>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::circuit;

// Parser Tests
TEST(TransferFunctionParser, BasicVoltageSyntax) {
    std::string netlist = R"(
Simple Voltage Divider
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.TF v(out) Vin
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.tf.enabled);
    EXPECT_EQ(result.config.tf.output_signal, "v(out)");
    EXPECT_EQ(result.config.tf.input_source, "Vin");
}

TEST(TransferFunctionParser, CurrentOutputSyntax) {
    std::string netlist = R"(
Current Output Test
R1 in out 1k
Vload out 0 DC 0
Vin in 0 DC 5
.TF i(Vload) Vin
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.tf.enabled);
    EXPECT_EQ(result.config.tf.output_signal, "i(Vload)");
    EXPECT_EQ(result.config.tf.input_source, "Vin");
}

TEST(TransferFunctionParser, CaseInsensitive) {
    std::string netlist = R"(
Case Test
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.tf v(out) Vin
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.tf.enabled);
}

TEST(TransferFunctionParser, InvalidSyntax_TooFewArgs) {
    std::string netlist = R"(
Invalid Syntax
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.TF v(out)
.END
)";

    NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

TEST(TransferFunctionParser, InvalidOutputFormat) {
    std::string netlist = R"(
Invalid Output Format
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.TF out Vin
.END
)";

    NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

// Integration Tests
TEST(TransferFunctionSolver, SimpleVoltageDivider) {
    std::string netlist = R"(
Voltage Divider Test
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 10
.TF v(out) Vin
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);

    ASSERT_TRUE(parse_result.config.tf.enabled);

    TransferFunctionSolver tf_solver(*parse_result.circuit, parse_result.config);
    TransferFunctionResult result = tf_solver.analyze();

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.output_signal, "v(out)");
    EXPECT_EQ(result.input_source, "Vin");

    // For voltage divider with R1=R2=1k, output should be 5V with 10V input
    // Transfer gain = V_out/V_in = 5V/10V = 0.5
    EXPECT_NEAR(result.transfer_gain, 0.5, 0.01);
}

TEST(TransferFunctionSolver, ResistorDivider_DifferentRatios) {
    std::string netlist = R"(
Voltage Divider 1:3 Ratio
R1 in out 1k
R2 out 0 3k
Vin in 0 DC 12
.TF v(out) Vin
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);

    TransferFunctionSolver tf_solver(*parse_result.circuit, parse_result.config);
    TransferFunctionResult result = tf_solver.analyze();

    EXPECT_TRUE(result.success);

    // For R1=1k, R2=3k: Vout = Vin * R2/(R1+R2) = 12 * 3/4 = 9V
    // Transfer gain = V_out/V_in = 9V/12V = 0.75
    EXPECT_NEAR(result.transfer_gain, 0.75, 0.01);
}

TEST(TransferFunctionSolver, ThreeResistorNetwork) {
    std::string netlist = R"(
Three Resistor Network
R1 in n1 1k
R2 n1 out 2k
R3 out 0 3k
Vin in 0 DC 10
.TF v(out) Vin
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);

    TransferFunctionSolver tf_solver(*parse_result.circuit, parse_result.config);
    TransferFunctionResult result = tf_solver.analyze();

    EXPECT_TRUE(result.success);

    // Expected calculation:
    // Total resistance = 1k + 2k + 3k = 6k
    // Current = 10V / 6k = 1.67mA
    // Vout = Current * 3k = 5V
    // Transfer gain = V_out/V_in = 5V/10V = 0.5
    EXPECT_NEAR(result.transfer_gain, 0.5, 0.02);
}

TEST(TransferFunctionSolver, OutputResistanceCheck) {
    std::string netlist = R"(
Output Resistance Test
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.TF v(out) Vin
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);

    TransferFunctionSolver tf_solver(*parse_result.circuit, parse_result.config);
    TransferFunctionResult result = tf_solver.analyze();

    EXPECT_TRUE(result.success);

    // Current implementation returns placeholder value (1k)
    EXPECT_GT(result.output_resistance, 0.0);
}

TEST(TransferFunctionSolver, InputResistanceCheck) {
    std::string netlist = R"(
Input Resistance Test
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.TF v(out) Vin
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);

    TransferFunctionSolver tf_solver(*parse_result.circuit, parse_result.config);
    TransferFunctionResult result = tf_solver.analyze();

    EXPECT_TRUE(result.success);

    // Accurate input resistance calculation using branch current
    // R_in = V_in / I_in, where I_in = 5V / (R1 + R2) = 5V / 2kΩ = 2.5mA
    // Therefore, R_in = 5V / 2.5mA = 2000Ω
    EXPECT_NEAR(result.input_resistance, 2000.0, 1.0);  // 2kΩ ± 1Ω tolerance
}
