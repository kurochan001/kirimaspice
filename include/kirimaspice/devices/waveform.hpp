// include/kirimaspice/devices/waveform.hpp
#pragma once
#include "kirimaspice/core/types.hpp"
#include <vector>
#include <memory>
#include <cmath>

namespace kirimaspice::devices {

/// Base class for time-varying waveforms
class Waveform {
public:
    virtual ~Waveform() = default;

    /// Evaluate waveform at given time
    /// @param time Simulation time (seconds)
    /// @return Voltage value at time t
    virtual Real evaluate(Real time) const = 0;

    /// Get DC value (for DC operating point analysis)
    virtual Real dc_value() const = 0;
};

/// Sinusoidal waveform: V(t) = VO + VA * sin(2*pi*FREQ*t + PHASE/180*pi)
/// SPICE syntax: SIN(VO VA FREQ TD THETA PHASE)
/// - VO: Offset voltage (V)
/// - VA: Amplitude (V)
/// - FREQ: Frequency (Hz)
/// - TD: Delay time (s) - waveform starts at t=TD
/// - THETA: Damping factor (1/s) - amplitude decays as exp(-THETA*(t-TD))
/// - PHASE: Phase angle (degrees)
class SinWaveform : public Waveform {
public:
    SinWaveform(Real offset, Real amplitude, Real frequency,
                Real delay = 0.0, Real damping = 0.0, Real phase = 0.0)
        : offset_(offset)
        , amplitude_(amplitude)
        , frequency_(frequency)
        , delay_(delay)
        , damping_(damping)
        , phase_(phase * M_PI / 180.0)  // Convert degrees to radians
    {}

    Real evaluate(Real time) const override {
        if (time < delay_) {
            return offset_;
        }

        Real t = time - delay_;
        Real omega = 2.0 * M_PI * frequency_;
        Real decay = (damping_ != 0.0) ? std::exp(-damping_ * t) : 1.0;

        return offset_ + amplitude_ * decay * std::sin(omega * t + phase_);
    }

    Real dc_value() const override {
        return offset_;
    }

private:
    Real offset_;     // VO (V)
    Real amplitude_;  // VA (V)
    Real frequency_;  // FREQ (Hz)
    Real delay_;      // TD (s)
    Real damping_;    // THETA (1/s)
    Real phase_;      // PHASE (radians)
};

/// Pulse waveform: Rectangular pulse train
/// SPICE syntax: PULSE(V1 V2 TD TR TF PW PER)
/// - V1: Initial value (V)
/// - V2: Pulsed value (V)
/// - TD: Delay time (s)
/// - TR: Rise time (s)
/// - TF: Fall time (s)
/// - PW: Pulse width (s)
/// - PER: Period (s)
class PulseWaveform : public Waveform {
public:
    PulseWaveform(Real v1, Real v2, Real delay, Real rise_time,
                  Real fall_time, Real pulse_width, Real period)
        : v1_(v1)
        , v2_(v2)
        , delay_(delay)
        , rise_time_(rise_time)
        , fall_time_(fall_time)
        , pulse_width_(pulse_width)
        , period_(period)
    {}

    Real evaluate(Real time) const override {
        if (time < delay_) {
            return v1_;
        }

        // Time within current period
        Real t = time - delay_;
        if (period_ > 0.0) {
            t = std::fmod(t, period_);
        }

        // Rising edge: [0, TR]
        if (t < rise_time_) {
            Real fraction = t / rise_time_;
            return v1_ + (v2_ - v1_) * fraction;
        }

        // High level: [TR, TR+PW]
        if (t < rise_time_ + pulse_width_) {
            return v2_;
        }

        // Falling edge: [TR+PW, TR+PW+TF]
        if (t < rise_time_ + pulse_width_ + fall_time_) {
            Real t_fall = t - rise_time_ - pulse_width_;
            Real fraction = t_fall / fall_time_;
            return v2_ - (v2_ - v1_) * fraction;
        }

        // Low level: [TR+PW+TF, PER]
        return v1_;
    }

    Real dc_value() const override {
        return v1_;
    }

private:
    Real v1_;          // Initial value (V)
    Real v2_;          // Pulsed value (V)
    Real delay_;       // TD (s)
    Real rise_time_;   // TR (s)
    Real fall_time_;   // TF (s)
    Real pulse_width_; // PW (s)
    Real period_;      // PER (s)
};

/// Piecewise Linear waveform: Interpolate between time-voltage pairs
/// SPICE syntax: PWL(T1 V1 T2 V2 T3 V3 ...)
/// Values before T1 are V1, values after last time point are held constant
class PWLWaveform : public Waveform {
public:
    /// Construct from time-voltage pairs
    /// @param points Vector of (time, voltage) pairs, must be sorted by time
    PWLWaveform(std::vector<std::pair<Real, Real>> points)
        : points_(std::move(points))
    {
        if (points_.empty()) {
            throw std::invalid_argument("PWLWaveform: points vector cannot be empty");
        }
    }

    Real evaluate(Real time) const override {
        // Before first point: return first voltage
        if (time <= points_.front().first) {
            return points_.front().second;
        }

        // After last point: return last voltage
        if (time >= points_.back().first) {
            return points_.back().second;
        }

        // Linear interpolation between points
        for (size_t i = 0; i < points_.size() - 1; ++i) {
            Real t1 = points_[i].first;
            Real v1 = points_[i].second;
            Real t2 = points_[i + 1].first;
            Real v2 = points_[i + 1].second;

            if (time >= t1 && time <= t2) {
                Real fraction = (time - t1) / (t2 - t1);
                return v1 + (v2 - v1) * fraction;
            }
        }

        // Should never reach here
        return points_.back().second;
    }

    Real dc_value() const override {
        return points_.front().second;
    }

private:
    std::vector<std::pair<Real, Real>> points_;  // (time, voltage) pairs
};

} // namespace kirimaspice::devices
