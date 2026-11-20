// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Input that would change the result is not dropped: an element of a letter
// the program does not simulate is a parse error (parse_element), at the
// top level and in a subcircuit body, in either case of the letter.

#include <gtest/gtest.h>

#include <string>

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
