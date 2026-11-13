// include/kirimaspice/solvers/fourier_analyzer.hpp
#pragma once

#include "kirimaspice/core/types.hpp"

#include <vector>

namespace kirimaspice {

/// Fourier analyzer
/// Computes Fourier series coefficients from transient analysis waveform
/// Uses trapezoidal integration to compute DC, fundamental, and harmonics
class FourierAnalyzer {
public:
    /// Perform Fourier analysis on a time-domain signal
    /// @param time Time values [s]
    /// @param signal Signal values (voltage or current)
    /// @param fundamental_freq Fundamental frequency [Hz]
    /// @param signal_name Signal name (e.g., "v(out)")
    /// @param num_harmonics Number of harmonics to calculate (default: 9)
    /// @return Fourier analysis result
    static FourierResult analyze(const std::vector<Real>& time,
                                 const std::vector<Real>& signal,
                                 Real fundamental_freq,
                                 const std::string& signal_name,
                                 std::uint32_t num_harmonics = 9);

    /// Print Fourier analysis result to console
    /// @param result Fourier analysis result
    static void print_result(const FourierResult& result);

private:
    /// Compute DC component using trapezoidal integration
    /// @param time Time values [s]
    /// @param signal Signal values
    /// @param period Period [s]
    /// @return DC component
    static Real compute_dc_component(const std::vector<Real>& time,
                                     const std::vector<Real>& signal,
                                     Real period);

    /// Compute Fourier coefficient (cosine or sine) using trapezoidal integration
    /// @param time Time values [s]
    /// @param signal Signal values
    /// @param harmonic Harmonic number (1=fundamental, 2=2nd, etc.)
    /// @param fundamental_freq Fundamental frequency [Hz]
    /// @param use_sine true: compute sine coefficient, false: compute cosine coefficient
    /// @return Fourier coefficient
    static Real compute_fourier_coefficient(const std::vector<Real>& time,
                                            const std::vector<Real>& signal,
                                            std::uint32_t harmonic,
                                            Real fundamental_freq,
                                            bool use_sine);

    /// Compute Total Harmonic Distortion (THD)
    /// @param harmonics Harmonic components (1=fundamental, 2=2nd, ...)
    /// @return THD in percent (%)
    static Real compute_thd(const std::vector<FourierHarmonic>& harmonics);

    /// Trapezoidal integration
    /// @param x X values (independent variable)
    /// @param y Y values (dependent variable)
    /// @return Integral value
    static Real trapezoidal_integrate(const std::vector<Real>& x,
                                      const std::vector<Real>& y);
};

} // namespace kirimaspice
