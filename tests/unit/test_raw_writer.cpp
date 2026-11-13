// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_raw_writer.cpp
#include <gtest/gtest.h>

#include "kirimaspice/output/raw_writer.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace kirimaspice;

namespace {

struct RawPlot {
    std::size_t num_variables = 0;
    std::vector<std::vector<double>> points;  // values of each point, variable 0 first
};

// Read the first plot of an ASCII raw file: in each point, the first value
// is on the line with the point index and the others follow, one per line.
RawPlot read_ascii_raw(const std::filesystem::path& path) {
    std::ifstream in(path);
    RawPlot plot;
    std::string line;
    while (std::getline(in, line) && line != "Values:") {
        if (line.rfind("No. Variables:", 0) == 0) {
            plot.num_variables = std::stoul(line.substr(14));
        }
    }
    while (std::getline(in, line) && !line.empty()) {
        std::istringstream fields(line);
        if (line.front() != '\t') {
            std::size_t index = 0;
            double value = 0.0;
            fields >> index >> value;
            plot.points.push_back({value});
        } else {
            double value = 0.0;
            fields >> value;
            plot.points.back().push_back(value);
        }
    }
    return plot;
}

}  // namespace

// Each point of a raw file holds one value per variable in the header. The
// operating point has no scale variable, so its first value is v(in).
TEST(RawWriter, OperatingPointHasOneValuePerVariable) {
    const auto path = std::filesystem::path(testing::TempDir()) / "raw_writer_op.raw";
    std::filesystem::remove(path);
    {
        output::RawWriter writer(path.string(), false);
        writer.write_dc({"0", "in", "out"}, DenseVector{1.0, 0.5}, {"V1"}, DenseVector{-1e-3});
    }

    const RawPlot plot = read_ascii_raw(path);
    EXPECT_EQ(plot.num_variables, 3u);
    ASSERT_EQ(plot.points.size(), 1u);
    ASSERT_EQ(plot.points[0].size(), 3u);
    EXPECT_DOUBLE_EQ(plot.points[0][0], 1.0);
    EXPECT_DOUBLE_EQ(plot.points[0][1], 0.5);
    EXPECT_DOUBLE_EQ(plot.points[0][2], -1e-3);
    std::filesystem::remove(path);
}
