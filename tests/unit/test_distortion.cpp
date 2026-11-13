// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_distortion.cpp
#include <gtest/gtest.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/distortion_solver.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::circuit;

// ========================================
// Parser Tests
// ========================================

TEST(DistortionParser, BasicSyntax_HarmonicOnly) {
    std::string netlist = R"(
RC Circuit
R1 in out 1k
C1 out 0 1u
Vin in 0 DC 1
.AC DEC 10 1k 100k
.DISTO 1k 0
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.distortion.enabled);
    EXPECT_DOUBLE_EQ(result.config.distortion.rload, 1000.0);
    EXPECT_EQ(result.config.distortion.mode, SimulationConfig::Distortion::AnalysisMode::HARMONIC);
}

TEST(DistortionParser, IntermodulationOnly) {
    std::string netlist = R"(
RC Circuit
R1 in out 1k
C1 out 0 1u
Vin in 0 DC 1
.AC DEC 10 1k 100k
.DISTO 10k 1
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.distortion.enabled);
    EXPECT_DOUBLE_EQ(result.config.distortion.rload, 10000.0);
    EXPECT_EQ(result.config.distortion.mode, SimulationConfig::Distortion::AnalysisMode::INTERMODULATION);
}

TEST(DistortionParser, BothWithSkew) {
    std::string netlist = R"(
RC Circuit
R1 in out 1k
C1 out 0 1u
Vin in 0 DC 1
.AC DEC 10 1k 100k
.DISTO 1k 2 1.1
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.distortion.enabled);
    EXPECT_DOUBLE_EQ(result.config.distortion.rload, 1000.0);
    EXPECT_EQ(result.config.distortion.mode, SimulationConfig::Distortion::AnalysisMode::BOTH);
    EXPECT_DOUBLE_EQ(result.config.distortion.f2_f1_ratio, 1.1);
}

TEST(DistortionParser, InvalidSyntax_TooFewArgs) {
    std::string netlist = R"(
RC Circuit
R1 in out 1k
C1 out 0 1u
Vin in 0 AC 1
.AC DEC 10 1k 100k
.DISTO 1k
.END
)";

    NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

TEST(DistortionParser, InvalidInterValue) {
    std::string netlist = R"(
RC Circuit
R1 in out 1k
C1 out 0 1u
Vin in 0 AC 1
.AC DEC 10 1k 100k
.DISTO 1k 5
.END
)";

    NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

// ========================================
// DistortionSolver Tests
// ========================================

TEST(DistortionSolver, Analyze_NoData) {
    // Test with empty AC data
    std::string netlist = R"(
RC Circuit
R1 in out 1k
C1 out 0 1u
Vin in 0 DC 1
.AC DEC 10 1k 100k
.DISTO 1k 0
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);

    std::vector<Real> ac_freqs;
    std::vector<std::vector<std::complex<Real>>> ac_results;

    auto disto_result = DistortionSolver::analyze(*parse_result.circuit, parse_result.config,
                                                   ac_freqs, ac_results);

    EXPECT_FALSE(disto_result.success);
}

TEST(DistortionSolver, PrintResult_DoesNotCrash) {
    DistortionResult result;
    result.success = true;
    result.fundamental_freq = 1000.0;
    result.fundamental_mag = 1.0;
    result.hd2_percent = 10.0;
    result.hd3_percent = 5.0;
    result.thd_percent = 11.18;

    EXPECT_NO_THROW(DistortionSolver::print_result(result));
}

// ========================================
// Integration Tests
// ========================================

TEST(DistortionSolver, Integration_BasicAnalysis) {
    // Create mock AC sweep data
    std::vector<Real> ac_freqs;
    std::vector<std::vector<std::complex<Real>>> ac_results;

    Real f0 = 1000.0;
    for (int i = 1; i <= 10; ++i) {
        ac_freqs.push_back(f0 * i);

        std::vector<std::complex<Real>> result_vec;
        if (i == 1) {
            result_vec.push_back(std::complex<Real>(1.0, 0.0));  // Fundamental
        } else if (i == 2) {
            result_vec.push_back(std::complex<Real>(0.1, 0.0));  // 2nd harmonic
        } else if (i == 3) {
            result_vec.push_back(std::complex<Real>(0.05, 0.0));  // 3rd harmonic
        } else {
            result_vec.push_back(std::complex<Real>(0.01, 0.0));  // Higher harmonics
        }
        ac_results.push_back(result_vec);
    }

    // Create config
    SimulationConfig config;
    config.distortion.enabled = true;
    config.distortion.mode = SimulationConfig::Distortion::AnalysisMode::HARMONIC;
    config.distortion.rload = 1000.0;

    // Create dummy circuit
    CircuitGraph circuit;

    auto disto_result = DistortionSolver::analyze(circuit, config, ac_freqs, ac_results);

    EXPECT_TRUE(disto_result.success);
    EXPECT_NEAR(disto_result.fundamental_freq, 1000.0, 1.0);
    EXPECT_NEAR(disto_result.fundamental_mag, 1.0, 0.01);
    EXPECT_NEAR(disto_result.hd2_percent, 10.0, 0.1);
    EXPECT_NEAR(disto_result.hd3_percent, 5.0, 0.1);
    // THD = sqrt(0.1^2 + 0.05^2 + 7*0.01^2) * 100% = 11.489%
    EXPECT_NEAR(disto_result.thd_percent, 11.49, 0.01);
}

TEST(DistortionSolver, Integration_IntermodulationMode) {
    // Create mock AC sweep data
    std::vector<Real> ac_freqs = {1000.0, 2000.0};
    std::vector<std::vector<std::complex<Real>>> ac_results;

    for (size_t i = 0; i < ac_freqs.size(); ++i) {
        std::vector<std::complex<Real>> result_vec;
        result_vec.push_back(std::complex<Real>(1.0, 0.0));
        ac_results.push_back(result_vec);
    }

    // Create config
    SimulationConfig config;
    config.distortion.enabled = true;
    config.distortion.mode = SimulationConfig::Distortion::AnalysisMode::INTERMODULATION;
    config.distortion.rload = 1000.0;
    config.distortion.f2_f1_ratio = 1.1;

    // Create dummy circuit
    CircuitGraph circuit;

    auto disto_result = DistortionSolver::analyze(circuit, config, ac_freqs, ac_results);

    EXPECT_TRUE(disto_result.success);
    EXPECT_NEAR(disto_result.f2_hz, 1100.0, 1.0);
    EXPECT_NEAR(disto_result.im2_freq_hz, 100.0, 1.0);  // f2 - f1
    EXPECT_NEAR(disto_result.im3_freq_hz, 900.0, 1.0);  // 2*f1 - f2
    EXPECT_NEAR(disto_result.imd_percent, 0.1, 0.01);  // Placeholder
}
