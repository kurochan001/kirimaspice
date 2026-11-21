// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_wav_writer.cpp
// WavWriter::write_transient resamples each node at sample_rate by linear
// interpolation between the transient's time points, clips to +-1 V and
// scales to full range (16-bit: round(v * 32767), little-endian), the
// channels of one sample frame side by side (RIFF WAVE PCM). The first
// test computes that from the definition and compares every byte; the
// second asks that a long transient be written in time that grows with the
// number of points and samples, not with their product.

#include <gtest/gtest.h>
#include "gtest_output.hpp"
#include "kirimaspice/output/wav_writer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace kirimaspice;

namespace {

// Two nodes over [0, duration] with uneven steps: a sine of 0.9 V at
// 440 Hz, and a ramp from -1.5 V to 1.5 V that the writer clips
solvers::TransientResult two_node_transient(std::size_t num_points, Real duration) {
    solvers::TransientResult result;
    result.status = ConvergenceStatus::SUCCESS;
    for (std::size_t k = 0; k < num_points; ++k) {
        // u in [0, 1], denser near the ends
        const Real s = static_cast<Real>(k) / static_cast<Real>(num_points - 1);
        const Real u = 0.5 - 0.5 * std::cos(M_PI * s);
        const Real t = duration * u;
        solvers::TimePoint tp;
        tp.time = t;
        tp.node_voltages = {0.9 * std::sin(2.0 * M_PI * 440.0 * t), -1.5 + 3.0 * u};
        result.timepoints.push_back(tp);
    }
    return result;
}

std::string read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void put_u16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void put_u32(std::string& out, std::uint32_t v) {
    for (int b = 0; b < 4; ++b) {
        out.push_back(static_cast<char>((v >> (8 * b)) & 0xFF));
    }
}

// The file by the definition, 16-bit
std::string expected_wav(const solvers::TransientResult& result, std::uint32_t rate) {
    std::vector<Real> times;
    for (const auto& tp : result.timepoints) {
        times.push_back(tp.time);
    }
    const std::size_t channels = result.timepoints.front().node_voltages.size();
    const auto samples =
        static_cast<std::uint32_t>(std::ceil((times.back() - times.front()) * rate));
    const auto block_align = static_cast<std::uint16_t>(channels * 2);
    const std::uint32_t data_bytes = samples * block_align;

    std::string out = "RIFF";
    put_u32(out, 36 + data_bytes);
    out += "WAVEfmt ";
    put_u32(out, 16);
    put_u16(out, 1);
    put_u16(out, static_cast<std::uint16_t>(channels));
    put_u32(out, rate);
    put_u32(out, rate * block_align);
    put_u16(out, block_align);
    put_u16(out, 16);
    out += "data";
    put_u32(out, data_bytes);

    for (std::uint32_t i = 0; i < samples; ++i) {
        const Real t = times.front() + i * (1.0 / rate);
        for (std::size_t ch = 0; ch < channels; ++ch) {
            Real v = 0.0;
            if (t <= times.front()) {
                v = result.timepoints.front().node_voltages[ch];
            } else if (t >= times.back()) {
                v = result.timepoints.back().node_voltages[ch];
            } else {
                const std::size_t hi = static_cast<std::size_t>(
                    std::upper_bound(times.begin(), times.end(), t) - times.begin());
                const Real t0 = times[hi - 1];
                const Real t1 = times[hi];
                const Real v0 = result.timepoints[hi - 1].node_voltages[ch];
                const Real v1 = result.timepoints[hi].node_voltages[ch];
                v = v0 + (t - t0) / (t1 - t0) * (v1 - v0);
            }
            v = std::clamp(v, -1.0, 1.0);
            const auto sample = static_cast<std::int32_t>(std::round(v * 32767));
            put_u16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(sample)));
        }
    }
    return out;
}

}  // namespace

TEST(WavWriterResampling, EveryByteFollowsTheDefinition) {
    const auto result = two_node_transient(1001, 20e-3);
    const std::string path = kirimaspice::test::test_output_path("two_nodes.wav").string();
    output::WavWriter writer(path, 8000, 16);
    writer.write_transient({"0", "a", "b"}, result);

    const std::string actual = read_bytes(path);
    const std::string expected = expected_wav(result, 8000);
    ASSERT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(actual == expected) << "the file differs from the definition";
}

// 100,001 points and 22,050 samples of two channels. Rebuilding a channel's
// array for every sample costs about 4.4e9 copies, tens of seconds; one
// array per channel and a forward walk through the time points take
// milliseconds. The bound leaves a margin of a hundred times and more.
TEST(WavWriterResampling, LongTransientInLinearTime) {
    const auto result = two_node_transient(100001, 0.5);
    const std::string path = kirimaspice::test::test_output_path("long.wav").string();
    output::WavWriter writer(path, 44100, 16);

    const auto start = std::chrono::steady_clock::now();
    writer.write_transient({"0", "a", "b"}, result);
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed.count(), 2.0);
    EXPECT_TRUE(read_bytes(path) == expected_wav(result, 44100));
}
