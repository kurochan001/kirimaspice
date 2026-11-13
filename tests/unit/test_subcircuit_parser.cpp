// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_subcircuit_parser.cpp
// Unit tests for subcircuit parsing (.SUBCKT, .ENDS, X devices)
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/circuit/subcircuit.hpp"
#include <gtest/gtest.h>

using namespace kirimaspice;
using namespace kirimaspice::parser;

class SubcircuitParserTest : public ::testing::Test {
protected:
    NetlistParser parser;
};

// Test basic .SUBCKT/.ENDS parsing
TEST_F(SubcircuitParserTest, BasicSubcircuitDefinition) {
    std::string netlist = R"(Simple subcircuit test
.SUBCKT BUFFER in out
R1 in out 1k
.ENDS BUFFER
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Check subcircuit was registered
    EXPECT_TRUE(result.circuit->subcircuits().has_subcircuit("BUFFER"));

    // Get subcircuit definition
    const auto& def = result.circuit->subcircuits().get_subcircuit("BUFFER");
    EXPECT_EQ(def.name, "BUFFER");
    EXPECT_EQ(def.pins.size(), 2);
    EXPECT_EQ(def.pins[0], "in");
    EXPECT_EQ(def.pins[1], "out");
    EXPECT_EQ(def.internal_netlist_lines.size(), 1);
    EXPECT_EQ(def.internal_netlist_lines[0], "R1 in out 1k");
}

// Test subcircuit with parameters
TEST_F(SubcircuitParserTest, SubcircuitWithParameters) {
    std::string netlist = R"(Subcircuit with parameters
.SUBCKT AMPLIFIER in out GAIN=10 OFFSET=0.5
R1 in out 1k
.ENDS AMPLIFIER
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    const auto& def = result.circuit->subcircuits().get_subcircuit("AMPLIFIER");
    EXPECT_EQ(def.name, "AMPLIFIER");
    EXPECT_EQ(def.pins.size(), 2);

    // Check parameters
    EXPECT_TRUE(def.has_parameter("GAIN"));
    EXPECT_TRUE(def.has_parameter("OFFSET"));
    EXPECT_DOUBLE_EQ(def.get_parameter("GAIN"), 10.0);
    EXPECT_DOUBLE_EQ(def.get_parameter("OFFSET"), 0.5);
}

// Test X device (subcircuit instance)
TEST_F(SubcircuitParserTest, SubcircuitInstance) {
    std::string netlist = R"(Subcircuit instance test
.SUBCKT BUFFER in out
R1 in out 1k
.ENDS BUFFER

X1 signal gnd BUFFER
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // Check instance was registered
    const auto& instances = result.circuit->subcircuits().get_instances();
    EXPECT_EQ(instances.size(), 1);

    const auto& inst = instances[0];
    EXPECT_EQ(inst.instance_name, "X1");
    EXPECT_EQ(inst.subcircuit_type, "BUFFER");
    EXPECT_EQ(inst.connected_nodes.size(), 2);
    EXPECT_EQ(inst.connected_nodes[0], "signal");
    EXPECT_EQ(inst.connected_nodes[1], "gnd");
}

// Test X device with parameter override
TEST_F(SubcircuitParserTest, SubcircuitInstanceWithParameters) {
    std::string netlist = R"(Instance with parameters
.SUBCKT AMP in out GAIN=10
R1 in out 1k
.ENDS AMP

X1 vin vout AMP GAIN=20
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    const auto& instances = result.circuit->subcircuits().get_instances();
    EXPECT_EQ(instances.size(), 1);

    const auto& inst = instances[0];
    EXPECT_EQ(inst.instance_name, "X1");
    EXPECT_EQ(inst.subcircuit_type, "AMP");
    EXPECT_EQ(inst.connected_nodes.size(), 2);

    // Check parameter override
    EXPECT_EQ(inst.instance_params.size(), 1);
    EXPECT_DOUBLE_EQ(inst.instance_params.at("GAIN"), 20.0);
}

// Test case-insensitive subcircuit names
TEST_F(SubcircuitParserTest, CaseInsensitiveNames) {
    std::string netlist = R"(Case insensitive test
.SUBCKT buffer in out
R1 in out 1k
.ENDS buffer

X1 a b BUFFER
X2 c d Buffer
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    // All should reference the same subcircuit (case-insensitive)
    EXPECT_TRUE(result.circuit->subcircuits().has_subcircuit("BUFFER"));
    EXPECT_TRUE(result.circuit->subcircuits().has_subcircuit("buffer"));
    EXPECT_TRUE(result.circuit->subcircuits().has_subcircuit("Buffer"));

    const auto& instances = result.circuit->subcircuits().get_instances();
    EXPECT_EQ(instances.size(), 2);
}

// Test error: nested .SUBCKT
TEST_F(SubcircuitParserTest, ErrorNestedSubcircuit) {
    std::string netlist = R"(Nested subcircuit error
.SUBCKT OUTER in out
.SUBCKT INNER a b
R1 a b 1k
.ENDS INNER
.ENDS OUTER
.END
)";

    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

// Test error: .ENDS without .SUBCKT
TEST_F(SubcircuitParserTest, ErrorEndsWithoutSubckt) {
    std::string netlist = R"(Unmatched .ENDS error
.ENDS ORPHAN
.END
)";

    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

// Test multiple subcircuit definitions
TEST_F(SubcircuitParserTest, MultipleSubcircuits) {
    std::string netlist = R"(Multiple subcircuits
.SUBCKT BUF1 in out
R1 in out 1k
.ENDS BUF1

.SUBCKT BUF2 in out
R2 in out 2k
.ENDS BUF2

X1 a b BUF1
X2 c d BUF2
.END
)";

    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    EXPECT_TRUE(result.circuit->subcircuits().has_subcircuit("BUF1"));
    EXPECT_TRUE(result.circuit->subcircuits().has_subcircuit("BUF2"));

    const auto& instances = result.circuit->subcircuits().get_instances();
    EXPECT_EQ(instances.size(), 2);
}
