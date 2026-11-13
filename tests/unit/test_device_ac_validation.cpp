// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_device_ac_validation.cpp
// Nonlinear device (Diode/BJT/MOSFET) AC analysis validation against ngspice

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
// Diode AC Validation
//==============================================================================
class DiodeACValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "tests/validation/diode_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path;
        }
    }

    // Build diode circuit: V(2V DC + 1V AC) -> R(1k) -> Diode -> GND
    CircuitGraph build_diode_circuit() {
        CircuitGraph circuit;
        circuit.add_voltage_source("Vdc", "in", "0", 2.0, 1.0);  // DC=2V, AC=1V
        circuit.add_resistor("R1", "in", "out", 1000.0);         // 1kΩ
        circuit.add_diode("D1", "out", "0", 1e-14, 1.0);         // IS=1e-14, N=1.0
        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 1000.0;    // 1 kHz
        config.ac.stop_freq = 10000.0;    // 10 kHz
        config.ac.num_points = 10;        // 10 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(DiodeACValidation, MatchesNgspice) {
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0);

    auto circuit = build_diode_circuit();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    auto comparison = compare_ac_results(ref_data, result, node_out);

    // Relaxed tolerances for nonlinear device (DC operating point differences)
    const double max_mag_error_db = 0.1;
    const double max_phase_error_deg = 1.0;

    double max_mag_err = 0.0;
    double max_phase_err = 0.0;

    for (const auto& point : comparison) {
        max_mag_err = std::max(max_mag_err, std::abs(point.mag_error_db));
        max_phase_err = std::max(max_phase_err, std::abs(point.phase_error_deg));

        EXPECT_LE(std::abs(point.mag_error_db), max_mag_error_db)
            << "At " << point.frequency << " Hz, magnitude error exceeds tolerance";
        EXPECT_LE(std::abs(point.phase_error_deg), max_phase_error_deg)
            << "At " << point.frequency << " Hz, phase error exceeds tolerance";
    }

    std::cout << "\nDiode AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

//==============================================================================
// BJT Amplifier AC Validation
//==============================================================================
class BJTAmplifierACValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "tests/validation/bjt_amplifier_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path;
        }
    }

    // Build common-emitter amplifier with bias network
    CircuitGraph build_bjt_amplifier() {
        CircuitGraph circuit;

        // Power supply
        circuit.add_voltage_source("Vcc", "vcc", "0", 12.0, 0.0);  // 12V DC

        // Input signal (AC only)
        circuit.add_voltage_source("Vin", "in", "0", 0.0, 1.0);  // 0V DC, 1V AC

        // Input coupling capacitor
        circuit.add_capacitor("Cin", "in", "base", 10e-6);  // 10µF

        // Bias network
        circuit.add_resistor("R1", "vcc", "base", 47000.0);  // 47kΩ
        circuit.add_resistor("R2", "base", "0", 10000.0);    // 10kΩ

        // Emitter resistor with bypass capacitor
        circuit.add_resistor("Re", "emitter", "0", 1000.0);  // 1kΩ
        circuit.add_capacitor("Ce", "emitter", "0", 100e-6); // 100µF

        // Collector resistor
        circuit.add_resistor("Rc", "vcc", "collector", 4700.0);  // 4.7kΩ

        // Output coupling capacitor
        circuit.add_capacitor("Cout", "collector", "out", 10e-6);  // 10µF

        // Load resistor
        circuit.add_resistor("Rload", "out", "0", 10000.0);  // 10kΩ

        // BJT (NPN, β=100, default parameters)
        circuit.add_bjt_npn("Q1", "collector", "base", "emitter");

        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 10.0;      // 10 Hz
        config.ac.stop_freq = 100000.0;   // 100 kHz
        config.ac.num_points = 20;        // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(BJTAmplifierACValidation, MatchesNgspice) {
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0);

    auto circuit = build_bjt_amplifier();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    auto comparison = compare_ac_results(ref_data, result, node_out);

    // BJT amplifier: moderate tolerances due to model differences
    const double max_mag_error_db = 0.5;
    const double max_phase_error_deg = 5.0;

    double max_mag_err = 0.0;
    double max_phase_err = 0.0;

    for (const auto& point : comparison) {
        max_mag_err = std::max(max_mag_err, std::abs(point.mag_error_db));
        max_phase_err = std::max(max_phase_err, std::abs(point.phase_error_deg));

        EXPECT_LE(std::abs(point.mag_error_db), max_mag_error_db)
            << "At " << point.frequency << " Hz, magnitude error exceeds tolerance";
        EXPECT_LE(std::abs(point.phase_error_deg), max_phase_error_deg)
            << "At " << point.frequency << " Hz, phase error exceeds tolerance";
    }

    std::cout << "\nBJT Amplifier AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

//==============================================================================
// MOSFET Amplifier AC Validation
//==============================================================================
class MOSFETAmplifierACValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "tests/validation/mosfet_amplifier_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path;
        }
    }

    // Build simple common-source amplifier (matches ngspice reference)
    // Circuit: VDD(5V) -> RD(10k) -> MOSFET Drain
    //          VGS(2.0V DC + 10mV AC) -> MOSFET Gate
    //          MOSFET Source -> GND
    CircuitGraph build_mosfet_amplifier() {
        CircuitGraph circuit;

        // Power supply: VDD = 5V
        circuit.add_voltage_source("VDD", "vdd", "0", 5.0, 0.0);

        // Gate voltage with AC: VGS = 2.0V DC + 10mV AC
        circuit.add_voltage_source("VGS", "vgs", "0", 2.0, 0.01);  // DC=2.0V, AC=10mV

        // Drain resistor: RD = 10kΩ
        circuit.add_resistor("RD", "vdd", "d", 10000.0);

        // MOSFET: M1 (Drain Gate Source)
        // KP=2e-5, W=10u, L=1u, VTO=0.7
        circuit.add_mosfet_n("M1", "d", "vgs", "0", 2e-5, 10e-6, 1e-6, 0.7);

        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 10.0;      // 10 Hz
        config.ac.stop_freq = 100000.0;   // 100 kHz
        config.ac.num_points = 20;        // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(MOSFETAmplifierACValidation, MatchesNgspice) {
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0);

    auto circuit = build_mosfet_amplifier();
    NodeId node_d = circuit.get_node_id("d");  // Drain node
    ACResult result = run_ac_analysis(circuit);

    auto comparison = compare_ac_results(ref_data, result, node_d);

    // MOSFET amplifier: moderate tolerances due to model differences
    const double max_mag_error_db = 0.5;
    const double max_phase_error_deg = 2.0;  // Match original test

    double max_mag_err = 0.0;
    double max_phase_err = 0.0;

    for (const auto& point : comparison) {
        max_mag_err = std::max(max_mag_err, std::abs(point.mag_error_db));
        max_phase_err = std::max(max_phase_err, std::abs(point.phase_error_deg));

        EXPECT_LE(std::abs(point.mag_error_db), max_mag_error_db)
            << "At " << point.frequency << " Hz, magnitude error exceeds tolerance";
        EXPECT_LE(std::abs(point.phase_error_deg), max_phase_error_deg)
            << "At " << point.frequency << " Hz, phase error exceeds tolerance";
    }

    std::cout << "\nMOSFET Amplifier AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}
