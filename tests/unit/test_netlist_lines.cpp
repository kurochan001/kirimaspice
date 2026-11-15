// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// How the lines of a netlist are joined, selected and split before the
// elements are read. ngspice-47 reads each netlist below the same way (run
// on 2026-11-15 with an .OP and a print of the source current).

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

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

// A new directory of its own under the system's temporary directory, removed
// with everything in it at the end of the test
class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::random_device random;
        for (;;) {
            path_ = std::filesystem::temp_directory_path() /
                    ("kirimaspice-netlist-lines-" + std::to_string(random()));
            if (std::filesystem::create_directory(path_)) {
                break;
            }
        }
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path write(const std::string& name, const std::string& text) const {
        const auto file = path_ / name;
        std::ofstream(file) << text;
        return file;
    }

private:
    std::filesystem::path path_;
};

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

// A .SUBCKT ends with .ENDS; without it the lines after it are not its body
// but an error (NetlistParser::Impl::parse)
TEST(NetlistLines, SubcircuitWithoutEnds) {
    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string("t\nV1 in 0 1\nR0 in 0 1k\n.subckt load a b\nR1 a b 1k\n"
                                     "R2 in 0 1\nX1 in 0 load\n.op\n.end\n"),
                 parser::ParseError);
}

// A .MODEL parameter may have blanks on either side of its '='
// (parse_model_directive)
TEST(NetlistLines, ModelParameterSpacing) {
    for (const std::string vto : {"vto=0.3", "vto= 0.3", "vto =0.3", "vto = 0.3"}) {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string("t\n.model nch nmos (kp=1e-4 " + vto +
                                                ")\nVD d 0 1\nVG g 0 1\n"
                                                "M1 d g 0 0 nch W=10u L=1u\n.op\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr) << vto;
        const auto m1 = device_as<devices::MOSFET_N_Level1>(parsed, "M1");
        ASSERT_NE(m1, nullptr) << vto;
        EXPECT_DOUBLE_EQ(m1->threshold_voltage(), 0.3) << vto;
        EXPECT_DOUBLE_EQ(m1->process_transconductance(), 1e-4) << vto;
    }
}

// A .LIB section may insert another section of the same file; only a section
// that is already being inserted is circular (preprocess_lib_sections).
// ngspice-47 reads the first netlist so and draws 0.5 mA from V1.
TEST(NetlistLines, LibrarySectionFromTheSameFile) {
    const TemporaryDirectory directory;
    directory.write("corners.lib",
                    "* corners\n.lib tt\n.param rval=2k\n.lib 'corners.lib' common\n.endl tt\n"
                    ".lib common\nR1 in 0 {rval}\n.endl common\n");
    const auto netlist =
        directory.write("top.cir", "t\n.lib 'corners.lib' tt\nV1 in 0 1\n.op\n.end\n");
    parser::NetlistParser parser;
    const auto parsed = parser.parse_file(netlist.string());
    ASSERT_NE(parsed.circuit, nullptr);
    const auto r1 = device_as<devices::Resistor>(parsed, "R1");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(r1->resistance(), 2e3);
}

TEST(NetlistLines, LibrarySectionThatInsertsItself) {
    const TemporaryDirectory directory;
    directory.write("self.lib", "* self\n.lib tt\nR1 in 0 1k\n.lib 'self.lib' tt\n.endl tt\n");
    const auto netlist =
        directory.write("top.cir", "t\n.lib 'self.lib' tt\nV1 in 0 1\n.op\n.end\n");
    parser::NetlistParser parser;
    try {
        parser.parse_file(netlist.string());
        ADD_FAILURE() << "no error";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("Circular"), std::string::npos) << e.what();
    }
}

// Blanks may stand on either side of the '=' of an assignment, on any line
// (util::split_netlist_line); ngspice-47 draws 1/2k + 1/3k + 1/4k from V1
TEST(NetlistLines, ParameterAssignmentsWithBlanks) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\n.param ra ={2*1000} rb= 3000 rc = {4*1000}\nV1 in 0 1\nR1 in 0 {ra}\n"
        "R2 in 0 {rb}\nR3 in 0 {rc}\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R1")->resistance(), 2e3);
    EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R2")->resistance(), 3e3);
    EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R3")->resistance(), 4e3);
}

// ... on an element line; ngspice-47 draws 1.25 mA from VD
TEST(NetlistLines, InstanceParametersWithBlanks) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\n.model nch nmos (vto=0.5 kp=1e-4)\nVD d 0 1\nVG g 0 1\n"
        "M1 d g 0 0 nch W = 100u L =1u\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const auto m1 = device_as<devices::MOSFET_N_Level1>(parsed, "M1");
    ASSERT_NE(m1, nullptr);
    EXPECT_DOUBLE_EQ(m1->channel_width(), 100e-6);
    EXPECT_DOUBLE_EQ(m1->channel_length(), 1e-6);
}

// A brace expression on a .MODEL line may hold blanks, as anywhere else
// (parse_model_directive); ngspice-47 draws 125 uA from VD, with VTO 0.5 V
TEST(NetlistLines, ModelExpressionWithBlanks) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\n.param a=0.25\n.model nch nmos (kp=1e-4 vto={a * 2})\nVD d 0 1\nVG g 0 1\n"
        "M1 d g 0 0 nch W=10u L=1u\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const auto m1 = device_as<devices::MOSFET_N_Level1>(parsed, "M1");
    ASSERT_NE(m1, nullptr);
    EXPECT_DOUBLE_EQ(m1->threshold_voltage(), 0.5);
}

// A B source's expression without braces runs to the end of the line
// (parse_behavioral_source_device); ngspice-47 gives V(o) = 2 V for both
TEST(NetlistLines, BehavioralExpressionWithBlanks) {
    for (const std::string b : {"B1 o 0 V=V(a) + 1", "B1 o 0 V = V(a) >= 0.5 ? 2 : 0"}) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\nV1 a 0 1\n" + b + "\nR1 o 0 1k\n.op\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr) << b;
        solvers::DCSolver solver(*parsed.circuit, parsed.config);
        DenseVector x;
        SolverStats stats;
        ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << b;
        EXPECT_NEAR(x[parsed.circuit->get_node_id("o") - 1], 2.0, 1e-9) << b;
    }
}

// An element name is used once (CircuitGraph::register_device); ngspice-47
// stops with "device already exists"
TEST(NetlistLines, ElementNameUsedTwice) {
    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string("t\nV1 in 0 1\nR1 in 0 1k\nR1 in 0 1k\n.op\n.end\n"),
                 parser::ParseError);
}

// The POLY form of a controlled source is not implemented, so it is an error,
// not a linear source with POLY(1) for a node (reject_poly_form); a node whose
// name starts with "poly" is a node
TEST(NetlistLines, PolyFormOfAControlledSource) {
    for (const std::string e : {"E1 out 0 POLY(1) in 0 0 2", "G1 out 0 poly (1) in 0 0 2",
                                "F1 out 0 POLY(1) V1 0 2", "H1 out 0 POLY(1) V1 0 2"}) {
        parser::NetlistParser parser;
        EXPECT_THROW(parser.parse_string("t\nV1 in 0 1\n" + e + "\nR1 out 0 1k\n.op\n.end\n"),
                     parser::ParseError)
            << e;
    }
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\nV1 polynode 0 1\nE1 out 0 polynode 0 2\nR1 out 0 1k\n.op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    EXPECT_EQ(parsed.circuit->get_devices().size(), 3u);
}

// The number of points of .AC is a whole number of at least 1
// (read_ac_point_count); another is an error at the .AC line, not a sweep
// with its fraction dropped or with no points
TEST(NetlistLines, AcPointCount) {
    const std::string circuit = "t\nV1 in 0 DC 0 AC 1\nR1 in out 1k\nC1 out 0 1u\n";
    {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string(circuit + ".ac dec 10 10 1k\n.end\n");
        EXPECT_EQ(parsed.config.ac.num_points, 10u);
    }
    for (const std::string count : {"-1", "0", "2.5", "x"}) {
        parser::NetlistParser parser;
        EXPECT_THROW(parser.parse_string(circuit + ".ac dec " + count + " 10 1k\n.end\n"),
                     parser::ParseError)
            << count;
    }
}
