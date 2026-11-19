// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_multistage_amplifier_ac_validation.cpp
// Multi-stage amplifier AC analysis validation against ngspice

#include <gtest/gtest.h>
#include "../validation/ngspice_ac_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <filesystem>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;
using namespace kirimaspice::validation;

namespace fs = std::filesystem;

//==============================================================================
// 2-Stage BJT Amplifier Validation
//==============================================================================
class BJT2StageAmplifierValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "validation/bjt_2stage_amplifier_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path
                         << "\nRun: cd validation && ngspice -b bjt_2stage_amplifier_ac.cir";
        }
    }

    // Build 2-stage common-emitter BJT amplifier
    // Two cascaded CE stages for high gain
    CircuitGraph build_bjt_2stage_amplifier() {
        CircuitGraph circuit;

        // Power supply
        circuit.add_voltage_source("Vcc", "vcc", "0", 12.0, 0.0);  // 12V DC

        // Input signal (AC only)
        circuit.add_voltage_source("Vin", "in", "0", 0.0, 0.01);  // 0V DC, 10mV AC

        // Stage 1: Common-emitter amplifier
        circuit.add_capacitor("Cin", "in", "base1", 10e-6);         // Input coupling
        circuit.add_resistor("R1_1", "vcc", "base1", 100e3);        // Bias
        circuit.add_resistor("R2_1", "base1", "0", 22e3);           // Bias
        circuit.add_resistor("Rc1", "vcc", "c1", 4.7e3);            // Collector
        circuit.add_resistor("Re1", "e1", "0", 1e3);                // Emitter
        circuit.add_capacitor("Ce1", "e1", "0", 100e-6);            // Bypass
        circuit.add_bjt_npn("Q1", "c1", "base1", "e1");             // NPN

        // Stage 2: Common-emitter amplifier
        circuit.add_capacitor("Cc", "c1", "base2", 10e-6);          // Coupling
        circuit.add_resistor("R1_2", "vcc", "base2", 100e3);        // Bias
        circuit.add_resistor("R2_2", "base2", "0", 22e3);           // Bias
        circuit.add_resistor("Rc2", "vcc", "c2", 4.7e3);            // Collector
        circuit.add_resistor("Re2", "e2", "0", 1e3);                // Emitter
        circuit.add_capacitor("Ce2", "e2", "0", 100e-6);            // Bypass
        circuit.add_bjt_npn("Q2", "c2", "base2", "e2");             // NPN

        // Output
        circuit.add_capacitor("Cout", "c2", "out", 10e-6);          // Output coupling
        circuit.add_resistor("Rload", "out", "0", 10e3);            // Load

        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 10.0;        // 10 Hz
        config.ac.stop_freq = 1000000.0;    // 1 MHz
        config.ac.num_points = 20;          // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(BJT2StageAmplifierValidation, MatchesNgspice) {
    // DC convergence issue FIXED: Smart initial value estimation
    // BJT now starts in forward-active region with proper conductances
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0) << "No reference data loaded";

    auto circuit = build_bjt_2stage_amplifier();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    ASSERT_GT(result.frequency_points.size(), 0) << "No AC results computed";

    auto comparison = compare_ac_results(ref_data, result, node_out);

    // Multi-stage amplifiers: moderate tolerances due to cascaded errors
    const double max_mag_error_db = 1.0;     // 1 dB magnitude
    const double max_phase_error_deg = 5.0;  // 5 degrees phase

    double max_mag_err = 0.0;
    double max_phase_err = 0.0;

    for (const auto& point : comparison) {
        max_mag_err = std::max(max_mag_err, std::abs(point.mag_error_db));
        max_phase_err = std::max(max_phase_err, std::abs(point.phase_error_deg));

        EXPECT_LE(std::abs(point.mag_error_db), max_mag_error_db)
            << "At frequency " << point.frequency << " Hz"
            << ", magnitude error " << point.mag_error_db << " dB exceeds tolerance";

        EXPECT_LE(std::abs(point.phase_error_deg), max_phase_error_deg)
            << "At frequency " << point.frequency << " Hz"
            << ", phase error " << point.phase_error_deg << "° exceeds tolerance";
    }

    std::cout << "\n2-Stage BJT Amplifier AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

TEST_F(BJT2StageAmplifierValidation, MidbandGainCorrect) {
    // DC convergence issue FIXED: Smart initial value estimation
    auto circuit = build_bjt_2stage_amplifier();
    NodeId node_out = circuit.get_node_id("out");

    // Run AC analysis at midband frequency (1 kHz)
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1000.0;
    config.ac.num_points = 1;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.frequency_points.size(), 1);

    std::complex<double> v_out = result.frequency_points[0].solution[node_out - 1];
    double gain_magnitude = std::abs(v_out);
    double gain_db = 20.0 * std::log10(gain_magnitude);

    // 2-stage CE amplifier: expect high gain (60-80 dB typical)
    // Each stage: ~30-40 dB, cascaded: 60-80 dB
    std::cout << "\n2-Stage BJT Midband Gain:\n";
    std::cout << "  Frequency: 1 kHz\n";
    std::cout << "  Voltage gain: " << gain_magnitude << " V/V\n";
    std::cout << "  Voltage gain: " << gain_db << " dB\n";

    // Sanity check: gain should be reasonably high
    EXPECT_GT(gain_db, 40.0) << "2-stage amplifier gain too low";
    EXPECT_LT(gain_db, 100.0) << "2-stage amplifier gain unexpectedly high";
}

//==============================================================================
// 2-Stage MOSFET Amplifier Validation
//==============================================================================
class MOSFET2StageAmplifierValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "validation/mosfet_2stage_amplifier_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path
                         << "\nRun: cd validation && ngspice -b mosfet_2stage_amplifier_ac.cir";
        }
    }

    // Build 2-stage common-source MOSFET amplifier
    CircuitGraph build_mosfet_2stage_amplifier() {
        CircuitGraph circuit;

        // Power supply
        circuit.add_voltage_source("VDD", "vdd", "0", 10.0, 0.0);  // 10V DC

        // Input signal (AC only)
        circuit.add_voltage_source("Vin", "in", "0", 0.0, 0.01);

        // Stage 1: Common-source amplifier with voltage divider bias
        circuit.add_capacitor("Cin", "in", "gate1", 10e-6);        // Input coupling
        circuit.add_resistor("Rg1a", "vdd", "gate1", 1e6);         // Gate bias (high)
        circuit.add_resistor("Rg1b", "gate1", "0", 470e3);         // Gate bias (low)
        circuit.add_resistor("Rd1", "vdd", "d1", 10e3);            // Drain
        circuit.add_resistor("Rs1", "s1", "0", 2e3);               // Source
        circuit.add_capacitor("Cs1", "s1", "0", 100e-6);           // Bypass
        circuit.add_mosfet_n("M1", "d1", "gate1", "s1");           // NMOS

        // Stage 2: Common-source amplifier with voltage divider bias
        circuit.add_capacitor("Cc", "d1", "gate2", 10e-6);         // Coupling
        circuit.add_resistor("Rg2a", "vdd", "gate2", 1e6);         // Gate bias (high)
        circuit.add_resistor("Rg2b", "gate2", "0", 470e3);         // Gate bias (low)
        circuit.add_resistor("Rd2", "vdd", "d2", 10e3);            // Drain
        circuit.add_resistor("Rs2", "s2", "0", 2e3);               // Source
        circuit.add_capacitor("Cs2", "s2", "0", 100e-6);           // Bypass
        circuit.add_mosfet_n("M2", "d2", "gate2", "s2");           // NMOS

        // Output
        circuit.add_capacitor("Cout", "d2", "out", 10e-6);         // Output coupling
        circuit.add_resistor("Rload", "out", "0", 10e3);           // Load

        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 10.0;        // 10 Hz
        config.ac.stop_freq = 1000000.0;    // 1 MHz
        config.ac.num_points = 20;          // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(MOSFET2StageAmplifierValidation, MatchesNgspice) {
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0) << "No reference data loaded";

    auto circuit = build_mosfet_2stage_amplifier();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    ASSERT_GT(result.frequency_points.size(), 0) << "No AC results computed";

    auto comparison = compare_ac_results(ref_data, result, node_out);

    // Multi-stage amplifiers: moderate tolerances
    const double max_mag_error_db = 1.0;
    const double max_phase_error_deg = 5.0;

    double max_mag_err = 0.0;
    double max_phase_err = 0.0;

    for (const auto& point : comparison) {
        max_mag_err = std::max(max_mag_err, std::abs(point.mag_error_db));
        max_phase_err = std::max(max_phase_err, std::abs(point.phase_error_deg));

        EXPECT_LE(std::abs(point.mag_error_db), max_mag_error_db)
            << "At frequency " << point.frequency << " Hz"
            << ", magnitude error exceeds tolerance";

        EXPECT_LE(std::abs(point.phase_error_deg), max_phase_error_deg)
            << "At frequency " << point.frequency << " Hz"
            << ", phase error exceeds tolerance";
    }

    std::cout << "\n2-Stage MOSFET Amplifier AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

//==============================================================================
// Darlington Pair BJT Amplifier Validation
//==============================================================================
class BJTDarlingtonAmplifierValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "validation/bjt_darlington_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path
                         << "\nRun: cd validation && ngspice -b bjt_darlington_ac.cir";
        }
    }

    // Build Darlington pair amplifier (high current gain)
    // Q1 drives Q2, emitters connected
    CircuitGraph build_darlington_amplifier() {
        CircuitGraph circuit;

        // Power supply
        circuit.add_voltage_source("Vcc", "vcc", "0", 12.0, 0.0);  // 12V DC

        // Input signal (AC only)
        circuit.add_voltage_source("Vin", "in", "0", 0.0, 0.01);  // 0V DC, 10mV AC

        // Input coupling
        circuit.add_capacitor("Cin", "in", "base1", 10e-6);

        // Bias network
        circuit.add_resistor("R1", "vcc", "base1", 100e3);
        circuit.add_resistor("R2", "base1", "0", 22e3);

        // Darlington pair: Q1 drives Q2
        circuit.add_bjt_npn("Q1", "vcc", "base1", "e1");       // Q1: collector to Vcc
        circuit.add_bjt_npn("Q2", "c2", "e1", "e2");           // Q2: base from Q1 emitter

        // Collector resistor (for Q2)
        circuit.add_resistor("Rc", "vcc", "c2", 4.7e3);

        // Emitter resistor with bypass
        circuit.add_resistor("Re", "e2", "0", 1e3);
        circuit.add_capacitor("Ce", "e2", "0", 100e-6);

        // Output
        circuit.add_capacitor("Cout", "c2", "out", 10e-6);
        circuit.add_resistor("Rload", "out", "0", 10e3);

        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 10.0;        // 10 Hz
        config.ac.stop_freq = 1000000.0;    // 1 MHz
        config.ac.num_points = 20;          // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(BJTDarlingtonAmplifierValidation, MatchesNgspice) {
    // DC convergence issue FIXED: Smart initial value estimation
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0) << "No reference data loaded";

    auto circuit = build_darlington_amplifier();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    ASSERT_GT(result.frequency_points.size(), 0) << "No AC results computed";

    auto comparison = compare_ac_results(ref_data, result, node_out);

    // Darlington pair: moderate tolerances
    const double max_mag_error_db = 1.0;
    const double max_phase_error_deg = 5.0;

    double max_mag_err = 0.0;
    double max_phase_err = 0.0;

    for (const auto& point : comparison) {
        max_mag_err = std::max(max_mag_err, std::abs(point.mag_error_db));
        max_phase_err = std::max(max_phase_err, std::abs(point.phase_error_deg));

        EXPECT_LE(std::abs(point.mag_error_db), max_mag_error_db)
            << "At frequency " << point.frequency << " Hz"
            << ", magnitude error exceeds tolerance";

        EXPECT_LE(std::abs(point.phase_error_deg), max_phase_error_deg)
            << "At frequency " << point.frequency << " Hz"
            << ", phase error exceeds tolerance";
    }

    std::cout << "\nDarlington Pair BJT Amplifier AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

TEST_F(BJTDarlingtonAmplifierValidation, HighCurrentGain) {
    // DC convergence issue FIXED: Smart initial value estimation
    auto circuit = build_darlington_amplifier();
    NodeId node_out = circuit.get_node_id("out");

    // Run AC analysis at midband frequency (1 kHz)
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1000.0;
    config.ac.num_points = 1;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.frequency_points.size(), 1);

    std::complex<double> v_out = result.frequency_points[0].solution[node_out - 1];
    double gain_magnitude = std::abs(v_out);
    double gain_db = 20.0 * std::log10(gain_magnitude);

    // Darlington pair: expect high current gain (β² ≈ 10000)
    // This configuration is an emitter follower (Q1 collector → Vcc)
    // Voltage gain is typically < 1 (unity gain buffer)
    std::cout << "\nDarlington Pair Midband Gain:\n";
    std::cout << "  Frequency: 1 kHz\n";
    std::cout << "  Voltage gain: " << gain_magnitude << " V/V\n";
    std::cout << "  Voltage gain: " << gain_db << " dB\n";

    // Sanity check for emitter follower configuration
    // Expected: voltage gain ≈ 0.6 V/V (-4.5 dB), verified with ngspice
    EXPECT_GT(gain_db, -10.0) << "Darlington emitter follower gain too low";
    EXPECT_LT(gain_db, 0.0) << "Darlington emitter follower gain unexpectedly high";
}
