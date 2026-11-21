// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_touchstone_layout.cpp
// The order of the network data in a Touchstone file (Touchstone File
// Format Specification, rev. 1.1, section 3, "Data Lines"): the frequency
// starts the first line; a 1-port file writes N11 and a 2-port file N11 N21
// N12 N22 on that line; 3 ports and more write the matrix row by row, each
// row on a new line, at most four pairs on a line, so that a row of 5 or
// more ports runs on to the next line.

#include <gtest/gtest.h>
#include "gtest_output.hpp"
#include "kirimaspice/output/touchstone_writer.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace kirimaspice;
using namespace kirimaspice::output;

namespace {

// An n-port result at 1 GHz whose S(i,j) is the real number 10 i + j
// (ports from 1), so that each value names its place in the matrix
SParameterResult numbered_result(std::uint32_t num_ports) {
    SParameterResult result;
    result.success = true;
    for (std::uint32_t p = 1; p <= num_ports; ++p) {
        PortDefinition port;
        port.port_number = p;
        port.node_positive = "n" + std::to_string(p);
        port.node_negative = "0";
        port.impedance = 50.0;
        result.ports.push_back(port);
    }
    SParameterPoint point;
    point.frequency = 1e9;
    point.S_matrix.assign(num_ports, std::vector<Complex>(num_ports));
    for (std::uint32_t i = 0; i < num_ports; ++i) {
        for (std::uint32_t j = 0; j < num_ports; ++j) {
            point.S_matrix[i][j] = Complex(10.0 * (i + 1) + (j + 1), 0.0);
        }
    }
    result.frequency_points.push_back(point);
    return result;
}

struct ParsedFile {
    std::vector<std::string> comments;        // the lines after the option line that start with !
    std::vector<std::vector<double>> data;    // the numbers of each data line
};

ParsedFile write_and_read(std::uint32_t num_ports) {
    const std::string path =
        kirimaspice::test::test_output_path("numbered.s" + std::to_string(num_ports) + "p").string();
    EXPECT_TRUE(TouchstoneWriter::write(path, numbered_result(num_ports),
                                        TouchstoneWriter::Format::RI));
    ParsedFile parsed;
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);  // the option line
    EXPECT_EQ(line.rfind("# Hz S RI R", 0), 0u) << line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] == '!') {
            parsed.comments.push_back(line);
            continue;
        }
        std::istringstream fields(line);
        std::vector<double> numbers;
        double value = 0.0;
        while (fields >> value) {
            numbers.push_back(value);
        }
        if (!numbers.empty()) {
            parsed.data.push_back(numbers);
        }
    }
    return parsed;
}

// The real parts of the pairs of one data line, after the frequency on the first
std::vector<double> real_parts(const std::vector<double>& line, bool first) {
    std::vector<double> parts;
    for (std::size_t k = first ? 1 : 0; k + 1 < line.size(); k += 2) {
        EXPECT_EQ(line[k + 1], 0.0);
        parts.push_back(line[k]);
    }
    return parts;
}

std::vector<std::vector<double>> entries(const ParsedFile& parsed) {
    std::vector<std::vector<double>> lines;
    for (std::size_t l = 0; l < parsed.data.size(); ++l) {
        lines.push_back(real_parts(parsed.data[l], l == 0));
    }
    return lines;
}

}  // namespace

TEST(TouchstoneLayout, OnePortIsOneLine) {
    const auto parsed = write_and_read(1);
    ASSERT_EQ(parsed.data.size(), 1u);
    EXPECT_DOUBLE_EQ(parsed.data[0][0], 1e9);
    EXPECT_EQ(entries(parsed), (std::vector<std::vector<double>>{{11}}));
}

TEST(TouchstoneLayout, TwoPortPutsS21BeforeS12) {
    const auto parsed = write_and_read(2);
    ASSERT_EQ(parsed.data.size(), 1u);
    EXPECT_DOUBLE_EQ(parsed.data[0][0], 1e9);
    EXPECT_EQ(entries(parsed), (std::vector<std::vector<double>>{{11, 21, 12, 22}}));
    // The comment that names the columns follows the same order
    ASSERT_FALSE(parsed.comments.empty());
    EXPECT_NE(parsed.comments.back().find("S11_re S11_im S21_re S21_im S12_re S12_im S22_re S22_im"),
              std::string::npos)
        << parsed.comments.back();
}

TEST(TouchstoneLayout, ThreePortWritesOneRowPerLine) {
    const auto parsed = write_and_read(3);
    ASSERT_EQ(parsed.data.size(), 3u);
    EXPECT_DOUBLE_EQ(parsed.data[0][0], 1e9);
    EXPECT_EQ(entries(parsed),
              (std::vector<std::vector<double>>{{11, 12, 13}, {21, 22, 23}, {31, 32, 33}}));
}

TEST(TouchstoneLayout, FourPortWritesOneRowPerLine) {
    const auto parsed = write_and_read(4);
    ASSERT_EQ(parsed.data.size(), 4u);
    EXPECT_DOUBLE_EQ(parsed.data[0][0], 1e9);
    EXPECT_EQ(entries(parsed), (std::vector<std::vector<double>>{{11, 12, 13, 14},
                                                                 {21, 22, 23, 24},
                                                                 {31, 32, 33, 34},
                                                                 {41, 42, 43, 44}}));
}

TEST(TouchstoneLayout, FivePortRowsRunOnAfterFourPairs) {
    const auto parsed = write_and_read(5);
    ASSERT_EQ(parsed.data.size(), 10u);
    EXPECT_DOUBLE_EQ(parsed.data[0][0], 1e9);
    EXPECT_EQ(entries(parsed), (std::vector<std::vector<double>>{{11, 12, 13, 14}, {15},
                                                                 {21, 22, 23, 24}, {25},
                                                                 {31, 32, 33, 34}, {35},
                                                                 {41, 42, 43, 44}, {45},
                                                                 {51, 52, 53, 54}, {55}}));
}
