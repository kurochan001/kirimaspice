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
#include "kirimaspice/devices/tline_lossy.hpp"
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

// The condition of .IF and .ELIF may follow the name with no blank, .if(0),
// and .ELSEIF is .ELIF (conditional_directive). ngspice-47 (run 2026-11-21)
// takes the .ELSEIF branch of the first netlist and the .ELSE branch of the
// second: I(V1) = -0.5 mA and -0.25 mA.
TEST(NetlistLines, ConditionRightAfterTheNameAndElseif) {
    {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string(
            "t\nV1 a 0 1\n.if(0)\nR1 a 0 1k\n.elseif(1)\nR1 a 0 2k\n.else\nR1 a 0 4k\n.endif\n"
            ".op\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr);
        EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R1")->resistance(), 2e3);
    }
    {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string(
            "t\nV1 a 0 1\n.IF(0)\nR1 a 0 1k\n.ELSEIF (0)\nR1 a 0 2k\n.ELSE\nR1 a 0 4k\n.ENDIF\n"
            ".op\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr);
        EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R1")->resistance(), 4e3);
    }
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

// A controlled source in the POLY form of dimension 1 with the coefficients
// 0 and g is the linear source of gain g, as in SPICE (POLY in any case, its
// dimension also after a blank). ngspice-45.2 gives V(out) = 2, -2, 2 and
// -2 V for these lines and for their linear forms alike, with I(V1) = -1 mA
// (run 2026-11-28). A node whose name starts with "poly" is a node.
TEST(NetlistLines, PolyFormOfAControlledSource) {
    const struct {
        std::string poly, linear;
        Real expected;
    } forms[] = {
        {"E1 out 0 POLY(1) in 0 0 2", "E1 out 0 in 0 2", 2.0},
        {"G1 out 0 poly (1) in 0 0 2m", "G1 out 0 in 0 2m", -2.0},
        {"F1 out 0 POLY(1) V1 0 2", "F1 out 0 V1 2", 2.0},
        {"H1 out 0 POLY(1) V1 0 2k", "H1 out 0 V1 2k", -2.0},
    };
    auto out = [](const std::string& line) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\nV1 in 0 1\nR0 in 0 1k\n" + line + "\nR1 out 0 1k\n.op\n.end\n");
        solvers::DCSolver solver(*parsed.circuit, parsed.config);
        DenseVector x;
        SolverStats stats;
        EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << line;
        return x.empty() ? 0.0 : x[parsed.circuit->get_node_id("out") - 1];
    };
    for (const auto& form : forms) {
        try {
            EXPECT_NEAR(out(form.poly), form.expected, 1e-9) << form.poly;
            EXPECT_NEAR(out(form.linear), form.expected, 1e-9) << form.linear;
        } catch (const std::exception& e) {
            ADD_FAILURE() << form.poly << ": " << e.what();
        }
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

// A .DC line has one sweep of four values or two of four
// (parse_dc_directive); a second sweep with fewer values, or values after the
// second, is an error, not a one-dimensional sweep with the rest dropped.
// ngspice-47 (run 2026-11-21) stops with "Bad syntax" for the first two; it
// runs the last with its ninth value ignored, which here is an error too.
TEST(NetlistLines, DcSweepValueCount) {
    const std::string circuit = "t\nV1 a 0 1\nV2 b 0 1\nR1 a b 1k\n";
    for (const std::string dc : {".dc V1 0 1 0.5 V2", ".dc V1 0 1 0.5 V2 0 1",
                                 ".dc V1 0 1 0.5 V2 0", ".dc V1 0 1 0.5 V2 0 1 0.5 7"}) {
        parser::NetlistParser parser;
        EXPECT_THROW(parser.parse_string(circuit + dc + "\n.end\n"), parser::ParseError) << dc;
    }
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(circuit + ".dc V1 0 1 0.5 V2 0 1 0.5\n.end\n");
    ASSERT_NE(parsed.config.dc_sweep.nested_sweep, nullptr);
    EXPECT_EQ(parsed.config.dc_sweep.nested_sweep->source_name, "V2");
}

// TSTART of a transient analysis is less than TSTOP, and a TMAX of 0 is not
// given, so the step limit is TSTEP; on a .TRAN line and in the tran command
// of a .CONTROL block alike (read_transient_times). ngspice-47 (run
// 2026-11-21) stops on .tran 1n 1u 2u and .tran 1n 1u 1u with "TSTART is
// invalid, must be less than TSTOP", runs .tran 10n 100n 0 0 with the 63 time
// points of .tran 10n 100n, and stops the run of .tran 10n 100n 0 -1n with
// "Timestep too small".
TEST(NetlistLines, TransientStartAndMaximumStep) {
    const std::string circuit = "t\nV1 a 0 PULSE(0 1 0 1n 1n 1u 2u)\nR1 a 0 1k\n";
    for (const std::string tran : {".tran 1n 1u 2u", ".tran 1n 1u 1u", ".tran 10n 100n 0 -1n"}) {
        parser::NetlistParser parser;
        EXPECT_THROW(parser.parse_string(circuit + tran + "\n.end\n"), parser::ParseError) << tran;
    }
    struct Case {
        const char* lines;
        Real max_step;
    };
    for (const Case& c : {Case{".tran 10n 100n 0 0\n", 10e-9}, Case{".tran 10n 100n 0 5n\n", 5e-9},
                          Case{".tran 10n 100n 20n\n", 10e-9},
                          Case{".tran 1n 1u\n.control\ntran 10n 100n 0 0\n.endc\n", 10e-9}}) {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string(circuit + c.lines + ".end\n");
        EXPECT_TRUE(parsed.config.tran_requested) << c.lines;
        EXPECT_DOUBLE_EQ(parsed.config.transient.initial_step, 10e-9) << c.lines;
        EXPECT_DOUBLE_EQ(parsed.config.transient.end_time, 100e-9) << c.lines;
        EXPECT_DOUBLE_EQ(parsed.config.transient.max_step, c.max_step) << c.lines;
    }
    // A TSTART that the last line omits stays as an earlier line set it
    // (docs/TRANSIENT_ANALYSIS.md, several .TRAN lines)
    parser::NetlistParser parser;
    const auto parsed =
        parser.parse_string(circuit + ".tran 1n 1u 20n\n.tran 10n 100n\n.end\n");
    EXPECT_DOUBLE_EQ(parsed.config.transient.start_time, 20e-9);
    EXPECT_DOUBLE_EQ(parsed.config.transient.max_step, 10e-9);
}

// The values of an O line are element values, numbers or expressions in
// braces, as on every element line (read_element_value)
TEST(NetlistLines, LossyLineValuesInBraces) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "t\n.param rr=2\nV1 a 0 1\nO1 a 0 b 0 R={rr} L={rr*1e-9} C=1p G=0 TD={2*1e-9}\nR1 b 0 50\n"
        ".op\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const auto o1 = device_as<devices::TLineLossy>(parsed, "O1");
    ASSERT_NE(o1, nullptr);
    EXPECT_DOUBLE_EQ(o1->r(), 2.0);
    EXPECT_DOUBLE_EQ(o1->l(), 2e-9);
    EXPECT_DOUBLE_EQ(o1->td(), 2e-9);
}

// Where a number is read, text after the number other than letters is an
// error, not dropped (parse_value_with_unit): R1 a 0 2*1k was 2 ohm, 1.5.3k
// was 1.5 kohm. ngspice-47 (run 2026-11-21) stops on the first ("unknown
// parameter (*)") and reads the second as 1.5 ohm.
TEST(NetlistLines, TextAfterANumberIsAnError) {
    for (const std::string line : {"R1 a 0 2*1k", "R1 a 0 1.5.3k", "C1 a 0 1n5", ".tran 1n 1u*2",
                                   ".model dm D IS=1e-14x2"}) {
        parser::NetlistParser parser;
        EXPECT_THROW(parser.parse_string("t\nV1 a 0 1\nR9 a 0 1k\n" + line + "\n.op\n.end\n"),
                     parser::ParseError)
            << line;
    }
}

// A .PARAM value without braces that is not a number is an expression, at the
// top level and in a subcircuit (util::read_parameter_value). ngspice-47 (run
// 2026-11-21) gives r = 2000 for .param r = 2*1000: I(V1) = -0.5 mA, and
// -0.5 mA with the .PARAM in the subcircuit; .param r = 2*1k is 2000 there
// as well, with the scale factor read in the expression.
TEST(NetlistLines, ParameterValueWithoutBraces) {
    {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string(
            "t\n.param r = 2*1000 q=1.5k\nV1 a 0 1\nR1 a 0 {r}\nR2 a 0 {q}\n.op\n.end\n");
        EXPECT_DOUBLE_EQ(parsed.config.global_params.at("r"), 2000.0);
        EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R1")->resistance(), 2e3);
        EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R2")->resistance(), 1.5e3);
    }
    {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string("t\n.param r = 2*1k\nV1 a 0 1\nR1 a 0 {r}\n.op\n.end\n");
        EXPECT_DOUBLE_EQ(parsed.config.global_params.at("r"), 2000.0);
    }
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.subckt load a\n.param rr = 2*1000\nR1 a 0 {rr}\n.ends\nV1 a 0 1\nX1 a load\n"
        ".op\n.end\n");
    parsed.circuit->expand_all_subcircuits();
    const auto r1 = device_as<devices::Resistor>(parsed, "X1.R1");
    ASSERT_NE(r1, nullptr);
    EXPECT_DOUBLE_EQ(r1->resistance(), 2e3);
}

// The .FUNC keyword is read in any case (parse_func_directive); the name of
// the function is the text before its '('. ngspice-47 (run 2026-11-21) gives
// I(V1) = -0.5 mA for .Func dbl(x) {2*x}, .param r={dbl(1000)}, R1 a 0 {r}.
TEST(NetlistLines, FuncKeywordInAnyCase) {
    for (const std::string keyword : {".func", ".FUNC", ".Func", ".fUnC"}) {
        parser::NetlistParser parser;
        const auto parsed = parser.parse_string("t\n" + keyword +
                                                " dbl(x) {2*x}\n.param r={dbl(1000)}\n"
                                                "V1 a 0 1\nR1 a 0 {r}\n.op\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr) << keyword;
        EXPECT_EQ(parsed.config.global_funcs.count("dbl"), 1u) << keyword;
        EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R1")->resistance(), 2e3)
            << keyword;
    }
}

// An element value that opens a brace and does not close it is an error
// (read_element_value), not the expression without its last character:
// ngspice-47 (run 2026-11-21) stops with 'Closing "}" not found' for {1k
TEST(NetlistLines, ElementValueWithoutItsClosingBrace) {
    for (const std::string r : {"R1 a 0 {1k", "R1 a 0 {", "C1 a 0 {1n"}) {
        parser::NetlistParser parser;
        EXPECT_THROW(parser.parse_string("t\nV1 a 0 1\n" + r + "\n.op\n.end\n"),
                     parser::ParseError)
            << r;
    }
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string("t\nV1 a 0 1\nR1 a 0 {2*1000}\n.op\n.end\n");
    EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R1")->resistance(), 2e3);
}

// Element names are not case-sensitive (CircuitGraph's device_key): R1 and
// r1 are one name, and F names its control in any case. ngspice-47 stops on
// the first ("device already exists") and gives V(out) = 2 V for the second.
TEST(NetlistLines, ElementNamesWithoutCase) {
    {
        parser::NetlistParser parser;
        EXPECT_THROW(parser.parse_string("t\nV1 in 0 1\nR1 in 0 1k\nr1 in 0 1k\n.op\n.end\n"),
                     parser::ParseError);
    }
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 in 0 1\nVsense in n1 0\nR1 n1 0 1k\nF1 0 out vsense 2\nR2 out 0 1k\n"
        ".dc v1 0 1 0.5\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("out") - 1], 2.0, 1e-9);
    EXPECT_NE(parsed.circuit->get_device("vSENSE"), nullptr);
    // .DC names its source in another case, and sweeps it
    DCSweepResult sweep;
    ASSERT_EQ(solver.solve_sweep(parsed.config.dc_sweep, sweep), ConvergenceStatus::SUCCESS);
    ASSERT_EQ(sweep.points.size(), 3u);
    EXPECT_NEAR(sweep.points.back().node_voltages[parsed.circuit->get_node_id("out") - 1], 2.0, 1e-9);
}

// A netlist whose lines end with CR LF is read as one whose lines end with LF
// (without_carriage_returns, as each text is read): a '+' line continues the
// line before it, .ENDIF and .ENDC end their blocks, and a file it includes
// is read so too. ngspice-47 (run 2026-11-21) reads the netlist so and gives
// V(b) = 0.5 V, I(V1) = -1 mA.
TEST(NetlistLines, LinesEndedByCarriageReturnAndLineFeed) {
    const std::string netlist =
        "crlf\r\nV1 a 0 1\r\nR1 a b 1k\r\nR3 b 0\r\n+ 1k\r\n.if (0)\r\nR5 a 0 1\r\n.endif\r\n"
        ".control\r\nop\r\n.endc\r\nR4 a 0 2k\r\n.op\r\n.end\r\n";
    auto check = [](const parser::NetlistParseResult& parsed) {
        ASSERT_NE(parsed.circuit, nullptr);
        EXPECT_EQ(device_names(parsed), (std::vector<std::string>{"R1", "R3", "R4", "V1"}));
        EXPECT_DOUBLE_EQ(device_as<devices::Resistor>(parsed, "R3")->resistance(), 1e3);
        EXPECT_TRUE(parsed.config.op_requested);
        solvers::DCSolver solver(*parsed.circuit, parsed.config);
        DenseVector x;
        SolverStats stats;
        ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
        EXPECT_NEAR(x[parsed.circuit->get_node_id("b") - 1], 0.5, 1e-12);
    };
    {
        parser::NetlistParser parser;
        check(parser.parse_string(netlist));
    }
    const TemporaryDirectory directory;
    directory.write("part.inc", "* part\r\nR3 b 0\r\n+ 1k\r\n");
    const auto file = directory.write(
        "top.cir",
        "crlf\r\nV1 a 0 1\r\nR1 a b 1k\r\n.include part.inc\r\nR4 a 0 2k\r\n.op\r\n.end\r\n");
    parser::NetlistParser parser;
    check(parser.parse_file(file.string()));
}

// A carriage return inside a line is not a line end; the line cannot be read,
// and that is an error, not the end of the netlist (NetlistParser::Impl::parse)
TEST(NetlistLines, CarriageReturnInsideALineIsAnError) {
    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string("t\nV1 a 0 1\nR1 a 0 1k\rR2 a 0 1k\nR3 a 0 2k\n.op\n.end\n"),
                 parser::ParseError);
}

// A model name on an S or Y line that the netlist does not define is an
// error, as for every other element (parse_scr_device, parse_triac_device),
// not the device's defaults without a message; .MODEL has no SCR or TRIAC
// type, so these lines take NAME=value parameters
TEST(NetlistLines, UndefinedScrAndTriacModels) {
    for (const auto& [line, message] :
         std::initializer_list<std::pair<std::string, std::string>>{
             {"S1 k 0 g SM", "Undefined SCR model: SM"}, {"Y1 k 0 g TM", "Undefined TRIAC model: TM"}}) {
        parser::NetlistParser parser;
        try {
            parser.parse_string("t\nVa a 0 10\nRa a k 100\n" + line + "\nVg g 0 0\n.op\n.end\n");
            ADD_FAILURE() << line << ": no error";
        } catch (const parser::ParseError& e) {
            EXPECT_EQ(e.line(), 4u) << line;
            EXPECT_NE(std::string(e.what()).find(message), std::string::npos) << e.what();
        }
    }
    parser::NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_string("t\nVa a 0 10\nRa a k 100\nS1 k 0 g VBF=5\nVg g 0 0\n.op\n.end\n"));
}

// The POLY form is the polynomial of its controls in SPICE's order of
// coefficients: 1, then x1 ... xn, then x1 x1, x1 x2, ..., x2 x2, ..., then
// the products of three (parse_poly_controlled_source). With Va = 2, Vb = 3
// and Vc = 5 V, ngspice-45.2 gives these V(o) (run 2026-11-28): each
// coefficient alone picks one product, x1 x1 = 4, x1 x2 = 6, x1 x3 = 10,
// x2 x2 = 9, x2 x3 = 15, x3 x3 = 25, and in two dimensions the fourth-order
// x1 x1 x2 = 12. A single coefficient is an error, as in ngspice-45.2.
TEST(NetlistLines, PolyFormIsItsPolynomial) {
    const std::string sources = "t\nVa a 0 2\nVb b 0 3\nVc c 0 5\n";
    auto v_o = [&](const std::string& lines) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string(sources + lines + "R1 o 0 1k\n.op\n.end\n");
        parsed.circuit->expand_all_subcircuits();
        solvers::DCSolver solver(*parsed.circuit, parsed.config);
        DenseVector x;
        SolverStats stats;
        EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << lines;
        return x.empty() ? 0.0 : x[parsed.circuit->get_node_id("o") - 1];
    };
    const std::pair<std::string, Real> cases[] = {
        {"E1 o 0 POLY(1) a 0 1 3 5\n", 27.0},
        {"E1 o 0 POLY(1) (a,0) 1 3 5\n", 27.0},
        {"E1 o 0 POLY(1) a 0 {1} {3} 5\n", 27.0},
        {"E1 o 0 POLY(3) a 0 b 0 c 0 0 0 0 0 1\n", 4.0},
        {"E1 o 0 POLY(3) a 0 b 0 c 0 0 0 0 0 0 1\n", 6.0},
        {"E1 o 0 POLY(3) a 0 b 0 c 0 0 0 0 0 0 0 1\n", 10.0},
        {"E1 o 0 POLY(3) a 0 b 0 c 0 0 0 0 0 0 0 0 1\n", 9.0},
        {"E1 o 0 POLY(3) a 0 b 0 c 0 0 0 0 0 0 0 0 0 1\n", 15.0},
        {"E1 o 0 POLY(3) a 0 b 0 c 0 0 0 0 0 0 0 0 0 0 1\n", 25.0},
        {"E1 o 0 POLY(2) a 0 b 0 0 0 0 0 0 0 0 1\n", 12.0},
        // I(V1) = -1 mA and I(V2) = -2 mA: 0 + 2 I + 3 I^2 = -1.997 mA from
        // 0 into o, and 1e6 I(V1)^2 = 1 V
        {"V1 p 0 1\nR2 p 0 1k\nF1 0 o POLY(1) V1 0 2 3\n", -1.997},
        {"V1 p 0 1\nR2 p 0 1k\nV2 q 0 1\nR3 q 0 500\nH1 o 0 POLY(2) V1 V2 0 0 0 1e6\n", 1.0},
        // In a body: the controls are the body's nodes and sources, Vs
        // carries 2 mA, and 1e6 I(Vs)^2 = 4 V
        {".subckt s x y\nVs x m 0\nR1 m 0 1k\nH1 y 0 POLY(1) Vs 0 0 1e6\n.ends\nX1 a o s\n", 4.0},
    };
    for (const auto& [lines, expected] : cases) {
        try {
            EXPECT_NEAR(v_o(lines), expected, 1e-9) << lines;
        } catch (const std::exception& e) {
            ADD_FAILURE() << lines << e.what();
        }
    }
    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string(sources + "E1 o 0 POLY(1) a 0 3\nR1 o 0 1k\n.op\n.end\n"),
                 parser::ParseError);
    EXPECT_THROW(parser.parse_string(sources + "E1 o 0 POLY(2) a 0 1 2\nR1 o 0 1k\n.op\n.end\n"),
                 parser::ParseError);
}
