// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

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
    std::string netlist = R"(Simple Voltage Divider
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
    std::string netlist = R"(Current Output Test
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
    std::string netlist = R"(Case Test
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

// The letter of the output is read in either case, as the rest of a netlist
// is: V(out) and I(Vload) are the outputs of BasicVoltageSyntax and
// CurrentOutputSyntax with the letter in upper case, kept as written
TEST(TransferFunctionParser, UpperCaseOutput) {
    for (const std::string output : {"V(out)", "I(Vload)"}) {
        const std::string netlist =
            "Upper case\nR1 in out 1k\nVload out 0 DC 0\nVin in 0 DC 5\n.TF " + output +
            " Vin\n.END\n";
        NetlistParser parser;
        auto result = parser.parse_string(netlist);
        EXPECT_TRUE(result.config.tf.enabled) << output;
        EXPECT_EQ(result.config.tf.output_signal, output) << output;
        EXPECT_EQ(result.config.tf.input_source, "Vin") << output;
    }
}

TEST(TransferFunctionParser, InvalidSyntax_TooFewArgs) {
    std::string netlist = R"(Invalid Syntax
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
    std::string netlist = R"(Invalid Output Format
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
    std::string netlist = R"(Voltage Divider Test
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
    std::string netlist = R"(Voltage Divider 1:3 Ratio
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
    std::string netlist = R"(Three Resistor Network
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
    std::string netlist = R"(Output Resistance Test
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
    std::string netlist = R"(Input Resistance Test
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

// The transfer function of the circuit linearized at its operating point,
// as SPICE's .TF gives it (TransferFunctionSolver::analyze through
// ACSolver::solve_excited at omega = 0): ngspice-45.2 gives these gains and
// input and output resistances (run 2026-11-28). They need the small-signal
// models of the devices: the VCVS's output is 0 Ohm, the diode's
// conductance at its operating point sets all three, a current source as
// input gives a transresistance and the voltage across it, v(b,c) is a
// difference of two nodes, and an inductor is the short of its DC model.
TEST(TransferFunctionSolver, MatchesNgspice) {
    const struct {
        std::string what, lines;
        Real gain, rin, rout;
    } cases[] = {
        {"vcvs", "V1 in 0 1\nR1 in b 1k\nR2 b 0 1k\nE1 out 0 b 0 10\nRo out 0 1k\n.tf v(out) V1\n", 5.0, 2000.0, 0.0},
        {"diode", "V1 in 0 1\nR1 in b 1k\nD1 b 0 dm\n.model dm D IS=1e-14\n.tf v(b) V1\n", 6.524539e-02,
         1.069799e+03, 6.524539e+01},
        {"current source", "I1 0 x 1m\nR3 x 0 2k\nR4 x y 1k\nR5 y 0 1k\n.tf v(y) I1\n", 500.0, 1000.0, 750.0},
        {"two nodes", "V1 in 0 1\nR1 in b 1k\nR2 b c 1k\nR3 c 0 1k\n.tf v(b,c) V1\n", 1.0 / 3.0, 3000.0,
         2000.0 / 3.0},
        {"inductor", "V1 in 0 1\nL1 in b 1m\nC1 b 0 1u\nR2 b 0 1k\n.tf v(b) V1\n", 1.0, 1000.0, 0.0},
    };
    for (const auto& c : cases) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\n" + c.lines + ".end\n");
        TransferFunctionSolver solver(*parsed.circuit, parsed.config);
        const auto result = solver.analyze();
        ASSERT_TRUE(result.success) << c.what;
        EXPECT_NEAR(result.transfer_gain, c.gain, 1e-5 * std::abs(c.gain)) << c.what;
        EXPECT_NEAR(result.input_resistance, c.rin, 1e-5 * c.rin) << c.what;
        // The inductor's DC short is 1e6 S, so its output resistance is 1 uOhm
        EXPECT_NEAR(result.output_resistance, c.rout, 1e-5 * c.rout + 2e-6) << c.what;
    }
}

// V( and I( in upper case are the outputs that v( and i( are: ngspice-45.2
// gives these values for these lines (run 2026-11-28), which are R2/(R1 + R2),
// R1 + R2 and R1 R2/(R1 + R2) of the divider, and 1/(R1 + R2) for the current
TEST(TransferFunctionSolver, UpperCaseOutputMatchesNgspice) {
    const struct {
        std::string what, lines;
        Real gain, rin, rout;
    } cases[] = {
        {"voltage", "V1 in 0 1\nR1 in a 1k\nR2 a 0 3k\n.TF V(a) V1\n", 0.75, 4000.0, 750.0},
        {"current", "V1 in 0 1\nR1 in a 1k\nVm a b 0\nR2 b 0 3k\n.TF I(Vm) V1\n", 2.5e-4, 4000.0,
         1e20},
    };
    for (const auto& c : cases) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\n" + c.lines + ".end\n");
        TransferFunctionSolver solver(*parsed.circuit, parsed.config);
        const auto result = solver.analyze();
        ASSERT_TRUE(result.success) << c.what;
        EXPECT_NEAR(result.transfer_gain, c.gain, 1e-6 * std::abs(c.gain)) << c.what;
        EXPECT_NEAR(result.input_resistance, c.rin, 1e-6 * c.rin) << c.what;
        EXPECT_NEAR(result.output_resistance, c.rout, 1e-6 * c.rout) << c.what;
    }
}

// A coupling of inductors (K) leaves the small-signal circuit at DC as the
// DC analysis has it: each inductor is the short of its DC model, and the
// coupling adds nothing (MutualInductance::stamp_ac at omega = 0), for a
// pair and for a group of three. ngspice-45.2 gives these values (run
// 2026-11-28): the dividers R1, R2 with L1 between them as a wire.
TEST(TransferFunctionSolver, CoupledInductorsMatchNgspice) {
    const struct {
        std::string what, lines;
        Real gain, rin, rout;
    } cases[] = {
        {"pair",
         "V1 in 0 1\nR1 in a 1k\nL1 a b 1m\nR2 b 0 1k\nL2 c 0 1m\nR3 c 0 1k\n"
         "K1 L1 L2 0.5\n.tf v(b) V1\n",
         0.5, 2000.0, 500.0},
        {"group of three",
         "V1 in 0 1\nR1 in a 1k\nL1 a b 1m\nR2 b 0 3k\nL2 c 0 2m\nR3 c 0 1k\nL3 d 0 4m\nR4 d 0 1k\n"
         "K1 L1 L2 0.5\nK2 L2 L3 0.4\nK3 L1 L3 0.3\n.tf v(b) V1\n",
         0.75, 4000.0, 750.0},
    };
    for (const auto& c : cases) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\n" + c.lines + ".end\n");
        TransferFunctionSolver solver(*parsed.circuit, parsed.config);
        const auto result = solver.analyze();
        ASSERT_TRUE(result.success) << c.what;
        // The inductor's DC short is 1e6 S: 1 uOhm in series with the divider
        EXPECT_NEAR(result.transfer_gain, c.gain, 1e-6 * std::abs(c.gain)) << c.what;
        EXPECT_NEAR(result.input_resistance, c.rin, 1e-6 * c.rin) << c.what;
        EXPECT_NEAR(result.output_resistance, c.rout, 1e-6 * c.rout) << c.what;
    }
}

// The current of an element that is not a voltage source, i(R1), is not an
// output: the analysis fails with a message instead of a value
TEST(TransferFunctionSolver, CurrentOfAResistorIsNotAnOutput) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 in 0 1\nR1 in 0 1k\n.tf i(R1) V1\n.end\n");
    TransferFunctionSolver solver(*parsed.circuit, parsed.config);
    EXPECT_FALSE(solver.analyze().success);
}

// A current output, i(<source>) of a voltage source: the gain is that
// source's branch current per unit of the input, positive into its +
// terminal, and the output resistance 1e20, as ngspice-45.2 prints for every
// current output tried (run 2026-11-28)
TEST(TransferFunctionSolver, CurrentOutputMatchesNgspice) {
    const struct {
        std::string what, lines;
        Real gain, rin;
    } cases[] = {
        {"series", "V1 in 0 1\nR1 in b 1k\nVm b c 0\nR2 c 0 1k\n.tf i(Vm) V1\n", 5e-4, 2000.0},
        {"divided", "V1 in 0 1\nR1 in b 1k\nVm b c 0\nR2 c 0 3k\nR3 b 0 2k\n.tf i(Vm) V1\n", 1.818182e-04, 2200.0},
        {"current source", "I1 0 b 1m\nR1 b 0 1k\nVm b c 0\nR2 c 0 1k\n.tf i(Vm) I1\n", 0.5, 500.0},
        {"vcvs", "V1 in 0 1\nR1 in b 1k\nR2 b 0 1k\nE1 x 0 b 0 2\nVm x y 0\nR3 y 0 1k\n.tf i(Vm) V1\n", 1e-3,
         2000.0},
    };
    for (const auto& c : cases) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\n" + c.lines + ".end\n");
        TransferFunctionSolver solver(*parsed.circuit, parsed.config);
        const auto result = solver.analyze();
        ASSERT_TRUE(result.success) << c.what;
        EXPECT_NEAR(result.transfer_gain, c.gain, 1e-6 * std::abs(c.gain)) << c.what;
        EXPECT_NEAR(result.input_resistance, c.rin, 1e-6 * c.rin) << c.what;
        EXPECT_EQ(result.output_resistance, 1e20) << c.what;
    }
}
