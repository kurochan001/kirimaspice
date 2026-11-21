// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/fourier_analyzer.cpp
#include "kirimaspice/solvers/fourier_analyzer.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <numbers>
#include <algorithm>

namespace kirimaspice {

FourierResult FourierAnalyzer::analyze(const std::vector<Real>& time,
                                       const std::vector<Real>& signal,
                                       Real fundamental_freq,
                                       const std::string& signal_name,
                                       std::uint32_t num_harmonics) {
    FourierResult result;
    result.signal = signal_name;
    result.fundamental_freq = fundamental_freq;

    if (time.empty() || signal.empty() || time.size() != signal.size()) {
        spdlog::error("Fourier analysis: invalid input data (empty or size mismatch)");
        return result;
    }

    if (!std::isfinite(fundamental_freq) || fundamental_freq <= 0.0) {
        spdlog::error("Fourier analysis: invalid fundamental frequency: {} Hz", fundamental_freq);
        return result;
    }

    // Period of fundamental frequency
    Real period = 1.0 / fundamental_freq;
    for (size_t i = 0; i < time.size(); ++i) {
        if (!std::isfinite(time[i]) || !std::isfinite(signal[i]) ||
            (i > 0 && time[i] <= time[i - 1])) {
            spdlog::error("Fourier analysis requires finite samples and increasing time");
            return result;
        }
    }

    // Find the last complete period in the time data
    Real t_start = time.back() - period;
    if (t_start < time.front()) {
        spdlog::error("Fourier analysis: insufficient data (need at least one period)");
        spdlog::error("  Time span: {} s, Period: {} s", time.back() - time.front(), period);
        return result;
    }

    // Extract data for the last complete period
    std::vector<Real> time_period;
    std::vector<Real> signal_period;
    const auto first = std::lower_bound(time.begin(), time.end(), t_start);
    const size_t index = static_cast<size_t>(first - time.begin());
    if (index > 0 && *first > t_start) {
        const Real fraction = (t_start - time[index - 1]) / (time[index] - time[index - 1]);
        time_period.push_back(0.0);
        signal_period.push_back(std::lerp(signal[index - 1], signal[index], fraction));
    }
    for (size_t i = 0; i < time.size(); ++i) {
        if (time[i] >= t_start) {
            time_period.push_back(time[i] - t_start);  // Shift to start at 0
            signal_period.push_back(signal[i]);
        }
    }

    spdlog::debug("Fourier analysis: analyzing last period ({} points)", time_period.size());

    // Compute DC component
    result.dc_component = compute_dc_component(time_period, signal_period, period);

    // Compute harmonics
    result.harmonics.reserve(num_harmonics);
    for (std::uint32_t n = 1; n <= num_harmonics; ++n) {
        FourierHarmonic harmonic;
        harmonic.harmonic_number = n;
        harmonic.frequency = n * fundamental_freq;

        // Compute Fourier coefficients (a_n, b_n)
        Real a_n = compute_fourier_coefficient(time_period, signal_period, n, fundamental_freq, false);
        Real b_n = compute_fourier_coefficient(time_period, signal_period, n, fundamental_freq, true);

        // Magnitude: A_n = sqrt(a_n^2 + b_n^2)
        harmonic.magnitude = std::sqrt(a_n * a_n + b_n * b_n);

        // Phase from a sine, as SPICE prints it: the harmonic is
        // A_n sin(n omega t + phi_n) with t from the start of the last
        // period, so a_n = A_n sin(phi_n), b_n = A_n cos(phi_n)
        harmonic.phase_degrees = std::atan2(a_n, b_n) * 180.0 / std::numbers::pi;

        // Normalized values (relative to fundamental)
        if (n == 1) {
            harmonic.normalized_mag = 1.0;
            harmonic.normalized_phase = 0.0;
        } else {
            Real fundamental_mag = result.harmonics[0].magnitude;
            if (fundamental_mag > 0.0) {
                harmonic.normalized_mag = harmonic.magnitude / fundamental_mag;
            } else {
                harmonic.normalized_mag = 0.0;
            }
            harmonic.normalized_phase = harmonic.phase_degrees - result.harmonics[0].phase_degrees;
        }

        result.harmonics.push_back(harmonic);

        spdlog::debug("  Harmonic {}: f={} Hz, mag={}, phase={} deg",
                      n, harmonic.frequency, harmonic.magnitude, harmonic.phase_degrees);
    }

    // Compute THD (Total Harmonic Distortion)
    result.thd = compute_thd(result.harmonics);

    result.success = true;
    return result;
}

void FourierAnalyzer::print_result(const FourierResult& result) {
    if (!result.success) {
        fmt::print("\nFourier analysis FAILED\n");
        return;
    }

    fmt::print("\n");
    fmt::print("Fourier Analysis Results\n");
    fmt::print("========================\n");
    fmt::print("Signal: {}\n", result.signal);
    fmt::print("Fundamental frequency: {:.6e} Hz\n", result.fundamental_freq);
    fmt::print("DC component: {:.6e}\n", result.dc_component);
    fmt::print("\n");

    // Print header
    fmt::print("{:>8} {:>12} {:>12} {:>12} {:>12} {:>12}\n",
               "Harmonic", "Frequency", "Fourier", "Normalized", "Phase", "Norm Phase");
    fmt::print("{:>8} {:>12} {:>12} {:>12} {:>12} {:>12}\n",
               "Number", "(Hz)", "Component", "Component", "(deg)", "(deg)");
    fmt::print("{:->8} {:->12} {:->12} {:->12} {:->12} {:->12}\n",
               "", "", "", "", "", "");

    // Print harmonics
    for (const auto& h : result.harmonics) {
        fmt::print("{:8d} {:12.6e} {:12.6e} {:12.6e} {:12.6e} {:12.6e}\n",
                   h.harmonic_number, h.frequency, h.magnitude, h.normalized_mag,
                   h.phase_degrees, h.normalized_phase);
    }

    fmt::print("\n");
    fmt::print("Total Harmonic Distortion (THD): {:.4f}%\n", result.thd);
    fmt::print("\n");
}

Real FourierAnalyzer::compute_dc_component(const std::vector<Real>& time,
                                           const std::vector<Real>& signal,
                                           Real period) {
    // DC component: a_0 = (1/T) * integral(f(t) dt) over one period
    Real integral = trapezoidal_integrate(time, signal);
    return integral / period;
}

Real FourierAnalyzer::compute_fourier_coefficient(const std::vector<Real>& time,
                                                   const std::vector<Real>& signal,
                                                   std::uint32_t harmonic,
                                                   Real fundamental_freq,
                                                   bool use_sine) {
    // Angular frequency: omega = 2*pi*f
    Real omega = 2.0 * std::numbers::pi * fundamental_freq;
    Real period = 1.0 / fundamental_freq;

    // Create integrand: f(t) * cos(n*omega*t) or f(t) * sin(n*omega*t)
    std::vector<Real> integrand(signal.size());
    for (size_t i = 0; i < signal.size(); ++i) {
        Real angle = harmonic * omega * time[i];
        if (use_sine) {
            integrand[i] = signal[i] * std::sin(angle);
        } else {
            integrand[i] = signal[i] * std::cos(angle);
        }
    }

    // Fourier coefficient: (2/T) * integral(f(t)*cos(n*omega*t) dt) or (2/T) * integral(f(t)*sin(n*omega*t) dt)
    Real integral = trapezoidal_integrate(time, integrand);
    return (2.0 / period) * integral;
}

Real FourierAnalyzer::compute_thd(const std::vector<FourierHarmonic>& harmonics) {
    if (harmonics.empty() || harmonics[0].magnitude == 0.0) {
        return 0.0;
    }

    // THD = sqrt(sum(A_n^2, n=2..N)) / A_1 * 100%
    Real fundamental_mag = harmonics[0].magnitude;
    Real sum_harmonics_squared = 0.0;

    for (size_t i = 1; i < harmonics.size(); ++i) {
        Real mag = harmonics[i].magnitude;
        sum_harmonics_squared += mag * mag;
    }

    Real thd = std::sqrt(sum_harmonics_squared) / fundamental_mag * 100.0;
    return thd;
}

Real FourierAnalyzer::trapezoidal_integrate(const std::vector<Real>& x,
                                            const std::vector<Real>& y) {
    if (x.size() != y.size() || x.size() < 2) {
        return 0.0;
    }

    Real integral = 0.0;
    for (size_t i = 1; i < x.size(); ++i) {
        Real dx = x[i] - x[i-1];
        Real avg_y = 0.5 * (y[i] + y[i-1]);
        integral += dx * avg_y;
    }

    return integral;
}

} // namespace kirimaspice
