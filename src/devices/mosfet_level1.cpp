// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/mosfet_level1.cpp
// Unified MOSFET Level 1 (Shichman-Hodges) template implementation
#include "kirimaspice/devices/mosfet_level1.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

namespace {

constexpr Real kReferenceTemperature = 300.15;  // [K], of SPICE's temperature laws

// SPICE's band gap of silicon [eV] at temp [K]
Real band_gap(Real temp) {
    return 1.16 - 7.02e-4 * temp * temp / (temp + 1108.0);
}

// SPICE's -3 Vt ln(T/Tref) + Eg(T) - Eg(Tref) T/Tref [V], the temperature
// dependence of a potential (PHI) on the band gap
Real band_gap_potential(Real temp) {
    const Real fact = temp / kReferenceTemperature;
    return -3.0 * core::compute_thermal_voltage(temp) * std::log(fact) + band_gap(temp) -
           band_gap(kReferenceTemperature) * fact;
}

}  // namespace

template<Polarity P>
MOSFET_Level1<P>::MOSFET_Level1(DeviceId id, const std::string& name,
                                  NodeId drain, NodeId gate, NodeId source,
                                  Real kp, Real w, Real l, Real vth,
                                  Real cgso, Real cgdo, Real cgbo,
                                  Real kf, Real af, Real ef, Real gamma,
                                  Real kt1, Real kt2, Real ute, Real tnom)
    : MOSFET_Base(id, name, (P == Polarity::N) ? DeviceType::MOSFET_N : DeviceType::MOSFET_P)
    , nodes_{drain, gate, source}
    , bulk_(source)
    , kp_(kp)
    , w_(w)
    , l_(l)
    , vth_(vth)
    , cgso_(cgso)
    , cgdo_(cgdo)
    , cgbo_(cgbo)
    , kf_(kf)
    , af_(af)
    , ef_(ef)
    , gamma_(gamma)
    , vth_tnom_(vth)
    , kp_tnom_(kp)
    , beta_(kp * w / l) {
    // Set temperature parameters on base class
    kt1_ = kt1;
    kt2_ = kt2;
    ute_ = ute;
    tnom_ = tnom;
    temperature_ = tnom;

    if (kp <= 0.0) {
        throw std::runtime_error("MOSFET process transconductance must be positive: " + name);
    }
    if (w <= 0.0) {
        throw std::runtime_error("MOSFET channel width must be positive: " + name);
    }
    if (l <= 0.0) {
        throw std::runtime_error("MOSFET channel length must be positive: " + name);
    }
    if (cgso < 0.0 || cgdo < 0.0 || cgbo < 0.0) {
        throw std::runtime_error("MOSFET overlap capacitance must be non-negative: " + name);
    }
    set_temperature(tnom);  // the values at TNOM, which set_temperature derives

    spdlog::debug("Created MOSFET_{} {} (D={}, G={}, S={}, Kp={:.6e}A/V², W={:.6e}m, L={:.6e}m, Vth={:.3f}V, β={:.6e}A/V²)",
                  Traits::type_string, name, drain, gate, source, kp_, w_, l_, vth_, beta_);
}

template<Polarity P>
void MOSFET_Level1<P>::set_threshold_voltage(Real vth) {
    vth_tnom_ = vth;  // VTO at TNOM, as set_parameter("VTO") takes it
    set_temperature(temperature_);
    spdlog::debug("{} set threshold voltage to {:.3f}V", name_, vth_);
}

template<Polarity P>
void MOSFET_Level1<P>::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }
    temperature_ = temp_kelvin;

    // SPICE's level 1, with r = T/TNOM, the band gap
    // Eg(T) = 1.16 - 7.02e-4 T^2 / (T + 1108) and, with the reference
    // temperature 300.15 K, p(T) = -3 Vt(T) ln(T/300.15) + Eg(T) - Eg(300.15) T/300.15:
    //   KP(T)  = KP r^UTE (UTE = -1.5, SPICE's exponent)
    //   PHI(T) = r PHI + p(T) - r p(TNOM)
    //   Vbi(T) = VTO - GAMMA sqrt(PHI) + (PHI(T) - PHI)/2 +- (Eg(TNOM) - Eg(T))/2
    //   VTO(T) = Vbi(T) + GAMMA sqrt(PHI(T))
    // in the device's own terms (+ for N, - for P), the threshold at VBS
    // being Vbi(T) + GAMMA sqrt(PHI(T) - VBS) (threshold()). KT1 and KT2,
    // which only the C++ constructor gives (0 from a netlist), add
    // KT1 dT + KT2 dT^2 to the signed VTO. At T = TNOM each change is 0.
    constexpr Real type = (P == Polarity::N) ? 1.0 : -1.0;
    const Real ratio = temp_kelvin / tnom_;
    const Real dt = temp_kelvin - tnom_;
    const Real dphi = (ratio - 1.0) * phi_tnom_ + band_gap_potential(temp_kelvin) -
                      ratio * band_gap_potential(tnom_);
    const Real half_deg = 0.5 * (band_gap(tnom_) - band_gap(temp_kelvin));
    const Real dvto_kt = kt1_ * dt + kt2_ * dt * dt;
    phi_ = phi_tnom_ + dphi;
    vbi_eff_ = type * (vth_tnom_ + half_deg + dvto_kt) - gamma_body_ * std::sqrt(phi_tnom_) +
               0.5 * dphi;
    vth_ = vth_tnom_ + half_deg + dvto_kt +
           type * (gamma_body_ * (std::sqrt(phi_) - std::sqrt(phi_tnom_)) + 0.5 * dphi);

    kp_ = kp_tnom_ * std::pow(ratio, ute_);
    beta_ = kp_ * w_ / l_;

    spdlog::debug("{} set temperature to {:.2f}K (VTO={:.4f}V, PHI={:.4f}V, KP={:.6e}A/V²)",
                  name_, temp_kelvin, vth_, phi_, kp_);
}

template<Polarity P>
void MOSFET_Level1<P>::set_body(NodeId bulk, Real gamma, Real phi, Real lambda) {
    if (gamma < 0.0 || phi <= 0.0 || lambda < 0.0) {
        throw std::runtime_error("MOSFET level 1 needs GAMMA >= 0, PHI > 0 and LAMBDA >= 0: " + name_);
    }
    bulk_ = bulk;
    gamma_body_ = gamma;
    phi_tnom_ = phi;
    lambda_ = lambda;
    nodes_.resize(3);
    if (bulk != nodes_[2]) {
        nodes_.push_back(bulk);
    }
    set_temperature(temperature_);  // PHI(T) and VTO(T) with the new body
}

template<Polarity P>
typename MOSFET_Level1<P>::Threshold MOSFET_Level1<P>::threshold(Real v_bs_eff) const {
    // Vbi + GAMMA sqrt(PHI - VBS), which is VTO + GAMMA (sqrt(PHI - VBS) -
    // sqrt(PHI)), at the present temperature (set_temperature); with the
    // body forward biased, the tangent of the square root at VBS = 0, not
    // below 0, as ngspice-47 gives (VTO 0.7 V, GAMMA 0.5, PHI 0.6 V:
    // 4.182331e-4 A at VBS = 0.3 V for the device of
    // tests/unit/test_mosfet_level1_spice.cpp)
    Real sarg = 0.0;
    Real dsarg = 0.0;
    if (v_bs_eff <= 0.0) {
        sarg = std::sqrt(phi_ - v_bs_eff);
        dsarg = -0.5 / sarg;
    } else {
        const Real sqrt_phi = std::sqrt(phi_);
        sarg = sqrt_phi - v_bs_eff / (2.0 * sqrt_phi);
        dsarg = -0.5 / sqrt_phi;
        if (sarg < 0.0) {
            sarg = 0.0;
            dsarg = 0.0;
        }
    }
    return {vbi_eff_ + gamma_body_ * sarg, gamma_body_ * dsarg};
}

template<Polarity P>
typename MOSFET_Level1<P>::ChannelCurrent MOSFET_Level1<P>::forward_current(Real v_gs_eff,
                                                                           Real v_ds_eff,
                                                                           Real v_bs_eff) const {
    // Shichman-Hodges with channel-length modulation (1 + LAMBDA VDS) in
    // both regions, as SPICE's level 1; gmb = -gm dVth/dVBS
    const Threshold th = threshold(v_bs_eff);
    const Real vov = v_gs_eff - th.vth;
    if (vov <= 0.0) {
        return {0.0, gmin_default, gmin_default, 0.0};  // cutoff
    }
    const Real vds = std::max(v_ds_eff, 0.0);
    const Real clm = 1.0 + lambda_ * vds;
    Real id, gm, gds;
    if (vds < vov) {
        const Real core = vov * vds - 0.5 * vds * vds;
        id = beta_ * core * clm;
        gm = beta_ * vds * clm;
        gds = beta_ * (vov - vds) * clm + beta_ * core * lambda_;
    } else {
        id = 0.5 * beta_ * vov * vov * clm;
        gm = beta_ * vov * clm;
        gds = 0.5 * beta_ * vov * vov * lambda_;
    }
    const Real gmb = -gm * th.dvth_dvbs;
    return {id, std::max(gm, gmin_default), std::max(gds, gmin_default), gmb};
}

template<Polarity P>
typename MOSFET_Level1<P>::Region MOSFET_Level1<P>::get_region(Real v_gs_eff, Real v_ds_eff) const {
    const Real vth_eff = threshold(0.0).vth;
    if (v_gs_eff < vth_eff) {
        return Region::CUTOFF;
    }
    return (v_ds_eff < v_gs_eff - vth_eff) ? Region::LINEAR : Region::SATURATION;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_drain_current(Real v_gs_eff, Real v_ds_eff) const {
    return forward_current(v_gs_eff, v_ds_eff, 0.0).id;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_transconductance(Real v_gs_eff, Real v_ds_eff) const {
    return forward_current(v_gs_eff, v_ds_eff, 0.0).gm;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_output_conductance(Real v_gs_eff, Real v_ds_eff) const {
    return forward_current(v_gs_eff, v_ds_eff, 0.0).gds;
}

template<Polarity P>
void MOSFET_Level1<P>::stamp_gmin(MNASystem& mna) const {
    // Add small conductance gmin between each terminal pair for numerical stability
    NodeId nd = nodes_[0];  // Drain
    NodeId ng = nodes_[1];  // Gate
    NodeId ns = nodes_[2];  // Source
    Real g = gmin_default;

    // D-S conductance
    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

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
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);

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
typename MOSFET_Level1<P>::ChannelCurrent MOSFET_Level1<P>::channel_current(Real v_gs_eff,
                                                                           Real v_ds_eff,
                                                                           Real v_bs_eff) const {
    if (v_ds_eff >= 0.0) {
        return forward_current(v_gs_eff, v_ds_eff, v_bs_eff);
    }
    // Reverse mode: the source acts as the drain. With VGD = VGS - VDS,
    // VSD = -VDS and VBD = VBS - VDS, id = -Id(VGD, VSD, VBD), so
    // d id/d VGS = -gm', d id/d VBS = -gmb' and d id/d VDS = gm' + gds' + gmb'.
    const ChannelCurrent r = forward_current(v_gs_eff - v_ds_eff, -v_ds_eff, v_bs_eff - v_ds_eff);
    return {-r.id, -r.gm, r.gm + r.gds + r.gmb, -r.gmb};
}

template<Polarity P>
void MOSFET_Level1<P>::stamp_dc_nonlinear(MNASystem& mna,
                                           const DenseVector& node_voltages) const {
    NodeId nd = nodes_[0];  // Drain
    NodeId ng = nodes_[1];  // Gate
    NodeId ns = nodes_[2];  // Source

    const NodeId nb = bulk_;

    // Get terminal voltages
    Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];
    Real vb = (nb == NODE_GND) ? 0.0 : node_voltages[nb - 1];

    // Compute effective voltages based on polarity
    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);
    const Real v_bs_eff = effective_vbs(vb, vs);

    // Limit the steps from the last evaluation as SPICE's level 1 does: VGS
    // in forward mode, VGD in reverse mode (core::limit_fet_gate_step), and
    // VDS (core::limit_fet_drain_step); the device is evaluated and
    // linearized at the limited voltages, which equal the iterate at
    // convergence
    if (enable_voltage_limiting_) {
        const Real vth = threshold(v_bs_eff).vth;
        const Real vgs_in = v_gs_eff, vds_in = v_ds_eff;
        // A voltage is derived again only where a limit moved it, so that an
        // unlimited iterate keeps its exact value
        if (vds_last_eval_ >= 0.0) {
            const Real vgs = core::limit_fet_gate_step(vgs_in, vgs_last_eval_, vth);
            const Real vds = (vgs != vgs_in) ? vgs - (vgs_in - vds_in) : vds_in;
            v_gs_eff = vgs;
            v_ds_eff = core::limit_fet_drain_step(vds, vds_last_eval_);
        } else {
            const Real vgd_in = vgs_in - vds_in;
            const Real vgd = core::limit_fet_gate_step(vgd_in, vgs_last_eval_ - vds_last_eval_, vth);
            const Real vds = (vgd != vgd_in) ? vgs_in - vgd : vds_in;
            v_ds_eff = core::limit_fet_drain_step(vds, vds_last_eval_);
            v_gs_eff = (vgd != vgd_in || v_ds_eff != vds_in) ? vgd + v_ds_eff : vgs_in;
        }
        limited_in_last_stamp_ = (v_gs_eff != vgs_in || v_ds_eff != vds_in);
    } else {
        limited_in_last_stamp_ = false;
    }
    vgs_last_eval_ = v_gs_eff;
    vds_last_eval_ = v_ds_eff;

    // Calculate drain current and small-signal parameters (physical values)
    const ChannelCurrent cc = channel_current(v_gs_eff, v_ds_eff, v_bs_eff);
    Real id_physical = cc.id;
    Real gm = cc.gm;
    Real gds = cc.gds;
    Real gmb = cc.gmb;

    // Convert to MNA convention
    // For N-channel: id_mna = id_physical (drain to source is positive)
    // For P-channel: id_mna = -id_physical (current direction reversal for MNA)
    Real id = Traits::current_sign * id_physical;

    // Equivalent current source (for Newton-Raphson)
    // The linearization uses effective voltages, but MNA stamps use terminal voltages
    // For N-channel: Id = gm × (Vg - Vs) + gds × (Vd - Vs) + id_eq
    // For P-channel: -Id = gm × (Vs - Vg) + gds × (Vs - Vd) + id_eq
    //                Id = -gm × (Vs - Vg) - gds × (Vs - Vd) - id_eq
    //                   = gm × (Vg - Vs) + gds × (Vd - Vs) - id_eq
    // So we can use the same stamping pattern with adjusted id_eq

    Real id_eq;
    if constexpr (P == Polarity::N) {
        id_eq = id - gm * v_gs_eff - gds * v_ds_eff - gmb * v_bs_eff;
    } else {
        // For P-channel: id_mna = -id_physical
        // The equivalent current is computed from physical current and effective voltages
        // id_eq = id_mna - gm × (Vg - Vs) - gds × (Vd - Vs)
        //       = -id_physical - gm × (-(Vs - Vg)) - gds × (-(Vs - Vd))
        //       = -id_physical + gm × v_gs_eff + gds × v_ds_eff
        id_eq = id + gm * v_gs_eff + gds * v_ds_eff + gmb * v_bs_eff;
    }

    Region region = get_region(v_gs_eff, v_ds_eff);
    const char* region_str = (region == Region::CUTOFF) ? "CUTOFF" :
                             (region == Region::LINEAR) ? "LINEAR" : "SATURATION";

    spdlog::trace("{} stamp_nonlinear: V_gs_eff={:.6f}V, V_ds_eff={:.6f}V, Id={:.6e}A, gm={:.6e}S, gds={:.6e}S, region={}",
                 name_, v_gs_eff, v_ds_eff, id, gm, gds, region_str);

    // Convert to MNA indices
    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // Stamp transconductance gm (gate-source voltage controls drain current)
    // MNA form: Id = gm × (Vg - Vs) + gds × (Vd - Vs) + id_eq
    if (nd_idx >= 0) {
        if (ng_idx >= 0) {
            mna.G.add_entry(nd_idx, ng_idx, gm);
        }
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -gm);
        }
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) {
            mna.G.add_entry(ns_idx, ng_idx, -gm);
        }
        mna.G.add_entry(ns_idx, ns_idx, gm);
    }

    // Stamp output conductance gds
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, gds);
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -gds);
        }
    }
    if (ns_idx >= 0) {
        if (nd_idx >= 0) {
            mna.G.add_entry(ns_idx, nd_idx, -gds);
        }
        mna.G.add_entry(ns_idx, ns_idx, gds);
    }

    // Stamp the body transconductance gmb (bulk-source voltage)
    const int nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);
    if (gmb != 0.0) {
        if (nd_idx >= 0) {
            if (nb_idx >= 0) mna.G.add_entry(nd_idx, nb_idx, gmb);
            if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -gmb);
        }
        if (ns_idx >= 0) {
            if (nb_idx >= 0) mna.G.add_entry(ns_idx, nb_idx, -gmb);
            mna.G.add_entry(ns_idx, ns_idx, gmb);
        }
    }

    // Stamp RHS (equivalent current source)
    if (nd_idx >= 0) {
        mna.b[nd_idx] -= id_eq;
    }
    if (ns_idx >= 0) {
        mna.b[ns_idx] += id_eq;
    }
}

template<Polarity P>
void MOSFET_Level1<P>::stamp_transient(MNASystem& mna,
                                        const DenseVector& node_voltages,
                                        Real timestep,
                                        IntegrationMethod method,
                                        const BDFCoefficients* bdf_coef,
                                        const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Step 1: Stamp DC nonlinear model
    stamp_dc_nonlinear(mna, node_voltages);

    // Save timestep and method for accept_solution()
    timestep_ = timestep;
    last_method_ = method;

    // The overlap capacitances CGSO W, CGDO W and CGBO L by backward Euler,
    // whatever the method: the current C / dt (V - V_prev) from the first
    // node to the second, with V_prev accepted at the last time point
    // (accept_solution); node k at element k - 1, as the stamps read it
    (void)bdf_coef;
    (void)tr_bdf2_coef;
    const int nd_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0] - 1);
    const int ng_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1] - 1);
    const int ns_idx = (nodes_[2] == NODE_GND) ? -1 : static_cast<int>(nodes_[2] - 1);
    const int nb_idx = (bulk_ == NODE_GND) ? -1 : static_cast<int>(bulk_ - 1);
    auto companion = [&](Real cap, int n1, int n2, Real v_prev) {
        if (cap <= 0.0) {
            return;
        }
        const Real g = cap / timestep;
        device_utils::stamp_capacitor_companion(mna, n1, n2, g, -g * v_prev, 1.0);
    };
    companion(cgso_ * w_, ng_idx, ns_idx, vgs_prev_);
    companion(cgdo_ * w_, ng_idx, nd_idx, vgd_prev_);
    companion(cgbo_ * l_, ng_idx, nb_idx, vgb_prev_);
}

template<Polarity P>
Real MOSFET_Level1<P>::get_gate_source_capacitance(Real v_gs_eff, Real v_ds_eff) const {
    return cgso_ * w_;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_gate_drain_capacitance(Real v_gs_eff, Real v_ds_eff) const {
    return cgdo_ * w_;
}

template<Polarity P>
void MOSFET_Level1<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                                 const DenseVector& dc_solution) const {
    NodeId nd_node = nodes_[0];
    NodeId ng_node = nodes_[1];
    NodeId ns_node = nodes_[2];

    const NodeId nb_node = bulk_;

    // Get DC operating point voltages
    Real v_d = (nd_node == NODE_GND) ? 0.0 : dc_solution[nd_node - 1];
    Real v_g = (ng_node == NODE_GND) ? 0.0 : dc_solution[ng_node - 1];
    Real v_s = (ns_node == NODE_GND) ? 0.0 : dc_solution[ns_node - 1];
    Real v_b = (nb_node == NODE_GND) ? 0.0 : dc_solution[nb_node - 1];

    // Compute effective voltages
    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(v_d, v_g, v_s, v_gs_eff, v_ds_eff);

    // Calculate small-signal parameters
    const ChannelCurrent cc = channel_current(v_gs_eff, v_ds_eff, effective_vbs(v_b, v_s));
    Real gm = cc.gm;
    Real gds = cc.gds;
    Real gmb = cc.gmb;
    Real cgs = get_gate_source_capacitance(v_gs_eff, v_ds_eff);
    Real cgd = get_gate_drain_capacitance(v_gs_eff, v_ds_eff);

    spdlog::trace("{} stamp_ac: V_gs_eff={:.6f}V, V_ds_eff={:.6f}V, Gm={:.6e}S, gds={:.6e}S, Cgs={:.6e}F, Cgd={:.6e}F",
                 name_, v_gs_eff, v_ds_eff, gm, gds, cgs, cgd);

    // Convert to MNA indices
    auto nd_idx = (nd_node == NODE_GND) ? -1 : static_cast<int>(nd_node - 1);
    auto ng_idx = (ng_node == NODE_GND) ? -1 : static_cast<int>(ng_node - 1);
    auto ns_idx = (ns_node == NODE_GND) ? -1 : static_cast<int>(ns_node - 1);

    // Admittances
    Complex ygs(0.0, omega * cgs);
    Complex ygd(0.0, omega * cgd);
    Complex yds(gds, 0.0);
    Complex gmvgs(gm, 0.0);

    // Stamp G-S capacitance
    if (ng_idx >= 0) {
        sys.Y.add_entry(ng_idx, ng_idx, ygs);
        if (ns_idx >= 0) {
            sys.Y.add_entry(ng_idx, ns_idx, -ygs);
        }
    }
    if (ns_idx >= 0) {
        sys.Y.add_entry(ns_idx, ns_idx, ygs);
        if (ng_idx >= 0) {
            sys.Y.add_entry(ns_idx, ng_idx, -ygs);
        }
    }

    // Stamp G-D capacitance
    if (ng_idx >= 0) {
        sys.Y.add_entry(ng_idx, ng_idx, ygd);
        if (nd_idx >= 0) {
            sys.Y.add_entry(ng_idx, nd_idx, -ygd);
        }
    }
    if (nd_idx >= 0) {
        sys.Y.add_entry(nd_idx, nd_idx, ygd);
        if (ng_idx >= 0) {
            sys.Y.add_entry(nd_idx, ng_idx, -ygd);
        }
    }

    // Stamp D-S output conductance
    if (nd_idx >= 0) {
        sys.Y.add_entry(nd_idx, nd_idx, yds);
        if (ns_idx >= 0) {
            sys.Y.add_entry(nd_idx, ns_idx, -yds);
        }
    }
    if (ns_idx >= 0) {
        sys.Y.add_entry(ns_idx, ns_idx, yds);
        if (nd_idx >= 0) {
            sys.Y.add_entry(ns_idx, nd_idx, -yds);
        }
    }

    // Stamp controlled current source: Id = Gm × (Vg - Vs)
    // This uses MNA convention for current direction
    if (nd_idx >= 0) {
        if (ng_idx >= 0) {
            sys.Y.add_entry(nd_idx, ng_idx, +gmvgs);
        }
        if (ns_idx >= 0) {
            sys.Y.add_entry(nd_idx, ns_idx, -gmvgs);
        }
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) {
            sys.Y.add_entry(ns_idx, ng_idx, -gmvgs);
        }
        sys.Y.add_entry(ns_idx, ns_idx, +gmvgs);
    }

    // Stamp the body transconductance: gmb × (Vb - Vs)
    const int nb_idx = (nb_node == NODE_GND) ? -1 : static_cast<int>(nb_node - 1);
    if (gmb != 0.0) {
        const Complex y_mb(gmb, 0.0);
        if (nd_idx >= 0) {
            if (nb_idx >= 0) sys.Y.add_entry(nd_idx, nb_idx, y_mb);
            if (ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, -y_mb);
        }
        if (ns_idx >= 0) {
            if (nb_idx >= 0) sys.Y.add_entry(ns_idx, nb_idx, -y_mb);
            sys.Y.add_entry(ns_idx, ns_idx, y_mb);
        }
    }
}

template<Polarity P>
std::vector<NoiseSource> MOSFET_Level1<P>::get_noise_sources(const DenseVector& dc_solution,
                                                               Real frequency) const {
    std::vector<NoiseSource> sources;

    NodeId drain_node = nodes_[0];
    NodeId gate_node = nodes_[1];
    NodeId source_node = nodes_[2];

    Real V_d = (drain_node == NODE_GND) ? 0.0 : dc_solution[drain_node - 1];
    Real V_g = (gate_node == NODE_GND) ? 0.0 : dc_solution[gate_node - 1];
    Real V_s = (source_node == NODE_GND) ? 0.0 : dc_solution[source_node - 1];

    const Real V_b = (bulk_ == NODE_GND) ? 0.0 : dc_solution[bulk_ - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(V_d, V_g, V_s, v_gs_eff, v_ds_eff);

    const ChannelCurrent cc = channel_current(v_gs_eff, v_ds_eff, effective_vbs(V_b, V_s));
    Real I_d = std::abs(cc.id);
    Real g_m = std::abs(cc.gm);

    constexpr Real gm_threshold = 1e-15;

    // Channel thermal noise
    if (g_m > gm_threshold) {
        Real I_n_th_squared = 4.0 * k_boltzmann * T_nominal * gamma_ * g_m;

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
void MOSFET_Level1<P>::save_state() {
    MOSFET_Base::save_state();
    vgs_last_eval_saved_ = vgs_last_eval_;
    vds_last_eval_saved_ = vds_last_eval_;
}

template<Polarity P>
void MOSFET_Level1<P>::restore_state() {
    MOSFET_Base::restore_state();
    vgs_last_eval_ = vgs_last_eval_saved_;
    vds_last_eval_ = vds_last_eval_saved_;
}

template<Polarity P>
void MOSFET_Level1<P>::reset_state() {
    MOSFET_Base::reset_state();
    vgs_last_eval_ = vds_last_eval_ = 0.0;
    vgs_last_eval_saved_ = vds_last_eval_saved_ = 0.0;
    limited_in_last_stamp_ = false;
}

template<Polarity P>
void MOSFET_Level1<P>::accept_solution(const DenseVector& node_voltages) {
    const int nd_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0]);
    const int ng_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1]);
    const int ns_idx = (nodes_[2] == NODE_GND) ? -1 : static_cast<int>(nodes_[2]);

    // G-S junction
    Real vgs_new = 0.0;
    if (ng_idx >= 0) vgs_new += node_voltages[ng_idx];
    if (ns_idx >= 0) vgs_new -= node_voltages[ns_idx];

    if (cgso_ > 0.0) {
        Real cgs = cgso_ * w_;
        charge_gs_prev_ = charge_gs_;
        charge_gs_ = cgs * vgs_new;
    }

    vgs_prev3_ = vgs_prev2_;
    vgs_prev2_ = vgs_prev_;
    vgs_prev_ = vgs_new;

    // G-D junction
    Real vgd_new = 0.0;
    if (ng_idx >= 0) vgd_new += node_voltages[ng_idx];
    if (nd_idx >= 0) vgd_new -= node_voltages[nd_idx];

    if (cgdo_ > 0.0) {
        Real cgd = cgdo_ * w_;
        charge_gd_prev_ = charge_gd_;
        charge_gd_ = cgd * vgd_new;
    }

    vgd_prev3_ = vgd_prev2_;
    vgd_prev2_ = vgd_prev_;
    vgd_prev_ = vgd_new;

    // G-B overlap, to the bulk
    Real vgb_new = 0.0;
    if (ng_idx >= 0) vgb_new += node_voltages[ng_idx];
    if (bulk_ != NODE_GND) vgb_new -= node_voltages[bulk_];

    if (cgbo_ > 0.0) {
        Real cgb = cgbo_ * l_;
        charge_gb_prev_ = charge_gb_;
        charge_gb_ = cgb * vgb_new;
    }

    vgb_prev3_ = vgb_prev2_;
    vgb_prev2_ = vgb_prev_;
    vgb_prev_ = vgb_new;

    first_accept_ = false;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    if (!is_dynamic()) {
        return 0.0;
    }

    const int nd_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0]);
    const int ng_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1]);
    const int ns_idx = (nodes_[2] == NODE_GND) ? -1 : static_cast<int>(nodes_[2]);

    Real total_lte = 0.0;

    // G-S junction LTE
    if (cgso_ > 0.0) {
        Real vgs_now = 0.0;
        if (ng_idx >= 0) vgs_now += node_voltages[ng_idx];
        if (ns_idx >= 0) vgs_now -= node_voltages[ns_idx];

        Real cgs = cgso_ * w_;
        Real charge_gs_now = cgs * vgs_now;
        Real delta_q_gs = std::abs(charge_gs_now - charge_gs_prev_);
        Real q_scale_gs = std::max(std::abs(charge_gs_prev_), std::abs(charge_gs_now));
        Real lte_gs = delta_q_gs / (chgtol + q_scale_gs);
        total_lte += lte_gs * lte_gs;
    }

    // G-D junction LTE
    if (cgdo_ > 0.0) {
        Real vgd_now = 0.0;
        if (ng_idx >= 0) vgd_now += node_voltages[ng_idx];
        if (nd_idx >= 0) vgd_now -= node_voltages[nd_idx];

        Real cgd = cgdo_ * w_;
        Real charge_gd_now = cgd * vgd_now;
        Real delta_q_gd = std::abs(charge_gd_now - charge_gd_prev_);
        Real q_scale_gd = std::max(std::abs(charge_gd_prev_), std::abs(charge_gd_now));
        Real lte_gd = delta_q_gd / (chgtol + q_scale_gd);
        total_lte += lte_gd * lte_gd;
    }

    // G-B junction LTE
    if (cgbo_ > 0.0) {
        Real vgb_now = 0.0;
        if (ng_idx >= 0) vgb_now += node_voltages[ng_idx];
        if (bulk_ != NODE_GND) vgb_now -= node_voltages[bulk_];

        Real cgb = cgbo_ * l_;
        Real charge_gb_now = cgb * vgb_now;
        Real delta_q_gb = std::abs(charge_gb_now - charge_gb_prev_);
        Real q_scale_gb = std::max(std::abs(charge_gb_prev_), std::abs(charge_gb_now));
        Real lte_gb = delta_q_gb / (chgtol + q_scale_gb);
        total_lte += lte_gb * lte_gb;
    }

    return std::sqrt(total_lte);
}

template<Polarity P>
std::optional<Real> MOSFET_Level1<P>::get_parameter(const std::string& param_name) const {
    if (param_name == "KP" || param_name == "kp") {
        return kp_tnom_;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        return vth_tnom_;
    } else if (param_name == "W" || param_name == "w") {
        return w_;
    } else if (param_name == "L" || param_name == "l") {
        return l_;
    }

    return std::nullopt;
}

template<Polarity P>
bool MOSFET_Level1<P>::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "KP" || param_name == "kp") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_{} {}: KP must be positive (got {}), not updated",
                        Traits::type_string, name_, value);
            return false;
        }
        kp_tnom_ = value;
        set_temperature(temperature_);  // KP(T) and beta
        spdlog::trace("MOSFET_{} {}: set KP = {} A/V², beta = {} A/V²",
                     Traits::type_string, name_, kp_tnom_, beta_);
        return true;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        vth_tnom_ = value;
        set_temperature(temperature_);  // VTO(T)
        spdlog::trace("MOSFET_{} {}: set VTO = {} V", Traits::type_string, name_, vth_tnom_);
        return true;
    } else if (param_name == "W" || param_name == "w") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_{} {}: W must be positive (got {}), not updated",
                        Traits::type_string, name_, value);
            return false;
        }
        w_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "L" || param_name == "l") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_{} {}: L must be positive (got {}), not updated",
                        Traits::type_string, name_, value);
            return false;
        }
        l_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    }

    return false;
}

template<Polarity P>
std::vector<std::string> MOSFET_Level1<P>::get_perturbable_parameters() const {
    return {"KP", "VTO", "W", "L"};
}

// Explicit template instantiations
template class MOSFET_Level1<Polarity::N>;
template class MOSFET_Level1<Polarity::P>;

} // namespace kirimaspice::devices
