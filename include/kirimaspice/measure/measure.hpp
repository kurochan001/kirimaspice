// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/measure/measure.hpp
#pragma once

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace kirimaspice::measure {

/// Waveform data for measurement (time or frequency domain)
struct WaveformData {
    std::vector<Real> x_values;  ///< Independent variable (time [s] or frequency [Hz])
    std::vector<Real> y_values;  ///< Dependent variable (signal value)
    std::string x_label;         ///< Label for x-axis (e.g., "time", "frequency")
    std::string y_label;         ///< Label for y-axis (e.g., "v(out)", "i(V1)")

    /// Check if data is valid
    bool is_valid() const {
        return !x_values.empty() && x_values.size() == y_values.size();
    }

    /// Get data size
    std::size_t size() const { return x_values.size(); }
};

/// MeasureEngine: Evaluates .measure statements on waveform data
/// Supports all SPICE-compatible measurement types:
/// - TRIG-TARG: Delay between trigger and target crossings
/// - FIND: Value at specific time/frequency
/// - WHEN: Time/frequency when condition is met
/// - MAX, MIN, AVG, RMS, PP, INTEG: Statistical measurements
class MeasureEngine {
public:
    /// Evaluate a measurement on waveform data
    /// @param config Measurement configuration
    /// @param data Waveform data (time/frequency vs signal)
    /// @return Measurement result
    static MeasureResult evaluate(const MeasureConfig& config, const WaveformData& data);

private:
    // =========================================================================
    // Core measurement implementations
    // =========================================================================

    /// TRIG-TARG measurement: delay between trigger and target crossings
    static MeasureResult measure_trig_targ(const MeasureConfig& config,
                                           const WaveformData& trig_data,
                                           const WaveformData& targ_data);

    /// FIND measurement: value at specific time/frequency
    static MeasureResult measure_find(const MeasureConfig& config,
                                      const WaveformData& data);

    /// WHEN measurement: time/frequency when condition is met
    static MeasureResult measure_when(const MeasureConfig& config,
                                      const WaveformData& data);

    /// MAX measurement: maximum value in range
    static MeasureResult measure_max(const MeasureConfig& config,
                                     const WaveformData& data);

    /// MIN measurement: minimum value in range
    static MeasureResult measure_min(const MeasureConfig& config,
                                     const WaveformData& data);

    /// AVG measurement: mean over time (or frequency) in range
    static MeasureResult measure_avg(const MeasureConfig& config,
                                     const WaveformData& data);

    /// RMS measurement: root of the mean of y^2 over time (or frequency) in range
    static MeasureResult measure_rms(const MeasureConfig& config,
                                     const WaveformData& data);

    /// PP measurement: peak-to-peak value (MAX - MIN)
    static MeasureResult measure_pp(const MeasureConfig& config,
                                    const WaveformData& data);

    /// INTEG measurement: integral (area under curve)
    static MeasureResult measure_integ(const MeasureConfig& config,
                                       const WaveformData& data);

    /// DERIV measurement: derivative at specific point
    static MeasureResult measure_deriv(const MeasureConfig& config,
                                       const WaveformData& data);

    // =========================================================================
    // Helper functions
    // =========================================================================

    /// Find crossing point where signal crosses threshold
    /// @param x_values Independent variable (time/frequency)
    /// @param y_values Signal values
    /// @param threshold Threshold value to detect crossing
    /// @param edge Edge type (RISE, FALL, CROSS)
    /// @param occurrence Which occurrence to find (1=first, 2=second, etc.)
    /// @param start_idx Start search from this index
    /// @return Pair of (crossing_x, crossing_y), or empty if not found
    static std::optional<std::pair<Real, Real>> find_crossing(
        const std::vector<Real>& x_values,
        const std::vector<Real>& y_values,
        Real threshold,
        MeasureEdge edge,
        int occurrence,
        std::size_t start_idx = 0);

    /// Interpolate signal value at specific x using cubic spline
    /// @param x_values Independent variable
    /// @param y_values Dependent variable
    /// @param x Target x value
    /// @return Interpolated y value, or empty if x is out of range
    static std::optional<Real> interpolate(const std::vector<Real>& x_values,
                                           const std::vector<Real>& y_values,
                                           Real x);

    /// Linear interpolation between two points (fallback for edge cases)
    static Real linear_interpolate(Real x0, Real y0, Real x1, Real y1, Real x);

    /// Get range indices based on FROM and TO specifications
    /// @param x_values Independent variable
    /// @param from Start of range (optional)
    /// @param to End of range (optional)
    /// @return Pair of (start_idx, end_idx)
    static std::pair<std::size_t, std::size_t> get_range_indices(
        const std::vector<Real>& x_values,
        std::optional<Real> from,
        std::optional<Real> to);

    /// Integral of y, or of y^2, over [from, to] by the trapezoidal rule,
    /// with y interpolated linearly at from and to when they fall between
    /// samples; from and to default to the ends of the data and are kept
    /// within them. AVG, RMS and INTEG all integrate through it.
    /// @return (integral, to - from), or empty when the interval has no width
    static std::optional<std::pair<Real, Real>> integrate_range(const WaveformData& data,
                                                                std::optional<Real> from,
                                                                std::optional<Real> to,
                                                                bool squared);

    /// Check if crossing is valid for specified edge type
    static bool is_valid_edge(Real y_before, Real y_after, Real threshold, MeasureEdge edge);

    /// Determine unit based on measurement type and signal
    static std::string determine_unit(const MeasureConfig& config);
};

} // namespace kirimaspice::measure
