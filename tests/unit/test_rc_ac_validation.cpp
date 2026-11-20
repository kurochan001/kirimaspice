// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_rc_ac_validation.cpp
// RC filter AC analysis validation against ngspice (Google Test version)

#include <gtest/gtest.h>
#include "../validation/ngspice_ac_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <filesystem>
#include <fstream>
#include "../validation/ngspice_dc_utils.hpp"
#include "source_path.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;
using namespace kirimaspice::validation;

namespace fs = std::filesystem;

// Test fixture for RC filter AC validation
class RCFilterACValidation : public ::testing::Test {
protected:
    void SetUp() override {
        // Check if reference data exists
        // The reference in the source tree (source_path.hpp)
        ref_data_path = kirimaspice::test::source_path("validation/rc_lowpass_ac_ngspice.csv").string();
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path
                         << "\nRun: cd validation && ngspice -b rc_lowpass_ac.cir";
        }
    }

    // Build RC low-pass filter circuit
    // V(1V AC) -> R(1k) -> C(1u) -> GND
    // Cutoff frequency: fc = 1/(2*pi*R*C) = 159.15 Hz
    CircuitGraph build_rc_lowpass() {
        CircuitGraph circuit;
        circuit.add_voltage_source("V1", "in", "0", 0.0, 1.0);  // DC=0V, AC=1V
        circuit.add_resistor("R1", "in", "out", 1000.0);        // 1kΩ
        circuit.add_capacitor("C1", "out", "0", 1e-6);          // 1µF
        return circuit;
    }

    // Run AC analysis with DEC sweep
    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 10.0;      // 10 Hz
        config.ac.stop_freq = 10000.0;    // 10 kHz
        config.ac.num_points = 20;        // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

// Test: RC low-pass filter matches ngspice within tight tolerance
TEST_F(RCFilterACValidation, LowPassMatchesNgspice) {
    // Load reference data
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0) << "No reference data loaded";

    // Build and analyze circuit
    auto circuit = build_rc_lowpass();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    ASSERT_GT(result.frequency_points.size(), 0) << "No AC results computed";

    // Compare with reference
    auto comparison = compare_ac_results(ref_data, result, node_out);

    // Check tolerances
    // Linear circuits should match very closely
    const double max_mag_error_db = 0.02;    // 0.02 dB magnitude
    const double max_phase_error_deg = 0.1;  // 0.1 degree phase

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

    // Print summary
    std::cout << "\nRC Low-Pass Filter AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

// Test: Cutoff frequency is correct
TEST_F(RCFilterACValidation, CutoffFrequencyCorrect) {
    auto circuit = build_rc_lowpass();
    NodeId node_out = circuit.get_node_id("out");

    // Calculate theoretical cutoff frequency
    const double R = 1000.0;    // 1kΩ
    const double C = 1e-6;      // 1µF
    const double fc_theory = 1.0 / (2.0 * M_PI * R * C);  // 159.15 Hz

    // Run AC analysis focused around cutoff
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = fc_theory * 0.5;   // 79.58 Hz
    config.ac.stop_freq = fc_theory * 2.0;    // 318.31 Hz
    config.ac.num_points = 100;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    // Find -3dB point (magnitude = 1/√2 = 0.7071)
    double fc_measured = 0.0;
    double min_error = 1e9;

    for (const auto& point : result.frequency_points) {
        std::complex<double> v_out = point.solution[node_out - 1];
        double mag = std::abs(v_out);
        double error = std::abs(mag - 0.7071);  // Target: -3dB = 0.7071

        if (error < min_error) {
            min_error = error;
            fc_measured = point.frequency;
        }
    }

    // Check measured cutoff is within 1% of theoretical
    double fc_error = std::abs(fc_measured - fc_theory) / fc_theory;
    EXPECT_LT(fc_error, 0.01)
        << "Cutoff frequency error: theoretical " << fc_theory << " Hz, "
        << "measured " << fc_measured << " Hz ("
        << (fc_error * 100.0) << "% error)";

    std::cout << "\nCutoff Frequency Validation:\n";
    std::cout << "  Theoretical: " << fc_theory << " Hz\n";
    std::cout << "  Measured: " << fc_measured << " Hz\n";
    std::cout << "  Error: " << (fc_error * 100.0) << "%\n";
}

// Test: High frequency attenuation is correct (-20dB/decade)
TEST_F(RCFilterACValidation, AttenuationRateCorrect) {
    auto circuit = build_rc_lowpass();
    NodeId node_out = circuit.get_node_id("out");

    // Run AC analysis from 1kHz to 10kHz (1 decade above cutoff)
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 1000.0;    // 1 kHz
    config.ac.stop_freq = 10000.0;    // 10 kHz
    config.ac.num_points = 20;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    // Calculate attenuation rate between 1kHz and 10kHz
    std::complex<double> v_1khz = result.frequency_points[0].solution[node_out - 1];
    std::complex<double> v_10khz = result.frequency_points.back().solution[node_out - 1];

    double mag_1khz_db = 20.0 * std::log10(std::abs(v_1khz));
    double mag_10khz_db = 20.0 * std::log10(std::abs(v_10khz));
    double attenuation_db_per_decade = mag_10khz_db - mag_1khz_db;

    // First-order RC filter should attenuate at -20dB/decade
    EXPECT_NEAR(attenuation_db_per_decade, -20.0, 0.5)
        << "Attenuation rate: " << attenuation_db_per_decade << " dB/decade, "
        << "expected -20 dB/decade for first-order filter";

    std::cout << "\nAttenuation Rate Validation:\n";
    std::cout << "  1 kHz magnitude: " << mag_1khz_db << " dB\n";
    std::cout << "  10 kHz magnitude: " << mag_10khz_db << " dB\n";
    std::cout << "  Attenuation rate: " << attenuation_db_per_decade << " dB/decade\n";
}

//==============================================================================
// The comparison helpers themselves
//==============================================================================
namespace {

fs::path write_reference(const std::string& name, const std::string& text) {
    const fs::path path = fs::temp_directory_path() / name;
    std::ofstream(path) << text;
    return path;
}

ACResult one_point_result(double frequency, std::complex<double> value) {
    ACFrequencyPoint point;
    point.frequency = frequency;
    point.omega = 2.0 * M_PI * frequency;
    point.solution = ComplexDenseVector(1);
    point.solution[0] = value;
    ACResult result;
    result.frequency_points.push_back(point);
    return result;
}

}  // namespace

TEST(ReferenceComparison, ZeroReferenceIsMatchedOnlyByZero) {
    NgspiceACData reference;
    reference.frequencies = {10.0, 100.0};
    reference.values = {{0.0, 0.0}, {0.0, 0.0}};

    auto nonzero = compare_ac_results(reference, one_point_result(10.0, {0.0625, 0.0129}), 1);
    ASSERT_EQ(nonzero.size(), 1u);
    EXPECT_TRUE(std::isinf(nonzero[0].mag_error_db));
    EXPECT_GT(nonzero[0].mag_error_db, 0.0);

    auto zero = compare_ac_results(reference, one_point_result(10.0, {0.0, 0.0}), 1);
    ASSERT_EQ(zero.size(), 1u);
    EXPECT_EQ(zero[0].mag_error_db, 0.0);
}

TEST(ReferenceComparison, SmallReferenceIsComparedAsARatio) {
    NgspiceACData reference;
    reference.frequencies = {10.0, 100.0};
    reference.values = {{1e-13, 0.0}, {1e-13, 0.0}};

    auto comparison = compare_ac_results(reference, one_point_result(10.0, {1e-12, 0.0}), 1);
    ASSERT_EQ(comparison.size(), 1u);
    EXPECT_NEAR(comparison[0].mag_error_db, 20.0, 1e-9);
}

TEST(ReferenceLoader, KeepsTheFirstPointWithAndWithoutAHeader) {
    const std::string rows = "1.0e+01 1.0 0.5\n1.0e+02 2.0 0.25\n";
    for (const std::string header : {"", " frequency Re(v(out)) Im(v(out))\n"}) {
        const auto path = write_reference("kirimaspice_ac_reference_test.csv", header + rows);
        const NgspiceACData data = load_ngspice_ac_data(path.string());
        fs::remove(path);
        ASSERT_EQ(data.frequencies.size(), 2u) << "header: '" << header << "'";
        EXPECT_EQ(data.frequencies.front(), 10.0);
        EXPECT_EQ(data.values.front(), std::complex<double>(1.0, 0.5));
    }
}

TEST(ReferenceLoader, KeepsTheFirstNodeWithAndWithoutAHeader) {
    const std::string rows = "n_0_0,1\nn_0_1,0.5\n";
    for (const std::string header : {"", "node,voltage\n"}) {
        const auto path = write_reference("kirimaspice_dc_reference_test.csv", header + rows);
        const NgspiceDCData data = load_ngspice_dc_data(path.string());
        fs::remove(path);
        ASSERT_EQ(data.node_voltages.size(), 2u) << "header: '" << header << "'";
        EXPECT_EQ(data.node_voltages.at("n_0_0"), 1.0);
    }
}
