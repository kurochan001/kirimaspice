// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A file is found relative to the file that names it (REFERENCE_MANUAL,
// Lines), also a library that an included file names with .LIB: the
// netlist includes sub/inc.cir, whose `.lib models.lib tt` names
// sub/models.lib. ngspice-47 finds it there (run 2026-11-21); before, the
// library was looked for next to the netlist and not found.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "gtest_output.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

}  // namespace

TEST(LibraryPaths, LibraryOfAnIncludedFileIsFoundNextToIt) {
    const auto dir = kirimaspice::test::test_output_path("netlist").parent_path();
    write(dir / "sub" / "models.lib", ".lib tt\nR2 a 0 3k\n.endl tt\n");
    write(dir / "sub" / "inc.cir", ".lib models.lib tt\n");
    write(dir / "main.cir", "t\nV1 in 0 4\nR1 in a 1k\n.include sub/inc.cir\n.end\n");
    parser::NetlistParser parser;
    auto parsed = parser.parse_file((dir / "main.cir").string());
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("a") - 1], 3.0, 1e-9);  // 4 V * 3k / (1k + 3k)
}
