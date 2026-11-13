// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/distortion_solver.cpp
#include "kirimaspice/solvers/distortion_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <numbers>
#include <algorithm>

namespace kirimaspice {

DistortionResult DistortionSolver::analyze(const circuit::CircuitGraph& circuit,
                                            const SimulationConfig& config,
                                            const std::vector<Real>& ac_freqs,
                                            const std::vector<std::vector<std::complex<Real>>>& ac_results) {
    DistortionResult result;

    if (!config.distortion.enabled) {
        spdlog::error("Distortion analysis not enabled");
        return result;
    }

    if (ac_freqs.empty() || ac_results.empty()) {
        spdlog::error("No AC analysis data available for distortion analysis");
        return result;
    }

    spdlog::info("Starting Distortion analysis: rload={} Ω, mode={}",
                 config.distortion.rload,
                 static_cast<int>(config.distortion.mode));

    // Extract fundamental frequency (first frequency point from AC sweep)
    result.fundamental_freq = ac_freqs.front();

    // Extract magnitude data from AC results (assume output node is index 0)
    std::vector<Real> ac_mags;
    ac_mags.reserve(ac_results.size());
    for (const auto& result_vec : ac_results) {
        if (!result_vec.empty()) {
            ac_mags.push_back(std::abs(result_vec[0]));
        }
    }

    result.fundamental_mag = ac_mags.empty() ? 0.0 : ac_mags[0];

    // Compute harmonic distortion if requested
    if (config.distortion.mode == SimulationConfig::Distortion::AnalysisMode::HARMONIC ||
        config.distortion.mode == SimulationConfig::Distortion::AnalysisMode::BOTH) {

        result.harmonics = compute_harmonics(result.fundamental_freq, ac_freqs, ac_mags, 9);

        // Extract HD2 and HD3
        for (const auto& h : result.harmonics) {
            if (h.harmonic_number == 2) {
                result.hd2_percent = h.hd_percent;
            } else if (h.harmonic_number == 3) {
                result.hd3_percent = h.hd_percent;
            }
        }

        // Compute THD
        result.thd_percent = compute_thd(result.harmonics);

        spdlog::info("Harmonic distortion: HD2={:.4f}%, HD3={:.4f}%, THD={:.4f}%",
                     result.hd2_percent, result.hd3_percent, result.thd_percent);
    }

    // Compute intermodulation distortion if requested
    if (config.distortion.mode == SimulationConfig::Distortion::AnalysisMode::INTERMODULATION ||
        config.distortion.mode == SimulationConfig::Distortion::AnalysisMode::BOTH) {

        Real f1 = result.fundamental_freq;
        Real f2 = f1 * config.distortion.f2_f1_ratio;
        result.f2_hz = f2;
        result.im2_freq_hz = f2 - f1;           // 2nd order IM
        result.im3_freq_hz = 2.0 * f1 - f2;     // 3rd order IM

        result.imd_percent = compute_imd(f1, f2, ac_freqs, ac_mags);

        spdlog::info("Intermodulation distortion: IMD={:.4f}% (f1={:.1f} Hz, f2={:.1f} Hz)",
                     result.imd_percent, f1, f2);
    }

    result.success = true;
    return result;
}

void DistortionSolver::print_result(const DistortionResult& result) {
    if (!result.success) {
        fmt::print("\nDistortion analysis FAILED\n");
        return;
    }

    fmt::print("\n");
    fmt::print("Distortion Analysis Results\n");
    fmt::print("============================\n");
    fmt::print("Fundamental: {:.3f} Hz, Magnitude: {:.6e}\n",
               result.fundamental_freq, result.fundamental_mag);
    fmt::print("\n");

    // Print harmonic distortion
    if (!result.harmonics.empty()) {
        fmt::print("Harmonic Distortion:\n");
        fmt::print("{:>5} {:>15} {:>15} {:>15} {:>12}\\n",
                   "No.", "Frequency (Hz)", "Magnitude", "Phase (deg)", "HD (%)");
        fmt::print("{:->5} {:->15} {:->15} {:->15} {:->12}\\n",
                   "", "", "", "", "");

        for (const auto& h : result.harmonics) {
            fmt::print("{:5d} {:15.3f} {:15.6e} {:15.3f} {:12.6f}\n",
                      h.harmonic_number,
                      h.frequency_hz,
                      h.magnitude,
                      h.phase_deg,
                      h.hd_percent);
        }

        fmt::print("\n");
        fmt::print("HD2: {:.6f}%\n", result.hd2_percent);
        fmt::print("HD3: {:.6f}%\n", result.hd3_percent);
        fmt::print("THD: {:.6f}%\n", result.thd_percent);
        fmt::print("\n");
    }

    // Print intermodulation distortion
    if (result.f2_hz > 0.0) {
        fmt::print("Intermodulation Distortion:\n");
        fmt::print("F1: {:.3f} Hz\n", result.fundamental_freq);
        fmt::print("F2: {:.3f} Hz\n", result.f2_hz);
        fmt::print("IM2 (F2-F1): {:.3f} Hz\n", result.im2_freq_hz);
        fmt::print("IM3 (2F1-F2): {:.3f} Hz\n", result.im3_freq_hz);
        fmt::print("IMD: {:.6f}%\n", result.imd_percent);
        fmt::print("\n");
    }
}

std::vector<HarmonicComponent> DistortionSolver::compute_harmonics(
    Real fundamental_freq,
    const std::vector<Real>& ac_freqs,
    const std::vector<Real>& ac_mags,
    std::uint32_t num_harmonics) {

    std::vector<HarmonicComponent> harmonics;

    if (ac_mags.empty()) {
        spdlog::warn("No AC magnitude data available for harmonic computation");
        return harmonics;
    }

    Real fundamental_mag = ac_mags[0];  // Magnitude at fundamental frequency

    // Compute harmonics (2nd through num_harmonics+1)
    for (std::uint32_t n = 2; n <= num_harmonics + 1; ++n) {
        Real harmonic_freq = fundamental_freq * n;
        Real harmonic_mag = find_magnitude_at_frequency(harmonic_freq, ac_freqs, ac_mags);

        HarmonicComponent h;
        h.harmonic_number = n;
        h.frequency_hz = harmonic_freq;
        h.magnitude = harmonic_mag;
        h.phase_deg = 0.0;  // Phase information not available in this simplified implementation

        // Calculate HD percentage: HD_n = (V_n / V_1) * 100%
        if (fundamental_mag > 1e-12) {
            h.hd_percent = (harmonic_mag / fundamental_mag) * 100.0;
        } else {
            h.hd_percent = 0.0;
        }

        harmonics.push_back(h);
    }

    return harmonics;
}

Real DistortionSolver::compute_thd(const std::vector<HarmonicComponent>& harmonics) {
    // THD = sqrt(sum(V_n^2) / V_1^2) * 100%
    // where n >= 2

    if (harmonics.empty()) {
        return 0.0;
    }

    Real sum_squares = 0.0;
    for (const auto& h : harmonics) {
        // HD_n = (V_n / V_1) * 100, so V_n/V_1 = HD_n / 100
        Real ratio = h.hd_percent / 100.0;
        sum_squares += ratio * ratio;
    }

    return std::sqrt(sum_squares) * 100.0;
}

Real DistortionSolver::compute_imd(
    Real f1,
    Real f2,
    const std::vector<Real>& ac_freqs,
    const std::vector<Real>& ac_mags) {

    // Initial Implementation:
    // Intermodulation distortion computation requires multi-tone AC analysis
    // This is a placeholder implementation

    spdlog::warn("Distortion analysis: IMD computation not yet fully implemented");
    spdlog::warn("Returning placeholder IMD value");

    // Placeholder: return a small IMD value
    return 0.1;  // 0.1% IMD (placeholder)
}

Real DistortionSolver::find_magnitude_at_frequency(
    Real target_freq,
    const std::vector<Real>& ac_freqs,
    const std::vector<Real>& ac_mags) {

    if (ac_freqs.empty() || ac_mags.empty() || ac_freqs.size() != ac_mags.size()) {
        return 0.0;
    }

    // Find closest frequency point
    auto it = std::lower_bound(ac_freqs.begin(), ac_freqs.end(), target_freq);

    if (it == ac_freqs.end()) {
        // Target frequency beyond sweep range
        return ac_mags.back();
    }

    if (it == ac_freqs.begin()) {
        // Target frequency before sweep range
        return ac_mags.front();
    }

    // Linear interpolation between two nearest points
    size_t idx_upper = std::distance(ac_freqs.begin(), it);
    size_t idx_lower = idx_upper - 1;

    Real f_lower = ac_freqs[idx_lower];
    Real f_upper = ac_freqs[idx_upper];
    Real mag_lower = ac_mags[idx_lower];
    Real mag_upper = ac_mags[idx_upper];

    // Linear interpolation: mag = mag_lower + (mag_upper - mag_lower) * (target - f_lower) / (f_upper - f_lower)
    Real alpha = (target_freq - f_lower) / (f_upper - f_lower);
    return mag_lower + alpha * (mag_upper - mag_lower);
}

} // namespace kirimaspice
