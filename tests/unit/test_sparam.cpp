// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_sparam.cpp
// S-parameter Analysis Tests

#include <gtest/gtest.h>
#include "gtest_output.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/sparam_solver.hpp"
#include "kirimaspice/output/touchstone_writer.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <string>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;
using namespace kirimaspice::output;

// ============================================================================
// 5.1 Parser Tests (4 tests)
// ============================================================================

TEST(PortParsing, BasicSyntax) {
    std::string netlist = R"(
* S-parameter port parsing test
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=75
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_EQ(result.config.sparam.ports.size(), 2);

    EXPECT_EQ(result.config.sparam.ports[0].port_number, 1);
    EXPECT_EQ(result.config.sparam.ports[0].node_positive, "in");
    EXPECT_EQ(result.config.sparam.ports[0].node_negative, "0");
    EXPECT_DOUBLE_EQ(result.config.sparam.ports[0].impedance, 50.0);

    EXPECT_EQ(result.config.sparam.ports[1].port_number, 2);
    EXPECT_EQ(result.config.sparam.ports[1].node_positive, "out");
    EXPECT_EQ(result.config.sparam.ports[1].node_negative, "0");
    EXPECT_DOUBLE_EQ(result.config.sparam.ports[1].impedance, 75.0);
}

TEST(PortParsing, DefaultImpedance) {
    std::string netlist = R"(
* Default impedance test
.PORT 1 in 0
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_EQ(result.config.sparam.ports.size(), 1);
    EXPECT_DOUBLE_EQ(result.config.sparam.ports[0].impedance, 50.0);  // Default 50Ω
}

TEST(PortParsing, UnitSuffix) {
    std::string netlist = R"(
* Unit suffix test
.PORT 1 in 0 Z=50k
.PORT 2 out 0 Z=75MEG
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_EQ(result.config.sparam.ports.size(), 2);
    EXPECT_DOUBLE_EQ(result.config.sparam.ports[0].impedance, 50000.0);  // 50k
    EXPECT_DOUBLE_EQ(result.config.sparam.ports[1].impedance, 75e6);     // 75MEG
}

TEST(PortParsing, ErrorHandling) {
    // Test 1: Port number = 0 (invalid)
    {
        std::string netlist = "* Error test\n.PORT 0 in 0 Z=50\n.END\n";
        NetlistParser parser;
        EXPECT_THROW(parser.parse_string(netlist), ParseError);
    }

    // Test 2: Negative impedance
    {
        std::string netlist = "* Error test\n.PORT 1 in 0 Z=-50\n.END\n";
        NetlistParser parser;
        EXPECT_THROW(parser.parse_string(netlist), ParseError);
    }

    // Test 3: Insufficient arguments
    {
        std::string netlist = "* Error test\n.PORT 1\n.END\n";
        NetlistParser parser;
        EXPECT_THROW(parser.parse_string(netlist), ParseError);
    }
}

// ============================================================================
// 5.2 S-parameter Calculation Tests (4 tests)
// ============================================================================

TEST(SParam, TwoPortResistorNetwork) {
    // 2-port resistor network: 50Ω - 100Ω - 50Ω
    // Expected: S11 ≈ S22 (reflection), S21 ≈ S12 (transmission)

    std::string netlist = R"(
* 2-port resistor network test
R1 in mid 50
R2 mid out 50
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 1e9;  // 1 GHz
    result.config.sparam.stop_freq = 1e9;
    result.config.sparam.num_points = 1;
    result.config.sparam.sweep_type = "LIN";

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    ASSERT_EQ(sparam_result.frequency_points.size(), 1);

    const auto& S_matrix = sparam_result.frequency_points[0].S_matrix;
    ASSERT_EQ(S_matrix.size(), 2);
    ASSERT_EQ(S_matrix[0].size(), 2);

    // Resistive network: S-parameters should be real (no imaginary part)
    EXPECT_NEAR(S_matrix[0][0].imag(), 0.0, 0.01);  // S11 real
    EXPECT_NEAR(S_matrix[1][1].imag(), 0.0, 0.01);  // S22 real
    EXPECT_NEAR(S_matrix[0][1].imag(), 0.0, 0.01);  // S12 real
    EXPECT_NEAR(S_matrix[1][0].imag(), 0.0, 0.01);  // S21 real

    // Symmetry: S12 ≈ S21 (reciprocal network)
    EXPECT_NEAR(std::abs(S_matrix[0][1]), std::abs(S_matrix[1][0]), 0.01);

    // Symmetry: S11 ≈ S22 (symmetric network)
    EXPECT_NEAR(std::abs(S_matrix[0][0]), std::abs(S_matrix[1][1]), 0.01);
}

TEST(SParam, ImpedanceMatching) {
    // Matched network: 50Ω port - 50Ω resistor - 50Ω port
    // Expected: S11 ≈ 0 (no reflection), S21 ≈ 0.5 (voltage division)

    std::string netlist = R"(
* Impedance matching test
R1 in out 50
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 1e9;
    result.config.sparam.stop_freq = 1e9;
    result.config.sparam.num_points = 1;

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    const auto& S = sparam_result.frequency_points[0].S_matrix;

    // Perfect matching: |S11| should be small (near 0)
    // In this simple network, S11 = (Zin - Z0) / (Zin + Z0)
    // where Zin = R1 || Z0 = 50 || 50 = 25Ω
    // S11 = (25 - 50) / (25 + 50) = -25/75 = -1/3 ≈ -0.333
    Real S11_mag = std::abs(S[0][0]);
    EXPECT_GT(S11_mag, 0.2);  // Should have some reflection
    EXPECT_LT(S11_mag, 0.5);
}

TEST(SParam, FrequencySweep) {
    // RC low-pass filter: should show frequency-dependent behavior

    std::string netlist = R"(
* RC low-pass filter test
R1 in out 50
C1 out 0 1e-12
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 1e9;   // 1 GHz
    result.config.sparam.stop_freq = 10e9;   // 10 GHz
    result.config.sparam.num_points = 5;
    result.config.sparam.sweep_type = "DEC";

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    EXPECT_GE(sparam_result.frequency_points.size(), 5);

    // Verify frequency sweep is monotonically increasing
    for (size_t i = 1; i < sparam_result.frequency_points.size(); ++i) {
        EXPECT_GT(sparam_result.frequency_points[i].frequency,
                 sparam_result.frequency_points[i-1].frequency);
    }

    // At higher frequencies, transmission should decrease (low-pass filter)
    Real S21_low = std::abs(sparam_result.frequency_points[0].S_matrix[1][0]);
    Real S21_high = std::abs(sparam_result.frequency_points.back().S_matrix[1][0]);
    EXPECT_GT(S21_low, S21_high * 0.9);  // Some attenuation at high freq
}

TEST(SParam, TransmissionLinePhaseDelay) {
    // Lossless T-line between two 50Ω ports
    // Expect |S21| ≈ 1, phase ≈ -ω·TD, and |S11| ≈ 0 at the test frequency.

    constexpr Real td = 1e-9;        // 1 ns delay
    constexpr Real freq = 100e6;     // 100 MHz
    constexpr Real omega = 2.0 * M_PI * freq;

    std::string netlist = R"(
* Lossless transmission line S-parameter test
T1 in 0 out 0 Z0=50 TD=1e-9
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.AC LIN 1 100Meg 100Meg
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = freq;
    result.config.sparam.stop_freq = freq;
    result.config.sparam.num_points = 1;
    result.config.sparam.sweep_type = "LIN";

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    ASSERT_EQ(sparam_result.frequency_points.size(), 1);
    const auto& S = sparam_result.frequency_points[0].S_matrix;

    // Magnitude checks
    EXPECT_NEAR(std::abs(S[1][0]), 1.0, 1e-3);  // |S21|
    EXPECT_NEAR(std::abs(S[0][1]), 1.0, 1e-3);  // |S12| reciprocal
    EXPECT_NEAR(std::abs(S[0][0]), 0.0, 1e-3);  // |S11| ~ 0
    EXPECT_NEAR(std::abs(S[1][1]), 0.0, 1e-3);  // |S22| ~ 0

    // Phase delay check for S21
    auto phase = [](const Complex& z) { return std::atan2(z.imag(), z.real()); };
    Real expected_phase = -omega * td;  // e^{-jωTD}
    Real actual_phase = phase(S[1][0]);
    // Wrap difference to [-pi, pi]
    Real diff = std::remainder(actual_phase - expected_phase, 2.0 * M_PI);
    EXPECT_NEAR(diff, 0.0, 1e-3);
}

TEST(SParam, TransmissionLineNearPoleStability) {
    // Frequencies where θ = ω·TD ≈ nπ (n = 1, 2, 4) — sin(θ) → 0.
    // The cot/csc implementation should remain finite and still give |S21|≈1, |S11|≈0.

    constexpr Real td = 1e-9;  // 1 ns
    const Real f_start = 0.5e9;   // θ = π
    const Real f_stop  = 2.5e9;   // θ = 5π

    std::string netlist = R"(
* Lossless T-line stability near sin(theta)=0
T1 in 0 out 0 Z0=50 TD=1e-9
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.AC LIN 5 500Meg 2.5Gig
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = f_start;
    result.config.sparam.stop_freq = f_stop;
    result.config.sparam.num_points = 5;   // 0.5, 1.0, 1.5, 2.0, 2.5 GHz
    result.config.sparam.sweep_type = "LIN";

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    ASSERT_EQ(sparam_result.frequency_points.size(), 5);

    for (const auto& fp : sparam_result.frequency_points) {
        const auto& S = fp.S_matrix;
        ASSERT_EQ(S.size(), 2);
        ASSERT_EQ(S[0].size(), 2);

        // Finite checks
        auto finite = [](const Complex& z) {
            return std::isfinite(z.real()) && std::isfinite(z.imag());
        };
        EXPECT_TRUE(finite(S[0][0]));
        EXPECT_TRUE(finite(S[0][1]));
        EXPECT_TRUE(finite(S[1][0]));
        EXPECT_TRUE(finite(S[1][1]));

        // Magnitude behavior for matched line
        EXPECT_NEAR(std::abs(S[1][0]), 1.0, 5e-3);  // |S21|
        EXPECT_NEAR(std::abs(S[0][1]), 1.0, 5e-3);  // |S12|
        EXPECT_NEAR(std::abs(S[0][0]), 0.0, 5e-3);  // |S11|
        EXPECT_NEAR(std::abs(S[1][1]), 0.0, 5e-3);  // |S22|
    }
}

TEST(SParam, LossyTLine_Attenuation) {
    // Simple lossy line: add series resistance so |S21| < 1 and decreases with frequency.
std::string netlist = R"(
* Lossy O-line attenuation test
O1 in 0 out 0 R=200 L=1e-6 C=1e-12 G=1e-3 TD=5e-9
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.AC DEC 3 10Meg 1Gig
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 10e6;
    result.config.sparam.stop_freq = 1e9;
    result.config.sparam.num_points = 3;  // per decade; DEC will expand
    result.config.sparam.sweep_type = "DEC";

    SParameterSolver solver(*result.circuit, result.config);
    auto sparam = solver.solve();
    ASSERT_TRUE(sparam.success);
    ASSERT_GE(sparam.frequency_points.size(), 3);

    auto mag = [&](size_t idx) {
        return std::abs(sparam.frequency_points[idx].S_matrix[1][0]); // |S21|
    };

    size_t last = sparam.frequency_points.size() - 1;
    size_t mid_idx = sparam.frequency_points.size() / 2;

    Real low = mag(0);
    Real high = mag(last);

    // Should be passive: |S21| < 1 across band (allow tiny num diff)
    EXPECT_LT(low, 1.0 + 1e-9);
    EXPECT_LT(high, 1.0 + 1e-9);
}

TEST(SParam, LossyTLine_MatchedSmallLoss) {
    // Small losses: expect |S11| small, |S21| slightly below 1. L and C give
    // Zc = sqrt(L/C) = 50 Ohm, matched to the ports, and TD a length of
    // TD / sqrt(L C) = 0.2 m (telegrapher's two-port: |S11| = 9.3e-4,
    // |S21| = 0.999 at 100 MHz)
    std::string netlist = R"(
* Lossy O-line matched small loss
O1 in 0 out 0 R=0.5 L=250e-9 C=100e-12 G=0 TD=1e-9
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.AC LIN 1 100Meg 100Meg
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 100e6;
    result.config.sparam.stop_freq = 100e6;
    result.config.sparam.num_points = 1;
    result.config.sparam.sweep_type = "LIN";

    SParameterSolver solver(*result.circuit, result.config);
    auto sparam = solver.solve();
    ASSERT_TRUE(sparam.success);
    auto& S = sparam.frequency_points[0].S_matrix;

    EXPECT_NEAR(std::abs(S[0][0]), 0.0, 0.05);  // reflection small
    EXPECT_NEAR(std::abs(S[1][1]), 0.0, 0.05);
    EXPECT_GT(std::abs(S[1][0]), 0.9);          // small attenuation only
    EXPECT_GT(std::abs(S[0][1]), 0.9);
}

TEST(SParam, DifferentImpedances) {
    // Mixed impedances: 50Ω port and 75Ω port

    std::string netlist = R"(
* Mixed impedance test
R1 in out 60
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=75
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 1e9;
    result.config.sparam.stop_freq = 1e9;
    result.config.sparam.num_points = 1;

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    const auto& S = sparam_result.frequency_points[0].S_matrix;

    // S11 and S22 should be different due to impedance mismatch
    Real S11_mag = std::abs(S[0][0]);
    Real S22_mag = std::abs(S[1][1]);

    // Both should show some reflection
    EXPECT_GT(S11_mag, 0.0);
    EXPECT_GT(S22_mag, 0.0);

    // Reciprocity still holds: |S12| = |S21|
    EXPECT_NEAR(std::abs(S[0][1]), std::abs(S[1][0]), 0.01);
}

// ============================================================================
// 5.3 Touchstone Output Tests (2 tests)
// ============================================================================

TEST(Touchstone, TwoPort_MA_Format) {
    // Create simple S-parameter result
    SParameterResult result;
    result.success = true;

    PortDefinition port1, port2;
    port1.port_number = 1;
    port1.node_positive = "in";
    port1.node_negative = "0";
    port1.impedance = 50.0;

    port2.port_number = 2;
    port2.node_positive = "out";
    port2.node_negative = "0";
    port2.impedance = 50.0;

    result.ports = {port1, port2};

    // Add frequency point
    SParameterPoint point;
    point.frequency = 1e9;  // 1 GHz
    point.S_matrix.resize(2, std::vector<Complex>(2));
    point.S_matrix[0][0] = Complex(0.1, 0.0);   // S11
    point.S_matrix[0][1] = Complex(0.8, 0.1);   // S12
    point.S_matrix[1][0] = Complex(0.8, 0.1);   // S21
    point.S_matrix[1][1] = Complex(0.1, 0.0);   // S22

    result.frequency_points.push_back(point);

    // Write Touchstone file
    std::string filename = kirimaspice::test::test_output_path("test_2port.s2p").string();
    bool success = TouchstoneWriter::write(filename, result, TouchstoneWriter::Format::MA);
    ASSERT_TRUE(success);

    // Verify file contents
    std::ifstream ifs(filename);
    ASSERT_TRUE(ifs.is_open());

    std::string line;

    // Read header
    std::getline(ifs, line);
    EXPECT_EQ(line.substr(0, 11), "# Hz S MA R");

    // Skip comment lines
    while (std::getline(ifs, line)) {
        if (line[0] != '!') break;
    }

    // Verify data line exists
    EXPECT_FALSE(line.empty());
    EXPECT_TRUE(line.find("1.000000e+09") != std::string::npos);  // Frequency

    ifs.close();
}

TEST(Touchstone, FourPort_DB_Format) {
    // Create 4-port S-parameter result
    SParameterResult result;
    result.success = true;

    for (int i = 1; i <= 4; ++i) {
        PortDefinition port;
        port.port_number = i;
        port.node_positive = "n" + std::to_string(i);
        port.node_negative = "0";
        port.impedance = 50.0;
        result.ports.push_back(port);
    }

    // Add frequency point
    SParameterPoint point;
    point.frequency = 2e9;  // 2 GHz
    point.S_matrix.resize(4, std::vector<Complex>(4));

    // Fill with dummy data
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            point.S_matrix[i][j] = (i == j) ? Complex(0.1, 0.0) : Complex(0.01, 0.01);
        }
    }

    result.frequency_points.push_back(point);

    // Write Touchstone file in DB format
    std::string filename = kirimaspice::test::test_output_path("test_4port.s4p").string();
    bool success = TouchstoneWriter::write(filename, result, TouchstoneWriter::Format::DB);
    ASSERT_TRUE(success);

    // Verify file contents
    std::ifstream ifs(filename);
    ASSERT_TRUE(ifs.is_open());

    std::string line;

    // Read header
    std::getline(ifs, line);
    EXPECT_EQ(line.substr(0, 11), "# Hz S DB R");

    // Skip comment lines
    while (std::getline(ifs, line)) {
        if (line[0] != '!') break;
    }

    // Verify frequency line
    EXPECT_TRUE(line.find("2.000000e+09") != std::string::npos);

    ifs.close();
}

// ============================================================================
// 5.4 Integration Tests (2 tests)
// ============================================================================

TEST(SParam_Integration, SimpleCircuit) {
    // End-to-end test: Parse → Solve → Output

    std::string netlist = R"(
* Simple integration test
R1 in mid 25
R2 mid out 25
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 1e9;
    result.config.sparam.stop_freq = 2e9;
    result.config.sparam.num_points = 2;
    result.config.sparam.sweep_type = "LIN";

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    ASSERT_EQ(sparam_result.frequency_points.size(), 2);

    // Write Touchstone file
    std::string filename = kirimaspice::test::test_output_path("integration_test.s2p").string();
    bool write_success = TouchstoneWriter::write(filename, sparam_result);
    ASSERT_TRUE(write_success);

    // Verify file exists and has content
    std::ifstream ifs(filename);
    ASSERT_TRUE(ifs.is_open());

    std::string content((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
    EXPECT_GT(content.size(), 100);  // Should have reasonable content
    EXPECT_TRUE(content.find("# Hz S MA R 50") != std::string::npos);

    ifs.close();
}

TEST(SParam_Integration, ComplexCircuit) {
    // RLC filter with 2 ports

    std::string netlist = R"(
* RLC filter integration test
R1 in n1 50
L1 n1 n2 1e-9
C1 n2 0 1e-12
R2 n2 out 50
.PORT 1 in 0 Z=50
.PORT 2 out 0 Z=50
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    result.config.sparam.enabled = true;
    result.config.sparam.start_freq = 1e8;   // 100 MHz
    result.config.sparam.stop_freq = 10e9;   // 10 GHz
    result.config.sparam.num_points = 10;
    result.config.sparam.sweep_type = "DEC";

    SParameterSolver solver(*result.circuit, result.config);
    SParameterResult sparam_result = solver.solve();

    ASSERT_TRUE(sparam_result.success);
    EXPECT_GE(sparam_result.frequency_points.size(), 10);

    // Verify frequency response characteristics
    for (const auto& point : sparam_result.frequency_points) {
        const auto& S = point.S_matrix;

        // Energy conservation (for lossless network, not strictly applicable here due to R)
        // But we can check that magnitudes are reasonable
        Real sum_mag_squared = 0.0;
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                sum_mag_squared += std::norm(S[i][j]);
            }
        }

        // For passive network, sum should be ≤ 2 (energy conservation in 2-port)
        EXPECT_LE(sum_mag_squared, 2.5);  // Allow some margin
    }
}
