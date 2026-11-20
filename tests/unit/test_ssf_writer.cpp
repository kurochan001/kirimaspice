// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_ssf_writer.cpp
#include <gtest/gtest.h>
#include "kirimaspice/output/ssf_writer.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/core/types.hpp"

#include <fstream>
#include <filesystem>
#include <cstring>
#include <iterator>
#include <map>
#include <unistd.h>
#include <zstd.h>

using namespace kirimaspice;
using namespace kirimaspice::output;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// Helper function to read SSF header
struct SSFHeaderReader {
    char magic[4];
    uint16_t version_major;
    uint16_t version_minor;
    uint64_t metadata_offset;
    uint64_t metadata_size;

    static SSFHeaderReader read_from_file(const std::string& filename) {
        SSFHeaderReader header;
        std::ifstream file(filename, std::ios::binary);
        EXPECT_TRUE(file.is_open()) << "Failed to open file: " << filename;

        file.read(header.magic, 4);
        file.read(reinterpret_cast<char*>(&header.version_major), 2);
        file.read(reinterpret_cast<char*>(&header.version_minor), 2);
        file.read(reinterpret_cast<char*>(&header.metadata_offset), 8);
        file.read(reinterpret_cast<char*>(&header.metadata_size), 8);

        return header;
    }
};

// Test: SSF file creation and basic header validation
TEST(SSFWriter, HeaderGeneration) {
    const std::string test_file = "test_header.ssf";

    {
        SSFWriter writer(test_file, 3);  // Zstd compression

        // Write minimal DC analysis
        DenseVector node_voltages = {5.0};
        std::vector<std::string> node_names = {"n1"};
        DenseVector branch_currents = {0.005};
        std::vector<std::string> branch_names = {"V1"};

        writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
    }

    // Verify file exists
    ASSERT_TRUE(std::filesystem::exists(test_file));

    // Read and validate header
    auto header = SSFHeaderReader::read_from_file(test_file);

    // Check magic
    EXPECT_EQ(header.magic[0], 'S');
    EXPECT_EQ(header.magic[1], 'S');
    EXPECT_EQ(header.magic[2], 'F');
    EXPECT_EQ(header.magic[3], 0x04);

    // Check version (3.4)
    EXPECT_EQ(header.version_major, 3);
    EXPECT_EQ(header.version_minor, 4);

    // Check metadata offset and size
    EXPECT_EQ(header.metadata_offset, 256);  // After header
    EXPECT_GT(header.metadata_size, 0u);

    // Cleanup
    std::filesystem::remove(test_file);
}

// Test: DC analysis output
TEST(SSFWriter, DCAnalysisOutput) {
    const std::string test_file = "test_dc.ssf";

    // Create simple voltage divider circuit
    CircuitGraph circuit;
    circuit.set_title("DC Test Circuit");
    circuit.add_voltage_source("V1", "n1", "0", 10.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);
    circuit.add_resistor("R2", "n2", "0", 1000.0);

    // Solve DC
    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Write SSF
    {
        auto node_names = circuit.get_node_names();
        auto voltage_sources = circuit.get_voltage_sources();

        size_t num_nodes = node_names.size();
        DenseVector node_voltages(solution.begin(), solution.begin() + num_nodes);
        DenseVector branch_currents(solution.begin() + num_nodes, solution.end());

        std::vector<std::string> branch_names;
        for (const auto& vsrc : voltage_sources) {
            branch_names.push_back(vsrc->name());
        }

        SSFWriter writer(test_file);
        writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
    }

    // Verify file
    ASSERT_TRUE(std::filesystem::exists(test_file));
    EXPECT_GT(std::filesystem::file_size(test_file), 256u);  // At least header size

    // Read header
    auto header = SSFHeaderReader::read_from_file(test_file);
    EXPECT_EQ(std::memcmp(header.magic, "SSF\x04", 4), 0);

    // Cleanup
    std::filesystem::remove(test_file);
}

// Test: Transient analysis output
TEST(SSFWriter, TransientAnalysisOutput) {
    const std::string test_file = "test_tran.ssf";

    // Create simple RC circuit
    CircuitGraph circuit;
    circuit.set_title("Transient Test Circuit");
    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);
    circuit.add_capacitor("C1", "n2", "0", 1e-6);

    // Solve transient (short simulation)
    TransientSolver solver(circuit);
    auto result = solver.solve(0.0, 5e-3, 1e-3);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 0u);

    // Write SSF
    {
        auto node_names = circuit.get_node_names();
        auto voltage_sources = circuit.get_voltage_sources();

        std::vector<std::string> branch_names;
        for (const auto& vsrc : voltage_sources) {
            branch_names.push_back(vsrc->name());
        }

        SSFWriter writer(test_file);
        writer.write_transient_analysis(result, node_names, branch_names);
    }

    // Verify file
    ASSERT_TRUE(std::filesystem::exists(test_file));
    EXPECT_GT(std::filesystem::file_size(test_file), 256u);

    // Read header
    auto header = SSFHeaderReader::read_from_file(test_file);
    EXPECT_EQ(std::memcmp(header.magic, "SSF\x04", 4), 0);

    // Cleanup
    std::filesystem::remove(test_file);
}

// Test: Multiple signals
TEST(SSFWriter, MultipleSignals) {
    const std::string test_file = "test_multiple_signals.ssf";

    // Create circuit with multiple nodes and branches
    CircuitGraph circuit;
    circuit.set_title("Multiple Signals Test");
    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_voltage_source("V2", "n3", "0", 3.0);
    circuit.add_resistor("R1", "n1", "n2", 1000.0);
    circuit.add_resistor("R2", "n2", "0", 1000.0);
    circuit.add_resistor("R3", "n3", "n2", 500.0);

    // Solve DC
    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    // Write SSF
    {
        auto node_names = circuit.get_node_names();
        auto voltage_sources = circuit.get_voltage_sources();

        size_t num_nodes = node_names.size();
        DenseVector node_voltages(solution.begin(), solution.begin() + num_nodes);
        DenseVector branch_currents(solution.begin() + num_nodes, solution.end());

        std::vector<std::string> branch_names;
        for (const auto& vsrc : voltage_sources) {
            branch_names.push_back(vsrc->name());
        }

        SSFWriter writer(test_file);
        writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
    }

    // Verify file
    ASSERT_TRUE(std::filesystem::exists(test_file));

    // Should have multiple signals (nodes + branches)
    // At least 3 nodes (n1, n2, n3) + 2 branches (V1, V2) = 5 signals
    auto header = SSFHeaderReader::read_from_file(test_file);
    EXPECT_GT(header.metadata_size, 100u);  // Should have substantial metadata

    // Cleanup
    std::filesystem::remove(test_file);
}

// Test: Compression types
TEST(SSFWriter, CompressionTypes) {
    const std::string test_file_none = "test_compression_none.ssf";
    const std::string test_file_zstd = "test_compression_zstd.ssf";

    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "n1", "0", 5.0);
    circuit.add_resistor("R1", "n1", "0", 1000.0);

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    solver.solve(solution, stats);

    auto node_names = circuit.get_node_names();
    auto voltage_sources = circuit.get_voltage_sources();

    size_t num_nodes = node_names.size();
    DenseVector node_voltages(solution.begin(), solution.begin() + num_nodes);
    DenseVector branch_currents(solution.begin() + num_nodes, solution.end());

    std::vector<std::string> branch_names;
    for (const auto& vsrc : voltage_sources) {
        branch_names.push_back(vsrc->name());
    }

    // Write without compression
    {
        SSFWriter writer(test_file_none, 0);  // compression_type=0
        writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
    }

    // Write with Zstd compression
    {
        SSFWriter writer(test_file_zstd, 3);  // compression_type=3
        writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
    }

    // Both files should exist
    ASSERT_TRUE(std::filesystem::exists(test_file_none));
    ASSERT_TRUE(std::filesystem::exists(test_file_zstd));

    // Compressed file should be smaller (for larger datasets)
    // For small data, might not be smaller due to overhead
    EXPECT_GT(std::filesystem::file_size(test_file_none), 256u);
    EXPECT_GT(std::filesystem::file_size(test_file_zstd), 256u);

    // Cleanup
    std::filesystem::remove(test_file_none);
    std::filesystem::remove(test_file_zstd);
}

// Test: File overwrite protection
TEST(SSFWriter, FileOverwrite) {
    const std::string test_file = "test_overwrite.ssf";

    // Create first file
    {
        SSFWriter writer(test_file);
        DenseVector node_voltages = {5.0};
        std::vector<std::string> node_names = {"n1"};
        DenseVector branch_currents = {0.005};
        std::vector<std::string> branch_names = {"V1"};
        writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
    }

    auto size1 = std::filesystem::file_size(test_file);

    // Overwrite with different data
    {
        SSFWriter writer(test_file);
        DenseVector node_voltages = {10.0, 7.5};
        std::vector<std::string> node_names = {"n1", "n2"};
        DenseVector branch_currents = {0.01};
        std::vector<std::string> branch_names = {"V1"};
        writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
    }

    auto size2 = std::filesystem::file_size(test_file);

    // Second file should be larger (more nodes)
    EXPECT_GT(size2, size1);

    // Cleanup
    std::filesystem::remove(test_file);
}

namespace {

// A minimal reader of what SSFWriter writes: the metadata's signal names
// and the values of each data block (zigzag varints of 1e-6 steps, after a
// 4-byte size, compressed with zstd when the header says so)
struct DecodedSsf {
    std::vector<std::string> signals;
    std::vector<std::vector<double>> blocks;
};

std::uint64_t read_varint(const std::vector<unsigned char>& b, std::size_t& i) {
    std::uint64_t r = 0;
    int shift = 0;
    while (true) {
        const unsigned char c = b.at(i++);
        r |= static_cast<std::uint64_t>(c & 0x7f) << shift;
        shift += 7;
        if (!(c & 0x80)) return r;
    }
}

DecodedSsf decode_ssf(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(in)), {});
    auto u64 = [&](std::size_t at) {
        std::uint64_t v;
        std::memcpy(&v, &d.at(at), 8);
        return v;
    };
    const std::uint64_t mo = u64(8), ms = u64(16), lo = u64(56), ls = u64(64);
    const unsigned char compression = d.at(88);

    DecodedSsf out;
    std::vector<unsigned char> meta(d.begin() + mo, d.begin() + mo + ms);
    std::size_t i = 0;
    const std::uint64_t n = read_varint(meta, i);
    std::map<std::string, std::string> md;
    auto read_string = [&]() {
        const std::uint64_t len = read_varint(meta, i);
        std::string s(meta.begin() + i, meta.begin() + i + len);
        i += len;
        return s;
    };
    for (std::uint64_t k = 0; k < n; ++k) {
        std::string key = read_string();
        md[key] = read_string();
    }
    for (int j = 1; md.count("signal." + std::to_string(j) + ".name"); ++j) {
        out.signals.push_back(md["signal." + std::to_string(j) + ".name"]);
    }
    for (std::uint64_t e = 0; e < ls / 28; ++e) {
        const std::uint64_t off = u64(lo + 28 * e + 8), size = u64(lo + 28 * e + 16);
        std::uint32_t raw_size;
        std::memcpy(&raw_size, &d.at(off), 4);
        std::vector<unsigned char> raw(raw_size);
        if (compression == 3) {
            ZSTD_decompress(raw.data(), raw.size(), &d.at(off + 4), size - 4);
        } else {
            raw.assign(d.begin() + off + 4, d.begin() + off + size);
        }
        std::vector<double> values;
        std::size_t p = 0;
        while (p < raw.size()) {
            const std::uint64_t z = read_varint(raw, p);
            const std::int64_t q = static_cast<std::int64_t>(z >> 1) ^ -static_cast<std::int64_t>(z & 1);
            values.push_back(static_cast<double>(q) * 1e-6);
        }
        out.blocks.push_back(values);
    }
    return out;
}

}  // namespace

// The signals of an SSF file are the unknowns of the solution: the node
// names the program passes start with ground ("0"), which is not one, so
// it is left out and each value lands on its own signal. Values are rounded
// to the nearest 1e-6 step (-1.2345e-3 A is stored as -1.235e-3 A).
TEST(SSFWriter, SignalsMatchTheValuesOfEachBlock) {
    const std::string path = (std::filesystem::temp_directory_path() /
                              ("ssf_signals_" + std::to_string(::getpid()) + ".ssf"))
                                 .string();
    {
        SSFWriter writer(path);
        writer.write_dc_analysis(DenseVector{2.0, 1.0}, {"0", "in", "mid"}, DenseVector{-1.2345e-3},
                                 {"V1"});
    }
    const DecodedSsf ssf = decode_ssf(path);
    std::filesystem::remove(path);
    ASSERT_EQ(ssf.signals, (std::vector<std::string>{"v(in)", "v(mid)", "i(v1)"}));
    ASSERT_EQ(ssf.blocks.size(), 1u);
    ASSERT_EQ(ssf.blocks[0].size(), 3u);
    EXPECT_NEAR(ssf.blocks[0][0], 2.0, 1e-12);
    EXPECT_NEAR(ssf.blocks[0][1], 1.0, 1e-12);
    EXPECT_NEAR(ssf.blocks[0][2], -1.235e-3, 1e-12);
}
