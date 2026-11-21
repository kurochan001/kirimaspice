// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/mosfet_sic.cpp
// Unified SiC MOSFET template implementation
#include "kirimaspice/devices/mosfet_sic.hpp"

#include "kirimaspice/core/device_utils.hpp"
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

template<Polarity P>
MOSFET_SiC<P>::MOSFET_SiC(DeviceId id, const std::string& name,
                          NodeId drain, NodeId gate, NodeId source,
                          Real kp, Real w, Real l, Real vth,
                          Real theta, Real rdrift, Real nfs, Real vbd,
                          Real cgso, Real cgdo, Real cgbo,
                          Real cjo, Real pb, Real mj,
                          Real kf, Real af, Real ef, Real gamma,
                          Real kt1, Real kt2, Real ute, Real rdtemp, Real tnom)
    : Device(id, name, device_type)
    , nodes_{drain, gate, source}
    , kp_(kp)
    , w_(w)
    , l_(l)
    , vth_(vth)
    , beta_(kp * w / l)
    // SiC-specific DC parameters
    , theta_(theta)
    , rdrift_(rdrift)
    , nfs_(nfs)
    , vbd_(vbd)
    // Capacitance parameters
    , cgso_(cgso)
    , cgdo_(cgdo)
    , cgbo_(cgbo)
    , cjo_(cjo)
    , pb_(pb)
    , mj_(mj)
    // Noise parameters
    , kf_(kf)
    , af_(af)
    , ef_(ef)
    , gamma_(gamma)
    // Temperature parameters
    , kt1_(kt1)
    , kt2_(kt2)
    , ute_(ute)
    , rdtemp_(rdtemp)
    , tnom_(tnom)
    , temperature_(tnom) {

    // Parameter validation
    if (kp <= 0.0) {
        throw std::runtime_error("SiC MOSFET process transconductance must be positive: " + name);
    }
    if (w <= 0.0) {
        throw std::runtime_error("SiC MOSFET channel width must be positive: " + name);
    }
    if (l <= 0.0) {
        throw std::runtime_error("SiC MOSFET channel length must be positive: " + name);
    }
    if (theta < 0.0) {
        throw std::runtime_error("SiC MOSFET mobility modulation must be non-negative: " + name);
    }
    if (rdrift < 0.0) {
        throw std::runtime_error("SiC MOSFET drift resistance must be non-negative: " + name);
    }
    if (vbd <= 0.0) {
        throw std::runtime_error("SiC MOSFET breakdown voltage must be positive: " + name);
    }
    if (cgso < 0.0 || cgdo < 0.0 || cgbo < 0.0) {
        throw std::runtime_error("SiC MOSFET overlap capacitance must be non-negative: " + name);
    }

    const char* polarity_str = (P == Polarity::N) ? "N" : "P";
    spdlog::debug("Created MOSFET_SiC_{} {} (D={}, G={}, S={}, Kp={:.6e}A/V², W={:.6e}m, L={:.6e}m, "
                  "Vth={:.3f}V, β={:.6e}A/V², Rdrift={:.6e}Ω, THETA={:.6e}V⁻¹, Vbd={:.1f}V)",
                  polarity_str, name, drain, gate, source, kp_, w_, l_, vth_, beta_, rdrift_, theta_, vbd_);
}

template<Polarity P>
void MOSFET_SiC<P>::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }
    temperature_ = temp_kelvin;
    spdlog::debug("{} set temperature to {:.2f}K", name_, temperature_);
}

template<Polarity P>
Real MOSFET_SiC<P>::compute_temperature_scaled_vth() const {
    // VTO(T) = VTO(Tnom) + KT1×(T-Tnom) + KT2×(T-Tnom)², of the effective
    // device: |VTO| for P-channel, so KT1 and KT2 move |VTO| for both
    const Real vto = (P == Polarity::N) ? vth_ : std::abs(vth_);
    const Real temp_diff = temperature_ - tnom_;
    return vto + kt1_ * temp_diff + kt2_ * temp_diff * temp_diff;
}

template<Polarity P>
Real MOSFET_SiC<P>::compute_temperature_scaled_kp() const {
    // Temperature scaling for mobility (process transconductance)
    // KP(T) = KP(Tnom) × (T/Tnom)^UTE
    Real temp_ratio = temperature_ / tnom_;
    return kp_ * std::pow(temp_ratio, ute_);
}

template<Polarity P>
Real MOSFET_SiC<P>::compute_temperature_scaled_rdrift() const {
    // SiC-specific: Drift resistance temperature scaling
    // Rd(T) = Rd(Tnom) × (T/Tnom)^RDTEMP
    Real temp_ratio = temperature_ / tnom_;
    return rdrift_ * std::pow(temp_ratio, rdtemp_);
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_gmin(MNASystem& mna) const {
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];
    Real g = gmin_default;

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // D-S conductance
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, g);
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -g);
        }
    }
    if (ns_idx >= 0) {
        mna.G.add_entry(ns_idx, ns_idx, g);
        if (nd_idx >= 0) {
            mna.G.add_entry(ns_idx, nd_idx, -g);
        }
    }

    // G-S conductance
    if (ng_idx >= 0) {
        mna.G.add_entry(ng_idx, ng_idx, g);
        if (ns_idx >= 0) {
            mna.G.add_entry(ng_idx, ns_idx, -g);
        }
    }
    if (ns_idx >= 0) {
        mna.G.add_entry(ns_idx, ns_idx, g);
        if (ng_idx >= 0) {
            mna.G.add_entry(ns_idx, ng_idx, -g);
        }
    }
}

template<Polarity P>
typename MOSFET_SiC<P>::DrainCurrent MOSFET_SiC<P>::drain_current(Real v_gs_eff,
                                                                  Real v_ds_eff) const {
    // Shichman-Hodges with SiC-specific terms, at the device's temperature:
    //   beta_eff = KP(T) W / L / (1 + THETA Vds)
    //   Id0 = beta_eff (Vov Vds - Vds^2 / 2) below Vds = Vov, beta_eff Vov^2 / 2 above
    //   Id = Id0 / (1 + Id0 RDRIFT(T) / Vds), the drop across the drift region
    // For Vds_eff < 0 the source acts as the drain, as in SPICE's MOSFETs:
    // the same current of VGD and VSD flows from source to drain
    const Real kp_temp = compute_temperature_scaled_kp();
    const Real rdrift_temp = compute_temperature_scaled_rdrift();
    const Real beta_temp = kp_temp * w_ / l_;
    const Real vth_eff = get_effective_threshold();

    auto current = [&](Real vgs, Real vds) {
        const Real v_ov = vgs - vth_eff;
        if (v_ov <= 0.0) {
            return 0.0;
        }
        const Real beta_eff = beta_temp / (1.0 + theta_ * vds);
        const Real id0 = (vds < v_ov) ? beta_eff * (v_ov * vds - 0.5 * vds * vds)
                                      : 0.5 * beta_eff * v_ov * v_ov;
        // Id0 / Vds, the channel's conductance, in closed form below
        // saturation so that it holds down to Vds = 0
        const Real g0 = (vds < v_ov) ? beta_eff * (v_ov - 0.5 * vds) : id0 / vds;
        if (rdrift_temp > 0.0) {
            return id0 / (1.0 + g0 * rdrift_temp);
        }
        return id0;
    };

    DrainCurrent dc;
    const bool reverse = v_ds_eff < 0.0;
    const Real vgs = reverse ? v_gs_eff - v_ds_eff : v_gs_eff;  // VGD in reverse
    const Real vds = reverse ? -v_ds_eff : v_ds_eff;           // VSD in reverse
    if (vgs < vth_eff) {
        return dc;  // cutoff
    }
    constexpr Real delta = 1e-6;
    const Real f = current(vgs, vds);
    // The derivatives of this current as they are: with THETA, gds is
    // negative where the mobility falls faster than the channel opens
    const Real gm = (current(vgs + delta, vds) - f) / delta;
    const Real gds = (current(vgs, vds + delta) - f) / delta;
    if (!reverse) {
        dc.id = f;
        dc.gm = gm;
        dc.gds = gds;
    } else {
        // Id = -f(Vgs - Vds, -Vds): its derivatives by Vgs and Vds
        dc.id = -f;
        dc.gm = -gm;
        dc.gds = gm + gds;
    }
    return dc;
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_dc_nonlinear(MNASystem& mna,
                                       const DenseVector& node_voltages) const {
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);
    if (v_ds_eff > vbd_) {
        spdlog::warn("{} Vds={:.3f}V exceeds breakdown voltage Vbd={:.1f}V",
                     name_, v_ds_eff, vbd_);
    }
    const DrainCurrent dc = drain_current(v_gs_eff, v_ds_eff);

    // The current from drain to source is id for N and -id for P; its
    // derivatives with respect to Vg, Vd and Vs are gm, gds and -(gm + gds)
    // for both (device_utils::stamp_vccs_3terminal, stamp_conductance_ds)
    const Real i_ds = (P == Polarity::N) ? dc.id : -dc.id;
    const Real i_eq = i_ds - dc.gm * (vg - vs) - dc.gds * (vd - vs);

    spdlog::trace("{} stamp_nonlinear ({}-ch): Vgs_eff={:.6f}V, Vds_eff={:.6f}V, Id={:.6e}A, "
                  "gm={:.6e}S, gds={:.6e}S",
                  name_, (P == Polarity::N) ? "N" : "P", v_gs_eff, v_ds_eff, dc.id, dc.gm, dc.gds);

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);
    device_utils::stamp_vccs_3terminal(mna.G, nd_idx, ng_idx, ns_idx, dc.gm, 1.0);
    device_utils::stamp_conductance_ds(mna.G, nd_idx, ns_idx, dc.gds);
    if (nd_idx >= 0) mna.b[nd_idx] -= i_eq;
    if (ns_idx >= 0) mna.b[ns_idx] += i_eq;
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_transient(MNASystem& mna,
                                    const DenseVector& node_voltages,
                                    Real timestep,
                                    IntegrationMethod method,
                                    const BDFCoefficients* bdf_coef,
                                    const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Step 1: Stamp DC nonlinear model
    stamp_dc_nonlinear(mna, node_voltages);

    // Step 2: The overlap capacitances CGSO W and CGDO W by backward Euler:
    // the current C / dt (V - V_prev) from the gate to the source and to
    // the drain, with V_prev accepted at the last time point (accept_solution)
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    (void)method;
    auto companion = [&](Real c, int n1, int n2, Real v_prev) {
        if (c <= 0.0) {
            return;
        }
        const Real g = c / timestep;
        device_utils::stamp_capacitor_companion(mna, n1, n2, g, -g * v_prev, 1.0);
    };
    companion(cgso_ * w_, ng_idx, ns_idx, vgs_prev_);
    companion(cgdo_ * w_, ng_idx, nd_idx, vgd_prev_);
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                             const DenseVector& dc_solution) const {
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    Real vd = (nd == NODE_GND) ? 0.0 : dc_solution[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : dc_solution[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : dc_solution[ns - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);
    const DrainCurrent dc = drain_current(v_gs_eff, v_ds_eff);

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // The small-signal current from drain to source, as in the DC stamp
    device_utils::stamp_complex_vccs_3terminal(sys.Y, nd_idx, ng_idx, ns_idx,
                                               Complex(dc.gm, 0.0), 1.0);
    device_utils::stamp_complex_conductance_ds(sys.Y, nd_idx, ns_idx, Complex(dc.gds, 0.0));

    // The overlap capacitances CGSO W and CGDO W
    device_utils::stamp_complex_capacitor(sys.Y, ng_idx, ns_idx,
                                          Complex(0.0, omega * cgso_ * w_), 1.0);
    device_utils::stamp_complex_capacitor(sys.Y, ng_idx, nd_idx,
                                          Complex(0.0, omega * cgdo_ * w_), 1.0);
}

template<Polarity P>
std::vector<NoiseSource> MOSFET_SiC<P>::get_noise_sources(const DenseVector& dc_solution,
                                                          Real frequency) const {
    std::vector<NoiseSource> sources;

    NodeId drain_node = nodes_[0];
    NodeId gate_node = nodes_[1];
    NodeId source_node = nodes_[2];

    Real V_d = (drain_node == NODE_GND) ? 0.0 : dc_solution[drain_node - 1];
    Real V_g = (gate_node == NODE_GND) ? 0.0 : dc_solution[gate_node - 1];
    Real V_s = (source_node == NODE_GND) ? 0.0 : dc_solution[source_node - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(V_d, V_g, V_s, v_gs_eff, v_ds_eff);

    const DrainCurrent dc = drain_current(v_gs_eff, v_ds_eff);
    // The channel's noise in either direction: the current and the
    // transconductance of the device as it conducts
    const Real I_d = std::abs(dc.id);
    const Real g_m = std::abs(dc.gm);

    constexpr Real gm_threshold = 1e-15;

    // Thermal noise
    if (g_m > gm_threshold) {
        Real I_n_th_squared = 4.0 * k_boltzmann * temperature_ * gamma_ * g_m;

        NoiseSource thermal;
        thermal.name = name_ + "_thermal";
        thermal.node_p = drain_node;
        thermal.node_n = source_node;
        thermal.spectral_density = I_n_th_squared;
        thermal.is_current = true;

        sources.push_back(thermal);
    }

    // Flicker noise
    constexpr Real I_threshold = 1e-15;
    if (kf_ > 0.0 && std::abs(I_d) > I_threshold && frequency > 0.0) {
        Real W_L = w_ * l_;
        Real I_n_f_squared = kf_ * std::pow(std::abs(I_d), af_) /
                             (std::pow(frequency, ef_) * std::pow(W_L, ef_));

        NoiseSource flicker;
        flicker.name = name_ + "_flicker";
        flicker.node_p = drain_node;
        flicker.node_n = source_node;
        flicker.spectral_density = I_n_f_squared;
        flicker.is_current = true;

        sources.push_back(flicker);
    }

    return sources;
}

template<Polarity P>
void MOSFET_SiC<P>::reset_state() {
    vgs_prev_ = 0.0;
    vgd_prev_ = 0.0;
    vds_prev_ = 0.0;
}

template<Polarity P>
void MOSFET_SiC<P>::accept_solution(const DenseVector& node_voltages) {
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns);

    Real vgs_new = 0.0;
    if (ng_idx >= 0) vgs_new += node_voltages[ng_idx];
    if (ns_idx >= 0) vgs_new -= node_voltages[ns_idx];

    Real vgd_new = 0.0;
    if (ng_idx >= 0) vgd_new += node_voltages[ng_idx];
    if (nd_idx >= 0) vgd_new -= node_voltages[nd_idx];

    Real vds_new = 0.0;
    if (nd_idx >= 0) vds_new += node_voltages[nd_idx];
    if (ns_idx >= 0) vds_new -= node_voltages[ns_idx];

    vgs_prev_ = vgs_new;
    vgd_prev_ = vgd_new;
    vds_prev_ = vds_new;
}

template<Polarity P>
std::optional<Real> MOSFET_SiC<P>::get_parameter(const std::string& param_name) const {
    if (param_name == "KP" || param_name == "kp") {
        return kp_;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        return vth_;
    } else if (param_name == "W" || param_name == "w") {
        return w_;
    } else if (param_name == "L" || param_name == "l") {
        return l_;
    } else if (param_name == "THETA" || param_name == "theta") {
        return theta_;
    } else if (param_name == "RDRIFT" || param_name == "rdrift" || param_name == "RD") {
        return rdrift_;
    } else if (param_name == "VBD" || param_name == "vbd") {
        return vbd_;
    }
    for (const auto& entry : plain_parameters()) {
        if (lower_case(param_name) == entry.name) return *entry.target;
    }
    return std::nullopt;
}

template<Polarity P>
bool MOSFET_SiC<P>::set_parameter(const std::string& param_name, Real value) {
    const char* polarity_str = (P == Polarity::N) ? "N" : "P";

    if (param_name == "KP" || param_name == "kp") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: KP must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        kp_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        vth_ = value;
        return true;
    } else if (param_name == "W" || param_name == "w") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: W must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        w_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "L" || param_name == "l") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: L must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        l_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "THETA" || param_name == "theta") {
        if (value < 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: THETA must be non-negative (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        theta_ = value;
        return true;
    } else if (param_name == "RDRIFT" || param_name == "rdrift" || param_name == "RD") {
        if (value < 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: RDRIFT must be non-negative (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        rdrift_ = value;
        return true;
    } else if (param_name == "VBD" || param_name == "vbd") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: VBD must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        vbd_ = value;
        return true;
    }
    for (const auto& entry : plain_parameters()) {
        if (lower_case(param_name) == entry.name) {
            if (entry.positive && value <= 0.0) return false;
            *entry.target = value;
            return true;
        }
    }
    return false;
}

template<Polarity P>
std::string MOSFET_SiC<P>::lower_case(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

template<Polarity P>
std::vector<typename MOSFET_SiC<P>::PlainParameter> MOSFET_SiC<P>::plain_parameters() const {
    // The parameters that nothing else is derived from; TNOM in kelvin
    auto* self = const_cast<MOSFET_SiC*>(this);
    return {{"nfs", &self->nfs_, false},     {"cgso", &self->cgso_, false},
            {"cgdo", &self->cgdo_, false},   {"cgbo", &self->cgbo_, false},
            {"cjo", &self->cjo_, false},     {"pb", &self->pb_, true},
            {"mj", &self->mj_, true},        {"kf", &self->kf_, false},
            {"af", &self->af_, true},        {"ef", &self->ef_, true},
            {"gamma", &self->gamma_, false}, {"kt1", &self->kt1_, false},
            {"kt2", &self->kt2_, false},     {"ute", &self->ute_, false},
            {"rdtemp", &self->rdtemp_, false}, {"tnom", &self->tnom_, true}};
}

template<Polarity P>
std::vector<std::string> MOSFET_SiC<P>::get_perturbable_parameters() const {
    return {"KP", "VTO", "W", "L", "THETA", "RDRIFT"};
}

// Explicit template instantiations
template class MOSFET_SiC<Polarity::N>;
template class MOSFET_SiC<Polarity::P>;

} // namespace kirimaspice::devices
