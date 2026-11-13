// src/devices/tline_lossless.cpp
#include "kirimaspice/devices/tline_lossless.hpp"
#include <algorithm>
#include <cmath>

namespace kirimaspice::devices {

TLineLossless::TLineLossless(DeviceId id, const std::string& name,
                             NodeId ap, NodeId an, NodeId bp, NodeId bn,
                             Real z0, Real td)
    : Device(id, name, DeviceType::TLINE_LOSSLESS),
      nodes_({ap, an, bp, bn}),
      z0_(z0), td_(td), g0_(1.0 / z0),
      branch_id_port_a_(INVALID_BRANCH),
      branch_id_port_b_(INVALID_BRANCH) {
    // Initialize history buffer with zero values
    HistoryPoint initial;
    initial.time = 0.0;
    initial.v_a = 0.0;
    initial.v_b = 0.0;
    initial.i_a = 0.0;
    initial.i_b = 0.0;
    history_.push_back(initial);
}

void TLineLossless::stamp_dc_linear(MNASystem& mna,
                                    const DenseVector& node_voltages) const {
    // DC analysis: Transmission line acts as open circuit
    // (infinite delay → no DC coupling between ports)
    // No stamp needed
    (void)mna; (void)node_voltages;  // Suppress unused warnings
}

void TLineLossless::stamp_transient(MNASystem& mna,
                                   const DenseVector& node_voltages,
                                   Real timestep,
                                   IntegrationMethod method,
                                   const BDFCoefficients* bdf_coef,
                                   const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    (void)method; (void)bdf_coef; (void)tr_bdf2_coef;  // Not needed for T-line

    // Cache timestep for later use in accept_solution
    last_timestep_ = timestep;

    // Get delayed signals: V_A((t+dt)-TD), V_B((t+dt)-TD), I_A((t+dt)-TD), I_B((t+dt)-TD)
    // stamp_transient builds equations for the NEXT time point (t+dt)
    Real next_time = current_time_ + timestep;
    Real v_a_delayed, v_b_delayed, i_a_delayed, i_b_delayed;
    get_delayed_signals(next_time, v_a_delayed, v_b_delayed, i_a_delayed, i_b_delayed);


    // Simplified characteristic equation model:
    // I_A(t) = G0 * (V_A(t) - V_B(t-TD))
    // I_B(t) = G0 * (V_B(t) - V_A(t-TD))
    // MNA form: -I_A + G0*V_A = G0*V_B(t-TD)
    //          -I_B + G0*V_B = G0*V_A(t-TD)

    const NodeId ap = nodes_[0];
    const NodeId an = nodes_[1];
    const NodeId bp = nodes_[2];
    const NodeId bn = nodes_[3];

    // Convert node IDs to MNA indices
    auto ap_idx = (ap == NODE_GND) ? -1 : static_cast<int>(ap - 1);
    auto an_idx = (an == NODE_GND) ? -1 : static_cast<int>(an - 1);
    auto bp_idx = (bp == NODE_GND) ? -1 : static_cast<int>(bp - 1);
    auto bn_idx = (bn == NODE_GND) ? -1 : static_cast<int>(bn - 1);

    // Port A conductance: [ap-an] has conductance G0 to ground
    if (ap_idx >= 0) {
        mna.G.add_entry(ap_idx, ap_idx, g0_);
        if (an_idx >= 0) {
            mna.G.add_entry(ap_idx, an_idx, -g0_);
        }
    }
    if (an_idx >= 0) {
        mna.G.add_entry(an_idx, an_idx, g0_);
        if (ap_idx >= 0) {
            mna.G.add_entry(an_idx, ap_idx, -g0_);
        }
    }

    // Port B conductance: [bp-bn] has conductance G0 to ground
    if (bp_idx >= 0) {
        mna.G.add_entry(bp_idx, bp_idx, g0_);
        if (bn_idx >= 0) {
            mna.G.add_entry(bp_idx, bn_idx, -g0_);
        }
    }
    if (bn_idx >= 0) {
        mna.G.add_entry(bn_idx, bn_idx, g0_);
        if (bp_idx >= 0) {
            mna.G.add_entry(bn_idx, bp_idx, -g0_);
        }
    }

    // Port A current source: G0 * V_B(t-TD)
    // From characteristic equation: I_A = G0*(V_A - V_B(t-TD))
    Real i_a_hist = g0_ * v_b_delayed;
    if (ap_idx >= 0) {
        mna.b[ap_idx] += i_a_hist;
    }
    if (an_idx >= 0) {
        mna.b[an_idx] -= i_a_hist;
    }

    // Port B current source: G0 * V_A(t-TD)
    // From characteristic equation: I_B = G0*(V_B - V_A(t-TD))
    Real i_b_hist = g0_ * v_a_delayed;
    if (bp_idx >= 0) {
        mna.b[bp_idx] += i_b_hist;
    }
    if (bn_idx >= 0) {
        mna.b[bn_idx] -= i_b_hist;
    }
}

void TLineLossless::stamp_ac(ComplexMNASystem& sys,
                            Real omega,
                            const DenseVector& dc_solution) const {
    (void)dc_solution;  // Suppress unused warning

    // Lossless transmission line Y-parameters (length l = v * TD):
    //   θ = ω · TD
    //   Y11 = Y22 = -j (1/Z0) · cot(θ)
    //   Y12 = Y21 =  j (1/Z0) · csc(θ)
    // This captures the frequency-dependent phase delay e^{-jωTD}.

    const Real theta = omega * td_;
    Real sin_theta = std::sin(theta);
    const Real cos_theta = std::cos(theta);

    // Avoid singularities at sin(theta) ≈ 0 (θ ≈ nπ)
    const Real sin_eps = 1e-12;
    if (std::abs(sin_theta) < sin_eps) {
        sin_theta = (sin_theta >= 0.0 ? sin_eps : -sin_eps);
    }

    const Real cot_theta = cos_theta / sin_theta;
    const Real csc_theta = 1.0 / sin_theta;

    const Complex j(0.0, 1.0);
    const Complex Y11 = -j * (g0_ * cot_theta);
    const Complex Y12 =  j * (g0_ * csc_theta);
    const Complex Y21 = Y12;
    const Complex Y22 = Y11;

    const NodeId ap = nodes_[0];
    const NodeId an = nodes_[1];
    const NodeId bp = nodes_[2];
    const NodeId bn = nodes_[3];

    // Convert node IDs to MNA indices
    auto ap_idx = (ap == NODE_GND) ? -1 : static_cast<int>(ap - 1);
    auto an_idx = (an == NODE_GND) ? -1 : static_cast<int>(an - 1);
    auto bp_idx = (bp == NODE_GND) ? -1 : static_cast<int>(bp - 1);
    auto bn_idx = (bn == NODE_GND) ? -1 : static_cast<int>(bn - 1);

    auto add = [&](int row, int col, const Complex& val) {
        if (row >= 0 && col >= 0) {
            sys.Y.add_entry(static_cast<std::uint32_t>(row),
                           static_cast<std::uint32_t>(col), val);
        }
    };

    // Port A current: I_A = Y11*V_A + Y12*V_B (V_A = Vap-Van, V_B = Vbp-Vbn)
    add(ap_idx, ap_idx,  Y11);
    add(ap_idx, an_idx, -Y11);
    add(ap_idx, bp_idx,  Y12);
    add(ap_idx, bn_idx, -Y12);

    add(an_idx, ap_idx, -Y11);
    add(an_idx, an_idx,  Y11);
    add(an_idx, bp_idx, -Y12);
    add(an_idx, bn_idx,  Y12);

    // Port B current: I_B = Y21*V_A + Y22*V_B
    add(bp_idx, ap_idx,  Y21);
    add(bp_idx, an_idx, -Y21);
    add(bp_idx, bp_idx,  Y22);
    add(bp_idx, bn_idx, -Y22);

    add(bn_idx, ap_idx, -Y21);
    add(bn_idx, an_idx,  Y21);
    add(bn_idx, bp_idx, -Y22);
    add(bn_idx, bn_idx,  Y22);
}

void TLineLossless::save_state() {
    history_saved_ = history_;
    current_time_saved_ = current_time_;
}

void TLineLossless::restore_state() {
    history_ = history_saved_;
    current_time_ = current_time_saved_;
}

void TLineLossless::reset_state() {
    history_.clear();
    HistoryPoint initial;
    initial.time = 0.0;
    initial.v_a = 0.0;
    initial.v_b = 0.0;
    initial.i_a = 0.0;
    initial.i_b = 0.0;
    history_.push_back(initial);
    current_time_ = 0.0;
}

void TLineLossless::accept_solution(const DenseVector& node_voltages) {
    // Update current time (increment by last timestep)
    current_time_ += last_timestep_;

    const NodeId ap = nodes_[0];
    const NodeId an = nodes_[1];
    const NodeId bp = nodes_[2];
    const NodeId bn = nodes_[3];

    // Extract port voltages and currents
    Real v_ap = (ap != NODE_GND) ? node_voltages[ap] : 0.0;
    Real v_an = (an != NODE_GND) ? node_voltages[an] : 0.0;
    Real v_bp = (bp != NODE_GND) ? node_voltages[bp] : 0.0;
    Real v_bn = (bn != NODE_GND) ? node_voltages[bn] : 0.0;

    Real v_a = v_ap - v_an;
    Real v_b = v_bp - v_bn;

    // Compute port currents using simplified characteristic equations
    // I_A(t) = G0 * (V_A(t) - V_B(t-TD))
    // I_B(t) = G0 * (V_B(t) - V_A(t-TD))
    Real v_a_delayed, v_b_delayed, i_a_delayed, i_b_delayed;
    get_delayed_signals(current_time_, v_a_delayed, v_b_delayed, i_a_delayed, i_b_delayed);

    // Characteristic equation (no recursive current terms)
    Real i_a = g0_ * (v_a - v_b_delayed);
    Real i_b = g0_ * (v_b - v_a_delayed);

    // Add new history point
    HistoryPoint new_point;
    new_point.time = current_time_;
    new_point.v_a = v_a;
    new_point.v_b = v_b;
    new_point.i_a = i_a;
    new_point.i_b = i_b;
    history_.push_back(new_point);

    // Remove old history points (keep only last 2*td window)
    const Real min_time = current_time_ - 2.0 * td_;
    while (history_.size() > 2 && history_.front().time < min_time) {
        history_.pop_front();
    }
}

void TLineLossless::get_delayed_signals(Real current_time,
                                       Real& v_a_delayed, Real& v_b_delayed,
                                       Real& i_a_delayed, Real& i_b_delayed) const {
    const Real target_time = current_time - td_;

    // If history is empty, use zero
    if (history_.empty()) {
        v_a_delayed = 0.0;
        v_b_delayed = 0.0;
        i_a_delayed = 0.0;
        i_b_delayed = 0.0;
        return;
    }

    // If target time is before first point, use first point (initial condition)
    // This handles the case where t < TD (before signal has propagated)
    if (target_time <= history_.front().time) {
        v_a_delayed = history_.front().v_a;
        v_b_delayed = history_.front().v_b;
        i_a_delayed = history_.front().i_a;
        i_b_delayed = history_.front().i_b;
        return;
    }

    // If target time is after last point, use last point (extrapolation)
    if (target_time >= history_.back().time) {
        v_a_delayed = history_.back().v_a;
        v_b_delayed = history_.back().v_b;
        i_a_delayed = history_.back().i_a;
        i_b_delayed = history_.back().i_b;
        return;
    }

    // Find bracketing points for linear interpolation
    // Find first point with time >= target_time
    auto it = std::lower_bound(history_.begin(), history_.end(), target_time,
                               [](const HistoryPoint& p, Real t) { return p.time < t; });

    // it points to first point with time >= target_time
    // it-1 points to last point with time < target_time
    const HistoryPoint& p1 = *(it - 1);
    const HistoryPoint& p2 = *it;

    // Linear interpolation
    const Real dt = p2.time - p1.time;
    if (dt < 1e-20) {
        // Points are too close, use p1
        v_a_delayed = p1.v_a;
        v_b_delayed = p1.v_b;
        i_a_delayed = p1.i_a;
        i_b_delayed = p1.i_b;
        return;
    }

    const Real alpha = (target_time - p1.time) / dt;
    v_a_delayed = p1.v_a + alpha * (p2.v_a - p1.v_a);
    v_b_delayed = p1.v_b + alpha * (p2.v_b - p1.v_b);
    i_a_delayed = p1.i_a + alpha * (p2.i_a - p1.i_a);
    i_b_delayed = p1.i_b + alpha * (p2.i_b - p1.i_b);
}

} // namespace kirimaspice::devices
