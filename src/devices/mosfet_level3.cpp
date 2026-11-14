// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/mosfet_level3.cpp
// Unified MOSFET Level 3 template implementation
#include "kirimaspice/devices/mosfet_level3.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace kirimaspice::devices {

template<Polarity P>
MOSFET_Level3<P>::MOSFET_Level3(DeviceId id, const std::string& name,
                                 NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                                 const MOSFETLevel3Params& params)
    : Device(id, name, device_type)
    , nodes_{drain, gate, source, bulk}
    , params_(params)
    , cox_(0.0)
    , beta_(0.0)
    , fs_(0.0)
{
    // Validate parameters
    if (!params_.validate()) {
        throw std::runtime_error("MOSFET_Level3: Invalid parameters for " + name);
    }

    // Calculate derived parameters
    Real leff = params_.get_leff();
    Real weff = params_.get_weff();

    // Oxide capacitance per unit area: Cox = εox / tox [F/m^2]
    cox_ = eps_ox / params_.tox;

    // Transconductance parameter: β = μ × Cox × W/L [A/V^2]
    // Note: uo is in cm^2/V-s, need to convert to m^2/V-s
    Real uo_si = params_.uo * 1e-4;  // cm^2/V-s → m^2/V-s
    beta_ = uo_si * cox_ * (weff / leff);

    // Surface potential coefficient (for body effect)
    // fs = sqrt(2 × q × εsi × Nsub) / Cox
    if (params_.nsub > 0.0) {
        Real nsub_si = params_.nsub * 1e6;  // cm^-3 → m^-3
        fs_ = std::sqrt(2.0 * q_electron * eps_si * nsub_si) / cox_;
    } else {
        fs_ = 0.0;
    }

    spdlog::debug("Created MOSFET_Level3 {} ({}-channel) (D={}, G={}, S={}, B={}, "
                 "W={:.3e}m, L={:.3e}m, Leff={:.3e}m, Weff={:.3e}m, "
                 "Cox={:.3e}F/m^2, β={:.3e}A/V^2)",
                 name, Traits::type_string, drain, gate, source, bulk,
                 params_.w, params_.l, leff, weff, cox_, beta_);
}

template<Polarity P>
Real MOSFET_Level3<P>::get_threshold_voltage(Real v_bs_eff) const {
    // Threshold voltage with body effect:
    // For N-channel: Vth(Vbs) = Vto + γ × (sqrt(φ - Vbs) - sqrt(φ))
    // For P-channel: |Vth|(Vsb) = |Vto| + γ × (sqrt(φ - Vsb) - sqrt(φ))

    Real gamma = params_.gamma;
    Real phi = params_.phi;

    // If gamma not specified, calculate from fs
    if (gamma == 0.0 && fs_ > 0.0) {
        gamma = fs_;
    }

    // v_bs_eff is VBS for N and VSB for P (compute_effective_voltages), so
    // that the P-channel device is the mirror of the N-channel one: a
    // reverse body bias (VBS < 0 for N, VSB < 0 for P) raises |Vth| in both
    Real sqrt_phi = std::sqrt(std::max(phi, 1e-6));
    Real sqrt_phi_vbs = std::sqrt(std::max(phi - v_bs_eff, 1e-6));

    Real vth = get_effective_threshold() + gamma * (sqrt_phi_vbs - sqrt_phi);

    // Narrow-width effect correction (delta parameter)
    if (params_.delta > 0.0) {
        Real weff = params_.get_weff();
        vth += params_.delta * std::sqrt(std::max(phi - v_bs_eff, 0.0)) / weff;
    }

    return vth;
}

template<Polarity P>
Real MOSFET_Level3<P>::get_effective_mobility(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    // Effective mobility with degradation:
    // μeff = μo / (1 + θ × (Vgs_eff - Vth))

    (void)v_ds_eff;  // Unused for now
    Real vth = get_threshold_voltage(v_bs_eff);
    Real vov = v_gs_eff - vth;  // Overdrive voltage

    if (vov <= 0.0) {
        return params_.uo * 1e-4;  // Below threshold, use nominal mobility
    }

    Real mu_eff = (params_.uo * 1e-4) / (1.0 + params_.theta * vov);
    return mu_eff;
}

template<Polarity P>
Real MOSFET_Level3<P>::get_vdsat(Real v_gs_eff, Real v_bs_eff) const {
    // Saturation voltage with velocity saturation

    Real vth = get_threshold_voltage(v_bs_eff);
    Real vov = v_gs_eff - vth;

    if (vov <= 0.0) {
        return 0.0;  // Below threshold
    }

    Real leff = params_.get_leff();
    Real vdsat = vov;

    // Short-channel effect (kappa parameter)
    if (params_.kappa > 0.0 && leff > 0.0) {
        vdsat = vov / (1.0 + params_.kappa * vov / leff);
    }

    // Velocity saturation (vmax parameter)
    if (params_.vmax > 0.0) {
        Real mu_eff = get_effective_mobility(v_gs_eff, vdsat, v_bs_eff);
        Real vdsat_vel = params_.vmax * leff / mu_eff;
        vdsat = std::min(vdsat, vdsat_vel);
    }

    return vdsat;
}

template<Polarity P>
Real MOSFET_Level3<P>::get_channel_length_modulation(Real v_ds_eff, Real vdsat) const {
    // Channel length modulation:
    // f_CLM = 1 + λ × (Vds - Vdsat)

    if (params_.lambda > 0.0) {
        return 1.0 + params_.lambda * (v_ds_eff - vdsat);
    } else if (params_.eta > 0.0) {
        return 1.0 + params_.eta * (v_ds_eff - vdsat);
    }

    return 1.0;
}

template<Polarity P>
typename MOSFET_Level3<P>::Region MOSFET_Level3<P>::get_region(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real vth = get_threshold_voltage(v_bs_eff);

    if (v_gs_eff < vth) {
        return Region::CUTOFF;
    }

    Real vdsat = get_vdsat(v_gs_eff, v_bs_eff);

    if (v_ds_eff < vdsat) {
        return Region::LINEAR;
    } else {
        return Region::SATURATION;
    }
}

template<Polarity P>
Real MOSFET_Level3<P>::get_drain_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    // MOSFET Level 3 drain current model (returns |Ids|)

    Real vth = get_threshold_voltage(v_bs_eff);

    if (v_gs_eff < vth) {
        return 0.0;  // Cutoff
    }

    Real vov = v_gs_eff - vth;
    Real vdsat = get_vdsat(v_gs_eff, v_bs_eff);

    // Update beta with mobility degradation
    Real mu_eff = get_effective_mobility(v_gs_eff, v_ds_eff, v_bs_eff);
    Real leff = params_.get_leff();
    Real weff = params_.get_weff();
    Real beta_eff = mu_eff * cox_ * (weff / leff);

    Real ids = 0.0;

    if (v_ds_eff < vdsat) {
        // Linear region
        ids = beta_eff * (vov * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
    } else {
        // Saturation region
        ids = 0.5 * beta_eff * vdsat * vdsat;
        Real f_clm = get_channel_length_modulation(v_ds_eff, vdsat);
        ids *= f_clm;
    }

    return ids;  // Always positive
}

template<Polarity P>
Real MOSFET_Level3<P>::get_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    // Transconductance: gm = dIds/dVgs_eff
    constexpr Real h = 1e-6;
    Real ids_plus = get_drain_current(v_gs_eff + h, v_ds_eff, v_bs_eff);
    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gm = (ids_plus - ids) / h;
    return std::max(gm, 0.0);
}

template<Polarity P>
Real MOSFET_Level3<P>::get_output_conductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    // Output conductance: gds = dIds/dVds_eff
    constexpr Real h = 1e-6;
    Real ids_plus = get_drain_current(v_gs_eff, v_ds_eff + h, v_bs_eff);
    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gds = (ids_plus - ids) / h;
    return std::max(gds, 0.0);
}

template<Polarity P>
Real MOSFET_Level3<P>::get_bulk_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    // Bulk transconductance: gmb = dIds/dVbs_eff
    constexpr Real h = 1e-6;
    Real ids_plus = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff + h);
    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gmb = (ids_plus - ids) / h;
    return gmb;  // Can be negative
}

template<Polarity P>
void MOSFET_Level3<P>::stamp_dc_linear(MNASystem& mna, const DenseVector& /*node_voltages*/) const {
    // Nonlinear device: only stamp Gmin in linear phase
    stamp_gmin(mna);
}

namespace {

// The current from drain to source has the derivatives gm, gds, gmb and
// -(gm + gds + gmb) with respect to the gate, drain, bulk and source voltages,
// for both polarities: for P it is -Ids(VSG, VSD, VSB), whose effective
// voltages are the negatives of VGS, VDS and VBS.
template <typename Matrix, typename T>
void stamp_channel(Matrix& m, std::uint32_t d, std::uint32_t g, std::uint32_t s, std::uint32_t b,
                   T gm, T gds, T gmb) {
    const T gss = gm + gds + gmb;
    auto add = [&](std::uint32_t row, std::uint32_t col, T value) {
        if (row != NODE_INVALID && col != NODE_INVALID) m.add_entry(row, col, value);
    };
    add(d, g, gm);
    add(d, d, gds);
    add(d, b, gmb);
    add(d, s, -gss);
    add(s, g, -gm);
    add(s, d, -gds);
    add(s, b, -gmb);
    add(s, s, gss);
}

std::uint32_t row_of(NodeId node) {
    return (node == NODE_GND) ? NODE_INVALID : static_cast<std::uint32_t>(node - 1);
}

}  // namespace

template<Polarity P>
void MOSFET_Level3<P>::terminal_voltages(const DenseVector& v, Real& vd, Real& vg, Real& vs,
                                         Real& vb) const {
    // node k at element k - 1, as the stamps receive the iterate
    vd = (nodes_[0] == NODE_GND) ? 0.0 : v[nodes_[0] - 1];
    vg = (nodes_[1] == NODE_GND) ? 0.0 : v[nodes_[1] - 1];
    vs = (nodes_[2] == NODE_GND) ? 0.0 : v[nodes_[2] - 1];
    vb = (nodes_[3] == NODE_GND) ? 0.0 : v[nodes_[3] - 1];
}

template<Polarity P>
void MOSFET_Level3<P>::stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const {
    Real vd = 0.0, vg = 0.0, vs = 0.0, vb = 0.0;
    terminal_voltages(node_voltages, vd, vg, vs, vb);
    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    const Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);
    const Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    const Real gds = get_output_conductance(v_gs_eff, v_ds_eff, v_bs_eff);
    const Real gmb = get_bulk_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);

    // Ids flows from drain to source for N and from source to drain for P
    constexpr Real sign = (P == Polarity::N) ? 1.0 : -1.0;
    const Real i_ds = sign * ids;
    const Real i_eq = i_ds - gm * (vg - vs) - gds * (vd - vs) - gmb * (vb - vs);

    const std::uint32_t d = row_of(nodes_[0]);
    const std::uint32_t g = row_of(nodes_[1]);
    const std::uint32_t s = row_of(nodes_[2]);
    const std::uint32_t b = row_of(nodes_[3]);
    stamp_channel(mna.G, d, g, s, b, gm, gds, gmb);
    if (d != NODE_INVALID) mna.b[d] -= i_eq;
    if (s != NODE_INVALID) mna.b[s] += i_eq;
}

template<Polarity P>
void MOSFET_Level3<P>::stamp_gmin(MNASystem& mna) const {
    std::uint32_t d = (nodes_[0] == NODE_GND) ? NODE_INVALID : (nodes_[0] - 1);
    std::uint32_t g = (nodes_[1] == NODE_GND) ? NODE_INVALID : (nodes_[1] - 1);
    std::uint32_t s = (nodes_[2] == NODE_GND) ? NODE_INVALID : (nodes_[2] - 1);
    std::uint32_t b = (nodes_[3] == NODE_GND) ? NODE_INVALID : (nodes_[3] - 1);

    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs = {
        {d, s}, {d, g}, {d, b}, {s, g}, {s, b}, {g, b}
    };

    for (const auto& [n1, n2] : pairs) {
        if (n1 != NODE_INVALID && n2 != NODE_INVALID) {
            mna.G.add_entry(n1, n1, gmin_default);
            mna.G.add_entry(n1, n2, -gmin_default);
            mna.G.add_entry(n2, n1, -gmin_default);
            mna.G.add_entry(n2, n2, gmin_default);
        }
    }
}

template<Polarity P>
void MOSFET_Level3<P>::stamp_transient(MNASystem& mna,
                                        const DenseVector& node_voltages,
                                        Real timestep,
                                        IntegrationMethod method,
                                        const BDFCoefficients* bdf_coef,
                                        const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    (void)bdf_coef;
    (void)tr_bdf2_coef;

    // First, stamp DC nonlinear behavior
    stamp_dc_nonlinear(mna, node_voltages);

    // TODO: Full capacitance model (Meyer/Ward) to be implemented
    // For now, simplified transient stamp

    spdlog::trace("{}: transient stamp at timestep={:.3e}s (method={}, simplified)",
                 name_, timestep, static_cast<int>(method));
}

template<Polarity P>
void MOSFET_Level3<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                                 const DenseVector& dc_solution) const {
    // Extract DC operating point
    Real vd = 0.0, vg = 0.0, vs = 0.0, vb = 0.0;
    terminal_voltages(dc_solution, vd, vg, vs, vb);

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    // Get small-signal parameters
    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gds = get_output_conductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gmb = get_bulk_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);

    // Get capacitances (simplified - overlap only)
    Real cgs = params_.cgso * params_.get_weff();
    Real cgd = params_.cgdo * params_.get_weff();
    Real cgb = params_.cgbo * params_.get_leff();

    std::uint32_t d = (nodes_[0] == NODE_GND) ? NODE_INVALID : (nodes_[0] - 1);
    std::uint32_t g = (nodes_[1] == NODE_GND) ? NODE_INVALID : (nodes_[1] - 1);
    std::uint32_t s = (nodes_[2] == NODE_GND) ? NODE_INVALID : (nodes_[2] - 1);
    std::uint32_t b = (nodes_[3] == NODE_GND) ? NODE_INVALID : (nodes_[3] - 1);

    Complex j_omega(0.0, omega);

    // The small-signal current from drain to source, as in the DC stamp
    stamp_channel(sys.Y, d, g, s, b, Complex(gm, 0.0), Complex(gds, 0.0), Complex(gmb, 0.0));

    // Stamp capacitances: Y = jωC
    if (g != NODE_INVALID && s != NODE_INVALID && cgs > 0.0) {
        Complex y_cgs = j_omega * cgs;
        sys.Y.add_entry(g, g, y_cgs);
        sys.Y.add_entry(g, s, -y_cgs);
        sys.Y.add_entry(s, g, -y_cgs);
        sys.Y.add_entry(s, s, y_cgs);
    }

    if (g != NODE_INVALID && d != NODE_INVALID && cgd > 0.0) {
        Complex y_cgd = j_omega * cgd;
        sys.Y.add_entry(g, g, y_cgd);
        sys.Y.add_entry(g, d, -y_cgd);
        sys.Y.add_entry(d, g, -y_cgd);
        sys.Y.add_entry(d, d, y_cgd);
    }

    if (g != NODE_INVALID && b != NODE_INVALID && cgb > 0.0) {
        Complex y_cgb = j_omega * cgb;
        sys.Y.add_entry(g, g, y_cgb);
        sys.Y.add_entry(g, b, -y_cgb);
        sys.Y.add_entry(b, g, -y_cgb);
        sys.Y.add_entry(b, b, y_cgb);
    }
}

template<Polarity P>
std::vector<NoiseSource> MOSFET_Level3<P>::get_noise_sources(const DenseVector& dc_solution,
                                                               Real frequency) const {
    std::vector<NoiseSource> sources;

    NodeId drain_node = nodes_[0];
    NodeId gate_node = nodes_[1];
    NodeId source_node = nodes_[2];
    NodeId bulk_node = nodes_[3];

    Real vd = (drain_node == NODE_GND) ? 0.0 : dc_solution[drain_node - 1];
    Real vg = (gate_node == NODE_GND) ? 0.0 : dc_solution[gate_node - 1];
    Real vs = (source_node == NODE_GND) ? 0.0 : dc_solution[source_node - 1];
    Real vb = (bulk_node == NODE_GND) ? 0.0 : dc_solution[bulk_node - 1];

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);

    constexpr Real gm_threshold = 1e-15;
    constexpr Real I_threshold = 1e-15;

    // Channel thermal noise: I²n = 4kT × (8/3) × gm
    if (gm > gm_threshold) {
        Real thermal_psd = 4.0 * k_boltzmann * 300.15 * (8.0 / 3.0) * gm;

        NoiseSource thermal;
        thermal.name = name_ + "_thermal";
        thermal.node_p = drain_node;
        thermal.node_n = source_node;
        thermal.spectral_density = thermal_psd;
        thermal.is_current = true;
        sources.push_back(thermal);
    }

    // Flicker noise: I²n = KF × |Ids|^AF / (Cox × Leff × Weff × f)
    // Note: Standard 1/f noise uses frequency exponent = 1.0
    if (params_.kf > 0.0 && std::abs(ids) > I_threshold && frequency > 0.0) {
        Real leff = params_.get_leff();
        Real weff = params_.get_weff();
        Real flicker_psd = params_.kf * std::pow(std::abs(ids), params_.af) /
                          (cox_ * leff * weff * frequency);  // 1/f noise

        NoiseSource flicker;
        flicker.name = name_ + "_flicker";
        flicker.node_p = drain_node;
        flicker.node_n = source_node;
        flicker.spectral_density = flicker_psd;
        flicker.is_current = true;
        sources.push_back(flicker);
    }

    return sources;
}

template<Polarity P>
void MOSFET_Level3<P>::save_state() {
    v1_saved_ = v1_prev_;
    v1_saved2_ = v1_prev2_;
    v1_saved3_ = v1_prev3_;
    v2_saved_ = v2_prev_;
    v2_saved2_ = v2_prev2_;
    v2_saved3_ = v2_prev3_;
    v3_saved_ = v3_prev_;
    v3_saved2_ = v3_prev2_;
    v3_saved3_ = v3_prev3_;
    vbs_saved_ = vbs_prev_;
    vbs_saved2_ = vbs_prev2_;
    vbs_saved3_ = vbs_prev3_;
    vbd_saved_ = vbd_prev_;
    vbd_saved2_ = vbd_prev2_;
    vbd_saved3_ = vbd_prev3_;
    timestep_saved_ = timestep_;
    method_saved_ = last_method_;
}

template<Polarity P>
void MOSFET_Level3<P>::restore_state() {
    v1_prev_ = v1_saved_;
    v1_prev2_ = v1_saved2_;
    v1_prev3_ = v1_saved3_;
    v2_prev_ = v2_saved_;
    v2_prev2_ = v2_saved2_;
    v2_prev3_ = v2_saved3_;
    v3_prev_ = v3_saved_;
    v3_prev2_ = v3_saved2_;
    v3_prev3_ = v3_saved3_;
    vbs_prev_ = vbs_saved_;
    vbs_prev2_ = vbs_saved2_;
    vbs_prev3_ = vbs_saved3_;
    vbd_prev_ = vbd_saved_;
    vbd_prev2_ = vbd_saved2_;
    vbd_prev3_ = vbd_saved3_;
    timestep_ = timestep_saved_;
    last_method_ = method_saved_;
}

template<Polarity P>
void MOSFET_Level3<P>::accept_solution(const DenseVector& node_voltages) {
    (void)node_voltages;

    if (first_accept_) {
        first_accept_ = false;
        return;
    }

    // Shift voltage histories
    v1_prev3_ = v1_prev2_;
    v1_prev2_ = v1_prev_;

    v2_prev3_ = v2_prev2_;
    v2_prev2_ = v2_prev_;

    v3_prev3_ = v3_prev2_;
    v3_prev2_ = v3_prev_;

    vbs_prev3_ = vbs_prev2_;
    vbs_prev2_ = vbs_prev_;

    vbd_prev3_ = vbd_prev2_;
    vbd_prev2_ = vbd_prev_;

    // Update charge for physical LTE
    charge_1_prev_ = charge_1_;
    charge_2_prev_ = charge_2_;
    charge_3_prev_ = charge_3_;
    charge_bs_prev_ = charge_bs_;
    charge_bd_prev_ = charge_bd_;
}

template<Polarity P>
Real MOSFET_Level3<P>::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    (void)node_voltages;

    Real lte_1 = std::abs(charge_1_ - charge_1_prev_) / chgtol;
    Real lte_2 = std::abs(charge_2_ - charge_2_prev_) / chgtol;
    Real lte_3 = std::abs(charge_3_ - charge_3_prev_) / chgtol;
    Real lte_bs = std::abs(charge_bs_ - charge_bs_prev_) / chgtol;
    Real lte_bd = std::abs(charge_bd_ - charge_bd_prev_) / chgtol;

    return lte_1 + lte_2 + lte_3 + lte_bs + lte_bd;
}

template<Polarity P>
std::optional<Real> MOSFET_Level3<P>::get_parameter(const std::string& param_name) const {
    if (param_name == "VTO" || param_name == "vto" || param_name == "VTH" || param_name == "vth") {
        return params_.vto;
    } else if (param_name == "UO" || param_name == "uo") {
        return params_.uo;
    } else if (param_name == "THETA" || param_name == "theta") {
        return params_.theta;
    } else if (param_name == "GAMMA" || param_name == "gamma") {
        return params_.gamma;
    } else if (param_name == "W" || param_name == "w") {
        return params_.w;
    } else if (param_name == "L" || param_name == "l") {
        return params_.l;
    }
    return std::nullopt;
}

template<Polarity P>
bool MOSFET_Level3<P>::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "VTO" || param_name == "vto" || param_name == "VTH" || param_name == "vth") {
        params_.vto = value;
        spdlog::trace("{}: set VTO = {} V", name_, params_.vto);
        return true;
    } else if (param_name == "UO" || param_name == "uo") {
        if (value <= 0.0) {
            spdlog::warn("{}: UO must be positive (got {}), not updated", name_, value);
            return false;
        }
        params_.uo = value;
        Real uo_si = params_.uo * 1e-4;
        Real leff = params_.get_leff();
        Real weff = params_.get_weff();
        beta_ = uo_si * cox_ * (weff / leff);
        spdlog::trace("{}: set UO = {} cm^2/V-s (beta = {:.3e} A/V^2)",
                     name_, params_.uo, beta_);
        return true;
    } else if (param_name == "W" || param_name == "w") {
        if (value <= 0.0) {
            spdlog::warn("{}: W must be positive (got {}), not updated", name_, value);
            return false;
        }
        params_.w = value;
        Real uo_si = params_.uo * 1e-4;
        Real leff = params_.get_leff();
        Real weff = params_.get_weff();
        beta_ = uo_si * cox_ * (weff / leff);
        spdlog::trace("{}: set W = {} m (beta = {:.3e} A/V^2)",
                     name_, params_.w, beta_);
        return true;
    } else if (param_name == "L" || param_name == "l") {
        if (value <= 0.0) {
            spdlog::warn("{}: L must be positive (got {}), not updated", name_, value);
            return false;
        }
        params_.l = value;
        Real uo_si = params_.uo * 1e-4;
        Real leff = params_.get_leff();
        Real weff = params_.get_weff();
        beta_ = uo_si * cox_ * (weff / leff);
        spdlog::trace("{}: set L = {} m (beta = {:.3e} A/V^2)",
                     name_, params_.l, beta_);
        return true;
    }
    return false;
}

template<Polarity P>
std::vector<std::string> MOSFET_Level3<P>::get_perturbable_parameters() const {
    return {"VTO", "UO", "W", "L", "GAMMA", "THETA"};
}

// Capacitance helper methods (Meyer model)

template<Polarity P>
Real MOSFET_Level3<P>::get_gate_source_capacitance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real w_eff = params_.get_weff();
    Real l_eff = params_.get_leff();
    Real cox_wl = cox_ * w_eff * l_eff;
    Real cgso = params_.cgso * w_eff;

    Region region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);

    switch (region) {
        case Region::CUTOFF:
            return cgso;
        case Region::LINEAR:
        case Region::SATURATION:
            return (2.0 / 3.0) * cox_wl + cgso;
        default:
            return cgso;
    }
}

template<Polarity P>
Real MOSFET_Level3<P>::get_gate_drain_capacitance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real w_eff = params_.get_weff();
    Real l_eff = params_.get_leff();
    Real cox_wl = cox_ * w_eff * l_eff;
    Real cgdo = params_.cgdo * w_eff;

    Region region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);

    switch (region) {
        case Region::CUTOFF:
            return cgdo;
        case Region::LINEAR:
            return (2.0 / 3.0) * cox_wl + cgdo;
        case Region::SATURATION:
            return cgdo;  // No channel capacitance in saturation
        default:
            return cgdo;
    }
}

template<Polarity P>
Real MOSFET_Level3<P>::get_gate_bulk_capacitance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real w_eff = params_.get_weff();
    Real l_eff = params_.get_leff();
    Real cox_wl = cox_ * w_eff * l_eff;
    Real cgbo = params_.cgbo * l_eff;

    Region region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);

    switch (region) {
        case Region::CUTOFF:
            return cox_wl + cgbo;  // Full gate-bulk coupling
        case Region::LINEAR:
        case Region::SATURATION:
            return cgbo;  // Inversion layer shields gate from bulk
        default:
            return cgbo;
    }
}

template<Polarity P>
Real MOSFET_Level3<P>::get_bulk_source_capacitance(Real v_bs_eff) const {
    if (params_.cbs == 0.0) {
        return 0.0;
    }

    Real cj0 = params_.cbs;
    Real vj = params_.pb;
    Real m = params_.mj;

    // For N-channel: vbs_eff = vbs (negative in normal operation)
    // For P-channel: vbs_eff = vsb (positive in normal operation)
    // In both cases, junction is reverse-biased when v_bs_eff < 0 (N) or > 0 (P)

    Real v_junction;
    if constexpr (P == Polarity::N) {
        v_junction = v_bs_eff;  // vbs
    } else {
        v_junction = -v_bs_eff;  // -vsb = vbs
    }

    if (v_junction < 0.0) {
        Real factor = 1.0 - v_junction / vj;
        if (factor <= 0.0) {
            return cj0 * 100.0;
        }
        return cj0 / std::pow(factor, m);
    } else {
        return cj0 * (1.0 + m * v_junction / vj);
    }
}

template<Polarity P>
Real MOSFET_Level3<P>::get_bulk_drain_capacitance(Real v_bd_eff) const {
    if (params_.cbd == 0.0) {
        return 0.0;
    }

    Real cj0 = params_.cbd;
    Real vj = params_.pb;
    Real m = params_.mj;

    Real v_junction;
    if constexpr (P == Polarity::N) {
        v_junction = v_bd_eff;  // vbd
    } else {
        v_junction = -v_bd_eff;  // -vdb = vbd
    }

    if (v_junction < 0.0) {
        Real factor = 1.0 - v_junction / vj;
        if (factor <= 0.0) {
            return cj0 * 100.0;
        }
        return cj0 / std::pow(factor, m);
    } else {
        return cj0 * (1.0 + m * v_junction / vj);
    }
}

// Explicit template instantiations
template class MOSFET_Level3<Polarity::N>;
template class MOSFET_Level3<Polarity::P>;

} // namespace kirimaspice::devices
