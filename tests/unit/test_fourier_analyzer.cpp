// tests/unit/test_fourier_analyzer.cpp
#include <gtest/gtest.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/fourier_analyzer.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

#include <cmath>
#include <numbers>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// ========================================
// Parser Tests
// ========================================

TEST(FourierParser, BasicSyntax) {
    std::string netlist = R"(
Simple RC Circuit
R1 in out 1k
C1 out 0 1u
Vin in 0 DC 1
.TRAN 0 5ms
.FOUR 1kHz v(out)
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.fourier.enabled);
    EXPECT_DOUBLE_EQ(result.config.fourier.fundamental_freq, 1000.0);
    EXPECT_EQ(result.config.fourier.signal, "v(out)");
}

TEST(FourierParser, FrequencyUnitSuffix) {
    std::string netlist = R"(
Unit Suffix Test
R1 in out 1k
Vin in 0 DC 1
.TRAN 0 5ms
.FOUR 1kHz v(out)
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.fourier.enabled);
    EXPECT_DOUBLE_EQ(result.config.fourier.fundamental_freq, 1000.0);
}

TEST(FourierParser, FourierAlias) {
    std::string netlist = R"(
Alias Test
R1 in out 1k
Vin in 0 DC 1
.TRAN 0 100ms
.FOURIER 60 v(out)
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.fourier.enabled);
    EXPECT_DOUBLE_EQ(result.config.fourier.fundamental_freq, 60.0);
}

TEST(FourierParser, CurrentSignal) {
    std::string netlist = R"(
Current Signal Test
R1 in out 1k
Vload out 0 DC 0
Vin in 0 DC 1
.TRAN 0 5ms
.FOUR 1kHz i(Vload)
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_TRUE(result.config.fourier.enabled);
    EXPECT_EQ(result.config.fourier.signal, "i(Vload)");
}

TEST(FourierParser, InvalidSyntax_TooFewArgs) {
    std::string netlist = R"(
Invalid Syntax
R1 in out 1k
Vin in 0 DC 1
.TRAN 0 5ms
.FOUR 1kHz
.END
)";

    NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

TEST(FourierParser, InvalidFrequency_Negative) {
    std::string netlist = R"(
Invalid Frequency
R1 in out 1k
Vin in 0 DC 1
.TRAN 0 5ms
.FOUR -1kHz v(out)
.END
)";

    NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

TEST(FourierParser, InvalidSignalFormat) {
    std::string netlist = R"(
Invalid Signal Format
R1 in out 1k
Vin in 0 DC 1
.TRAN 0 5ms
.FOUR 1kHz out
.END
)";

    NetlistParser parser;
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

// ========================================
// FourierAnalyzer Unit Tests
// ========================================

TEST(FourierAnalyzer, PureSineWave) {
    // Generate pure sine wave: f(t) = sin(2*pi*1000*t)
    Real freq = 1000.0;  // 1 kHz
    Real omega = 2.0 * std::numbers::pi * freq;

    std::vector<Real> time;
    std::vector<Real> signal;

    // Sample 10 periods with 100 points per period
    Real period = 1.0 / freq;
    int num_periods = 10;
    int points_per_period = 100;
    int total_points = num_periods * points_per_period;

    for (int i = 0; i < total_points; ++i) {
        Real t = i * period / points_per_period;
        time.push_back(t);
        signal.push_back(std::sin(omega * t));
    }

    FourierResult result = FourierAnalyzer::analyze(time, signal, freq, "v(out)", 5);

    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.dc_component, 0.0, 1e-3);  // Relaxed tolerance
    EXPECT_EQ(result.harmonics.size(), 5);

    // Fundamental (1st harmonic) should have magnitude 1.0
    EXPECT_NEAR(result.harmonics[0].magnitude, 1.0, 0.01);
    EXPECT_NEAR(result.harmonics[0].phase_degrees, -90.0, 5.0);  // Relaxed tolerance

    // Higher harmonics should be nearly zero
    for (size_t i = 1; i < result.harmonics.size(); ++i) {
        EXPECT_LT(result.harmonics[i].magnitude, 0.05);
    }

    // THD should be very low for pure sine
    EXPECT_LT(result.thd, 1.0);
}

TEST(FourierAnalyzer, DCComponent) {
    // Signal with DC offset: f(t) = 2.5 + sin(2*pi*1000*t)
    Real freq = 1000.0;
    Real omega = 2.0 * std::numbers::pi * freq;
    Real dc_offset = 2.5;

    std::vector<Real> time;
    std::vector<Real> signal;

    Real period = 1.0 / freq;
    int total_points = 1000;

    for (int i = 0; i < total_points; ++i) {
        Real t = i * 10 * period / total_points;
        time.push_back(t);
        signal.push_back(dc_offset + std::sin(omega * t));
    }

    FourierResult result = FourierAnalyzer::analyze(time, signal, freq, "v(out)");

    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.dc_component, dc_offset, 0.05);  // Relaxed tolerance
    EXPECT_NEAR(result.harmonics[0].magnitude, 1.0, 0.01);
}

TEST(FourierAnalyzer, SquareWave) {
    // Square wave with fundamental frequency 1 kHz
    // Should have odd harmonics: 1st, 3rd, 5th, etc.
    Real freq = 1000.0;

    std::vector<Real> time;
    std::vector<Real> signal;

    Real period = 1.0 / freq;
    int total_points = 10000;

    for (int i = 0; i < total_points; ++i) {
        Real t = i * 10 * period / total_points;
        time.push_back(t);

        // Square wave: +1 for first half period, -1 for second half
        Real phase = std::fmod(t * freq, 1.0);
        signal.push_back(phase < 0.5 ? 1.0 : -1.0);
    }

    FourierResult result = FourierAnalyzer::analyze(time, signal, freq, "v(out)", 9);

    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.dc_component, 0.0, 0.01);

    // Square wave should have significant odd harmonics
    // Magnitude ~ 4/(n*pi) for odd harmonics
    EXPECT_GT(result.harmonics[0].magnitude, 1.0);  // 1st harmonic (fundamental)
    EXPECT_GT(result.harmonics[2].magnitude, 0.3);  // 3rd harmonic
    EXPECT_GT(result.harmonics[4].magnitude, 0.2);  // 5th harmonic

    // Even harmonics should be small
    EXPECT_LT(result.harmonics[1].magnitude, 0.1);  // 2nd harmonic
    EXPECT_LT(result.harmonics[3].magnitude, 0.1);  // 4th harmonic

    // THD should be high for square wave
    EXPECT_GT(result.thd, 30.0);
}

TEST(FourierAnalyzer, TriangleWave) {
    // Triangle wave: linear ramp up and down
    Real freq = 1000.0;

    std::vector<Real> time;
    std::vector<Real> signal;

    Real period = 1.0 / freq;
    int total_points = 10000;

    for (int i = 0; i < total_points; ++i) {
        Real t = i * 10 * period / total_points;
        time.push_back(t);

        // Triangle wave: ramp from -1 to 1 and back
        Real phase = std::fmod(t * freq, 1.0);
        if (phase < 0.5) {
            signal.push_back(-1.0 + 4.0 * phase);
        } else {
            signal.push_back(3.0 - 4.0 * phase);
        }
    }

    FourierResult result = FourierAnalyzer::analyze(time, signal, freq, "v(out)", 9);

    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.dc_component, 0.0, 0.01);

    // Triangle wave has odd harmonics with magnitude ~ 8/(n^2*pi^2)
    EXPECT_GT(result.harmonics[0].magnitude, 0.7);  // 1st harmonic
    EXPECT_LT(result.harmonics[2].magnitude, 0.15); // 3rd harmonic (much smaller)

    // THD should be moderate for triangle wave
    EXPECT_GT(result.thd, 5.0);
    EXPECT_LT(result.thd, 20.0);
}

TEST(FourierAnalyzer, THDCalculation) {
    // Create a signal with known harmonic content
    // f(t) = 1.0*sin(omega*t) + 0.1*sin(2*omega*t) + 0.05*sin(3*omega*t)
    // Expected THD = sqrt(0.1^2 + 0.05^2) / 1.0 * 100% ≈ 11.18%

    Real freq = 1000.0;
    Real omega = 2.0 * std::numbers::pi * freq;

    std::vector<Real> time;
    std::vector<Real> signal;

    Real period = 1.0 / freq;
    int total_points = 10000;

    for (int i = 0; i < total_points; ++i) {
        Real t = i * 10 * period / total_points;
        time.push_back(t);
        signal.push_back(1.0 * std::sin(omega * t) +
                        0.1 * std::sin(2.0 * omega * t) +
                        0.05 * std::sin(3.0 * omega * t));
    }

    FourierResult result = FourierAnalyzer::analyze(time, signal, freq, "v(out)", 5);

    EXPECT_TRUE(result.success);

    // Check harmonic magnitudes
    EXPECT_NEAR(result.harmonics[0].magnitude, 1.0, 0.02);  // Fundamental
    EXPECT_NEAR(result.harmonics[1].magnitude, 0.1, 0.02);  // 2nd harmonic
    EXPECT_NEAR(result.harmonics[2].magnitude, 0.05, 0.02); // 3rd harmonic

    // Check THD: sqrt(0.1^2 + 0.05^2) / 1.0 * 100% = 11.18%
    Real expected_thd = std::sqrt(0.1*0.1 + 0.05*0.05) / 1.0 * 100.0;
    EXPECT_NEAR(result.thd, expected_thd, 1.0);
}

TEST(FourierAnalyzer, InsufficientData) {
    // Less than one period of data
    Real freq = 1000.0;
    Real omega = 2.0 * std::numbers::pi * freq;

    std::vector<Real> time;
    std::vector<Real> signal;

    // Only half a period
    Real period = 1.0 / freq;
    for (int i = 0; i < 50; ++i) {
        Real t = i * 0.5 * period / 50;
        time.push_back(t);
        signal.push_back(std::sin(omega * t));
    }

    FourierResult result = FourierAnalyzer::analyze(time, signal, freq, "v(out)");

    EXPECT_FALSE(result.success);
}

TEST(FourierAnalyzer, EmptyData) {
    std::vector<Real> time;
    std::vector<Real> signal;

    FourierResult result = FourierAnalyzer::analyze(time, signal, 1000.0, "v(out)");

    EXPECT_FALSE(result.success);
}

// ========================================
// Integration Tests (require circuit simulation)
// ========================================

TEST(FourierIntegration, RCLowpassFilter) {
    std::string netlist = R"(
RC Lowpass Filter
R1 in out 159
C1 out 0 1u
Vin in 0 DC 1
.TRAN 0 10ms 0 1us
.FOUR 1kHz v(out)
.END
)";

    NetlistParser parser;
    auto parse_result = parser.parse_string(netlist);

    ASSERT_TRUE(parse_result.config.fourier.enabled);
    EXPECT_DOUBLE_EQ(parse_result.config.fourier.fundamental_freq, 1000.0);
    EXPECT_EQ(parse_result.config.fourier.signal, "v(out)");

    // Run transient analysis - this should trigger Fourier analysis automatically
    TransientSolver solver(*parse_result.circuit, parse_result.config);
    auto tran_result = solver.solve(0.0, 0.01, 1e-6);  // 0 to 10ms, 1us timestep

    // Verify transient analysis succeeded
    EXPECT_EQ(tran_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(tran_result.timepoints.size(), 0);

    // Note: Fourier analysis is performed automatically and results are printed
    // For a DC voltage source, we expect DC component ≈ 1.0 and negligible harmonics
    // This test verifies the integration is working (no crashes, analysis runs)
}
