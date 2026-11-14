// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/finfet.cpp
// Unified FinFET template implementation
#include "kirimaspice/devices/finfet.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace kirimaspice::devices {

template<Polarity P>
FinFET<P>::FinFET(DeviceId id, const std::string& name,
                   NodeId drain, NodeId gate, NodeId source,
                   int nfin, Real hfin, Real tfin,
                   Real vth, Real u0, Real vsat, Real ss, Real dibl, Real tox,
                   Real cgs, Real cgd,
                   Real kf, Real af, Real kt1, Real tnom, Real l)
    : Device(id, name, device_type),
      nodes_{drain, gate, source},
      nfin_(nfin), hfin_(hfin), tfin_(tfin), l_(l),
      vth_(vth), u0_(u0), vsat_(vsat), ss_(ss), dibl_(dibl), tox_(tox),
      cgs_(cgs), cgd_(cgd),
      kf_(kf), af_(af), kt1_(kt1), tnom_(tnom) {

    // Calculate effective width: Weff = NFin × Hfin
    weff_ = static_cast<Real>(nfin_) * hfin_;

    // Calculate oxide capacitance per unit area: Cox = εox × ε0 / tox
    cox_ = epsilon_ox * epsilon_0 / tox_;

    // The gain factor of the square law, u0 Cox Weff / L
    beta_ = cox_ * weff_ * u0_ / l_;

    const char* type_str = (P == Polarity::N) ? "FinFET_N" : "FinFET_P";
    spdlog::debug("Created {}: {} D={} G={} S={} NFin={} Hfin={:.2e}m Tfin={:.2e}m "
                  "Weff={:.2e}m Vth={:.3f}V β={:.3e}A/V² SS={:.1f}mV/dec DIBL={:.3f}V/V",
                  type_str, name, drain, gate, source, nfin_, hfin_, tfin_, weff_, vth_, beta_, ss_*1e3, dibl_);
}

template<Polarity P>
void FinFET<P>::stamp_gmin(MNASystem& mna) const {
    NodeId nd = nodes_[0];  // Drain
    NodeId ns = nodes_[2];  // Source

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // Add minimum conductance between drain and source
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, gmin_default);
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -gmin_default);
        }
    }
    if (ns_idx >= 0) {
        if (nd_idx >= 0) {
            mna.G.add_entry(ns_idx, nd_idx, -gmin_default);
        }
        mna.G.add_entry(ns_idx, ns_idx, gmin_default);
    }
}

namespace {

int node_index(NodeId node) {
    return (node == NODE_GND) ? -1 : static_cast<int>(node - 1);
}

// The channel between drain and source: the current from drain to source
// has the derivatives gm, gds and -(gm + gds) with respect to the gate,
// drain and source voltages, for both polarities (see stamp_dc_nonlinear).
template <typename Matrix, typename T>
void stamp_channel(Matrix& m, int nd, int ng, int ns, T gm, T gds) {
    if (nd >= 0) {
        if (ng >= 0) m.add_entry(nd, ng, gm);
        m.add_entry(nd, nd, gds);
        if (ns >= 0) m.add_entry(nd, ns, -(gm + gds));
    }
    if (ns >= 0) {
        if (ng >= 0) m.add_entry(ns, ng, -gm);
        if (nd >= 0) m.add_entry(ns, nd, -gds);
        m.add_entry(ns, ns, gm + gds);
    }
}

// A two-terminal admittance y between nodes a and b
template <typename Matrix, typename T>
void stamp_admittance(Matrix& m, int a, int b, T y) {
    if (a >= 0) {
        m.add_entry(a, a, y);
        if (b >= 0) m.add_entry(a, b, -y);
    }
    if (b >= 0) {
        m.add_entry(b, b, y);
        if (a >= 0) m.add_entry(b, a, -y);
    }
}

}  // namespace

template<Polarity P>
typename FinFET<P>::DrainCurrent FinFET<P>::drain_current(Real v_gs_eff, Real v_ds_eff) const {
    // Threshold at the operating temperature, lowered by DIBL for VDS > 0
    const Real delta_t = temperature_ - tnom_;
    const Real vth_temp = get_effective_threshold() + kt1_ * delta_t;
    const Real dibl_slope = (v_ds_eff > 0.0) ? dibl_ : 0.0;  // -d Vth_eff / d VDS
    const Real vth_eff = vth_temp - dibl_slope * v_ds_eff;

    // Mobility u0 (Tnom / T)^1.5 and the gain factor u Cox Weff / L
    const Real u_temp = u0_ * std::pow(tnom_ / temperature_, 1.5);
    const Real beta = cox_ * weff_ * u_temp / l_;

    DrainCurrent out;
    if (v_gs_eff < vth_eff) {
        // Below the threshold: beta Vt^2 exp((VGS - Vth_eff) / (n Vt)) (1 - exp(-VDS / Vt))
        const Real vt = k_boltzmann * temperature_ / q_electron;
        const Real n = 1.0 + (ss_ * std::log(10.0)) / vt;
        const Real channel = beta * vt * vt * std::exp((v_gs_eff - vth_eff) / (n * vt));
        const Real drain_factor = 1.0 - std::exp(-v_ds_eff / vt);
        out.id = channel * drain_factor;
        out.gm = out.id / (n * vt);
        out.gds = channel * std::exp(-v_ds_eff / vt) / vt + out.gm * dibl_slope;
        return out;
    }

    const Real vov = v_gs_eff - vth_eff;
    if (v_ds_eff < vov) {
        // Linear region
        out.id = beta * (vov * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
        out.gm = beta * v_ds_eff;
        out.gds = beta * (vov - v_ds_eff);
    } else {
        // Saturation
        out.id = 0.5 * beta * vov * vov;
        out.gm = beta * vov;
        out.gds = 0.0;
    }
    // Vth_eff falls with VDS, which raises the current as VGS would
    out.gds += out.gm * dibl_slope;
    return out;
}

template<Polarity P>
void FinFET<P>::terminal_voltages(const DenseVector& v, Real& vd, Real& vg, Real& vs) const {
    vd = (nodes_[0] == NODE_GND) ? 0.0 : v[nodes_[0] - 1];
    vg = (nodes_[1] == NODE_GND) ? 0.0 : v[nodes_[1] - 1];
    vs = (nodes_[2] == NODE_GND) ? 0.0 : v[nodes_[2] - 1];
}

template<Polarity P>
void FinFET<P>::stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const {
    Real vd = 0.0, vg = 0.0, vs = 0.0;
    terminal_voltages(node_voltages, vd, vg, vs);
    Real v_gs_eff = 0.0, v_ds_eff = 0.0;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);
    const DrainCurrent dc = drain_current(v_gs_eff, v_ds_eff);

    // The current from drain to source is id for N and -id for P, where id
    // is a function of (VGS, VDS) for N and of (VSG, VSD) = -(VGS, VDS) for
    // P. Its derivatives with respect to Vg, Vd and Vs are gm, gds and
    // -(gm + gds) in both cases, so the matrix entries are the same and only
    // the current's sign differs.
    constexpr Real sign = (P == Polarity::N) ? 1.0 : -1.0;
    const Real i_ds = sign * dc.id;
    const Real i_eq = i_ds - dc.gm * (vg - vs) - dc.gds * (vd - vs);

    const int nd = node_index(nodes_[0]);
    const int ng = node_index(nodes_[1]);
    const int ns = node_index(nodes_[2]);
    stamp_channel(mna.G, nd, ng, ns, dc.gm, dc.gds);
    if (nd >= 0) mna.b[nd] -= i_eq;
    if (ns >= 0) mna.b[ns] += i_eq;
    stamp_gmin(mna);

    spdlog::trace("FinFET {} DC: Vgs_eff={:.6f}V Vds_eff={:.6f}V Id={:.3e}A Gm={:.3e}S Gds={:.3e}S",
                  name_, v_gs_eff, v_ds_eff, dc.id, dc.gm, dc.gds);
}

template<Polarity P>
void FinFET<P>::stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                                 Real timestep, IntegrationMethod /*method*/,
                                 const BDFCoefficients* /*bdf_coef*/,
                                 const TR_BDF2_Coefficients* /*tr_bdf2_coef*/) const {
    stamp_dc_nonlinear(mna, node_voltages);

    // CGS and CGD by backward Euler: the current C / dt (V - V_prev) from the
    // gate, with V = Vg - Vs and Vg - Vd and V_prev the accepted values
    const int nd = node_index(nodes_[0]);
    const int ng = node_index(nodes_[1]);
    const int ns = node_index(nodes_[2]);
    auto stamp_capacitance = [&](Real c, int other, Real v_prev) {
        if (c <= 0.0) {
            return;
        }
        const Real g = c / timestep;
        stamp_admittance(mna.G, ng, other, g);
        if (ng >= 0) mna.b[ng] += g * v_prev;
        if (other >= 0) mna.b[other] -= g * v_prev;
    };
    stamp_capacitance(cgs_, ns, v_gs_prev_);
    stamp_capacitance(cgd_, nd, v_gd_prev_);
}

template<Polarity P>
void FinFET<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                          const DenseVector& dc_solution) const {
    Real vd = 0.0, vg = 0.0, vs = 0.0;
    terminal_voltages(dc_solution, vd, vg, vs);
    Real v_gs_eff = 0.0, v_ds_eff = 0.0;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);
    const DrainCurrent dc = drain_current(v_gs_eff, v_ds_eff);

    // The small-signal current from drain to source, gm vgs + gds vds, as in
    // the DC stamp, with GMIN in parallel, and the gate capacitances
    const int nd = node_index(nodes_[0]);
    const int ng = node_index(nodes_[1]);
    const int ns = node_index(nodes_[2]);
    stamp_channel(sys.Y, nd, ng, ns, Complex(dc.gm, 0.0), Complex(dc.gds + gmin_default, 0.0));
    if (cgs_ > 0.0) stamp_admittance(sys.Y, ng, ns, Complex(0.0, omega * cgs_));
    if (cgd_ > 0.0) stamp_admittance(sys.Y, ng, nd, Complex(0.0, omega * cgd_));

    spdlog::trace("FinFET {} AC: gm={:.6e}S gds={:.6e}S @{:.3e}Hz",
                  name_, dc.gm, dc.gds, omega / (2.0 * M_PI));
}

template<Polarity P>
std::vector<NoiseSource> FinFET<P>::get_noise_sources(const DenseVector& dc_solution,
                                                        Real frequency) const {
    std::vector<NoiseSource> sources;
    Real vd = 0.0, vg = 0.0, vs = 0.0;
    terminal_voltages(dc_solution, vd, vg, vs);
    Real v_gs_eff = 0.0, v_ds_eff = 0.0;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);
    const DrainCurrent dc = drain_current(v_gs_eff, v_ds_eff);

    // Channel thermal noise: I_n^2 = 4 k T (2/3) gm [A^2/Hz]
    const Real thermal_psd = 4.0 * k_boltzmann * temperature_ * (2.0 / 3.0) * std::abs(dc.gm);
    if (thermal_psd > 0.0) {
        NoiseSource thermal;
        thermal.name = name_ + "_thermal";
        thermal.node_p = nodes_[0];
        thermal.node_n = nodes_[2];
        thermal.spectral_density = thermal_psd;
        thermal.is_current = true;
        sources.push_back(thermal);
    }

    // Flicker noise: I_n^2 = KF |ID|^AF / f [A^2/Hz]
    if (kf_ > 0.0 && frequency > 0.0) {
        NoiseSource flicker;
        flicker.name = name_ + "_flicker";
        flicker.node_p = nodes_[0];
        flicker.node_n = nodes_[2];
        flicker.spectral_density = kf_ * std::pow(std::abs(dc.id), af_) / frequency;
        flicker.is_current = true;
        sources.push_back(flicker);
    }
    return sources;
}

template<Polarity P>
void FinFET<P>::accept_solution(const DenseVector& node_voltages) {
    // node k at element k, ground at element 0 (Device::accept_solution);
    // the capacitances keep Vg - Vs and Vg - Vd for both polarities
    const Real v_d = (nodes_[0] == NODE_GND) ? 0.0 : node_voltages[nodes_[0]];
    const Real v_g = (nodes_[1] == NODE_GND) ? 0.0 : node_voltages[nodes_[1]];
    const Real v_s = (nodes_[2] == NODE_GND) ? 0.0 : node_voltages[nodes_[2]];
    v_gs_prev_ = v_g - v_s;
    v_gd_prev_ = v_g - v_d;
}

template<Polarity P>
std::optional<Real> FinFET<P>::get_parameter(const std::string& param_name) const {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "nfin") return static_cast<Real>(nfin_);
    if (param_lower == "hfin") return hfin_;
    if (param_lower == "tfin") return tfin_;
    if (param_lower == "l") return l_;
    if (param_lower == "weff") return weff_;
    if (param_lower == "vth" || param_lower == "vth0") return vth_;
    if (param_lower == "u0") return u0_;
    if (param_lower == "vsat") return vsat_;
    if (param_lower == "ss") return ss_;
    if (param_lower == "dibl") return dibl_;
    if (param_lower == "tox") return tox_;
    if (param_lower == "cgs") return cgs_;
    if (param_lower == "cgd") return cgd_;
    if (param_lower == "kf") return kf_;
    if (param_lower == "af") return af_;
    if (param_lower == "kt1") return kt1_;
    if (param_lower == "tnom") return tnom_;

    return std::nullopt;
}

template<Polarity P>
bool FinFET<P>::set_parameter(const std::string& param_name, Real value) {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "nfin") {
        if (value >= 1) {
            nfin_ = static_cast<int>(value);
            weff_ = static_cast<Real>(nfin_) * hfin_;
            beta_ = cox_ * weff_ * u0_ / l_;
            spdlog::trace("FinFET {} set NFin={}", name_, nfin_);
            return true;
        }
        return false;
    }
    if (param_lower == "hfin") {
        if (value > 0.0) {
            hfin_ = value;
            weff_ = static_cast<Real>(nfin_) * hfin_;
            beta_ = cox_ * weff_ * u0_ / l_;
            return true;
        }
        return false;
    }
    if (param_lower == "l") {
        if (value > 0.0) {
            l_ = value;
            beta_ = cox_ * weff_ * u0_ / l_;
            return true;
        }
        return false;
    }
    if (param_lower == "vth" || param_lower == "vth0" || param_lower == "vto") {
        vth_ = value;
        return true;
    }
    if (param_lower == "u0") {
        if (value > 0.0) {
            u0_ = value;
            beta_ = cox_ * weff_ * u0_ / l_;
            return true;
        }
        return false;
    }
    if (param_lower == "tox") {
        if (value > 0.0) {
            tox_ = value;
            cox_ = epsilon_ox * epsilon_0 / tox_;
            beta_ = cox_ * weff_ * u0_ / l_;
            return true;
        }
        return false;
    }
    // Parameters that no other one is derived from
    struct Plain {
        const char* name;
        Real* target;
        bool positive;
    };
    const Plain plain[] = {
        {"tfin", &tfin_, true}, {"vsat", &vsat_, true}, {"ss", &ss_, true},
        {"dibl", &dibl_, false}, {"cgs", &cgs_, false}, {"cgd", &cgd_, false},
        {"kf", &kf_, false},    {"af", &af_, true},     {"kt1", &kt1_, false},
        {"tnom", &tnom_, true},
    };
    for (const auto& entry : plain) {
        if (param_lower == entry.name) {
            if (entry.positive && value <= 0.0) {
                return false;
            }
            *entry.target = value;
            return true;
        }
    }

    return false;
}

template<Polarity P>
std::vector<std::string> FinFET<P>::get_perturbable_parameters() const {
    return {"NFIN", "HFIN", "VTH", "U0"};
}

// Explicit template instantiations
template class FinFET<Polarity::N>;
template class FinFET<Polarity::P>;

} // namespace kirimaspice::devices
