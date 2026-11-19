// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/ngspice_ac_utils.hpp
// Common utilities for AC analysis validation against ngspice

#ifndef KIRIMASPICE_TESTS_VALIDATION_NGSPICE_AC_UTILS_HPP
#define KIRIMASPICE_TESTS_VALIDATION_NGSPICE_AC_UTILS_HPP

#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <complex>
#include <iostream>
#include <iomanip>
#include <limits>

namespace kirimaspice::validation {

/**
 * @brief Structure to hold ngspice AC analysis data
 */
struct NgspiceACData {
    std::vector<double> frequencies;          ///< Frequency points [Hz]
    std::vector<std::complex<double>> values; ///< Complex voltage/current values
};

/**
 * @brief Load ngspice AC analysis data from CSV file
 *
 * Supports both comma-separated and whitespace-separated formats:
 *   frequency,real,imag     (CSV format)
 *   frequency  real  imag   (whitespace format)
 *
 * @param filename Path to ngspice CSV file
 * @return NgspiceACData with frequencies and complex values
 * @throws std::runtime_error if file cannot be opened or parsed
 */
inline NgspiceACData load_ngspice_ac_data(const std::string& filename) {
    NgspiceACData data;
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open ngspice AC data file: " + filename);
    }

    // A header line does not parse as three numbers and is passed over; a
    // file without a header keeps its first point.
    std::string line;
    // Parse data lines
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        // Replace commas with spaces to handle CSV format
        for (char& c : line) {
            if (c == ',') c = ' ';
        }

        std::istringstream iss(line);
        double freq, real_part, imag_part;
        if (iss >> freq >> real_part >> imag_part) {
            data.frequencies.push_back(freq);
            data.values.push_back(std::complex<double>(real_part, imag_part));
        }
    }

    if (data.frequencies.empty()) {
        throw std::runtime_error("No valid data found in: " + filename);
    }

    return data;
}

/**
 * @brief Interpolate complex value at a specific frequency using logarithmic interpolation
 *
 * For frequency-domain data, logarithmic interpolation is more appropriate than linear.
 *
 * @param data NgspiceACData containing frequency points and values
 * @param target_freq Target frequency [Hz]
 * @return Interpolated complex value at target_freq
 * @throws std::runtime_error if target_freq is out of range
 */
inline std::complex<double> interpolate_at_frequency(
    const NgspiceACData& data,
    double target_freq
) {
    const auto& freqs = data.frequencies;
    const auto& vals = data.values;

    // Check bounds
    if (target_freq < freqs.front() || target_freq > freqs.back()) {
        throw std::runtime_error(
            "Target frequency " + std::to_string(target_freq) +
            " Hz is out of range [" + std::to_string(freqs.front()) +
            ", " + std::to_string(freqs.back()) + "]"
        );
    }

    // Find bracketing indices using logarithmic scale
    size_t lower_idx = 0;
    for (size_t i = 0; i < freqs.size() - 1; ++i) {
        if (freqs[i] <= target_freq && target_freq <= freqs[i + 1]) {
            lower_idx = i;
            break;
        }
    }

    // If exact match, return directly
    if (std::abs(freqs[lower_idx] - target_freq) < 1e-9) {
        return vals[lower_idx];
    }
    if (std::abs(freqs[lower_idx + 1] - target_freq) < 1e-9) {
        return vals[lower_idx + 1];
    }

    // Logarithmic interpolation
    const double f1 = freqs[lower_idx];
    const double f2 = freqs[lower_idx + 1];
    const auto v1 = vals[lower_idx];
    const auto v2 = vals[lower_idx + 1];

    // Interpolation parameter in log space
    const double t = (std::log10(target_freq) - std::log10(f1)) /
                     (std::log10(f2) - std::log10(f1));

    // Linear interpolation in complex space (real and imag separately)
    const std::complex<double> interpolated = v1 * (1.0 - t) + v2 * t;

    return interpolated;
}

/**
 * @brief Comparison result for a single frequency point
 */
struct ACComparisonPoint {
    double frequency;              ///< Frequency [Hz]
    std::complex<double> expected; ///< ngspice value
    std::complex<double> actual;   ///< kirimaspice value
    double mag_error_db;           ///< Magnitude error [dB]
    double phase_error_deg;        ///< Phase error [degrees]
    double relative_error;         ///< Relative magnitude error (linear)
};

/**
 * @brief Compare kirimaspice AC results with ngspice reference data
 *
 * @param ngspice_data Reference data from ngspice
 * @param kirimaspice_result AC solver result from kirimaspice
 * @param node_id Node ID to compare
 * @return Vector of comparison points for each frequency
 */
inline std::vector<ACComparisonPoint> compare_ac_results(
    const NgspiceACData& ngspice_data,
    const solvers::ACResult& kirimaspice_result,
    NodeId node_id
) {
    std::vector<ACComparisonPoint> comparison;

    for (const auto& freq_point : kirimaspice_result.frequency_points) {
        ACComparisonPoint point;
        point.frequency = freq_point.frequency;

        // Get expected value from ngspice (interpolate if needed)
        point.expected = interpolate_at_frequency(ngspice_data, freq_point.frequency);

        // Get actual value from kirimaspice
        // solution vector is indexed by (NodeId - 1) since NodeId starts from 1
        point.actual = freq_point.solution[node_id - 1];

        // Calculate magnitude error in dB
        const double mag_expected = std::abs(point.expected);
        const double mag_actual = std::abs(point.actual);

        // A reference of exactly 0 V is matched only by 0 V: any other
        // output is an infinite error, so that a reference written for a
        // circuit that gives no output cannot pass as a match.
        if (mag_expected > 0.0) {
            const double mag_ratio = mag_actual / mag_expected;
            point.mag_error_db = 20.0 * std::log10(mag_ratio);
            point.relative_error = std::abs(mag_ratio - 1.0);
        } else if (mag_actual == 0.0) {
            point.mag_error_db = 0.0;
            point.relative_error = 0.0;
        } else {
            point.mag_error_db = std::numeric_limits<double>::infinity();
            point.relative_error = std::numeric_limits<double>::infinity();
        }

        // Calculate phase error in degrees
        const double phase_expected = std::arg(point.expected) * 180.0 / M_PI;
        const double phase_actual = std::arg(point.actual) * 180.0 / M_PI;
        point.phase_error_deg = phase_actual - phase_expected;

        // Wrap phase error to [-180, 180]
        while (point.phase_error_deg > 180.0) point.phase_error_deg -= 360.0;
        while (point.phase_error_deg < -180.0) point.phase_error_deg += 360.0;

        comparison.push_back(point);
    }

    return comparison;
}

/**
 * @brief Print comparison results to stdout
 *
 * @param comparison Vector of comparison points
 * @param max_mag_error_db Maximum allowed magnitude error [dB]
 * @param max_phase_error_deg Maximum allowed phase error [degrees]
 * @return true if all points are within tolerance, false otherwise
 */
inline bool print_and_check_comparison(
    const std::vector<ACComparisonPoint>& comparison,
    double max_mag_error_db = 0.01,
    double max_phase_error_deg = 0.1
) {
    std::cout << "\nAC Analysis Comparison Results:\n";
    std::cout << std::string(100, '=') << "\n";
    std::cout << std::setw(12) << "Frequency"
              << std::setw(15) << "Expected Mag"
              << std::setw(15) << "Actual Mag"
              << std::setw(15) << "Mag Error dB"
              << std::setw(15) << "Expected Ph"
              << std::setw(15) << "Actual Ph"
              << std::setw(13) << "Ph Error deg\n";
    std::cout << std::string(100, '-') << "\n";

    bool all_passed = true;
    double max_mag_err = 0.0;
    double max_phase_err = 0.0;

    for (const auto& point : comparison) {
        const double mag_expected = std::abs(point.expected);
        const double mag_actual = std::abs(point.actual);
        const double phase_expected = std::arg(point.expected) * 180.0 / M_PI;
        const double phase_actual = std::arg(point.actual) * 180.0 / M_PI;

        std::cout << std::setw(12) << std::scientific << std::setprecision(3) << point.frequency
                  << std::setw(15) << std::fixed << std::setprecision(6) << mag_expected
                  << std::setw(15) << mag_actual
                  << std::setw(15) << std::setprecision(4) << point.mag_error_db
                  << std::setw(15) << std::setprecision(2) << phase_expected
                  << std::setw(15) << phase_actual
                  << std::setw(13) << std::setprecision(4) << point.phase_error_deg;

        // Check tolerances
        const bool mag_ok = std::abs(point.mag_error_db) <= max_mag_error_db;
        const bool phase_ok = std::abs(point.phase_error_deg) <= max_phase_error_deg;

        if (!mag_ok || !phase_ok) {
            std::cout << "  *** FAIL ***";
            all_passed = false;
        }

        std::cout << "\n";

        max_mag_err = std::max(max_mag_err, std::abs(point.mag_error_db));
        max_phase_err = std::max(max_phase_err, std::abs(point.phase_error_deg));
    }

    std::cout << std::string(100, '=') << "\n";
    std::cout << "Maximum magnitude error: " << std::fixed << std::setprecision(4)
              << max_mag_err << " dB (tolerance: " << max_mag_error_db << " dB)\n";
    std::cout << "Maximum phase error: " << max_phase_err
              << " deg (tolerance: " << max_phase_error_deg << " deg)\n";

    if (all_passed) {
        std::cout << "\n✓ All points within tolerance\n";
    } else {
        std::cout << "\n✗ Some points exceeded tolerance\n";
    }

    return all_passed;
}

} // namespace kirimaspice::validation

#endif // KIRIMASPICE_TESTS_VALIDATION_NGSPICE_AC_UTILS_HPP
