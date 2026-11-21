// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_ssf_save_case.cpp
// SPICE names are not case-sensitive: .SAVE i(V1) v(OUT) asks for the
// signals that the SSF file names i(v1) and v(out) (the lower-case names
// of output/signal_names.hpp, as ngspice-47 writes them).

#include <gtest/gtest.h>
#include "gtest_output.hpp"

#include "kirimaspice/output/ssf_writer.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"

#include <fstream>
#include <iterator>
#include <string>

using namespace kirimaspice;

namespace {

std::string read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(SSFSaveCase, SaveTokensMatchWithoutCase) {
    parser::NetlistParser parser;
    const auto parsed = parser.parse_string(
        "save case\n"
        "V1 IN 0 DC 2\n"
        "R1 IN OUT 1k\n"
        "R2 OUT 0 1k\n"
        ".SAVE i(V1) v(OUT)\n"
        ".op\n"
        ".end\n");
    ASSERT_FALSE(parsed.config.output.save_all);

    const std::string path = kirimaspice::test::test_output_path("save_case.ssf").string();
    {
        output::SSFWriter writer(path, 0);
        writer.set_output_config(parsed.config.output);
        writer.write_dc_analysis(DenseVector{2.0, 1.0}, {"0", "IN", "OUT"}, DenseVector{-1e-3},
                                 {"V1"});
    }  // the destructor writes the file

    const std::string bytes = read_bytes(path);
    ASSERT_FALSE(bytes.empty());
    EXPECT_NE(bytes.find("v(out)"), std::string::npos);
    EXPECT_NE(bytes.find("i(v1)"), std::string::npos);
    EXPECT_EQ(bytes.find("v(in)"), std::string::npos);
}
