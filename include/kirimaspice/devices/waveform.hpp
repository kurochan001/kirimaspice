// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/waveform.hpp
#pragma once
#include "kirimaspice/core/types.hpp"
#include <vector>
#include <memory>
#include <cmath>
#include <limits>

namespace kirimaspice::devices {

/// The transient waveform of an independent source (VoltageSource,
/// CurrentSource): its value, in volts or amperes, at a time of the transient
class Waveform {
public:
    virtual ~Waveform() = default;

    /// Evaluate waveform at given time
    /// @param time Simulation time (seconds)
    /// @return The value at time t
    virtual Real evaluate(Real time) const = 0;

    /// The times in (0, t_stop) at which the waveform or its slope jumps,
    /// appended to out: the transient steps onto each of them
    /// (TransientSolver), as SPICE does at the breakpoints of a source
    virtual void breakpoints(Real t_stop, std::vector<Real>& out) const {
        (void)t_stop;
        (void)out;
    }
};

/// Sinusoidal waveform, SPICE syntax SIN(VO VA FREQ TD THETA PHASE):
///   VO + VA sin(PHASE) before TD, and from TD on
///   VO + VA exp(-THETA (t - TD)) sin(2 pi FREQ (t - TD) + PHASE)
/// with PHASE in degrees (ngspice-47 holds VO + VA sin(PHASE) before TD too)
class SinWaveform : public Waveform {
public:
    void breakpoints(Real t_stop, std::vector<Real>& out) const override {
        if (delay_ > 0.0 && delay_ < t_stop) {
            out.push_back(delay_);
        }
    }

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
            return offset_ + amplitude_ * std::sin(phase_);
        }

        Real t = time - delay_;
        Real omega = 2.0 * M_PI * frequency_;
        Real decay = (damping_ != 0.0) ? std::exp(-damping_ * t) : 1.0;

        return offset_ + amplitude_ * decay * std::sin(omega * t + phase_);
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
    /// The four corners of each period: TD + k PER + {0, TR, TR + PW, TR + PW + TF}
    void breakpoints(Real t_stop, std::vector<Real>& out) const override {
        const Real corners[] = {0.0, rise_time_, rise_time_ + pulse_width_,
                                rise_time_ + pulse_width_ + fall_time_};
        for (std::size_t k = 0;; ++k) {
            const Real start = delay_ + static_cast<Real>(k) * period_;
            if (start >= t_stop) {
                break;
            }
            for (const Real c : corners) {
                if (start + c > 0.0 && start + c < t_stop) {
                    out.push_back(start + c);
                }
            }
            if (period_ <= 0.0) {
                break;  // one pulse
            }
        }
    }

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

    /// The value at a time. On a corner the value is the one from before
    /// it (the left limit), as PWL's is on a time given twice: with TR = 0
    /// the pulse is V1 on TD + k PER and V2 after it, with TF = 0 it is V2
    /// on TD + k PER + TR + PW and V1 after it. So the transient, which
    /// steps onto each corner (breakpoints), has the old level at the point
    /// on the corner and the new one at the next point, and the operating
    /// point of a pulse with TD = TR = 0 takes V1. A time within a few
    /// roundings of a corner is on it, as the transient's time on a corner,
    /// a sum of steps, can differ from the corner by a rounding.
    Real evaluate(Real time) const override {
        const Real on_corner =
            16.0 * std::numeric_limits<Real>::epsilon() * (std::abs(time) + std::abs(delay_));
        if (time <= delay_ + on_corner) {
            return v1_;
        }

        // Time within the current period, in (0, PER]: the start of a
        // period is the end of the one before
        Real t = time - delay_;
        if (period_ > 0.0) {
            t = std::fmod(t, period_);
            if (t <= on_corner) {
                t = period_;
            }
        }

        // Rising edge: (0, TR)
        if (t < rise_time_) {
            Real fraction = t / rise_time_;
            return v1_ + (v2_ - v1_) * fraction;
        }

        // High level: [TR, TR+PW], its end included
        if (t <= rise_time_ + pulse_width_ + on_corner) {
            return v2_;
        }

        // Falling edge: (TR+PW, TR+PW+TF)
        if (t < rise_time_ + pulse_width_ + fall_time_) {
            Real t_fall = t - rise_time_ - pulse_width_;
            Real fraction = t_fall / fall_time_;
            return v2_ - (v2_ - v1_) * fraction;
        }

        // Low level: [TR+PW+TF, PER]
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
    void breakpoints(Real t_stop, std::vector<Real>& out) const override {
        for (const auto& [time, value] : points_) {
            if (time > 0.0 && time < t_stop) {
                out.push_back(time);
            }
        }
    }

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

private:
    std::vector<std::pair<Real, Real>> points_;  // (time, voltage) pairs
};

/// Exponential waveform, SPICE syntax EXP(V1 V2 TD1 TAU1 TD2 TAU2):
///   V1 before TD1,
///   V1 + (V2 - V1) (1 - exp(-(t - TD1) / TAU1)) from TD1 to TD2,
///   and from TD2 on that plus (V1 - V2) (1 - exp(-(t - TD2) / TAU2))
class ExpWaveform : public Waveform {
public:
    ExpWaveform(Real v1, Real v2, Real td1, Real tau1, Real td2, Real tau2)
        : v1_(v1), v2_(v2), td1_(td1), tau1_(tau1), td2_(td2), tau2_(tau2) {}

    void breakpoints(Real t_stop, std::vector<Real>& out) const override {
        for (const Real t : {td1_, td2_}) {
            if (t > 0.0 && t < t_stop) {
                out.push_back(t);
            }
        }
    }

    Real evaluate(Real time) const override {
        if (time < td1_) {
            return v1_;
        }
        Real value = v1_ + (v2_ - v1_) * (1.0 - std::exp(-(time - td1_) / tau1_));
        if (time >= td2_) {
            value += (v1_ - v2_) * (1.0 - std::exp(-(time - td2_) / tau2_));
        }
        return value;
    }

private:
    Real v1_, v2_, td1_, tau1_, td2_, tau2_;
};

/// Single-frequency FM waveform, SFFM(VO VA FC MDI FS TD PHASEM PHASEC), as
/// ngspice-47 gives it (src/spicelib/devices/vsrc/vsrcload.c, Modified BSD,
/// read 2026-11-21): 0 up to TD, then, with t = time - TD,
///   VO + VA sin(2 pi FC t + PHASEC + MDI sin(2 pi FS t + PHASEM))
/// with the phases in degrees. The parser gives the defaults of the
/// arguments left out and keeps MDI within 0 and FC/FS (make_waveform).
class SffmWaveform : public Waveform {
public:
    SffmWaveform(Real offset, Real amplitude, Real carrier, Real modulation_index,
                 Real signal, Real delay = 0.0, Real signal_phase_deg = 0.0,
                 Real carrier_phase_deg = 0.0)
        : offset_(offset), amplitude_(amplitude), carrier_(carrier),
          modulation_index_(modulation_index), signal_(signal), delay_(delay),
          signal_phase_(signal_phase_deg * M_PI / 180.0),
          carrier_phase_(carrier_phase_deg * M_PI / 180.0) {}

    Real evaluate(Real time) const override {
        const Real t = time - delay_;
        if (t <= 0.0) {
            return 0.0;
        }
        return offset_ + amplitude_ * std::sin(2.0 * M_PI * carrier_ * t + carrier_phase_ +
                                               modulation_index_ *
                                                   std::sin(2.0 * M_PI * signal_ * t + signal_phase_));
    }

private:
    Real offset_, amplitude_, carrier_, modulation_index_, signal_, delay_, signal_phase_,
        carrier_phase_;
};

} // namespace kirimaspice::devices
