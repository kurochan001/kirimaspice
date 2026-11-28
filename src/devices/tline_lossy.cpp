// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/tline_lossy.cpp
#include "kirimaspice/devices/tline_lossy.hpp"
#include "kirimaspice/devices/tline_lossless.hpp"
#include <fmt/format.h>
#include <stdexcept>
#include <cmath>
#include <algorithm>

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
    if (!(l_per_m_ > 0.0) || !(c_per_m_ > 0.0) || !(td_ > 0.0)) {
        throw std::invalid_argument(fmt::format(
            "O line {}: L, C and TD must be above 0; the length is TD / sqrt(L C)", name));
    }
    length_ = td_ / std::sqrt(l_per_m_ * c_per_m_);

    // The constant-parameter sections (see the class comment): as many as
    // keep each section's loss at most kLossPerSection
    z0_ = std::sqrt(l_per_m_ / c_per_m_);
    g0_ = 1.0 / z0_;
    const Real loss = std::max(r_per_m_ * length_ / z0_, g_per_m_ * length_ * z0_);
    sections_ = static_cast<int>(std::clamp(std::ceil(loss / kLossPerSection), 1.0,
                                            static_cast<Real>(kMaxSections)));
    const Real m = static_cast<Real>(sections_);
    tau_ = td_ / m;
    rs_ = 0.5 * r_per_m_ * length_ / m;
    gsh_ = 0.5 * g_per_m_ * length_ / m;
    g_port_ = 1.0 / (z0_ + rs_);
    k_hist_ = z0_ / (z0_ + rs_);

    reset_state();
}

TLineLossy::TwoPort TLineLossy::two_port(Real omega) const {
    if (omega == 0.0 && !(r_per_m_ > 0.0)) {
        // Without R the DC line is a wire, V_A = V_B, with its shunt
        // conductance G length: Z is 0, and x coth(x) / Z and -x csch(x) / Z
        // are 1/Z + Y/3 and -1/Z + Y/6 there. 1/Z is the wire, as the
        // lossless line's coupling (TLineLossless::kDcCoupling); the rest is
        // Y in all, here half of G length on each port
        const Real k = TLineLossless::kDcCoupling;
        const Real half_g = 0.5 * g_per_m_ * length_;
        return {Complex(k + half_g, 0.0), Complex(-k, 0.0)};
    }
    const Complex z = Complex(r_per_m_, omega * l_per_m_) * length_;
    const Complex y = Complex(g_per_m_, omega * c_per_m_) * length_;
    const Complex x = std::sqrt(z * y);
    // x coth(x) and x csch(x), even in x: their series near 0, and their
    // limits x and 2 x exp(-x) where cosh and sinh would overflow
    Complex f, g;
    if (std::abs(x) < 1e-4) {
        const Complex x2 = x * x;
        f = 1.0 + x2 / 3.0;
        g = 1.0 - x2 / 6.0;
    } else if (x.real() > 30.0) {
        f = x;
        g = 2.0 * x * std::exp(-x);
    } else {
        f = x * std::cosh(x) / std::sinh(x);
        g = x / std::sinh(x);
    }
    return {f / z, -g / z};
}

void TLineLossy::stamp_dc_linear(MNASystem& mna, const DenseVector& node_voltages) const {
    (void)node_voltages;
    last_stamp_was_dc_ = true;
    last_stamp_was_first_stage_ = false;
    const int idx[4] = {
        (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0] - 1),
        (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1] - 1),
        (nodes_[2] == NODE_GND) ? -1 : static_cast<int>(nodes_[2] - 1),
        (nodes_[3] == NODE_GND) ? -1 : static_cast<int>(nodes_[3] - 1),
    };
    // The port currents in terms of u = (V_A, V_B), V_A = V(ap) - V(an) and
    // V_B = V(bp) - V(bn), each port's current entering its + node
    auto stamp = [&](Real y11, Real y12) {
        const Real y[2][2] = {{y11, y12}, {y12, y11}};
        const int plus[2] = {idx[0], idx[2]};
        const int minus[2] = {idx[1], idx[3]};
        for (int p = 0; p < 2; ++p) {
            for (int q = 0; q < 2; ++q) {
                if (plus[p] >= 0 && plus[q] >= 0) mna.G.add_entry(plus[p], plus[q], y[p][q]);
                if (plus[p] >= 0 && minus[q] >= 0) mna.G.add_entry(plus[p], minus[q], -y[p][q]);
                if (minus[p] >= 0 && plus[q] >= 0) mna.G.add_entry(minus[p], plus[q], -y[p][q]);
                if (minus[p] >= 0 && minus[q] >= 0) mna.G.add_entry(minus[p], minus[q], y[p][q]);
            }
        }
    };
    // The two-port at omega = 0, which AC stamps there too (stamp_ac)
    const TwoPort tp = two_port(0.0);
    stamp(tp.y11.real(), tp.y12.real());
}

void TLineLossy::stamp_ac(ComplexMNASystem& sys,
                          Real omega,
                          const DenseVector& dc_solution) const {
    (void)dc_solution; // unused
    const TwoPort tp = two_port(omega);
    const Complex Y11 = tp.y11;
    const Complex Y12 = tp.y12;
    const Complex Y21 = Y12;
    const Complex Y22 = Y11;

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
// Transient: the constant-parameter (Bergeron) line
//------------------------------------------------------------------------------

void TLineLossy::stamp_transient(MNASystem& mna,
                         const DenseVector& node_voltages,
                         Real timestep,
                         IntegrationMethod method,
                         const BDFCoefficients* bdf_coef,
                         const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    (void)node_voltages;
    (void)bdf_coef;

    last_timestep_ = timestep;
    last_stamp_was_dc_ = false;
    last_stamp_was_first_stage_ = method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef != nullptr &&
                                  tr_bdf2_coef->stage == 1;

    // At a section's lossless line's own end A' (inside rs), as for the T
    // line (TLineLossless::stamp_transient):
    //   I_A' = G0 V_A' - [G0 V_B'(t - TD/M) + I_B'(t - TD/M)] = G0 V_A' - h_A
    // with V_A' = V_A - rs I_A', so I_A' = (G0 V_A - h_A) / (1 + G0 rs), and
    // the end adds its shunt: I_A = (1/(Z0 + rs) + gsh) V_A - k h_A,
    // k = Z0/(Z0 + rs). The line's ports are the A end of the first section
    // and the B end of the last; the delayed values are those at the end of
    // this step, from the points accepted before it (the step is at most
    // TD/M).
    const Real t = current_time_ + timestep;
    auto idx = [](NodeId n){ return (n == NODE_GND) ? -1 : static_cast<int>(n - 1); };
    const int port_plus[2] = {idx(nodes_[0]), idx(nodes_[2])};
    const int port_minus[2] = {idx(nodes_[1]), idx(nodes_[3])};
    const Real arriving[2] = {arriving_at_a(0, t), arriving_at_b(sections_ - 1, t)};
    const Real g = g_port_ + gsh_;
    for (int p = 0; p < 2; ++p) {
        const int a = port_plus[p];
        const int b = port_minus[p];
        if (a >= 0) {
            mna.G.add_entry(a, a, g);
            if (b >= 0) mna.G.add_entry(a, b, -g);
            mna.b[a] += arriving[p];
        }
        if (b >= 0) {
            mna.G.add_entry(b, b, g);
            if (a >= 0) mna.G.add_entry(b, a, -g);
            mna.b[b] -= arriving[p];
        }
    }
}

TLineLossy::HistoryPoint TLineLossy::delayed(int k, Real t) const {
    // Linear interpolation in the section's accepted points; before the
    // first, the first (the operating point's steady state); after the
    // last, the last
    const auto& h = history_[static_cast<std::size_t>(k)];
    if (t <= h.front().time) {
        return h.front();
    }
    if (t >= h.back().time) {
        return h.back();
    }
    auto it = std::lower_bound(h.begin(), h.end(), t,
                               [](const HistoryPoint& p, Real time) { return p.time < time; });
    const HistoryPoint& p1 = *(it - 1);
    const HistoryPoint& p2 = *it;
    const Real dt = p2.time - p1.time;
    if (dt < 1e-20) {
        return p1;
    }
    const Real a = (t - p1.time) / dt;
    return HistoryPoint{t, p1.v_a + a * (p2.v_a - p1.v_a), p1.v_b + a * (p2.v_b - p1.v_b),
                        p1.i_a + a * (p2.i_a - p1.i_a), p1.i_b + a * (p2.i_b - p1.i_b)};
}

Real TLineLossy::arriving_at_a(int k, Real t) const {
    const HistoryPoint p = delayed(k, t - tau_);
    return k_hist_ * (g0_ * p.v_b + p.i_b);
}

Real TLineLossy::arriving_at_b(int k, Real t) const {
    const HistoryPoint p = delayed(k, t - tau_);
    return k_hist_ * (g0_ * p.v_a + p.i_a);
}

std::vector<Real> TLineLossy::junction_voltages(Real t, Real v_a, Real v_b) const {
    // Junction j of sections j-1 and j meets nothing else: the B end of the
    // one and the A end of the other carry opposite currents,
    //   g V_j - arriving_at_b(j-1) + g V_j - arriving_at_a(j) = 0,
    // with g = 1/(Z0 + rs) + gsh, and the arriving waves are known
    std::vector<Real> v(static_cast<std::size_t>(sections_) + 1);
    v.front() = v_a;
    v.back() = v_b;
    const Real g = g_port_ + gsh_;
    for (int j = 1; j < sections_; ++j) {
        v[static_cast<std::size_t>(j)] = (arriving_at_b(j - 1, t) + arriving_at_a(j, t)) / (2.0 * g);
    }
    return v;
}

void TLineLossy::save_state() {
    history_saved_ = history_;
    current_time_saved_ = current_time_;
}

void TLineLossy::restore_state() {
    history_ = history_saved_;
    current_time_ = current_time_saved_;
}

void TLineLossy::reset_state() {
    history_.assign(static_cast<std::size_t>(sections_),
                    std::deque<HistoryPoint>{HistoryPoint{0.0, 0.0, 0.0, 0.0, 0.0}});
    current_time_ = 0.0;
    last_stamp_was_first_stage_ = false;
    last_stamp_was_dc_ = false;
}

void TLineLossy::accept_solution(const DenseVector& node_voltages) {
    // TR-BDF2's first stage ends at t_n + gamma h, between two time points:
    // the line's clock and history stay at t_n, from which the second stage
    // steps the whole h (stamp_transient)
    if (last_stamp_was_first_stage_) {
        return;
    }
    if (!last_stamp_was_dc_) {
        current_time_ += last_timestep_;
    }

    // node_voltages is 0-based; NodeId starts at 1 (0 is ground)
    auto val = [&](NodeId n) { return (n == NODE_GND) ? 0.0 : node_voltages[n]; };
    const Real v_a = val(nodes_[0]) - val(nodes_[1]);
    const Real v_b = val(nodes_[2]) - val(nodes_[3]);
    const auto m = static_cast<std::size_t>(sections_);

    if (last_stamp_was_dc_) {
        // An accepted operating point: the line has been in it for all
        // t <= 0. In that steady state each section's lossless line is a
        // wire, and the sections form a ladder: 2 rs between junctions and
        // 2 gsh from each inner junction, solved for its junction voltages
        // from the port voltages (a tridiagonal system). Without R the
        // junctions are at one voltage, and the current is the wire's of the
        // DC stamp, less what the shunts before a section draw.
        last_stamp_was_dc_ = false;
        std::vector<Real> v(m + 1, 0.5 * (v_a + v_b));
        std::vector<Real> current(m, 0.0);
        if (rs_ > 0.0) {
            v.front() = v_a;
            v.back() = v_b;
            const Real gs = 1.0 / (2.0 * rs_);
            // Thomas algorithm for junctions 1..M-1:
            //   -gs V_{j-1} + (2 gs + 2 gsh) V_j - gs V_{j+1} = 0
            std::vector<Real> c(m + 1, 0.0), d(m + 1, 0.0);
            const Real diag = 2.0 * gs + 2.0 * gsh_;
            for (std::size_t j = 1; j < m; ++j) {
                const Real rhs = (j == 1 ? gs * v_a : 0.0) + (j == m - 1 ? gs * v_b : 0.0);
                const Real denom = diag + gs * (j > 1 ? c[j - 1] : 0.0);
                c[j] = -gs / denom;
                d[j] = (rhs + gs * (j > 1 ? d[j - 1] : 0.0)) / denom;
            }
            for (std::size_t j = m - 1; j >= 1; --j) {
                v[j] = d[j] - c[j] * (j + 1 < m ? v[j + 1] : 0.0);
            }
            for (std::size_t k = 0; k < m; ++k) {
                current[k] = (v[k] - v[k + 1]) * gs;
            }
        } else {
            const Real i_line = TLineLossless::kDcCoupling * (v_a - v_b);
            for (std::size_t k = 0; k < m; ++k) {
                current[k] = i_line + (static_cast<Real>(m) - 1.0 - 2.0 * static_cast<Real>(k)) * gsh_ * v[k];
            }
        }
        for (std::size_t k = 0; k < m; ++k) {
            const Real v_inner = v[k] - rs_ * current[k];
            history_[k].assign(1, HistoryPoint{current_time_, v_inner, v_inner, current[k], -current[k]});
        }
        return;
    }

    // The junction voltages at this point, and each section's inner values
    // (see stamp_transient)
    const std::vector<Real> v = junction_voltages(current_time_, v_a, v_b);
    std::vector<HistoryPoint> points(m);
    for (std::size_t k = 0; k < m; ++k) {
        const int kk = static_cast<int>(k);
        const Real i_a = g_port_ * v[k] - arriving_at_a(kk, current_time_);
        const Real i_b = g_port_ * v[k + 1] - arriving_at_b(kk, current_time_);
        points[k] = HistoryPoint{current_time_, v[k] - rs_ * i_a, v[k + 1] - rs_ * i_b, i_a, i_b};
    }
    const Real min_time = current_time_ - 2.0 * tau_;
    for (std::size_t k = 0; k < m; ++k) {
        auto& h = history_[k];
        h.push_back(points[k]);
        while (h.size() > 2 && h.front().time < min_time) {
            h.pop_front();
        }
    }
}

} // namespace kirimaspice::devices
