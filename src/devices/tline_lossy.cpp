// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/tline_lossy.cpp
#include "kirimaspice/devices/tline_lossy.hpp"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace kirimaspice::devices {

TLineLossy::TLineLossy(DeviceId id, const std::string& name,
                       NodeId ap, NodeId an, NodeId bp, NodeId bn,
                       Real r_per_m, Real l_per_m, Real c_per_m, Real g_per_m,
                       Real td)
    : Device(id, name, DeviceType::TLINE_LOSSY),
      nodes_({ap, an, bp, bn}),
      r_per_m_(r_per_m), l_per_m_(l_per_m),
      c_per_m_(c_per_m), g_per_m_(g_per_m),
      td_(td) {
    // Approximate characteristic impedance and attenuation (low-frequency)
    z0_ = std::sqrt(l_per_m_ / std::max(c_per_m_, 1e-30));
    g0_ = 1.0 / std::max(z0_, 1e-30);
    alpha_ = 0.5 * (r_per_m_ / std::max(z0_, 1e-30) + g_per_m_ * z0_);
    // Simple RC/LR blend time constant for dispersion-like low-pass filtering
    Real tau_lr = (r_per_m_ > 0.0) ? (l_per_m_ / std::max(r_per_m_, 1e-12)) : 0.0;
    Real tau_gc = (g_per_m_ > 0.0) ? (std::max(c_per_m_, 1e-18) / std::max(g_per_m_, 1e-15)) : 0.0;
    if (tau_lr > 0.0 && tau_gc > 0.0) {
        tau_filter_ = 0.5 * (tau_lr + tau_gc);
    } else if (tau_lr > 0.0) {
        tau_filter_ = tau_lr;
    } else if (tau_gc > 0.0) {
        tau_filter_ = tau_gc;
    } else {
        tau_filter_ = td_;
    }

    // Optional: enable 2nd-order IIR loss filter (env flag, default OFF to keep legacy)
    const char* env_iir = std::getenv("KIRIMASPICE_TLINE_LOSSY_IIR");
    use_iir_filter_ = (env_iir && std::string(env_iir) == "1");

    // init history with zeros
    HistoryPoint init{0.0, 0.0, 0.0, 0.0, 0.0};
    history_.push_back(init);
}

void TLineLossy::stamp_ac(ComplexMNASystem& sys,
                          Real omega,
                          const DenseVector& dc_solution) const {
    (void)dc_solution; // unused

    // θ = ω * TD (use TD directly; length from TD and propagation speed is implicit)
    // γ = sqrt((R + jωL)(G + jωC))
    Complex j(0.0, 1.0);
    Complex Z_r = Complex(r_per_m_, omega * l_per_m_);
    Complex Y_g = Complex(g_per_m_, omega * c_per_m_);
    Complex gamma = std::sqrt(Z_r * Y_g);

    // Map TD to effective length in time domain; use gamma*td as phase/amplitude factor
    Complex gamma_l = gamma * td_;

    // Compute hyperbolic functions safely
    Complex sinh_gl = std::sinh(gamma_l);
    Complex cosh_gl = std::cosh(gamma_l);

    // Avoid division by zero near sinh_gl = 0
    const Real eps = 1e-12;
    if (std::abs(sinh_gl.real()) < eps && std::abs(sinh_gl.imag()) < eps) {
        // clamp to epsilon to avoid blow-up; direction preserved
        sinh_gl = Complex((sinh_gl.real() >= 0 ? eps : -eps),
                          (sinh_gl.imag() >= 0 ? eps : -eps));
    }

    Complex Zc = std::sqrt(Z_r / Y_g);

    // Y-parameters
    Complex Y11 = (1.0 / Zc) * (cosh_gl / sinh_gl);   // coth = cosh/sinh
    Complex Y12 = -(1.0 / Zc) * (1.0 / sinh_gl);      // -csch = -1/sinh
    Complex Y21 = Y12;
    Complex Y22 = Y11;

    // Node mapping
    const NodeId ap = nodes_[0];
    const NodeId an = nodes_[1];
    const NodeId bp = nodes_[2];
    const NodeId bn = nodes_[3];

    auto idx = [](NodeId n) { return (n == NODE_GND) ? -1 : static_cast<int>(n - 1); };
    int ap_idx = idx(ap), an_idx = idx(an), bp_idx = idx(bp), bn_idx = idx(bn);

    auto add = [&](int row, int col, const Complex& val) {
        if (row >= 0 && col >= 0) {
            sys.Y.add_entry(static_cast<std::uint32_t>(row),
                           static_cast<std::uint32_t>(col), val);
        }
    };

    // Port A currents: I_A = Y11*V_A + Y12*V_B (V_A = Vap-Van, V_B = Vbp-Vbn)
    add(ap_idx, ap_idx,  Y11);
    add(ap_idx, an_idx, -Y11);
    add(ap_idx, bp_idx,  Y12);
    add(ap_idx, bn_idx, -Y12);

    add(an_idx, ap_idx, -Y11);
    add(an_idx, an_idx,  Y11);
    add(an_idx, bp_idx, -Y12);
    add(an_idx, bn_idx,  Y12);

    // Port B currents: I_B = Y21*V_A + Y22*V_B
    add(bp_idx, ap_idx,  Y21);
    add(bp_idx, an_idx, -Y21);
    add(bp_idx, bp_idx,  Y22);
    add(bp_idx, bn_idx, -Y22);

    add(bn_idx, ap_idx, -Y21);
    add(bn_idx, an_idx,  Y21);
    add(bn_idx, bp_idx, -Y22);
    add(bn_idx, bn_idx,  Y22);
}

//------------------------------------------------------------------------------ 
// Transient (approximate): use lossless Branin with exponential attenuation e^{-alpha*TD}
//------------------------------------------------------------------------------

void TLineLossy::stamp_transient(MNASystem& mna,
                         const DenseVector& node_voltages,
                         Real timestep,
                         IntegrationMethod method,
                         const BDFCoefficients* bdf_coef,
                         const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    (void)method; (void)bdf_coef; (void)tr_bdf2_coef;

    last_timestep_ = timestep;

    // The delayed, attenuated and filtered port voltages at the end of this
    // step, from the state accepted at its start; the stamp does not change
    // that state (accept_solution advances it once per accepted step)
    const FilteredWaves waves = filtered_waves(current_time_ + timestep, timestep);
    const Real v_a_eff = waves.a;
    const Real v_b_eff = waves.b;

    const NodeId ap = nodes_[0];
    const NodeId an = nodes_[1];
    const NodeId bp = nodes_[2];
    const NodeId bn = nodes_[3];

    auto idx = [](NodeId n){ return (n == NODE_GND) ? -1 : static_cast<int>(n - 1); };
    int ap_idx = idx(ap), an_idx = idx(an), bp_idx = idx(bp), bn_idx = idx(bn);

    // Conductance G0 to ground on each port (same as lossless)
    if (ap_idx >= 0) {
        mna.G.add_entry(ap_idx, ap_idx, g0_);
        if (an_idx >= 0) mna.G.add_entry(ap_idx, an_idx, -g0_);
    }
    if (an_idx >= 0) {
        mna.G.add_entry(an_idx, an_idx, g0_);
        if (ap_idx >= 0) mna.G.add_entry(an_idx, ap_idx, -g0_);
    }
    if (bp_idx >= 0) {
        mna.G.add_entry(bp_idx, bp_idx, g0_);
        if (bn_idx >= 0) mna.G.add_entry(bp_idx, bn_idx, -g0_);
    }
    if (bn_idx >= 0) {
        mna.G.add_entry(bn_idx, bn_idx, g0_);
        if (bp_idx >= 0) mna.G.add_entry(bn_idx, bp_idx, -g0_);
    }

    // History current sources with attenuation
    Real i_a_hist = g0_ * v_b_eff;
    if (ap_idx >= 0) mna.b[ap_idx] += i_a_hist;
    if (an_idx >= 0) mna.b[an_idx] -= i_a_hist;

    Real i_b_hist = g0_ * v_a_eff;
    if (bp_idx >= 0) mna.b[bp_idx] += i_b_hist;
    if (bn_idx >= 0) mna.b[bn_idx] -= i_b_hist;
}

TLineLossy::FilteredWaves TLineLossy::filtered_waves(Real time, Real timestep) const {
    Real v_a_delayed, v_b_delayed, i_a_delayed, i_b_delayed;
    get_delayed_signals(time, v_a_delayed, v_b_delayed, i_a_delayed, i_b_delayed);

    // Attenuation for one-way propagation over TD
    const Real atten = std::exp(-alpha_ * td_);
    v_a_delayed *= atten;
    v_b_delayed *= atten;

    // Low-pass smoothing to emulate frequency-dependent loss: one pole, or
    // two of the same cutoff when the IIR filter is on
    FilteredWaves w;
    if (tau_filter_ > 0.0) {
        const Real alpha_f = timestep / (tau_filter_ + timestep);
        w.a1 = filt_a_ + alpha_f * (v_a_delayed - filt_a_);
        w.b1 = filt_b_ + alpha_f * (v_b_delayed - filt_b_);
        w.a2 = filt_a2_ + alpha_f * (w.a1 - filt_a2_);
        w.b2 = filt_b2_ + alpha_f * (w.b1 - filt_b2_);
        w.a = use_iir_filter_ ? w.a2 : w.a1;
        w.b = use_iir_filter_ ? w.b2 : w.b1;
    } else {
        w.a = w.a1 = w.a2 = v_a_delayed;
        w.b = w.b1 = w.b2 = v_b_delayed;
    }
    return w;
}

void TLineLossy::save_state() {
    history_saved_ = history_;
    current_time_saved_ = current_time_;
    filt_a_saved_ = filt_a_;
    filt_b_saved_ = filt_b_;
    filt_a2_saved_ = filt_a2_;
    filt_b2_saved_ = filt_b2_;
}

void TLineLossy::restore_state() {
    history_ = history_saved_;
    current_time_ = current_time_saved_;
    filt_a_ = filt_a_saved_;
    filt_b_ = filt_b_saved_;
    filt_a2_ = filt_a2_saved_;
    filt_b2_ = filt_b2_saved_;
}

void TLineLossy::reset_state() {
    history_.clear();
    HistoryPoint init{0.0, 0.0, 0.0, 0.0, 0.0};
    history_.push_back(init);
    current_time_ = 0.0;
    filt_a_ = filt_b_ = 0.0;
    filt_a2_ = filt_b2_ = 0.0;
}

void TLineLossy::accept_solution(const DenseVector& node_voltages) {
    current_time_ += last_timestep_;

    const NodeId ap = nodes_[0];
    const NodeId an = nodes_[1];
    const NodeId bp = nodes_[2];
    const NodeId bn = nodes_[3];

    // node_voltages is 0-based; NodeId starts at 1 (0 is ground)
    auto val = [&](NodeId n) { return (n == NODE_GND) ? 0.0 : node_voltages[n]; };
    Real v_a = val(ap) - val(an);
    Real v_b = val(bp) - val(bn);

    // The filter state at the end of the accepted step, as the stamp of
    // that step computed it
    const FilteredWaves waves = filtered_waves(current_time_, last_timestep_);
    filt_a_ = waves.a1;
    filt_b_ = waves.b1;
    filt_a2_ = waves.a2;
    filt_b2_ = waves.b2;

    Real i_a = g0_ * (v_a - waves.b);
    Real i_b = g0_ * (v_b - waves.a);

    HistoryPoint p{current_time_, v_a, v_b, i_a, i_b};
    history_.push_back(p);

    const Real min_time = current_time_ - 2.0 * td_;
    while (history_.size() > 2 && history_.front().time < min_time) {
        history_.pop_front();
    }
}

void TLineLossy::get_delayed_signals(Real current_time,
                                     Real& v_a_delayed, Real& v_b_delayed,
                                     Real& i_a_delayed, Real& i_b_delayed) const {
    Real target = current_time - td_;

    if (history_.empty()) {
        v_a_delayed = v_b_delayed = i_a_delayed = i_b_delayed = 0.0;
        return;
    }
    if (target <= history_.front().time) {
        v_a_delayed = history_.front().v_a;
        v_b_delayed = history_.front().v_b;
        i_a_delayed = history_.front().i_a;
        i_b_delayed = history_.front().i_b;
        return;
    }
    if (target >= history_.back().time) {
        v_a_delayed = history_.back().v_a;
        v_b_delayed = history_.back().v_b;
        i_a_delayed = history_.back().i_a;
        i_b_delayed = history_.back().i_b;
        return;
    }

    auto it = std::lower_bound(history_.begin(), history_.end(), target,
        [](const HistoryPoint& p, Real t){ return p.time < t; });
    const HistoryPoint& p1 = *(it - 1);
    const HistoryPoint& p2 = *it;
    Real dt = p2.time - p1.time;
    if (dt < 1e-20) {
        v_a_delayed = p1.v_a; v_b_delayed = p1.v_b;
        i_a_delayed = p1.i_a; i_b_delayed = p1.i_b;
        return;
    }
    Real a = (target - p1.time) / dt;
    v_a_delayed = p1.v_a + a * (p2.v_a - p1.v_a);
    v_b_delayed = p1.v_b + a * (p2.v_b - p1.v_b);
    i_a_delayed = p1.i_a + a * (p2.i_a - p1.i_a);
    i_b_delayed = p1.i_b + a * (p2.i_b - p1.i_b);
}

} // namespace kirimaspice::devices
