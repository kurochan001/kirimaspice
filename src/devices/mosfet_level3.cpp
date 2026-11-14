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
    beta_ = process_transconductance() * (weff / leff);

    // Surface potential coefficient (for body effect)
    // fs = sqrt(2 × q × εsi × Nsub) / Cox
    if (params_.nsub > 0.0) {
        Real nsub_si = params_.nsub * 1e6;  // cm^-3 → m^-3
        fs_ = std::sqrt(2.0 * q_electron * eps_si * nsub_si) / cox_;
    } else {
        fs_ = 0.0;
    }

    // PHI, GAMMA and VTO that the card does not give follow from NSUB (and
    // TPG, NSS): PHI = 2 Vt ln(NSUB / ni) with ni = 1.45e10 cm^-3, GAMMA =
    // sqrt(2 q epsSi NSUB) / Cox, VTO = VFB + TYPE (GAMMA sqrt(PHI) + PHI)
    // with VFB = PHIms - q NSS / Cox
    constexpr Real type = (P == Polarity::N) ? 1.0 : -1.0;
    const Real vt_nom = k_boltzmann * params_.tnom / q_electron;
    const Real nsub_si = params_.nsub * 1e6;
    phi_ = params_.phi;
    if (!params_.phi_given && nsub_si > 1.45e16) {
        phi_ = std::max(0.1, 2.0 * vt_nom * std::log(nsub_si / 1.45e16));
    }
    gamma_ = params_.gamma;
    if (!params_.gamma_given && nsub_si > 0.0) {
        gamma_ = std::sqrt(2.0 * eps_si * q_electron * nsub_si) / cox_;
    }
    xd_ = (nsub_si > 0.0) ? std::sqrt(2.0 * eps_si / (q_electron * nsub_si)) : 0.0;
    Real vto = params_.vto;
    if (!params_.vto_given && nsub_si > 0.0) {
        const Real tnom = params_.tnom;
        const Real egfet = 1.16 - 7.02e-4 * tnom * tnom / (tnom + 1108.0);
        // the gate's work function: 3.2 V for aluminium (TPG = 0), and for
        // silicon 3.25 V plus Eg when its type is the substrate's opposite
        const Real wkfng = (params_.tpg == 0.0)
                               ? 3.2
                               : 3.25 + 0.5 * egfet * (1.0 - type * params_.tpg);
        const Real phims = wkfng - (3.25 + 0.5 * egfet + type * 0.5 * phi_);
        const Real vfb = phims - q_electron * params_.nss * 1e4 / cox_;
        vto = vfb + type * (gamma_ * std::sqrt(phi_) + phi_);
    }
    vbi_ = type * vto - gamma_ * std::sqrt(phi_);
    sigma_ = params_.eta * 8.15e-22 / (cox_ * leff * leff * leff);
    narrow_factor_ = params_.delta * 0.5 * 3.14159265358979323846 * eps_si / cox_;

    spdlog::debug("Created MOSFET_Level3 {} ({}-channel) (D={}, G={}, S={}, B={}, "
                 "W={:.3e}m, L={:.3e}m, Leff={:.3e}m, Weff={:.3e}m, "
                 "Cox={:.3e}F/m^2, β={:.3e}A/V^2)",
                 name, Traits::type_string, drain, gate, source, bulk,
                 params_.w, params_.l, leff, weff, cox_, beta_);
}

template<Polarity P>
Real MOSFET_Level3<P>::process_transconductance() const {
    // KP when the card gives it, as in SPICE, and UO Cox otherwise (UO in
    // cm^2/Vs)
    return (params_.kp > 0.0) ? params_.kp : params_.uo * 1e-4 * cox_;
}

// The SPICE level 3 MOSFET, written from its published equations (A.
// Vladimirescu and S. Liu, "The Simulation of MOS Integrated Circuits Using
// SPICE2", UCB/ERL M80/7, 1980) in the N-channel form; a P-channel device
// takes VSG, VSD and VSB and TYPE VTO.

template<Polarity P>
void MOSFET_Level3<P>::body_potential(Real vbs, Real& sqphbs, Real& phibs) const {
    if (vbs <= 0.0) {
        phibs = phi_ - vbs;
        sqphbs = std::sqrt(phibs);
    } else {
        // forward bias: sqrt(PHI) / (1 + VBS / (2 PHI)), which keeps PHI - VBS positive
        sqphbs = std::sqrt(phi_) / (1.0 + vbs / (phi_ + phi_));
        phibs = sqphbs * sqphbs;
    }
}

template<Polarity P>
Real MOSFET_Level3<P>::short_channel_factor(Real sqphbs) const {
    // Fs = 1 - XJ / Leff ((LD + Wc) / XJ sqrt(1 - (Wp / (XJ + Wp))^2) - LD / XJ),
    // with Wp = Xd sqrt(PHI - VBS) and Wc / XJ a quadratic in Wp / XJ
    if (params_.xj <= 0.0 || xd_ <= 0.0) {
        return 1.0;
    }
    const Real wponxj = xd_ * sqphbs / params_.xj;
    const Real djonxj = params_.ld / params_.xj;
    const Real wconxj = 0.0631353 + 0.8013292 * wponxj - 0.01110777 * wponxj * wponxj;
    const Real argc = wponxj / (1.0 + wponxj);
    return 1.0 - params_.xj / params_.get_leff() *
                     ((wconxj + djonxj) * std::sqrt(1.0 - argc * argc) - djonxj);
}

template<Polarity P>
typename MOSFET_Level3<P>::Point MOSFET_Level3<P>::evaluate_forward(Real vgs, Real vds,
                                                                     Real vbs) const {
    Point out;
    const Real leff = params_.get_leff();
    const Real weff = params_.get_weff();
    const Real vt = k_boltzmann * params_.tnom / q_electron;

    // Threshold: Vbi - SIGMA VDS + GAMMA Fs sqrt(PHI - VBS) + Fn (PHI - VBS)
    Real sqphbs = 0.0, phibs = 0.0;
    body_potential(vbs, sqphbs, phibs);
    const Real gammas = gamma_ * short_channel_factor(sqphbs);
    const Real fnarrow = narrow_factor_ / weff;
    const Real fbody = 0.25 * gammas / sqphbs + fnarrow;  // Fb
    out.vth = vbi_ - sigma_ * vds + gammas * sqphbs + fnarrow * phibs;

    // Weak inversion (NFS > 0): below von = Vth + n Vt the current falls as
    // exp((VGS - von) / (n Vt)), n = 1 + q NFS / Cox + Cd / Cox
    Real vgsx = vgs;
    Real xn = 1.0;
    out.von = out.vth;
    if (params_.nfs > 0.0) {
        xn = 1.0 + q_electron * params_.nfs * 1e4 / cox_ +
             (gammas * sqphbs + fnarrow * phibs) / (phibs + phibs);
        out.von = out.vth + vt * xn;
        vgsx = std::max(vgs, out.von);
    } else if (vgs <= out.vth) {
        return out;
    }

    // Mobility THETA degrades, and the saturation voltage, with VMAX
    const Real vov = vgsx - out.vth;
    const Real fgate = 1.0 / (1.0 + params_.theta * vov);
    const Real us = params_.uo * 1e-4 * fgate;
    const Real vsat = vov / (1.0 + fbody);
    Real vdsc = 0.0;
    if (params_.vmax > 0.0) {
        vdsc = leff * params_.vmax / us;
        out.vdsat = vsat + vdsc - std::sqrt(vsat * vsat + vdsc * vdsc);
    } else {
        out.vdsat = vsat;
    }

    // Current at min(VDS, Vdsat): beta (Vov - (1 + Fb) VDS / 2) VDS, divided
    // by 1 + VDS / Vdsc with VMAX
    const Real vdsx = std::min(vds, out.vdsat);
    const Real fdrain = (params_.vmax > 0.0) ? 1.0 / (1.0 + vdsx / vdsc) : 1.0;
    const Real beta = process_transconductance() * weff / leff * fgate;
    Real ids = beta * fdrain * (vov - 0.5 * (1.0 + fbody) * vdsx) * vdsx;

    // Channel-length modulation above Vdsat: Ids / (1 - dL / Leff), with
    // dL = Xd sqrt(KAPPA (VDS - Vdsat)), or with VMAX
    // dL = sqrt((Ep Xd^2 / 2)^2 + KAPPA Xd^2 (VDS - Vdsat)) - Ep Xd^2 / 2,
    // Ep = Idsat / (Gdsat Leff); dL above Leff / 2 is limited to
    // Leff - Leff^2 / (4 dL)
    if (vds > out.vdsat && params_.kappa > 0.0 && xd_ > 0.0) {
        const Real alpha = xd_ * xd_;
        Real delxl = 0.0;
        if (params_.vmax > 0.0) {
            const Real gdsat = std::max(ids * (1.0 - fdrain) / vdsc, 1e-12);
            const Real emax = ids / (leff * gdsat);
            const Real a = 0.5 * emax * alpha;
            delxl = std::sqrt(a * a + params_.kappa * alpha * (vds - out.vdsat)) - a;
        } else {
            delxl = std::sqrt(params_.kappa * (vds - out.vdsat) * alpha);
        }
        if (delxl > 0.5 * leff) {
            delxl = leff - leff * leff / (4.0 * delxl);
        }
        ids /= (1.0 - delxl / leff);
    }

    if (params_.nfs > 0.0 && vgs < out.von) {
        ids *= std::exp((vgs - out.von) / (vt * xn));
    }
    out.ids = ids;
    return out;
}

template<Polarity P>
Real MOSFET_Level3<P>::get_threshold_voltage(Real v_bs_eff) const {
    Real sqphbs = 0.0, phibs = 0.0;
    body_potential(v_bs_eff, sqphbs, phibs);
    const Real gammas = gamma_ * short_channel_factor(sqphbs);
    return vbi_ + gammas * sqphbs + narrow_factor_ / params_.get_weff() * phibs;
}

template<Polarity P>
typename MOSFET_Level3<P>::Region MOSFET_Level3<P>::get_region(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    if (v_ds_eff < 0.0) {
        return get_region(v_gs_eff - v_ds_eff, -v_ds_eff, v_bs_eff - v_ds_eff);
    }
    const Point point = evaluate_forward(v_gs_eff, v_ds_eff, v_bs_eff);
    if (v_gs_eff < point.vth) {
        return Region::CUTOFF;
    }
    return (v_ds_eff < point.vdsat) ? Region::LINEAR : Region::SATURATION;
}

template<Polarity P>
Real MOSFET_Level3<P>::get_drain_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    if (v_ds_eff >= 0.0) {
        return evaluate_forward(v_gs_eff, v_ds_eff, v_bs_eff).ids;
    }
    // Drain and source swap roles: the device sees VGD, VSD and VBD
    return -evaluate_forward(v_gs_eff - v_ds_eff, -v_ds_eff, v_bs_eff - v_ds_eff).ids;
}

// gm, gds and gmb are central differences of get_drain_current

template<Polarity P>
Real MOSFET_Level3<P>::get_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real h = 1e-6;
    return (get_drain_current(v_gs_eff + h, v_ds_eff, v_bs_eff) -
            get_drain_current(v_gs_eff - h, v_ds_eff, v_bs_eff)) / (2.0 * h);
}

template<Polarity P>
Real MOSFET_Level3<P>::get_output_conductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real h = 1e-6;
    return (get_drain_current(v_gs_eff, v_ds_eff + h, v_bs_eff) -
            get_drain_current(v_gs_eff, v_ds_eff - h, v_bs_eff)) / (2.0 * h);
}

template<Polarity P>
Real MOSFET_Level3<P>::get_bulk_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real h = 1e-6;
    return (get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff + h) -
            get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff - h)) / (2.0 * h);
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
        Real leff = params_.get_leff();
        Real weff = params_.get_weff();
        beta_ = process_transconductance() * (weff / leff);
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
