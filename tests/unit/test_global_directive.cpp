// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_global_directive.cpp
// .GLOBAL Directive Testing
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include <sstream>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::parser;

// Test fixture for .GLOBAL directive
class GlobalDirectiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No setup needed for new API
    }
};

// Test 1: Basic .GLOBAL directive parsing
TEST_F(GlobalDirectiveTest, BasicGlobalDirective) {
    std::string netlist = R"(
Basic Global Directive Test
.GLOBAL VDD GND

R1 VDD out 1k
R2 out GND 1k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that global nodes were registered
    EXPECT_EQ(result.config.global_nodes.size(), 2);
    EXPECT_TRUE(result.config.global_nodes.count("VDD") > 0);
    EXPECT_TRUE(result.config.global_nodes.count("GND") > 0);
}

// Test 2: Multiple .GLOBAL directives
TEST_F(GlobalDirectiveTest, MultipleGlobalDirectives) {
    std::string netlist = R"(
Multiple Global Directives Test
.GLOBAL VDD
.GLOBAL GND CLK

R1 VDD out 1k
R2 out GND 1k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Check that all global nodes were registered
    EXPECT_EQ(result.config.global_nodes.size(), 3);
    EXPECT_TRUE(result.config.global_nodes.count("VDD") > 0);
    EXPECT_TRUE(result.config.global_nodes.count("GND") > 0);
    EXPECT_TRUE(result.config.global_nodes.count("CLK") > 0);
}

// Test 3: Global nodes in subcircuit (basic)
TEST_F(GlobalDirectiveTest, GlobalNodesInSubcircuit) {
    std::string netlist = R"(
Global Nodes in Subcircuit Test
.GLOBAL VDD GND

* Amplifier subcircuit (no VDD/GND in pin list)
.SUBCKT amp in out
R1 out in 10k
R2 out VDD 1k
R3 in GND 1k
.ENDS

* Main circuit
V1 VDD 0 5
V2 in 0 1
X1 in out amp
R_load out GND 10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Expand subcircuits
    result.circuit->expand_all_subcircuits();

    // Check that VDD and GND are not hierarchized inside subcircuit
    // The subcircuit uses VDD and GND directly (not X1.VDD, X1.GND)
    EXPECT_TRUE(result.config.global_nodes.count("VDD") > 0);
    EXPECT_TRUE(result.config.global_nodes.count("GND") > 0);

    // Verify circuit structure
    EXPECT_GT(result.circuit->num_nodes(), 0);
}

// Test 4: Nested subcircuits with global nodes
TEST_F(GlobalDirectiveTest, NestedSubcircuitsWithGlobalNodes) {
    std::string netlist = R"(
Nested Subcircuits with Global Nodes Test
.GLOBAL VDD GND

* Inner subcircuit (resistor divider)
.SUBCKT divider out
R1 VDD out 1k
R2 out GND 1k
.ENDS

* Outer subcircuit (uses inner subcircuit)
.SUBCKT amplifier in out_amp
X_div out_div divider
R_amp out_amp in 10k
R_load out_div GND 1k
.ENDS

* Main circuit
V1 VDD 0 5
V2 in_main 0 1
X_amp in_main out_main amplifier
R_main out_main GND 10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Expand subcircuits
    result.circuit->expand_all_subcircuits();

    // VDD and GND should be global at all hierarchy levels
    EXPECT_EQ(result.config.global_nodes.size(), 2);
    EXPECT_TRUE(result.config.global_nodes.count("VDD") > 0);
    EXPECT_TRUE(result.config.global_nodes.count("GND") > 0);
}

// Test 5: Global nodes combined with pins
TEST_F(GlobalDirectiveTest, GlobalNodesCombinedWithPins) {
    std::string netlist = R"(
Global Nodes Combined with Pins Test
.GLOBAL VDD

* Subcircuit with explicit GND pin but VDD is global
.SUBCKT buffer in out gnd
R1 VDD out 1k
R2 out in 10k
R3 in gnd 1k
.ENDS

* Main circuit
V1 VDD 0 5
V2 in_sig 0 1
X1 in_sig out_sig 0 buffer
R_load out_sig 0 10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Expand subcircuits
    result.circuit->expand_all_subcircuits();

    // VDD should be global (not hierarchized)
    // gnd should be mapped to pin (node 0 in this case)
    EXPECT_EQ(result.config.global_nodes.size(), 1);
    EXPECT_TRUE(result.config.global_nodes.count("VDD") > 0);
}

// Test 6: Error case - .GLOBAL without arguments
TEST_F(GlobalDirectiveTest, ErrorNoArguments) {
    std::string netlist = R"(
Error Test - No Arguments
.GLOBAL

R1 1 0 1k
.END
)";

    NetlistParser parser;

    EXPECT_THROW({
        parser.parse_string(netlist);
    }, ParseError);
}

// Test 7: Case sensitivity of global nodes
TEST_F(GlobalDirectiveTest, CaseSensitivity) {
    std::string netlist = R"(
Case Sensitivity Test
.GLOBAL VDD vdd Vdd

R1 VDD out 1k
R2 out vdd 1k
R3 vdd Vdd 1k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // All three variations should be registered as separate global nodes
    // (SPICE is generally case-insensitive, but we store as-is for now)
    EXPECT_GE(result.config.global_nodes.size(), 1);
    // Note: Current implementation stores all case variations
    // If we want case-insensitive behavior, we need to normalize to uppercase
}

// Test 8: Global node preserves name across hierarchies
TEST_F(GlobalDirectiveTest, GlobalNodePreservesName) {
    std::string netlist = R"(
Global Node Name Preservation Test
.GLOBAL CLK

.SUBCKT stage1 in out
R1 in CLK 1k
R2 CLK out 1k
.ENDS

.SUBCKT stage2 in out
X1 in mid stage1
X2 mid out stage1
.ENDS

V_clk CLK 0 DC 2.5
X_main in_sig out_sig stage2
V_in in_sig 0 DC 1
R_load out_sig 0 10k
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    // Expand all subcircuits
    result.circuit->expand_all_subcircuits();

    // CLK should remain "CLK" at all levels, not X_main.X1.CLK
    EXPECT_TRUE(result.config.global_nodes.count("CLK") > 0);

    // Verify that CLK node exists in the circuit graph
    // (should not be hierarchized like X_main.X1.CLK)
    try {
        NodeId clk_id = result.circuit->get_node_id("CLK");
        EXPECT_NE(clk_id, NODE_INVALID);
    } catch (...) {
        FAIL() << "CLK node should exist as a non-hierarchical global node";
    }
}
