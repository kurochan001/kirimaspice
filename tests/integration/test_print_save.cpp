// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/integration/test_print_save.cpp
// .print/.save directive tests

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/output/ssf_writer.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

class PrintSaveTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::warn);  // Reduce log noise in tests
    }
};

//==============================================================================
// Test 1: .PRINT directive parsing
//==============================================================================
TEST_F(PrintSaveTest, PrintDirectiveParsing) {
    std::string netlist = R"(
.title Test Print Directive Parsing
V1 1 0 DC 5.0
R1 1 0 1k
.dc
.print dc v(1) i(v1)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.output.print_signals.size(), 2);
    EXPECT_EQ(result.config.output.print_signals[0], "v(1)");
    EXPECT_EQ(result.config.output.print_signals[1], "i(v1)");
    EXPECT_TRUE(result.config.output.save_all);  // Default: save all
}

//==============================================================================
// Test 2: .SAVE directive parsing
//==============================================================================
TEST_F(PrintSaveTest, SaveDirectiveParsing) {
    std::string netlist = R"(
.title Test Save Directive Parsing
V1 1 0 DC 5.0
R1 1 2 1k
R2 2 0 2k
.dc
.save v(1) v(2)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.output.save_signals.size(), 2);
    EXPECT_EQ(result.config.output.save_signals[0], "v(1)");
    EXPECT_EQ(result.config.output.save_signals[1], "v(2)");
    EXPECT_FALSE(result.config.output.save_all);  // .SAVE disables save_all

    // .SAVE signals are also added to .PRINT
    ASSERT_GE(result.config.output.print_signals.size(), 2);
}

//==============================================================================
// Test 3: Multiple signals in .PRINT
//==============================================================================
TEST_F(PrintSaveTest, PrintMultipleSignals) {
    std::string netlist = R"(
.title Multiple Signals in PRINT
V1 1 0 DC 5.0
R1 1 2 1k
R2 2 3 2k
R3 3 0 1k
.dc
.print dc v(1) v(2) v(3) i(v1)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.output.print_signals.size(), 4);
    EXPECT_EQ(result.config.output.print_signals[0], "v(1)");
    EXPECT_EQ(result.config.output.print_signals[1], "v(2)");
    EXPECT_EQ(result.config.output.print_signals[2], "v(3)");
    EXPECT_EQ(result.config.output.print_signals[3], "i(v1)");
}

//==============================================================================
// Test 4: .SAVE overrides default save_all
//==============================================================================
TEST_F(PrintSaveTest, SaveOverridesDefault) {
    std::string netlist = R"(
.title Save Overrides Default Behavior
V1 1 0 DC 10.0
R1 1 2 1k
R2 2 3 1k
R3 3 0 1k
.dc
.save v(2)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    // Without .SAVE, save_all would be true
    EXPECT_FALSE(result.config.output.save_all);
    EXPECT_EQ(result.config.output.save_signals.size(), 1);
}

//==============================================================================
// Test 5: No .PRINT/.SAVE (default behavior)
//==============================================================================
TEST_F(PrintSaveTest, DefaultBehaviorNoDirectives) {
    std::string netlist = R"(
.title No Print/Save Directives
V1 1 0 DC 5.0
R1 1 0 1k
.dc
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    // Default: save_all = true, no specific signals
    EXPECT_TRUE(result.config.output.save_all);
    EXPECT_EQ(result.config.output.print_signals.size(), 0);
    EXPECT_EQ(result.config.output.save_signals.size(), 0);
}

//==============================================================================
// Test 6: .PRINT and .SAVE combined
//==============================================================================
TEST_F(PrintSaveTest, PrintAndSaveCombined) {
    std::string netlist = R"(
.title Print and Save Combined
V1 1 0 DC 10.0
R1 1 2 1k
R2 2 3 1k
R3 3 0 1k
.dc
.print dc v(1) v(2) v(3)
.save v(2)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    auto& config = result.config;

    // .PRINT adds 3 signals
    EXPECT_GE(config.output.print_signals.size(), 3);

    // .SAVE adds 1 signal (and also to print_signals)
    EXPECT_EQ(config.output.save_signals.size(), 1);
    EXPECT_EQ(config.output.save_signals[0], "v(2)");

    // save_all is disabled by .SAVE
    EXPECT_FALSE(config.output.save_all);
}

//==============================================================================
// Test 7: Invalid .PRINT syntax (missing analysis type)
//==============================================================================
TEST_F(PrintSaveTest, InvalidPrintSyntaxMissingAnalysisType) {
    std::string netlist = R"(
.title Invalid Print Syntax
V1 1 0 DC 5.0
R1 1 0 1k
.dc
.print v(1)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    // Parser should warn but continue
    // No signals should be added due to invalid syntax
    EXPECT_EQ(result.config.output.print_signals.size(), 0);
}

//==============================================================================
// Test 8: Case insensitive .PRINT/.SAVE
//==============================================================================
TEST_F(PrintSaveTest, CaseInsensitiveDirectives) {
    std::string netlist = R"(
.title Case Insensitive Directives
V1 1 0 DC 5.0
R1 1 2 1k
R2 2 0 1k
.dc
.PRINT DC V(1) V(2)
.SAVE V(2)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    auto& config = result.config;
    EXPECT_GE(config.output.print_signals.size(), 2);
    EXPECT_EQ(config.output.save_signals.size(), 1);
}

//==============================================================================
// Test 9: .PRINT for transient analysis
//==============================================================================
TEST_F(PrintSaveTest, PrintTransientAnalysis) {
    std::string netlist = R"(
.title Transient Print Directive
V1 1 0 DC 0 PULSE(0 5 0 1n 1n 10u 20u)
R1 1 2 1k
C1 2 0 1u
.tran 1n 30u
.print tran v(2)
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    ASSERT_EQ(result.config.output.print_signals.size(), 1);
    EXPECT_EQ(result.config.output.print_signals[0], "v(2)");
}

//==============================================================================
// Test 10: SSF Writer with output config
//==============================================================================
TEST_F(PrintSaveTest, SSFWriterWithOutputConfig) {
    // Test that SSFWriter can receive output config without crashing
    SimulationConfig config;
    config.output.save_all = false;
    config.output.save_signals.push_back("v(1)");
    config.output.save_signals.push_back("v(2)");
    config.output.print_signals.push_back("v(1)");
    config.output.print_signals.push_back("v(2)");
    config.output.print_signals.push_back("v(3)");

    // Create SSF writer and set output config
    output::SSFWriter writer("/tmp/test_output_config.ssf", 0);
    writer.set_output_config(config.output);

    // Just verify the call succeeds
    SUCCEED();
}
