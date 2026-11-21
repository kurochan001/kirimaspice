// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/tline_lossy.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include <cmath>
#include <deque>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

/// Lossy transmission line (O-element): R, L, C and G per unit length, and the
/// delay TD of the line without loss, which fixes its length TD / sqrt(L C).
/// DC and AC take the line's exact two-port (two_port); the transient is an
/// approximation (KNOWN_ISSUES).
class TLineLossy : public Device {
public:
    TLineLossy(DeviceId id, const std::string& name,
               NodeId ap, NodeId an, NodeId bp, NodeId bn,
               Real r_per_m, Real l_per_m, Real c_per_m, Real g_per_m,
               Real td);

    ~TLineLossy() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    void stamp_dc_linear(MNASystem& mna,
                         const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                            const DenseVector& node_voltages) const override {
        (void)mna; (void)node_voltages; // linear device
    }

    void stamp_transient(MNASystem& mna,
                         const DenseVector& node_voltages,
                         Real timestep,
                         IntegrationMethod method,
                         const BDFCoefficients* bdf_coef = nullptr,
                         const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return true; }

    // Accessors
    Real r() const { return r_per_m_; }
    Real l() const { return l_per_m_; }
    Real c() const { return c_per_m_; }
    Real g() const { return g_per_m_; }
    Real td() const { return td_; }
    /// TD / sqrt(L C), in the unit of length of R, L, C and G
    Real length() const { return length_; }

    /// The line's admittance parameters at angular frequency omega:
    /// I_A = Y11 V_A + Y12 V_B, I_B = Y12 V_A + Y11 V_B. With the series
    /// impedance Z = (R + j omega L) length, the shunt admittance
    /// Y = (G + j omega C) length and x = sqrt(Z Y), Y11 = x coth(x) / Z and
    /// Y12 = -x csch(x) / Z (the telegrapher's equations). Z is 0 only at DC
    /// without R, where the line is a wire.
    struct TwoPort {
        Complex y11, y12;
    };
    TwoPort two_port(Real omega) const;

private:
    std::vector<NodeId> nodes_; // [ap, an, bp, bn]
    Real r_per_m_;
    Real l_per_m_;
    Real c_per_m_;
    Real g_per_m_;
    Real td_;
    Real length_;

    Real z0_;   // approx characteristic impedance (sqrt(L/C))
    Real g0_;   // 1/Z0
    Real alpha_; // approximate attenuation constant (Np) using low-frequency approx

    struct HistoryPoint {
        Real time;
        Real v_a;
        Real v_b;
        Real i_a;
        Real i_b;
    };

    mutable std::deque<HistoryPoint> history_;
    mutable std::deque<HistoryPoint> history_saved_;
    mutable Real current_time_ = 0.0;
    mutable Real current_time_saved_ = 0.0;
    mutable Real last_timestep_ = 0.0;
    // The last stamp was TR-BDF2's first stage, whose point t_n + gamma h is
    // not a time point of the line (accept_solution)
    mutable bool last_stamp_was_first_stage_ = false;
    /// The filtered delayed port voltages after a step of timestep ending at
    /// time: a1/b1 after the first pole, a2/b2 after the second, a/b the
    /// ones the line uses
    struct FilteredWaves {
        Real a = 0.0, b = 0.0, a1 = 0.0, b1 = 0.0, a2 = 0.0, b2 = 0.0;
    };
    FilteredWaves filtered_waves(Real time, Real timestep) const;

    mutable Real filt_a_ = 0.0;   // 1st-pole filtered delayed V_a
    mutable Real filt_b_ = 0.0;   // 1st-pole filtered delayed V_b
    mutable Real filt_a2_ = 0.0;  // 2nd-pole state (optional)
    mutable Real filt_b2_ = 0.0;  // 2nd-pole state (optional)
    mutable Real filt_a_saved_ = 0.0;
    mutable Real filt_b_saved_ = 0.0;
    mutable Real filt_a2_saved_ = 0.0;
    mutable Real filt_b2_saved_ = 0.0;

    // Characteristic RC/LR time constants for simple dispersion/attenuation filter
    Real tau_filter_ = 0.0;
    bool use_iir_filter_ = false; // Env: KIRIMASPICE_TLINE_LOSSY_IIR=1 to enable 2-pole filter

    void get_delayed_signals(Real current_time,
                             Real& v_a_delayed, Real& v_b_delayed,
                             Real& i_a_delayed, Real& i_b_delayed) const;

    void save_state() override;
    void restore_state() override;
    void reset_state() override;
    void accept_solution(const DenseVector& node_voltages) override;
};

} // namespace kirimaspice::devices
