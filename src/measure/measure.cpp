// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/measure/measure.cpp
#include "kirimaspice/measure/measure.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace kirimaspice::measure {

// =============================================================================
// Main entry point
// =============================================================================

MeasureResult MeasureEngine::evaluate(const MeasureConfig& config, const WaveformData& data) {
    // Validate input data
    if (!data.is_valid()) {
        return MeasureResult::failure_result(config.name,
            "Invalid waveform data: empty or mismatched x/y sizes");
    }

    // Dispatch to appropriate measurement function based on type
    try {
        switch (config.type) {
            case MeasureType::MAX:
                return measure_max(config, data);
            case MeasureType::MIN:
                return measure_min(config, data);
            case MeasureType::AVG:
                return measure_avg(config, data);
            case MeasureType::RMS:
                return measure_rms(config, data);
            case MeasureType::PP:
                return measure_pp(config, data);
            case MeasureType::INTEG:
                return measure_integ(config, data);
            case MeasureType::FIND:
                return measure_find(config, data);
            case MeasureType::WHEN:
                return measure_when(config, data);
            case MeasureType::DERIV:
                return measure_deriv(config, data);
            case MeasureType::TRIG_TARG:
                // TRIG_TARG requires two waveforms, handle separately
                return MeasureResult::failure_result(config.name,
                    "TRIG_TARG measurement requires separate trigger and target data");
            default:
                return MeasureResult::failure_result(config.name,
                    "Unknown measurement type");
        }
    } catch (const std::exception& e) {
        return MeasureResult::failure_result(config.name,
            std::string("Measurement error: ") + e.what());
    }
}

// =============================================================================
// Statistical measurements (simple)
// =============================================================================

MeasureResult MeasureEngine::measure_max(const MeasureConfig& config,
                                         const WaveformData& data) {
    auto [start_idx, end_idx] = get_range_indices(data.x_values, config.from, config.to);

    if (start_idx >= end_idx || end_idx > data.y_values.size()) {
        return MeasureResult::failure_result(config.name, "Invalid measurement range");
    }

    // Find maximum value in range
    auto max_it = std::max_element(data.y_values.begin() + start_idx,
                                    data.y_values.begin() + end_idx);

    Real max_value = *max_it;
    std::size_t max_idx = std::distance(data.y_values.begin(), max_it);

    auto result = MeasureResult::success_result(config.name, max_value,
                                                 determine_unit(config));

    // Record time/frequency where maximum occurred
    if (config.analysis_type == SimMode::TRANSIENT) {
        result.at_time = data.x_values[max_idx];
    } else if (config.analysis_type == SimMode::AC) {
        result.at_frequency = data.x_values[max_idx];
    }

    return result;
}

MeasureResult MeasureEngine::measure_min(const MeasureConfig& config,
                                         const WaveformData& data) {
    auto [start_idx, end_idx] = get_range_indices(data.x_values, config.from, config.to);

    if (start_idx >= end_idx || end_idx > data.y_values.size()) {
        return MeasureResult::failure_result(config.name, "Invalid measurement range");
    }

    // Find minimum value in range
    auto min_it = std::min_element(data.y_values.begin() + start_idx,
                                    data.y_values.begin() + end_idx);

    Real min_value = *min_it;
    std::size_t min_idx = std::distance(data.y_values.begin(), min_it);

    auto result = MeasureResult::success_result(config.name, min_value,
                                                 determine_unit(config));

    // Record time/frequency where minimum occurred
    if (config.analysis_type == SimMode::TRANSIENT) {
        result.at_time = data.x_values[min_idx];
    } else if (config.analysis_type == SimMode::AC) {
        result.at_frequency = data.x_values[min_idx];
    }

    return result;
}

MeasureResult MeasureEngine::measure_avg(const MeasureConfig& config,
                                         const WaveformData& data) {
    // The mean over time (or frequency): the integral over the interval
    // divided by its width, as ngspice-47 measures AVG
    const auto range = integrate_range(data, config.from, config.to, false);
    if (!range) {
        return MeasureResult::failure_result(config.name, "Invalid measurement range");
    }
    const auto [integral, width] = *range;

    return MeasureResult::success_result(config.name, integral / width,
                                         determine_unit(config));
}

MeasureResult MeasureEngine::measure_rms(const MeasureConfig& config,
                                         const WaveformData& data) {
    // sqrt of the mean of y^2 over time (or frequency), as ngspice-47
    // measures RMS
    const auto range = integrate_range(data, config.from, config.to, true);
    if (!range) {
        return MeasureResult::failure_result(config.name, "Invalid measurement range");
    }
    const auto [integral, width] = *range;

    return MeasureResult::success_result(config.name, std::sqrt(integral / width),
                                         determine_unit(config));
}

MeasureResult MeasureEngine::measure_pp(const MeasureConfig& config,
                                        const WaveformData& data) {
    auto [start_idx, end_idx] = get_range_indices(data.x_values, config.from, config.to);

    if (start_idx >= end_idx || end_idx > data.y_values.size()) {
        return MeasureResult::failure_result(config.name, "Invalid measurement range");
    }

    // Find max and min
    auto [min_it, max_it] = std::minmax_element(data.y_values.begin() + start_idx,
                                                  data.y_values.begin() + end_idx);

    Real pp_value = *max_it - *min_it;

    return MeasureResult::success_result(config.name, pp_value,
                                         determine_unit(config));
}

MeasureResult MeasureEngine::measure_integ(const MeasureConfig& config,
                                           const WaveformData& data) {
    const auto range = integrate_range(data, config.from, config.to, false);
    if (!range) {
        return MeasureResult::failure_result(config.name, "Invalid measurement range");
    }
    const Real integral = range->first;

    // Unit is signal_unit * x_unit (e.g., V*s, A*s)
    std::string unit = determine_unit(config);
    if (config.analysis_type == SimMode::TRANSIENT) {
        unit += "*s";  // Volt-seconds or Ampere-seconds
    } else if (config.analysis_type == SimMode::AC) {
        unit += "*Hz";
    }

    return MeasureResult::success_result(config.name, integral, unit);
}

MeasureResult MeasureEngine::measure_deriv(const MeasureConfig& config,
                                           const WaveformData& data) {
    if (!config.at.has_value()) {
        return MeasureResult::failure_result(config.name,
            "DERIV measurement requires AT specification");
    }

    Real at_x = config.at.value();

    // Find indices around the target x value
    auto it = std::lower_bound(data.x_values.begin(), data.x_values.end(), at_x);
    if (it == data.x_values.begin() || it == data.x_values.end()) {
        return MeasureResult::failure_result(config.name,
            "AT value is outside data range");
    }

    std::size_t idx = std::distance(data.x_values.begin(), it);

    // Calculate derivative using central difference
    Real dx = data.x_values[idx] - data.x_values[idx - 1];
    Real dy = data.y_values[idx] - data.y_values[idx - 1];
    Real derivative = dy / dx;

    // Unit is signal_unit / x_unit (e.g., V/s, A/s)
    std::string unit = determine_unit(config);
    if (config.analysis_type == SimMode::TRANSIENT) {
        unit += "/s";  // V/s or A/s
    } else if (config.analysis_type == SimMode::AC) {
        unit += "/Hz";
    }

    return MeasureResult::success_result(config.name, derivative, unit);
}

// =============================================================================
// Complex measurements (FIND, WHEN, TRIG_TARG)
// =============================================================================

MeasureResult MeasureEngine::measure_find(const MeasureConfig& config,
                                          const WaveformData& data) {
    if (!config.at.has_value()) {
        return MeasureResult::failure_result(config.name,
            "FIND measurement requires AT specification");
    }

    Real at_x = config.at.value();

    // Interpolate signal value at specified x
    auto y_opt = interpolate(data.x_values, data.y_values, at_x);

    if (!y_opt.has_value()) {
        if (config.default_val.has_value()) {
            // Use default value if provided
            return MeasureResult::success_result(config.name, config.default_val.value(),
                                                 determine_unit(config));
        } else {
            return MeasureResult::failure_result(config.name,
                "AT value is outside data range and no default value specified");
        }
    }

    auto result = MeasureResult::success_result(config.name, y_opt.value(),
                                                 determine_unit(config));

    // Record where the measurement was taken
    if (config.analysis_type == SimMode::TRANSIENT) {
        result.at_time = at_x;
    } else if (config.analysis_type == SimMode::AC) {
        result.at_frequency = at_x;
    }

    return result;
}

MeasureResult MeasureEngine::measure_when(const MeasureConfig& config,
                                          const WaveformData& data) {
    // Find crossing where signal meets threshold
    auto crossing_opt = find_crossing(data.x_values, data.y_values,
                                      config.when_value, config.when_edge,
                                      config.when_occurrence);

    if (!crossing_opt.has_value()) {
        if (config.default_val.has_value()) {
            return MeasureResult::success_result(config.name, config.default_val.value(),
                                                 determine_unit(config));
        } else {
            return MeasureResult::failure_result(config.name,
                "No crossing found matching criteria");
        }
    }

    auto [crossing_x, crossing_y] = crossing_opt.value();

    auto result = MeasureResult::success_result(config.name, crossing_x,
                                                 config.analysis_type == SimMode::TRANSIENT ? "s" : "Hz");

    // Record where the crossing occurred
    if (config.analysis_type == SimMode::TRANSIENT) {
        result.at_time = crossing_x;
    } else if (config.analysis_type == SimMode::AC) {
        result.at_frequency = crossing_x;
    }

    return result;
}

MeasureResult MeasureEngine::measure_trig_targ(const MeasureConfig& config,
                                               const WaveformData& trig_data,
                                               const WaveformData& targ_data) {
    // Find trigger crossing
    auto trig_opt = find_crossing(trig_data.x_values, trig_data.y_values,
                                   config.trig_value, config.trig_edge,
                                   config.trig_occurrence);

    if (!trig_opt.has_value()) {
        return MeasureResult::failure_result(config.name, "Trigger crossing not found");
    }

    auto [trig_x, trig_y] = trig_opt.value();

    // Find target crossing (starting search after trigger)
    auto targ_opt = find_crossing(targ_data.x_values, targ_data.y_values,
                                   config.targ_value, config.targ_edge,
                                   config.targ_occurrence);

    if (!targ_opt.has_value()) {
        return MeasureResult::failure_result(config.name, "Target crossing not found");
    }

    auto [targ_x, targ_y] = targ_opt.value();

    // Ensure target occurs after trigger
    if (targ_x < trig_x) {
        return MeasureResult::failure_result(config.name,
            "Target crossing occurs before trigger crossing");
    }

    // Calculate delay (difference in x values)
    Real delay = targ_x - trig_x;

    auto result = MeasureResult::success_result(config.name, delay,
                                                 config.analysis_type == SimMode::TRANSIENT ? "s" : "Hz");

    // Record trigger and target times
    result.trig_time = trig_x;
    result.targ_time = targ_x;

    if (config.analysis_type == SimMode::TRANSIENT) {
        result.at_time = delay;
    } else if (config.analysis_type == SimMode::AC) {
        result.at_frequency = delay;
    }

    return result;
}

// =============================================================================
// Helper functions
// =============================================================================

std::pair<std::size_t, std::size_t> MeasureEngine::get_range_indices(
    const std::vector<Real>& x_values,
    std::optional<Real> from,
    std::optional<Real> to) {

    std::size_t start_idx = 0;
    std::size_t end_idx = x_values.size();

    if (from.has_value()) {
        auto it = std::lower_bound(x_values.begin(), x_values.end(), from.value());
        start_idx = std::distance(x_values.begin(), it);
    }

    if (to.has_value()) {
        auto it = std::upper_bound(x_values.begin(), x_values.end(), to.value());
        end_idx = std::distance(x_values.begin(), it);
    }

    return {start_idx, end_idx};
}

std::optional<std::pair<Real, Real>> MeasureEngine::integrate_range(
    const WaveformData& data,
    std::optional<Real> from,
    std::optional<Real> to,
    bool squared) {

    // The interval is [FROM, TO] within the data, the whole data by default
    const auto& x = data.x_values;
    const auto& y = data.y_values;
    const Real a = std::max(from.value_or(x.front()), x.front());
    const Real b = std::min(to.value_or(x.back()), x.back());
    if (!(b > a)) {
        return std::nullopt;
    }

    // The trapezoidal rule on the samples inside the interval, with y
    // interpolated linearly at its ends when they fall between samples (at
    // a time held twice, a step, the interval takes the side inside it)
    const auto f = [squared](Real v) { return squared ? v * v : v; };
    const auto inside = std::upper_bound(x.begin(), x.end(), a);
    Real x_prev = a;
    Real f_prev = (inside != x.begin() && *(inside - 1) == a)
                      ? f(y[static_cast<std::size_t>(std::distance(x.begin(), inside)) - 1])
                      : f(*interpolate(x, y, a));
    Real integral = 0.0;
    for (auto it = inside; it != x.end() && *it < b; ++it) {
        const auto i = static_cast<std::size_t>(std::distance(x.begin(), it));
        const Real f_i = f(y[i]);
        integral += (f_prev + f_i) / 2.0 * (x[i] - x_prev);
        x_prev = x[i];
        f_prev = f_i;
    }
    integral += (f_prev + f(*interpolate(x, y, b))) / 2.0 * (b - x_prev);

    return std::pair<Real, Real>{integral, b - a};
}

std::string MeasureEngine::determine_unit(const MeasureConfig& config) {
    // Extract unit from signal name (e.g., "v(out)" -> "V", "i(V1)" -> "A")
    if (config.signal.empty()) {
        return "";
    }

    char first_char = std::tolower(config.signal[0]);
    if (first_char == 'v') {
        if (config.is_db) {
            return "dB";
        } else if (config.is_phase) {
            return "deg";
        } else {
            return "V";
        }
    } else if (first_char == 'i') {
        if (config.is_db) {
            return "dB";
        } else {
            return "A";
        }
    }

    return "";
}

std::optional<std::pair<Real, Real>> MeasureEngine::find_crossing(
    const std::vector<Real>& x_values,
    const std::vector<Real>& y_values,
    Real threshold,
    MeasureEdge edge,
    int occurrence,
    std::size_t start_idx) {

    if (x_values.size() < 2 || y_values.size() < 2) {
        return std::nullopt;
    }

    int found_count = 0;

    // Scan through data points looking for crossings
    for (std::size_t i = start_idx; i < y_values.size() - 1; ++i) {
        Real y_before = y_values[i];
        Real y_after = y_values[i + 1];

        // Check if this is a valid crossing for the specified edge type
        if (is_valid_edge(y_before, y_after, threshold, edge)) {
            found_count++;

            // Check if this is the occurrence we're looking for
            if (found_count == occurrence) {
                // Perform linear interpolation to find exact crossing point
                Real x0 = x_values[i];
                Real x1 = x_values[i + 1];
                Real y0 = y_values[i];
                Real y1 = y_values[i + 1];

                // Solve for x where y = threshold
                // y = y0 + (y1 - y0) * (x - x0) / (x1 - x0) = threshold
                // (x - x0) / (x1 - x0) = (threshold - y0) / (y1 - y0)
                // x = x0 + (x1 - x0) * (threshold - y0) / (y1 - y0)

                Real x_crossing = x0 + (x1 - x0) * (threshold - y0) / (y1 - y0);
                Real y_crossing = threshold;

                return std::make_pair(x_crossing, y_crossing);
            }
        }
    }

    // Crossing not found
    return std::nullopt;
}

std::optional<Real> MeasureEngine::interpolate(const std::vector<Real>& x_values,
                                               const std::vector<Real>& y_values,
                                               Real x) {
    if (x_values.empty() || y_values.empty() || x_values.size() != y_values.size()) {
        return std::nullopt;
    }

    // Check if x is within range
    if (x < x_values.front() || x > x_values.back()) {
        return std::nullopt;
    }

    // Find the interval containing x
    auto it = std::lower_bound(x_values.begin(), x_values.end(), x);

    if (it == x_values.end()) {
        // x is exactly at the last point
        return y_values.back();
    }

    if (it == x_values.begin()) {
        // x is exactly at the first point
        return y_values.front();
    }

    // Get indices
    std::size_t i1 = std::distance(x_values.begin(), it);
    std::size_t i0 = i1 - 1;

    // Perform linear interpolation
    Real x0 = x_values[i0];
    Real x1 = x_values[i1];
    Real y0 = y_values[i0];
    Real y1 = y_values[i1];

    Real y_interp = linear_interpolate(x0, y0, x1, y1, x);

    return y_interp;
}

Real MeasureEngine::linear_interpolate(Real x0, Real y0, Real x1, Real y1, Real x) {
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

bool MeasureEngine::is_valid_edge(Real y_before, Real y_after, Real threshold, MeasureEdge edge) {
    bool crosses = (y_before < threshold && y_after >= threshold) ||
                   (y_before > threshold && y_after <= threshold);

    if (!crosses) return false;

    switch (edge) {
        case MeasureEdge::CROSS:
            return true;
        case MeasureEdge::RISE:
            return y_before < threshold && y_after >= threshold;
        case MeasureEdge::FALL:
            return y_before > threshold && y_after <= threshold;
    }

    return false;
}

} // namespace kirimaspice::measure
