// tests/unit/test_filter_ac_validation.cpp
// Additional filter AC analysis validation against ngspice

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
// RC High-Pass Filter Validation
//==============================================================================
class RCHighPassFilterValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "tests/validation/rc_highpass_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path
                         << "\nRun: cd tests/validation && ngspice -b rc_highpass_ac.cir";
        }
    }

    // Build RC high-pass filter circuit
    // V(1V AC) -> C(1u) -> R(1k) -> GND
    // Cutoff frequency: fc = 1/(2*pi*R*C) = 159.15 Hz
    CircuitGraph build_rc_highpass() {
        CircuitGraph circuit;
        circuit.add_voltage_source("V1", "in", "0", 0.0, 1.0);  // DC=0V, AC=1V
        circuit.add_capacitor("C1", "in", "out", 1e-6);         // 1µF
        circuit.add_resistor("R1", "out", "0", 1000.0);         // 1kΩ
        return circuit;
    }

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

TEST_F(RCHighPassFilterValidation, HighPassMatchesNgspice) {
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0) << "No reference data loaded";

    auto circuit = build_rc_highpass();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    ASSERT_GT(result.frequency_points.size(), 0) << "No AC results computed";

    auto comparison = compare_ac_results(ref_data, result, node_out);

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

    std::cout << "\nRC High-Pass Filter AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

TEST_F(RCHighPassFilterValidation, CutoffFrequencyCorrect) {
    auto circuit = build_rc_highpass();
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

    std::cout << "\nHigh-Pass Cutoff Frequency Validation:\n";
    std::cout << "  Theoretical: " << fc_theory << " Hz\n";
    std::cout << "  Measured: " << fc_measured << " Hz\n";
    std::cout << "  Error: " << (fc_error * 100.0) << "%\n";
}

//==============================================================================
// RLC Band-Pass Filter Validation
//==============================================================================
class RLCBandPassFilterValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "tests/validation/rlc_bandpass_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path
                         << "\nRun: cd tests/validation && ngspice -b rlc_bandpass_ac.cir";
        }
    }

    // Build RLC band-pass filter circuit (series resonance with load)
    // V(1V AC) -> R_series(100) -> L(10m) -> C(1u) -> R_load(1k) -> GND
    // Center frequency: f0 = 1/(2*pi*sqrt(LC)) = 1591.5 Hz
    CircuitGraph build_rlc_bandpass() {
        CircuitGraph circuit;
        circuit.add_voltage_source("V1", "in", "0", 0.0, 1.0);  // DC=0V, AC=1V
        circuit.add_resistor("R1", "in", "n1", 100.0);          // 100Ω series
        circuit.add_inductor("L1", "n1", "n2", 10e-3);          // 10mH
        circuit.add_capacitor("C1", "n2", "out", 1e-6);         // 1µF
        circuit.add_resistor("Rload", "out", "0", 1000.0);      // 1kΩ load
        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 100.0;      // 100 Hz
        config.ac.stop_freq = 100000.0;    // 100 kHz
        config.ac.num_points = 20;         // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(RLCBandPassFilterValidation, BandPassMatchesNgspice) {
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0) << "No reference data loaded";

    auto circuit = build_rlc_bandpass();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    ASSERT_GT(result.frequency_points.size(), 0) << "No AC results computed";

    auto comparison = compare_ac_results(ref_data, result, node_out);

    // Linear circuits should match very closely
    const double max_mag_error_db = 0.02;
    const double max_phase_error_deg = 0.2;  // Slightly relaxed for phase

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

    std::cout << "\nRLC Band-Pass Filter AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

TEST_F(RLCBandPassFilterValidation, CenterFrequencyCorrect) {
    auto circuit = build_rlc_bandpass();
    NodeId node_out = circuit.get_node_id("out");

    // Calculate theoretical center frequency
    const double L = 10e-3;     // 10mH
    const double C = 1e-6;      // 1µF
    const double f0_theory = 1.0 / (2.0 * M_PI * std::sqrt(L * C));  // 1591.5 Hz

    // Run AC analysis focused around center frequency
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = f0_theory * 0.5;   // 795.8 Hz
    config.ac.stop_freq = f0_theory * 2.0;    // 3183.0 Hz
    config.ac.num_points = 200;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    // Find peak magnitude (resonance point)
    double f0_measured = 0.0;
    double max_mag = 0.0;

    for (const auto& point : result.frequency_points) {
        std::complex<double> v_out = point.solution[node_out - 1];
        double mag = std::abs(v_out);

        if (mag > max_mag) {
            max_mag = mag;
            f0_measured = point.frequency;
        }
    }

    // Check measured center frequency is within 1% of theoretical
    double f0_error = std::abs(f0_measured - f0_theory) / f0_theory;
    EXPECT_LT(f0_error, 0.01)
        << "Center frequency error: theoretical " << f0_theory << " Hz, "
        << "measured " << f0_measured << " Hz ("
        << (f0_error * 100.0) << "% error)";

    std::cout << "\nBand-Pass Center Frequency Validation:\n";
    std::cout << "  Theoretical: " << f0_theory << " Hz\n";
    std::cout << "  Measured: " << f0_measured << " Hz\n";
    std::cout << "  Error: " << (f0_error * 100.0) << "%\n";
    std::cout << "  Peak magnitude: " << max_mag << " V\n";
}

//==============================================================================
// RLC Band-Stop Filter Validation
//==============================================================================
class RLCBandStopFilterValidation : public ::testing::Test {
protected:
    void SetUp() override {
        ref_data_path = "tests/validation/rlc_bandstop_ac_ngspice.csv";
        if (!fs::exists(ref_data_path)) {
            GTEST_SKIP() << "Reference data not found: " << ref_data_path
                         << "\nRun: cd tests/validation && ngspice -b rlc_bandstop_ac.cir";
        }
    }

    // Build RLC band-stop filter circuit (series LC trap to GND)
    // V(1V AC) -> R1(1k) -> out -> R_load(1k) -> GND
    //                        |
    //                   [series: L-C] (trap)
    //                        |
    //                       GND
    // Series LC trap creates low impedance at resonance, shorting signal to GND
    // Notch frequency: f0 = 1/(2*pi*sqrt(LC)) = 1591.5 Hz
    CircuitGraph build_rlc_bandstop() {
        CircuitGraph circuit;
        circuit.add_voltage_source("V1", "in", "0", 0.0, 1.0);  // DC=0V, AC=1V
        circuit.add_resistor("R1", "in", "out", 1000.0);        // 1kΩ series
        circuit.add_inductor("L1", "out", "trap", 10e-3);       // 10mH (trap)
        circuit.add_capacitor("C1", "trap", "0", 1e-6);         // 1µF (trap)
        circuit.add_resistor("Rload", "out", "0", 1000.0);      // 1kΩ load
        return circuit;
    }

    ACResult run_ac_analysis(CircuitGraph& circuit) {
        SimulationConfig config;
        config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
        config.ac.start_freq = 100.0;      // 100 Hz
        config.ac.stop_freq = 100000.0;    // 100 kHz
        config.ac.num_points = 20;         // 20 points/decade

        ACSolver solver(circuit, config);
        return solver.solve();
    }

    std::string ref_data_path;
};

TEST_F(RLCBandStopFilterValidation, BandStopMatchesNgspice) {
    NgspiceACData ref_data = load_ngspice_ac_data(ref_data_path);
    ASSERT_GT(ref_data.frequencies.size(), 0) << "No reference data loaded";

    auto circuit = build_rlc_bandstop();
    NodeId node_out = circuit.get_node_id("out");
    ACResult result = run_ac_analysis(circuit);

    ASSERT_GT(result.frequency_points.size(), 0) << "No AC results computed";

    auto comparison = compare_ac_results(ref_data, result, node_out);

    // Band-stop filter: tight magnitude tolerance, relaxed phase near notch
    const double max_mag_error_db = 0.02;
    const double max_phase_error_deg = 2.0;  // Phase changes rapidly near notch

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

    std::cout << "\nRLC Band-Stop Filter AC Validation Summary:\n";
    std::cout << "  Frequency points: " << comparison.size() << "\n";
    std::cout << "  Max magnitude error: " << max_mag_err << " dB (tolerance: "
              << max_mag_error_db << " dB)\n";
    std::cout << "  Max phase error: " << max_phase_err << "° (tolerance: "
              << max_phase_error_deg << "°)\n";
}

TEST_F(RLCBandStopFilterValidation, NotchFrequencyCorrect) {
    auto circuit = build_rlc_bandstop();
    NodeId node_out = circuit.get_node_id("out");

    // Calculate theoretical notch frequency
    const double L = 10e-3;     // 10mH
    const double C = 1e-6;      // 1µF
    const double f0_theory = 1.0 / (2.0 * M_PI * std::sqrt(L * C));  // 1591.5 Hz

    // Run AC analysis focused around notch frequency
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = f0_theory * 0.5;   // 795.8 Hz
    config.ac.stop_freq = f0_theory * 2.0;    // 3183.0 Hz
    config.ac.num_points = 200;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    // Find minimum magnitude (notch point)
    double f0_measured = 0.0;
    double min_mag = 1e9;

    for (const auto& point : result.frequency_points) {
        std::complex<double> v_out = point.solution[node_out - 1];
        double mag = std::abs(v_out);

        if (mag < min_mag) {
            min_mag = mag;
            f0_measured = point.frequency;
        }
    }

    // Check measured notch frequency is within 1% of theoretical
    double f0_error = std::abs(f0_measured - f0_theory) / f0_theory;
    EXPECT_LT(f0_error, 0.01)
        << "Notch frequency error: theoretical " << f0_theory << " Hz, "
        << "measured " << f0_measured << " Hz ("
        << (f0_error * 100.0) << "% error)";

    std::cout << "\nBand-Stop Notch Frequency Validation:\n";
    std::cout << "  Theoretical: " << f0_theory << " Hz\n";
    std::cout << "  Measured: " << f0_measured << " Hz\n";
    std::cout << "  Error: " << (f0_error * 100.0) << "%\n";
    std::cout << "  Minimum magnitude: " << min_mag << " V\n";
}
