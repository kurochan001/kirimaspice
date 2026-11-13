// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_ac_csv_writer.cpp
// Unit tests for AC CSV Writer
#include "kirimaspice/output/ac_csv_writer.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::output;
using namespace kirimaspice::solvers;

class ACCSVWriterTest : public ::testing::Test {
protected:
    // Helper: read file contents
    std::string read_file(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Helper: count lines starting with pattern
    int count_lines_with_prefix(const std::string& content, const std::string& prefix) {
        std::istringstream stream(content);
        std::string line;
        int count = 0;
        while (std::getline(stream, line)) {
            if (line.find(prefix) == 0) {
                count++;
            }
        }
        return count;
    }

    // Helper: extract data lines (non-comment, non-header)
    std::vector<std::string> extract_data_lines(const std::string& content) {
        std::istringstream stream(content);
        std::string line;
        std::vector<std::string> data_lines;
        bool header_seen = false;

        while (std::getline(stream, line)) {
            // Skip comment lines
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Skip column header line (first non-comment line)
            if (!header_seen) {
                header_seen = true;
                continue;
            }

            data_lines.push_back(line);
        }

        return data_lines;
    }
};

// Test: Create CSV file with basic metadata
TEST_F(ACCSVWriterTest, CreateFile_WithMetadata) {
    // Create simple RC circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 1e-6);

    // Run AC analysis
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 10.0;
    config.ac.stop_freq = 100.0;
    config.ac.num_points = 10;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // Write to CSV
    std::string filepath = "/tmp/test_ac_output.csv";
    ACCSVWriter writer(filepath, circuit);
    writer.write(result, "RC Low-Pass Filter Test");

    // Read file contents
    std::string content = read_file(filepath);
    ASSERT_FALSE(content.empty());

    // Check metadata
    EXPECT_GT(count_lines_with_prefix(content, "# "), 0) << "Should have metadata comment lines";
    EXPECT_GT(content.find("RC Low-Pass Filter Test"), 0) << "Should contain title";
    EXPECT_GT(content.find("AC Analysis Result"), 0) << "Should contain analysis type";
    EXPECT_GT(content.find("kirimaspice"), 0) << "Should contain simulator name";

    std::cout << "CSV file created: " << filepath << std::endl;
    std::cout << "File size: " << content.size() << " bytes" << std::endl;
}

// Test: Column headers format
TEST_F(ACCSVWriterTest, ColumnHeaders_Format) {
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 100.0;
    config.ac.stop_freq = 200.0;
    config.ac.num_points = 5;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    std::string filepath = "/tmp/test_ac_headers.csv";
    ACCSVWriter writer(filepath, circuit);
    writer.write(result);

    // Read file
    std::ifstream file(filepath);
    std::string line;

    // Skip comment lines
    while (std::getline(file, line)) {
        if (!line.empty() && line[0] != '#') {
            break;
        }
    }

    // Check column header format
    EXPECT_TRUE(line.find("frequency") != std::string::npos) << "Should have 'frequency' column";
    EXPECT_TRUE(line.find("_real") != std::string::npos) << "Should have '_real' columns";
    EXPECT_TRUE(line.find("_imag") != std::string::npos) << "Should have '_imag' columns";
    EXPECT_TRUE(line.find("_mag_dB") != std::string::npos) << "Should have '_mag_dB' columns";
    EXPECT_TRUE(line.find("_phase_deg") != std::string::npos) << "Should have '_phase_deg' columns";

    std::cout << "Column header: " << line << std::endl;
}

// Test: Data values correctness
TEST_F(ACCSVWriterTest, DataValues_Correctness) {
    circuit::CircuitGraph circuit;
    Real R = 1000.0;
    Real C = 1e-6;

    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", R);
    circuit.add_capacitor("C1", "out", "0", C);

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 100.0;
    config.ac.stop_freq = 100.1;  // Slightly different to avoid f_start == f_stop
    config.ac.num_points = 2;      // Use 2 points, take first one

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    std::string filepath = "/tmp/test_ac_data.csv";
    ACCSVWriter writer(filepath, circuit);
    writer.write(result);

    // Read file contents
    std::string content = read_file(filepath);
    std::vector<std::string> data_lines = extract_data_lines(content);

    ASSERT_GE(data_lines.size(), 1) << "Should have at least 1 data row";

    // Parse data line (use first frequency point at 100 Hz)
    std::string data_line = data_lines[0];
    std::istringstream ss(data_line);
    std::string token;
    std::vector<double> values;

    while (std::getline(ss, token, ',')) {
        values.push_back(std::stod(token));
    }

    // Check frequency
    EXPECT_NEAR(values[0], 100.0, 0.01) << "Frequency should be 100 Hz";

    // For RC Low-Pass: H(jω) = 1 / (1 + jωRC)
    double omega = 2.0 * M_PI * 100.0;
    Complex H_theory = 1.0 / (1.0 + Complex(0.0, omega * R * C));
    double mag_theory_dB = 20.0 * std::log10(std::abs(H_theory));
    double phase_theory_deg = std::arg(H_theory) * 180.0 / M_PI;

    // v(in) columns: [1] real, [2] imag, [3] mag_dB, [4] phase_deg
    // v(out) columns: [5] real, [6] imag, [7] mag_dB, [8] phase_deg

    EXPECT_NEAR(values[7], mag_theory_dB, 0.01) << "v(out) magnitude should match theory";
    EXPECT_NEAR(values[8], phase_theory_deg, 0.1) << "v(out) phase should match theory";

    std::cout << "Data line: " << data_line << std::endl;
    std::cout << "Theoretical magnitude: " << mag_theory_dB << " dB" << std::endl;
    std::cout << "Theoretical phase: " << phase_theory_deg << " degrees" << std::endl;
}

// Test: Multiple frequency points
TEST_F(ACCSVWriterTest, MultipleFrequencyPoints) {
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 1e-6);

    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    config.ac.start_freq = 10.0;
    config.ac.stop_freq = 10000.0;
    config.ac.num_points = 10;

    ACSolver solver(circuit, config);
    ACResult result = solver.solve();

    std::string filepath = "/tmp/test_ac_multiple_points.csv";
    ACCSVWriter writer(filepath, circuit);
    writer.write(result);

    // Read file
    std::string content = read_file(filepath);
    std::vector<std::string> data_lines = extract_data_lines(content);

    EXPECT_EQ(data_lines.size(), result.frequency_points.size())
        << "Number of data rows should match frequency points";

    // Check that frequencies are monotonically increasing
    double prev_freq = 0.0;
    for (const auto& line : data_lines) {
        std::istringstream ss(line);
        std::string freq_str;
        std::getline(ss, freq_str, ',');
        double freq = std::stod(freq_str);

        EXPECT_GT(freq, prev_freq) << "Frequencies should be monotonically increasing";
        prev_freq = freq;
    }

    std::cout << "Number of frequency points: " << data_lines.size() << std::endl;
}
