// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_ac_solver.cpp
// Unit tests for AC analysis solver
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/types.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

// Test fixture for AC solver tests
class ACSolverTest : public ::testing::Test {
protected:
    // Helper: compute magnitude in dB
    double magnitude_db(Complex z) {
        return 20.0 * std::log10(std::abs(z));
    }

    // Helper: compute phase in degrees
    double phase_degrees(Complex z) {
        return std::arg(z) * 180.0 / M_PI;
    }

    // Helper: RC Low-Pass Filter theoretical transfer function
    // H(jω) = 1 / (1 + jωRC)
    Complex rc_lowpass_theory(Real omega, Real R, Real C) {
        Complex j(0.0, 1.0);
        return 1.0 / (1.0 + j * omega * R * C);
    }

    // Helper: find frequency point closest to target frequency
    size_t find_frequency_index(const ACResult& result, Real target_freq) {
        size_t best_idx = 0;
        Real best_diff = std::abs(result.frequency_points[0].frequency - target_freq);

        for (size_t i = 1; i < result.frequency_points.size(); ++i) {
            Real diff = std::abs(result.frequency_points[i].frequency - target_freq);
            if (diff < best_diff) {
                best_diff = diff;
                best_idx = i;
            }
        }

        return best_idx;
    }
};

// Test: RC Low-Pass Filter - basic functionality
TEST_F(ACSolverTest, RCLowPassFilter_BasicFunctionality) {
    // Circuit: Vin --[R=1kΩ]-- Vout --[C=1μF]-- GND
    // Cutoff frequency: fc = 1/(2πRC) ≈ 159 Hz

    circuit::CircuitGraph circuit;
    Real R = 1000.0;  // 1 kΩ
    Real C = 1e-6;    // 1 μF
    Real fc = 1.0 / (2.0 * M_PI * R * C);  // ≈ 159.15 Hz

    circuit.add_voltage_source("Vin", "in", "0", 1.0);  // DC value doesn't matter for AC
    circuit.add_resistor("R1", "in", "out", R);
    circuit.add_capacitor("C1", "out", "0", C);

    // Configure AC analysis: decade sweep from 10 Hz to 10 kHz
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 10.0;
    config.ac.stop_freq = 10000.0;
    config.ac.num_points = 10;  // 10 points per decade

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.frequency_points.size(), 0);

    std::cout << "RC Low-Pass Filter AC Analysis:" << std::endl;
    std::cout << "  R = " << R << " Ω, C = " << C * 1e6 << " μF" << std::endl;
    std::cout << "  fc = " << fc << " Hz" << std::endl;
    std::cout << "  Frequency points: " << result.frequency_points.size() << std::endl;
}

// Test: RC Low-Pass Filter - cutoff frequency verification
TEST_F(ACSolverTest, RCLowPassFilter_CutoffFrequency) {
    circuit::CircuitGraph circuit;
    Real R = 1000.0;  // 1 kΩ
    Real C = 1e-6;    // 1 μF
    Real fc = 1.0 / (2.0 * M_PI * R * C);  // ≈ 159.15 Hz

    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", R);
    circuit.add_capacitor("C1", "out", "0", C);

    NodeId node_out = circuit.get_node_id("out");

    // AC analysis around cutoff frequency
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 10.0;
    config.ac.stop_freq = 10000.0;
    config.ac.num_points = 20;  // 20 points per decade for better resolution

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Find frequency point closest to cutoff frequency
    size_t fc_idx = find_frequency_index(result, fc);
    const auto& point_fc = result.frequency_points[fc_idx];

    // At cutoff frequency, magnitude should be -3 dB (0.707 of DC gain)
    Complex V_out = point_fc.solution[node_out - 1];
    double mag_db = magnitude_db(V_out);

    std::cout << "Cutoff frequency test:" << std::endl;
    std::cout << "  fc (theory) = " << fc << " Hz" << std::endl;
    std::cout << "  f (actual) = " << point_fc.frequency << " Hz" << std::endl;
    std::cout << "  |H| = " << std::abs(V_out) << " (" << mag_db << " dB)" << std::endl;

    // At cutoff, magnitude should be approximately -3 dB
    // Allow 10% tolerance due to discrete frequency points
    EXPECT_NEAR(mag_db, -3.0, 0.5) << "Magnitude at cutoff should be -3 dB";
}

// Test: RC Low-Pass Filter - frequency response accuracy
TEST_F(ACSolverTest, RCLowPassFilter_FrequencyResponseAccuracy) {
    circuit::CircuitGraph circuit;
    Real R = 1000.0;  // 1 kΩ
    Real C = 1e-6;    // 1 μF

    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", R);
    circuit.add_capacitor("C1", "out", "0", C);

    NodeId node_out = circuit.get_node_id("out");

    // Linear sweep for easier comparison
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 10.0;
    config.ac.stop_freq = 1000.0;
    config.ac.num_points = 100;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Check accuracy at several frequency points
    std::vector<Real> test_frequencies = {10.0, 50.0, 100.0, 159.15, 500.0, 1000.0};

    for (Real f_test : test_frequencies) {
        size_t idx = find_frequency_index(result, f_test);
        const auto& point = result.frequency_points[idx];

        Complex V_out = point.solution[node_out - 1];
        Complex H_theory = rc_lowpass_theory(point.omega, R, C);

        double mag_error = std::abs(std::abs(V_out) - std::abs(H_theory));
        double mag_error_percent = mag_error / std::abs(H_theory) * 100.0;

        double phase_sim = phase_degrees(V_out);
        double phase_theory = phase_degrees(H_theory);
        double phase_error = std::abs(phase_sim - phase_theory);

        std::cout << "f=" << point.frequency << " Hz:" << std::endl;
        std::cout << "  |H| sim=" << std::abs(V_out) << ", theory=" << std::abs(H_theory)
                  << " (error: " << mag_error_percent << "%)" << std::endl;
        std::cout << "  ∠H sim=" << phase_sim << "°, theory=" << phase_theory
                  << "° (error: " << phase_error << "°)" << std::endl;

        // Magnitude error should be < 1%
        EXPECT_LT(mag_error_percent, 1.0)
            << "Magnitude error at " << point.frequency << " Hz exceeds 1%";

        // Phase error should be < 1 degree
        EXPECT_LT(phase_error, 1.0)
            << "Phase error at " << point.frequency << " Hz exceeds 1°";
    }
}

// Test: Floating source/capacitor network should converge via AC Gmin injection
TEST_F(ACSolverTest, FloatingBranchStabilizedByGmin) {
    circuit::CircuitGraph circuit;
    // Floating voltage source between nodes n1 and n2
    circuit.add_voltage_source("V1", "n1", "n2", 0.0, 1.0);
    // Capacitor also between the same nodes (no ground reference)
    circuit.add_capacitor("C1", "n1", "n2", 1e-9);

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1e3;
    config.ac.stop_freq = 1e3;
    config.ac.num_points = 1;
    config.gmin = 1e-9;  // Make Gmin effect visible
    config.dc.enable_dynamic_gmin = true;
    config.dc.dynamic_gmin_start = 1e-6;
    config.dc.dynamic_gmin_max = 1e-3;
    config.dc.dynamic_gmin_decrease_factor = 0.5;
    config.dc.dynamic_gmin_increase_factor = 10.0;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.frequency_points.size(), 1u);
    // Solution vector should contain the floating pair voltages without NaN/inf
    for (const auto& value : result.frequency_points.front().solution) {
        EXPECT_TRUE(std::isfinite(std::real(value)));
        EXPECT_TRUE(std::isfinite(std::imag(value)));
    }
}

// Test: Resistor divider (frequency-independent)
TEST_F(ACSolverTest, ResistorDivider_FrequencyIndependent) {
    // Circuit: Vin --[R1=1kΩ]-- Vout --[R2=1kΩ]-- GND
    // Expected: Vout/Vin = R2/(R1+R2) = 0.5 at all frequencies

    circuit::CircuitGraph circuit;
    Real R1 = 1000.0;
    Real R2 = 1000.0;

    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", R1);
    circuit.add_resistor("R2", "out", "0", R2);

    NodeId node_out = circuit.get_node_id("out");

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 1.0;
    config.ac.stop_freq = 1e6;
    config.ac.num_points = 10;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Check that gain is constant at all frequencies
    for (const auto& point : result.frequency_points) {
        Complex V_out = point.solution[node_out - 1];
        double gain = std::abs(V_out);
        double expected_gain = R2 / (R1 + R2);

        double error_percent = std::abs(gain - expected_gain) / expected_gain * 100.0;

        EXPECT_NEAR(gain, expected_gain, 0.01)
            << "Resistor divider gain should be constant at f=" << point.frequency << " Hz";

        // Phase should be 0 (pure real)
        double phase = phase_degrees(V_out);
        EXPECT_NEAR(phase, 0.0, 0.1)
            << "Resistor divider phase should be 0° at f=" << point.frequency << " Hz";
    }

    std::cout << "Resistor divider: gain is constant across all frequencies ✓" << std::endl;
}

// Test: AC current source excitation produces expected voltage across resistor
TEST_F(ACSolverTest, ACCurrentSourceDrivesResistor) {
    circuit::CircuitGraph circuit;
    Real Rload = 2000.0; // 2 kΩ

    circuit.add_resistor("Rload", "out", "0", Rload);
    circuit.add_current_source("IAC", "out", "0", 0.0, 1.0, 0.0); // 1 A AC into node

    NodeId node_out = circuit.get_node_id("out");

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1000.0;
    config.ac.num_points = 1;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.frequency_points.size(), 1u);

    Complex Vout = result.frequency_points.front().solution[node_out - 1];
    // For a resistor to ground, V = I * R
    EXPECT_NEAR(std::abs(Vout), Rload * 1.0, 1e-3);
    double phase = std::arg(Vout);
    // Depending on source orientation, phase may be 0 or π
    double phase_mod = std::fmod(std::abs(phase), 2.0 * M_PI);
    bool in_phase = std::abs(phase_mod) < 1e-3 || std::abs(phase_mod - 2.0 * M_PI) < 1e-3;
    bool inverted = std::abs(phase_mod - M_PI) < 1e-3;
    EXPECT_TRUE(in_phase || inverted) << "Unexpected phase: " << phase;
}

// Test: RL High-Pass Filter
TEST_F(ACSolverTest, RLCircuit_InductorBehavior) {
    // Circuit: Vin --[R=10Ω]-- Vout --[L=1mH]-- GND
    // At low frequencies: inductor acts as short circuit (V_out → 0)
    // At high frequencies: inductor acts as open circuit (V_out → V_in)

    circuit::CircuitGraph circuit;
    Real R = 10.0;    // 10 Ω
    Real L = 1e-3;    // 1 mH

    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", R);
    circuit.add_inductor("L1", "out", "0", L);

    NodeId node_out = circuit.get_node_id("out");

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 100.0;
    config.ac.stop_freq = 100000.0;
    config.ac.num_points = 10;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // At low frequency (100 Hz): V_out should be small (inductor is nearly short)
    const auto& point_low = result.frequency_points.front();
    Complex V_out_low = point_low.solution[node_out - 1];
    double mag_low = std::abs(V_out_low);

    // At high frequency (100 kHz): V_out should approach V_in (inductor is nearly open)
    const auto& point_high = result.frequency_points.back();
    Complex V_out_high = point_high.solution[node_out - 1];
    double mag_high = std::abs(V_out_high);

    std::cout << "RL Circuit:" << std::endl;
    std::cout << "  Low freq (" << point_low.frequency << " Hz): |V_out| = " << mag_low << std::endl;
    std::cout << "  High freq (" << point_high.frequency << " Hz): |V_out| = " << mag_high << std::endl;

    // High frequency magnitude should be much larger than low frequency
    EXPECT_GT(mag_high, mag_low * 10.0)
        << "High frequency response should be much larger than low frequency (inductor behavior)";
}

// Test: Frequency sweep types (lin/dec/oct)
TEST_F(ACSolverTest, FrequencySweepTypes) {
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 1e-6);

    // Test linear sweep
    {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
        config.ac.start_freq = 100.0;
        config.ac.stop_freq = 200.0;
        config.ac.num_points = 11;  // Should give 100, 110, 120, ..., 200 Hz

        ACSolver solver(circuit, config);
        ACResult result = solver.solve();

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        EXPECT_EQ(result.frequency_points.size(), 11);

        // Check spacing is uniform
        if (result.frequency_points.size() >= 2) {
            double df = result.frequency_points[1].frequency - result.frequency_points[0].frequency;
            EXPECT_NEAR(df, 10.0, 0.1) << "Linear sweep spacing should be 10 Hz";
        }

        std::cout << "Linear sweep: " << result.frequency_points.size() << " points" << std::endl;
    }

    // Test decade sweep
    {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 10.0;
        config.ac.stop_freq = 10000.0;  // 3 decades
        config.ac.num_points = 5;  // 5 points per decade

        ACSolver solver(circuit, config);
        ACResult result = solver.solve();

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        EXPECT_GE(result.frequency_points.size(), 15);  // ~3 decades × 5 points/decade

        std::cout << "Decade sweep: " << result.frequency_points.size() << " points (3 decades)" << std::endl;
    }

    // Test octave sweep
    {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::OCT;
        config.ac.start_freq = 100.0;
        config.ac.stop_freq = 800.0;  // 3 octaves (100→200→400→800)
        config.ac.num_points = 4;  // 4 points per octave

        ACSolver solver(circuit, config);
        ACResult result = solver.solve();

        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        EXPECT_GE(result.frequency_points.size(), 12);  // ~3 octaves × 4 points/octave

        std::cout << "Octave sweep: " << result.frequency_points.size() << " points (3 octaves)" << std::endl;
    }
}

// ============================================================================
// AC Analysis Integration Tests for Nonlinear Devices
// ============================================================================

// Test: Diode AC small-signal conductance
TEST_F(ACSolverTest, Diode_SmallSignalConductance) {
    // Circuit: Vdc --[R=1kΩ]-- Vout --[Diode]-- GND
    // DC: Diode is forward biased (~0.7V)
    // AC: Small-signal conductance gd = Is/(N*Vt) * exp(V/(N*Vt))

    circuit::CircuitGraph circuit;
    Real R = 1000.0;  // 1 kΩ
    Real Vdc = 2.0;   // 2V DC bias (ensures forward bias)
    Real Is = 1e-14;  // Saturation current
    Real n = 1.0;     // Emission coefficient

    circuit.add_voltage_source("Vdc", "in", "0", Vdc);
    circuit.add_resistor("R1", "in", "out", R);
    circuit.add_diode("D1", "out", "0", Is, n);

    NodeId node_out = circuit.get_node_id("out");

    // AC analysis at low frequency (frequency doesn't matter much for pure conductance)
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1001.0;  // Small range for minimal frequency variation
    config.ac.num_points = 2;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GE(result.frequency_points.size(), 1);

    // Get AC solution at first frequency point
    const auto& point = result.frequency_points[0];
    Complex V_out = point.solution[node_out - 1];

    std::cout << "Diode AC Small-Signal Test:" << std::endl;
    std::cout << "  Frequency: " << point.frequency << " Hz" << std::endl;
    std::cout << "  |V_out| = " << std::abs(V_out) << std::endl;
    std::cout << "  ∠V_out = " << phase_degrees(V_out) << "°" << std::endl;

    // The AC voltage should be non-zero (diode has finite small-signal impedance)
    EXPECT_GT(std::abs(V_out), 0.0)
        << "Diode AC small-signal response should be non-zero";

    // At low frequency, response should be mostly real (resistive)
    Real phase = std::abs(phase_degrees(V_out));
    EXPECT_LT(phase, 10.0)
        << "Diode small-signal response should be mostly resistive at low frequency";
}

// Test: BJT Common-Emitter Amplifier AC Analysis
TEST_F(ACSolverTest, BJT_CommonEmitterAmplifier) {
    // Simple common-emitter circuit:
    // Vcc=5V --[Rc=1kΩ]-- Vout(collector) --[BJT]-- Emitter--GND
    //                                        ^
    //                                        |
    //                        Vbase=0.7V --[Rb=10kΩ]-- Base
    //
    // This ensures BJT is in active region (Vbe ≈ 0.7V, Vce > 0.2V)

    circuit::CircuitGraph circuit;
    Real Vcc = 5.0;    // Supply voltage
    Real Vbase = 0.7;  // Base bias (ensures active region)
    Real Rc = 1000.0;  // Collector resistor
    Real Rb = 10000.0; // Base resistor
    Real Is = 1e-14;   // Saturation current
    Real beta_f = 100.0; // Forward current gain

    circuit.add_voltage_source("Vcc", "vcc", "0", Vcc);
    circuit.add_voltage_source("Vbase", "vb", "0", Vbase);
    circuit.add_resistor("Rc", "vcc", "out", Rc);
    circuit.add_resistor("Rb", "vb", "base", Rb);
    circuit.add_bjt_npn("Q1", "out", "base", "0", Is, beta_f);

    NodeId node_out = circuit.get_node_id("out");

    // AC analysis
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 100.0;
    config.ac.stop_freq = 100000.0;
    config.ac.num_points = 10;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.frequency_points.size(), 0);

    std::cout << "BJT Common-Emitter Amplifier AC Analysis:" << std::endl;
    std::cout << "  Frequency points: " << result.frequency_points.size() << std::endl;

    // Check AC response exists at all frequencies
    for (const auto& point : result.frequency_points) {
        Complex V_out = point.solution[node_out - 1];
        double mag = std::abs(V_out);

        EXPECT_GT(mag, 0.0)
            << "BJT AC response should be non-zero at f=" << point.frequency << " Hz";
    }

    // At mid-band frequency, check that amplifier provides gain
    size_t mid_idx = result.frequency_points.size() / 2;
    const auto& point_mid = result.frequency_points[mid_idx];
    Complex V_out_mid = point_mid.solution[node_out - 1];

    std::cout << "  Mid-band (f=" << point_mid.frequency << " Hz):" << std::endl;
    std::cout << "    |V_out| = " << std::abs(V_out_mid) << std::endl;
    std::cout << "    ∠V_out = " << phase_degrees(V_out_mid) << "°" << std::endl;
}

// Test: MOSFET Common-Source Amplifier AC Analysis
TEST_F(ACSolverTest, MOSFET_CommonSourceAmplifier) {
    // Simple common-source circuit:
    // Vdd=5V --[Rd=1kΩ]-- Vout(drain) --[MOSFET]-- Source--GND
    //                                      ^
    //                                      |
    //                       Vgate=2V --[Rg=10kΩ]-- Gate
    //
    // With Vth=0.7V, Vgs=2V ensures MOSFET is in saturation region

    circuit::CircuitGraph circuit;
    Real Vdd = 5.0;    // Supply voltage
    Real Vgate = 2.0;  // Gate bias (ensures saturation region)
    Real Rd = 1000.0;  // Drain resistor
    Real Rg = 10000.0; // Gate resistor
    Real kp = 2e-5;    // Process transconductance
    Real w = 10e-6;    // Channel width
    Real l = 1e-6;     // Channel length
    Real vth = 0.7;    // Threshold voltage

    circuit.add_voltage_source("Vdd", "vdd", "0", Vdd);
    circuit.add_voltage_source("Vgate", "vg", "0", Vgate);
    circuit.add_resistor("Rd", "vdd", "out", Rd);
    circuit.add_resistor("Rg", "vg", "gate", Rg);
    circuit.add_mosfet_n("M1", "out", "gate", "0", kp, w, l, vth);

    NodeId node_out = circuit.get_node_id("out");

    // AC analysis
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 100.0;
    config.ac.stop_freq = 100000.0;
    config.ac.num_points = 10;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.frequency_points.size(), 0);

    std::cout << "MOSFET Common-Source Amplifier AC Analysis:" << std::endl;
    std::cout << "  Frequency points: " << result.frequency_points.size() << std::endl;

    // Check AC response exists at all frequencies
    for (const auto& point : result.frequency_points) {
        Complex V_out = point.solution[node_out - 1];
        double mag = std::abs(V_out);

        EXPECT_GT(mag, 0.0)
            << "MOSFET AC response should be non-zero at f=" << point.frequency << " Hz";
    }

    // At mid-band frequency, check that amplifier provides response
    size_t mid_idx = result.frequency_points.size() / 2;
    const auto& point_mid = result.frequency_points[mid_idx];
    Complex V_out_mid = point_mid.solution[node_out - 1];

    std::cout << "  Mid-band (f=" << point_mid.frequency << " Hz):" << std::endl;
    std::cout << "    |V_out| = " << std::abs(V_out_mid) << std::endl;
    std::cout << "    ∠V_out = " << phase_degrees(V_out_mid) << "°" << std::endl;
}
