// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// How the lines of a netlist are joined, selected and split before the
// elements are read. ngspice-47 reads each netlist below the same way (run
// on 2026-11-15 with an .OP and a print of the source current).

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"

using namespace kirimaspice;

namespace {

std::vector<std::string> device_names(const parser::NetlistParseResult& parsed) {
    std::vector<std::string> names;
    for (const auto& device : parsed.circuit->get_devices()) {
        std::string name = device->name();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

template <typename T>
std::shared_ptr<T> device_as(const parser::NetlistParseResult& parsed, const std::string& name) {
    return std::dynamic_pointer_cast<T>(parsed.circuit->get_device(name));
}

}  // namespace

// A '+' line continues the line before it, and a ';' comment on that line
// ends at the end of its own line (preprocess_continuations)
TEST(NetlistLines, ContinuationAfterAnInlineComment) {
    parser::NetlistParser parser;
    const auto parsed =
        parser.parse_string("t\nV1 a 0 1\nR1 a 0 ; load resistor\n+ 1k\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const auto r1 = device_as<devices::Resistor>(parsed, "R1");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(r1->resistance(), 1e3);
}

// Comment lines and empty lines between a line and its continuation do not
// end the line (preprocess_continuations)
TEST(NetlistLines, ContinuationAcrossACommentLine) {
    parser::NetlistParser parser;
    const auto parsed =
        parser.parse_string("t\nV1 a 0 1\nR1 a 0\n* the value\n\n+ 1k\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const auto r1 = device_as<devices::Resistor>(parsed, "R1");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(r1->resistance(), 1e3);
}

// The instance parameters of a MOSFET continued after a ';' comment
// (preprocess_continuations)
TEST(NetlistLines, InstanceParametersAfterAnInlineComment) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\n.model nch nmos (vto=0.5 kp=1e-4)\nVD d 0 1\nVG g 0 1\n"
        "M1 d g 0 0 nch ; the input device\n+ W=100u L=1u\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const auto m1 = device_as<devices::MOSFET_N_Level1>(parsed, "M1");
    ASSERT_NE(m1, nullptr);
    EXPECT_DOUBLE_EQ(m1->channel_width(), 100e-6);
    EXPECT_DOUBLE_EQ(m1->channel_length(), 1e-6);
}

// A '+' line right after the title continues the title, not an element
// (preprocess_continuations)
TEST(NetlistLines, ContinuationOfTheTitle) {
    parser::NetlistParser parser;
    const auto parsed =
        parser.parse_string("my title\n+ R9 a 0 1\nV1 a 0 1\nR1 a 0 1k\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    EXPECT_EQ(parsed.circuit->get_devices().size(), 2u);
}

// A .ELSE is taken only when no branch of its .IF was taken and the branch
// that holds the .IF is taken (preprocess_conditionals): inside a branch
// not taken, neither branch of a nested .IF is
TEST(NetlistLines, ElseInsideABranchNotTaken) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\nV1 a 0 1\nR1 a 0 1k\n.if (0)\n.if (0)\nR2 a 0 1k\n.else\nR3 a 0 1\n.endif\n"
        ".endif\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    EXPECT_EQ(device_names(parsed), (std::vector<std::string>{"R1", "V1"}));
}

// ... and inside a branch taken, the .ELSE of a false .IF is
TEST(NetlistLines, ElseInsideABranchTaken) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\nV1 a 0 1\nR1 a 0 1k\n.if (1)\n.if (0)\nR2 a 0 1k\n.else\nR3 a 0 1\n.endif\n"
        ".endif\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    EXPECT_EQ(device_names(parsed), (std::vector<std::string>{"R1", "R3", "V1"}));
}
