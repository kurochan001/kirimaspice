// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "kirimaspice/devices/bjt.hpp"

#include "kirimaspice/core/device_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

namespace {

constexpr Real kGmin = 1e-12;    // [S] across each junction, as SPICE adds GMIN
constexpr Real kFc = 0.5;        // forward-bias coefficient of the depletion capacitance
constexpr Real kMaxExpArg = 80;  // exp() beyond this is continued linearly

// isat (exp(v / n_vt) - 1), continued with its slope beyond kMaxExpArg
struct Junction {
    Real i = 0.0, g = 0.0;
};
Junction junction(Real v, Real n_vt, Real isat) {
    if (isat <= 0.0) {
        return {};
    }
    const Real arg = v / n_vt;
    if (arg > kMaxExpArg) {
        const Real e = std::exp(kMaxExpArg);
        return {isat * (e * (1.0 + arg - kMaxExpArg) - 1.0), isat * e / n_vt};
    }
    return {isat * std::expm1(arg), isat * std::exp(arg) / n_vt};
}

Real junction_current(Real v, Real n_vt, Real isat) {
    return junction(v, n_vt, isat).i;
}

// The charge of a depletion capacitance CJ (1 - v/VJ)^-M, continued linearly
// in the capacitance above FC VJ (SPICE's F1, F2, F3), and that capacitance
struct Depletion {
    Real q = 0.0, c = 0.0;
};
Depletion depletion(Real v, Real cj, Real vj, Real m) {
    Depletion d;
    if (cj <= 0.0) {
        return d;
    }
    const Real fcv = kFc * vj;
    if (v < fcv) {
        const Real arg = 1.0 - v / vj;
        const Real sarg = std::pow(arg, -m);
        d.c = cj * sarg;
        d.q = (std::abs(1.0 - m) < 1e-12) ? -cj * vj * std::log(arg)
                                           : cj * vj * (1.0 - arg * sarg) / (1.0 - m);
    } else {
        const Real f1 = (std::abs(1.0 - m) < 1e-12) ? -vj * std::log(1.0 - kFc)
                                                     : vj * (1.0 - std::pow(1.0 - kFc, 1.0 - m)) / (1.0 - m);
        const Real f2 = std::pow(1.0 - kFc, 1.0 + m);
        const Real f3 = 1.0 - kFc * (1.0 + m);
        d.c = cj / f2 * (f3 + m * v / vj);
        d.q = cj * f1 + cj / f2 * (f3 * (v - fcv) + m / (2.0 * vj) * (v * v - fcv * fcv));
    }
    return d;
}

int index_of(NodeId n) { return (n == NODE_GND) ? -1 : static_cast<int>(n - 1); }

}  // namespace

template <BJTPolarity Pol>
BJT<Pol>::BJT(DeviceId id, const std::string& name, NodeId collector, NodeId base, NodeId emitter,
              Real is, Real beta_f, Real nf, Real isc, Real beta_r, Real nr, Real vaf, Real var,
              Real ikf, Real ikr, Real rb, Real rbm, Real irb, Real ise, Real ne, Real isc_leak,
              Real nc, Real cje, Real vje, Real mje, Real cjc, Real vjc, Real mjc, Real tf, Real tr,
              Real kf, Real af, Real ff, Real xti, Real xtb, Real eg, Real tnom)
    : Device(id, name, Pol == BJTPolarity::NPN ? DeviceType::BJT_NPN : DeviceType::BJT_PNP),
      nodes_{collector, base, emitter},
      is_(is), beta_f_(beta_f), nf_(nf), isc_(isc > 0.0 ? isc : is), beta_r_(beta_r), nr_(nr),
      vaf_(vaf), var_(var), ikf_(ikf), ikr_(ikr), rb_(rb), rbm_(rbm > 0.0 ? rbm : rb), irb_(irb),
      ise_(ise), ne_(ne), isc_leak_(isc_leak), nc_(nc), cje_(cje), vje_(vje), mje_(mje),
      cjc_(cjc), vjc_(vjc), mjc_(mjc), tf_(tf), tr_(tr), kf_(kf), af_(af), ff_(ff), xti_(xti),
      xtb_(xtb), eg_(eg), tnom_(tnom), isc_tracks_is_(isc <= 0.0) {
    if (is_ <= 0.0) throw std::runtime_error("BJT " + name + ": IS must be above 0");
    if (beta_f_ <= 0.0 || beta_r_ <= 0.0) throw std::runtime_error("BJT " + name + ": BF and BR must be above 0");
    if (nf_ <= 0.0 || nr_ <= 0.0 || ne_ <= 0.0 || nc_ <= 0.0) {
        throw std::runtime_error("BJT " + name + ": NF, NR, NE and NC must be above 0");
    }
    is_tnom_ = is_;
    isc_tnom_ = isc_;
    ise_tnom_ = ise_;
    isc_leak_tnom_ = isc_leak_;
    beta_f_tnom_ = beta_f_;
    beta_r_tnom_ = beta_r_;
    temp_ = tnom_;
    vt_ = core::compute_thermal_voltage(tnom_);
}

template <BJTPolarity Pol>
void BJT<Pol>::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }
    temp_ = temp_kelvin;
    vt_ = core::compute_thermal_voltage(temp_kelvin);
    is_ = core::scale_saturation_current(is_tnom_, xti_, eg_, nf_, temp_kelvin, tnom_);
    isc_ = core::scale_saturation_current(isc_tnom_, xti_, eg_, nr_, temp_kelvin, tnom_);
    ise_ = core::scale_saturation_current(ise_tnom_, xti_, eg_, ne_, temp_kelvin, tnom_);
    isc_leak_ = core::scale_saturation_current(isc_leak_tnom_, xti_, eg_, nc_, temp_kelvin, tnom_);
    beta_f_ = core::scale_mobility_temperature(beta_f_tnom_, xtb_, temp_kelvin, tnom_);
    beta_r_ = core::scale_mobility_temperature(beta_r_tnom_, xtb_, temp_kelvin, tnom_);
}

// SPICE's Gummel-Poon model:
//   If = IS (exp(vbe/(NF vt)) - 1) + GMIN vbe,  Ir = IS (exp(vbc/(NR vt)) - 1) + GMIN vbc
//   ILE = ISE (exp(vbe/(NE vt)) - 1),           ILC = ISC (exp(vbc/(NC vt)) - 1)
//   q1 = 1 / (1 - vbc/VAF - vbe/VAR),  q2 = If/IKF + Ir/IKR
//   qb = q1 (1 + sqrt(1 + 4 q2)) / 2
//   Ic = (If - Ir) / qb - Ir/BR - ILC,  Ib = If/BF + ILE + Ir/BR + ILC
template <BJTPolarity Pol>
typename BJT<Pol>::Evaluation BJT<Pol>::evaluate(Real vbe, Real vbc) const {
    const Junction forward = junction(vbe, nf_ * vt_, is_);
    const Junction reverse = junction(vbc, nr_ * vt_, isc_);
    const Junction leak_be = junction(vbe, ne_ * vt_, ise_);
    const Junction leak_bc = junction(vbc, nc_ * vt_, isc_leak_);
    const Real i_f = forward.i + kGmin * vbe, g_f = forward.g + kGmin;
    const Real i_r = reverse.i + kGmin * vbc, g_r = reverse.g + kGmin;

    Real inv_q1 = 1.0;
    if (vaf_ > 0.0) inv_q1 -= vbc / vaf_;
    if (var_ > 0.0) inv_q1 -= vbe / var_;
    const bool q1_limited = inv_q1 <= 1e-4;
    inv_q1 = std::max(inv_q1, 1e-4);  // the base of a punched-through device
    const Real q1 = 1.0 / inv_q1;
    const Real dq1_be = (!q1_limited && var_ > 0.0) ? q1 * q1 / var_ : 0.0;
    const Real dq1_bc = (!q1_limited && vaf_ > 0.0) ? q1 * q1 / vaf_ : 0.0;
    Real q2 = 0.0;
    if (ikf_ > 0.0) q2 += i_f / ikf_;
    if (ikr_ > 0.0) q2 += i_r / ikr_;
    const Real root = std::sqrt(std::max(0.0, 1.0 + 4.0 * q2));
    const Real qb = q1 * (1.0 + root) / 2.0;
    const Real dqb_be = dq1_be * (1.0 + root) / 2.0 +
                        ((root > 0.0 && ikf_ > 0.0) ? q1 * g_f / (root * ikf_) : 0.0);
    const Real dqb_bc = dq1_bc * (1.0 + root) / 2.0 +
                        ((root > 0.0 && ikr_ > 0.0) ? q1 * g_r / (root * ikr_) : 0.0);
    const Real transport = (i_f - i_r) / qb;

    Evaluation e;
    auto& c = e.currents;
    c.qb = qb;
    c.ic = transport - i_r / beta_r_ - leak_bc.i;
    c.ib = i_f / beta_f_ + leak_be.i + i_r / beta_r_ + leak_bc.i;
    auto& d = e.derivatives;
    d.dic_dvbe = (g_f - transport * dqb_be) / qb;
    d.dic_dvbc = (-g_r - transport * dqb_bc) / qb - g_r / beta_r_ - leak_bc.g;
    d.dib_dvbe = g_f / beta_f_ + leak_be.g;
    d.dib_dvbc = g_r / beta_r_ + leak_bc.g;
    return e;
}

template <BJTPolarity Pol>
typename BJT<Pol>::Currents BJT<Pol>::currents(Real vbe, Real vbc) const {
    return evaluate(vbe, vbc).currents;
}

template <BJTPolarity Pol>
typename BJT<Pol>::Derivatives BJT<Pol>::derivatives(Real vbe, Real vbc) const {
    return evaluate(vbe, vbc).derivatives;
}

template <BJTPolarity Pol>
Real BJT<Pol>::get_base_resistance(Real vbe, Real vbc) const {
    if (rb_ <= 0.0) {
        return 0.0;
    }
    const Currents c = currents(vbe, vbc);
    if (irb_ <= 0.0) {
        return rbm_ + (rb_ - rbm_) / c.qb;
    }
    const Real ratio = std::abs(c.ib) / irb_;
    // Rationalize sqrt(1 + a) - 1: it loses all digits as Ib approaches 0.
    const Real z = 6.0 * std::sqrt(ratio) / (1.0 + std::sqrt(1.0 + 144.0 / (M_PI * M_PI) * ratio));
    if (z < 1e-4) {
        // 3 (tan z - z) / (z tan^2 z) = 1 - 4 z^2 / 15 + O(z^4).
        return rbm_ + (rb_ - rbm_) * (1.0 - 4.0 * z * z / 15.0);
    }
    const Real tz = std::tan(z);
    return rbm_ + 3.0 * (rb_ - rbm_) * (tz - z) / (z * tz * tz);
}

template <BJTPolarity Pol>
typename BJT<Pol>::Charges BJT<Pol>::junction_charges(Real vbe, Real vbc) const {
    // Depletion charges, and the diffusion charges TF If / qb and TR Ir
    const Currents c = currents(vbe, vbc);
    const Real i_f = junction_current(vbe, nf_ * vt_, is_);
    const Real i_r = junction_current(vbc, nr_ * vt_, isc_);
    Charges q;
    q.qbe = depletion(vbe, cje_, vje_, mje_).q + tf_ * i_f / c.qb;
    q.qbc = depletion(vbc, cjc_, vjc_, mjc_).q + tr_ * i_r;
    return q;
}

template <BJTPolarity Pol>
typename BJT<Pol>::Capacitances BJT<Pol>::capacitances(Real vbe, Real vbc) const {
    // The derivatives of the charges, by central differences
    constexpr Real h = 1e-6;  // [V]
    Capacitances c;
    c.cbe = (junction_charges(vbe + h, vbc).qbe - junction_charges(vbe - h, vbc).qbe) / (2.0 * h);
    c.cbc = (junction_charges(vbe, vbc + h).qbc - junction_charges(vbe, vbc - h).qbc) / (2.0 * h);
    c.cbe = std::max(c.cbe, 0.0);
    c.cbc = std::max(c.cbc, 0.0);
    return c;
}

template <BJTPolarity Pol>
std::array<Real, 2> BJT<Pol>::junction_voltages(const DenseVector& v, bool ground_at_zero) const {
    auto voltage = [&](NodeId n) -> Real {
        if (n == NODE_GND) return 0.0;
        const std::size_t k = ground_at_zero ? n : n - 1;
        return k < v.size() ? v[k] : 0.0;
    };
    const Real vc = voltage(nodes_[0]), vb = voltage(nodes_[1]), ve = voltage(nodes_[2]);
    return {polarity * (vb - ve), polarity * (vb - vc)};
}

template <BJTPolarity Pol>
void BJT<Pol>::stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const {
    auto [vbe, vbc] = junction_voltages(node_voltages, false);

    // Limit the step of each junction voltage from its last evaluation
    // (SPICE's pnjlim); at convergence the limited voltages are the voltages
    Real vbe_lim = vbe, vbc_lim = vbc;
    if (enable_voltage_limiting_) {
        vbe_lim = core::limit_junction_step(vbe, vbe_last_eval_, nf_ * vt_, core::compute_vcrit(nf_ * vt_, is_));
        vbc_lim = core::limit_junction_step(vbc, vbc_last_eval_, nr_ * vt_, core::compute_vcrit(nr_ * vt_, isc_));
    }
    vbe_last_eval_ = vbe_lim;
    vbc_last_eval_ = vbc_lim;
    limited_in_last_stamp_ = (vbe_lim != vbe) || (vbc_lim != vbc);

    const Evaluation e = evaluate(vbe_lim, vbc_lim);
    const auto& c = e.currents;
    const auto& d = e.derivatives;

    // The terminal currents into collector and base are polarity times the
    // NPN-like device's, and their derivatives by the node voltages are the
    // same for both polarities (polarity^2 = 1): with vbe = p (Vb - Ve) and
    // vbc = p (Vb - Vc), dIc/dVb = dic_dvbe + dic_dvbc, dIc/dVe = -dic_dvbe,
    // dIc/dVc = -dic_dvbc. The Norton currents are polarity times
    // i - di/dvbe vbe - di/dvbc vbc.
    const Real ic_eq = polarity * (c.ic - d.dic_dvbe * vbe_lim - d.dic_dvbc * vbc_lim);
    const Real ib_eq = polarity * (c.ib - d.dib_dvbe * vbe_lim - d.dib_dvbc * vbc_lim);

    const int nc = index_of(nodes_[0]), nb = index_of(nodes_[1]), ne = index_of(nodes_[2]);
    auto add = [&](int row, int col, Real g) {
        if (row >= 0 && col >= 0) mna.G.add_entry(row, col, g);
    };
    // Collector: Ic leaves the node into the device
    add(nc, nb, d.dic_dvbe + d.dic_dvbc);
    add(nc, ne, -d.dic_dvbe);
    add(nc, nc, -d.dic_dvbc);
    // Base: Ib leaves the node into the device
    add(nb, nb, d.dib_dvbe + d.dib_dvbc);
    add(nb, ne, -d.dib_dvbe);
    add(nb, nc, -d.dib_dvbc);
    // Emitter: Ic + Ib come out of the device into the node
    add(ne, nb, -(d.dic_dvbe + d.dic_dvbc + d.dib_dvbe + d.dib_dvbc));
    add(ne, ne, d.dic_dvbe + d.dib_dvbe);
    add(ne, nc, d.dic_dvbc + d.dib_dvbc);
    if (nc >= 0) mna.b[nc] -= ic_eq;
    if (nb >= 0) mna.b[nb] -= ib_eq;
    if (ne >= 0) mna.b[ne] += ic_eq + ib_eq;
}

template <BJTPolarity Pol>
void BJT<Pol>::stamp_gmin(MNASystem& mna, const DenseVector& node_voltages) const {
    (void)mna;
    (void)node_voltages;
    // GMIN is part of the junction currents (currents())
}

template <BJTPolarity Pol>
void BJT<Pol>::stamp_transient(MNASystem& mna, const DenseVector& node_voltages, Real timestep,
                               IntegrationMethod method, const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    stamp_dc_nonlinear(mna, node_voltages);
    if (!is_dynamic()) {
        return;
    }
    cap_dt_ = timestep;
    cap_method_ = method;
    cap_bdf_ = bdf_coef ? std::optional<BDFCoefficients>(*bdf_coef) : std::nullopt;
    cap_tr_bdf2_ = tr_bdf2_coef ? std::optional<TR_BDF2_Coefficients>(*tr_bdf2_coef) : std::nullopt;
    cap_stamped_ = true;

    // Each junction capacitance at the present iterate, integrated as a
    // capacitor is (device_utils::capacitor_companion) from the history of
    // the accepted points. The voltages are those of the NPN-like device, so
    // the companion's current flows from base to emitter (to collector) for
    // NPN and the other way for PNP: its nodes are swapped for PNP.
    const auto [vbe, vbc] = junction_voltages(node_voltages, false);
    const Capacitances c = capacitances(vbe, vbc);
    const int nc = index_of(nodes_[0]), nb = index_of(nodes_[1]), ne = index_of(nodes_[2]);
    const bool npn = Pol == BJTPolarity::NPN;
    const auto& h = history_;
    const auto be = device_utils::capacitor_companion(method, c.cbe, timestep, h.vbe[0], h.vbe[1],
                                                      h.vbe[2], h.ibe, bdf_coef, tr_bdf2_coef);
    const auto bc = device_utils::capacitor_companion(method, c.cbc, timestep, h.vbc[0], h.vbc[1],
                                                      h.vbc[2], h.ibc, bdf_coef, tr_bdf2_coef);
    device_utils::stamp_capacitor_companion(mna, npn ? nb : ne, npn ? ne : nb, be.g, be.i_eq, 1.0);
    device_utils::stamp_capacitor_companion(mna, npn ? nb : nc, npn ? nc : nb, bc.g, bc.i_eq, 1.0);
}

template <BJTPolarity Pol>
void BJT<Pol>::stamp_ac(ComplexMNASystem& sys, Real omega, const DenseVector& dc_solution) const {
    const auto [vbe, vbc] = junction_voltages(dc_solution, false);
    const Derivatives d = derivatives(vbe, vbc);
    const Capacitances c = capacitances(vbe, vbc);
    const int nc = index_of(nodes_[0]), nb = index_of(nodes_[1]), ne = index_of(nodes_[2]);
    auto add = [&](int row, int col, Complex y) {
        if (row >= 0 && col >= 0) sys.Y.add_entry(row, col, y);
    };
    // The linearized currents, as in the DC stamp
    add(nc, nb, Complex(d.dic_dvbe + d.dic_dvbc, 0.0));
    add(nc, ne, Complex(-d.dic_dvbe, 0.0));
    add(nc, nc, Complex(-d.dic_dvbc, 0.0));
    add(nb, nb, Complex(d.dib_dvbe + d.dib_dvbc, 0.0));
    add(nb, ne, Complex(-d.dib_dvbe, 0.0));
    add(nb, nc, Complex(-d.dib_dvbc, 0.0));
    add(ne, nb, Complex(-(d.dic_dvbe + d.dic_dvbc + d.dib_dvbe + d.dib_dvbc), 0.0));
    add(ne, ne, Complex(d.dic_dvbe + d.dib_dvbe, 0.0));
    add(ne, nc, Complex(d.dic_dvbc + d.dib_dvbc, 0.0));
    // The junction capacitances, between base and emitter and base and collector
    auto admittance = [&](int a, int b, Complex y) {
        add(a, a, y);
        add(b, b, y);
        add(a, b, -y);
        add(b, a, -y);
    };
    admittance(nb, ne, Complex(0.0, omega * c.cbe));
    admittance(nb, nc, Complex(0.0, omega * c.cbc));
}

template <BJTPolarity Pol>
void BJT<Pol>::accept_solution(const DenseVector& node_voltages) {
    // node_voltages holds ground at element 0
    const auto [vbe, vbc] = junction_voltages(node_voltages, true);
    auto& h = history_;
    if (cap_stamped_ && h.valid) {
        // The capacitor currents at the accepted point, from the companion of
        // the step that ended there
        const Capacitances c = capacitances(vbe, vbc);
        const BDFCoefficients* bdf = cap_bdf_ ? &*cap_bdf_ : nullptr;
        const TR_BDF2_Coefficients* tr = cap_tr_bdf2_ ? &*cap_tr_bdf2_ : nullptr;
        const auto be = device_utils::capacitor_companion(cap_method_, c.cbe, cap_dt_, h.vbe[0],
                                                          h.vbe[1], h.vbe[2], h.ibe, bdf, tr);
        const auto bc = device_utils::capacitor_companion(cap_method_, c.cbc, cap_dt_, h.vbc[0],
                                                          h.vbc[1], h.vbc[2], h.ibc, bdf, tr);
        h.ibe = be.g * vbe + be.i_eq;
        h.ibc = bc.g * vbc + bc.i_eq;
    } else {
        h.ibe = 0.0;
        h.ibc = 0.0;
        h.vbe.fill(vbe);
        h.vbc.fill(vbc);
    }
    h.vbe = {vbe, h.vbe[0], h.vbe[1]};
    h.vbc = {vbc, h.vbc[0], h.vbc[1]};
    const Charges q = junction_charges(vbe, vbc);
    h.qbe = q.qbe;
    h.qbc = q.qbc;
    h.valid = true;
    cap_stamped_ = false;
}

template <BJTPolarity Pol>
void BJT<Pol>::save_state() {
    saved_history_ = history_;
}

template <BJTPolarity Pol>
void BJT<Pol>::restore_state() {
    history_ = saved_history_;
}

template <BJTPolarity Pol>
void BJT<Pol>::reset_state() {
    history_ = History{};
    saved_history_ = History{};
    vbe_last_eval_ = 0.0;
    vbc_last_eval_ = 0.0;
    cap_stamped_ = false;
    cap_bdf_.reset();
    cap_tr_bdf2_.reset();
}

template <BJTPolarity Pol>
Real BJT<Pol>::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    // The change of the junction charges over the step, against CHGTOL
    const auto [vbe, vbc] = junction_voltages(node_voltages, false);
    const Charges q = junction_charges(vbe, vbc);
    if (!history_.valid || chgtol <= 0.0) {
        return 0.0;
    }
    return (std::abs(q.qbe - history_.qbe) + std::abs(q.qbc - history_.qbc)) / chgtol;
}

template <BJTPolarity Pol>
std::vector<NoiseSource> BJT<Pol>::get_noise_sources(const DenseVector& dc_solution,
                                                     Real frequency) const {
    // Shot noise 2 q |Ic| from collector to emitter and 2 q |Ib| from base to
    // emitter, and flicker noise KF |Ib|^AF / f^FF from base to emitter
    const auto [vbe, vbc] = junction_voltages(dc_solution, false);
    const Currents c = currents(vbe, vbc);
    std::vector<NoiseSource> sources;
    constexpr Real q = 1.602176634e-19;
    sources.push_back({name_ + "_shot_ic", nodes_[0], nodes_[2], 2.0 * q * std::abs(c.ic), true});
    sources.push_back({name_ + "_shot_ib", nodes_[1], nodes_[2], 2.0 * q * std::abs(c.ib), true});
    if (kf_ > 0.0 && frequency > 0.0) {
        sources.push_back({name_ + "_flicker", nodes_[1], nodes_[2],
                           kf_ * std::pow(std::abs(c.ib), af_) / std::pow(frequency, ff_), true});
    }
    return sources;
}

template <BJTPolarity Pol>
std::optional<Real> BJT<Pol>::get_parameter(const std::string& param_name) const {
    std::string key = param_name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::toupper(c); });
    if (key == "BETA_F") key = "BF";
    if (key == "BETA_R") key = "BR";
    if (key == "IS") return is_tnom_;  // IS, BF, BR, ISE and ISC at TNOM
    if (key == "BF") return beta_f_tnom_;
    if (key == "NF") return nf_;
    if (key == "BR") return beta_r_tnom_;
    if (key == "NR") return nr_;
    if (key == "VAF") return vaf_;
    if (key == "VAR") return var_;
    if (key == "IKF") return ikf_;
    if (key == "IKR") return ikr_;
    if (key == "ISE") return ise_tnom_;
    if (key == "NE") return ne_;
    if (key == "ISC") return isc_leak_tnom_;
    if (key == "NC") return nc_;
    if (key == "CJE") return cje_;
    if (key == "CJC") return cjc_;
    return std::nullopt;
}

template <BJTPolarity Pol>
bool BJT<Pol>::set_parameter(const std::string& param_name, Real value) {
    std::string key = param_name;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::toupper(c); });
    if (key == "BETA_F") key = "BF";
    if (key == "BETA_R") key = "BR";
    if (!std::isfinite(value)) return false;
    // IS, BF, BR, ISE and ISC are the card's, at TNOM; the device then takes
    // them, and those the emission coefficients scale, at its temperature
    if (key == "IS" && value > 0.0) {
        is_tnom_ = value;
        if (isc_tracks_is_) isc_tnom_ = value;
    } else if (key == "BF" && value > 0.0) {
        beta_f_tnom_ = value;
    } else if (key == "NF" && value > 0.0) {
        nf_ = value;
    } else if (key == "BR" && value > 0.0) {
        beta_r_tnom_ = value;
    } else if (key == "NR" && value > 0.0) {
        nr_ = value;
    } else if (key == "VAF" && value >= 0.0) {
        vaf_ = value;
    } else if (key == "VAR" && value >= 0.0) {
        var_ = value;
    } else if (key == "IKF" && value >= 0.0) {
        ikf_ = value;
    } else if (key == "IKR" && value >= 0.0) {
        ikr_ = value;
    } else if (key == "ISE" && value >= 0.0) {
        ise_tnom_ = value;
    } else if (key == "NE" && value > 0.0) {
        ne_ = value;
    } else if (key == "ISC" && value >= 0.0) {
        isc_leak_tnom_ = value;
    } else if (key == "NC" && value > 0.0) {
        nc_ = value;
    } else if (key == "CJE" && value >= 0.0) {
        cje_ = value;
    } else if (key == "CJC" && value >= 0.0) {
        cjc_ = value;
    } else {
        return false;
    }
    set_temperature(temp_);
    return true;
}

template <BJTPolarity Pol>
std::vector<std::string> BJT<Pol>::get_perturbable_parameters() const {
    return {"IS", "BF", "NF", "BR", "NR", "VAF", "VAR", "IKF", "IKR"};
}

template class BJT<BJTPolarity::NPN>;
template class BJT<BJTPolarity::PNP>;

}  // namespace kirimaspice::devices
