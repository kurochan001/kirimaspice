// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Input that would change the result is not dropped: an element of a letter
// the program does not simulate is a parse error (parse_element), at the
// top level and in a subcircuit body, in either case of the letter. A
// directive the parser does not read is named in one warning, and every
// directive read is recorded (SimulationConfig::directives_read).

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"

using namespace kirimaspice;

namespace {

std::string parse_error_of(const std::string& netlist) {
    parser::NetlistParser parser;
    try {
        auto parsed = parser.parse_string(netlist);
        parsed.circuit->expand_all_subcircuits();
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST(ParserUnknownInput, UnknownElementLetterIsAnError) {
    for (const char* line : {"J1 in 0 0 JM", "j1 in 0 0 JM", "N1 in 0", "P1 in 0", "U1 in 0 x"}) {
        SCOPED_TRACE(line);
        const std::string error =
            parse_error_of(std::string("t\nV1 in 0 1\nR1 in 0 1k\n") + line + "\n.end\n");
        EXPECT_NE(error.find("Unknown element letter"), std::string::npos) << error;
    }
}

TEST(ParserUnknownInput, UnknownElementLetterInASubcircuitIsAnError) {
    const std::string error = parse_error_of(
        "t\n.subckt cell a\nJ1 a 0 0 JM\n.ends\nV1 in 0 1\nX1 in cell\n.end\n");
    EXPECT_NE(error.find("Unknown element letter"), std::string::npos) << error;
}

TEST(ParserUnknownInput, KnownElementLettersStillParse) {
    EXPECT_EQ(parse_error_of("t\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1n\nL1 out 0 1m\n.end\n"), "");
}

namespace {

// The warnings of parsing netlist, and its configuration
std::string parse_warnings(const std::string& netlist, SimulationConfig* config = nullptr) {
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("unknown_input", sink);
    logger->set_level(spdlog::level::warn);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    spdlog::set_default_logger(previous);
    if (config) {
        *config = parsed.config;
    }
    return captured.str();
}

std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

}  // namespace

TEST(ParserUnknownInput, UnknownDirectiveIsNamedOnce) {
    const std::string warnings =
        parse_warnings("t\nV1 in 0 1\nR1 in 0 1k\n.fooo a\n.fooo b\n.op\n.end\n");
    EXPECT_EQ(count(warnings, "unknown directive .fooo is ignored"), 1u) << warnings;
}

TEST(ParserUnknownInput, UnsupportedDirectivesAreNamed) {
    const std::string warnings = parse_warnings(
        "t\nV1 in 0 1\nR1 in 0 1k\n.noise v(in) V1 dec 10 1 1k\n.measure tran x max v(in)\n.end\n");
    EXPECT_EQ(count(warnings, ".noise is not supported and is ignored"), 1u) << warnings;
    EXPECT_EQ(count(warnings, ".measure is not supported and is ignored"), 1u) << warnings;
}

TEST(ParserUnknownInput, DirectivesOfThePreprocessorAndEndGiveNoWarning) {
    const std::string warnings = parse_warnings(
        "t\nV1 in 0 1\nR1 in 0 1k\n.if (1)\n.op\n.else\n.dc V1 0 1 1\n.endif\n.end\n");
    EXPECT_EQ(warnings, "");
}

TEST(ParserUnknownInput, DirectivesReadAreRecorded) {
    SimulationConfig config;
    parse_warnings("t\nV1 in 0 1\nR1 in 0 1k\n.tf v(in) V1\n.op\n.end\n", &config);
    EXPECT_TRUE(config.directives_read.count(".TF"));
    EXPECT_TRUE(config.directives_read.count(".OP"));
    EXPECT_FALSE(config.directives_read.count(".TRAN"));
}
