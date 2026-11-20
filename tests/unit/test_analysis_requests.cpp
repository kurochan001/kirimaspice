// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The analyses a netlist asks for are those the parser reads
// (SimulationConfig's op_requested, tran_requested, ac_requested and
// dc_sweep_enabled), and the program runs those alone (main.cpp's
// run_analyses). So an analysis line counts as every other directive does:
// joined to its continuation lines, from .INCLUDE text, in the branch of .IF
// that is taken, never in a comment; .OPTIONS asks for no analysis.
// ngspice-47 runs the transient of the continued and the included .TRAN
// below (run on 2026-11-20).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"

using namespace kirimaspice;

namespace {

SimulationConfig config_of(const std::string& netlist) {
    parser::NetlistParser parser;
    return parser.parse_string(netlist).config;
}

const std::string kCircuit = "t\nV1 in 0 PULSE(0 1 0 1u 1u 1m 2m)\nR1 in out 1k\nC1 out 0 1n\n";

bool asks_for_nothing(const SimulationConfig& c) {
    return !c.op_requested && !c.tran_requested && !c.ac_requested && !c.dc_sweep_enabled;
}

}  // namespace

TEST(AnalysisRequests, EachDirectiveIsRecorded) {
    EXPECT_TRUE(config_of(kCircuit + ".op\n.end\n").op_requested);
    EXPECT_TRUE(config_of(kCircuit + ".tran 1u 10u\n.end\n").tran_requested);
    EXPECT_TRUE(config_of(kCircuit + ".ac dec 10 1 1k\n.end\n").ac_requested);
    EXPECT_TRUE(config_of(kCircuit + ".dc V1 0 1 0.5\n.end\n").dc_sweep_enabled);
    EXPECT_TRUE(asks_for_nothing(config_of(kCircuit + ".end\n")));
}

TEST(AnalysisRequests, ContinuedTranIsRead) {
    const SimulationConfig c = config_of(kCircuit + ".tran\n+ 10u 1m\n.end\n");
    EXPECT_TRUE(c.tran_requested);
    EXPECT_DOUBLE_EQ(c.transient.initial_step, 10e-6);
    EXPECT_DOUBLE_EQ(c.transient.end_time, 1e-3);
}

TEST(AnalysisRequests, CommentAndOptionsAskForNothing) {
    const SimulationConfig comment =
        config_of(kCircuit + "* this deck used to run .tran 1n 1u before\n.op\n.end\n");
    EXPECT_TRUE(comment.op_requested);
    EXPECT_FALSE(comment.tran_requested);
    EXPECT_TRUE(asks_for_nothing(config_of(kCircuit + ".options reltol=1e-4\n.end\n")));
}

TEST(AnalysisRequests, OnlyTheBranchOfIfTaken) {
    const SimulationConfig c =
        config_of(kCircuit + ".if (0)\n.tran 1u 10u\n.else\n.op\n.endif\n.end\n");
    EXPECT_FALSE(c.tran_requested);
    EXPECT_TRUE(c.op_requested);
}

TEST(AnalysisRequests, IncludedTranIsRead) {
    std::random_device random;
    const auto dir = std::filesystem::temp_directory_path() /
                     ("kirimaspice-analysis-requests-" + std::to_string(random()));
    ASSERT_TRUE(std::filesystem::create_directory(dir));
    std::ofstream(dir / "tran.inc") << ".tran 1u 10u\n";
    std::ofstream(dir / "top.cir") << kCircuit << ".include tran.inc\n.end\n";
    parser::NetlistParser parser;
    const SimulationConfig c = parser.parse_file((dir / "top.cir").string()).config;
    std::filesystem::remove_all(dir);
    EXPECT_TRUE(c.tran_requested);
    EXPECT_DOUBLE_EQ(c.transient.end_time, 10e-6);
}
