// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/mosfet_bsim1.cpp
// Unified MOSFET BSIM1 template implementation
#include "kirimaspice/devices/mosfet_bsim1.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace kirimaspice::devices {

namespace {
struct Bsim1DebugConfig {
    bool enabled = false;
    std::string device_filter;
};

// Thread-safe environment variable access
const Bsim1DebugConfig& get_bsim1_debug_config() {
    static const Bsim1DebugConfig config = [] {
        Bsim1DebugConfig cfg;
        if (const char* env = std::getenv("KIRIMASPICE_BSIM1_DEBUG")) {
            cfg.enabled = true;
            if (*env != '\0') {
                cfg.device_filter = env;
            }
        }
        return cfg;
    }();
    return config;
}

template<Polarity P>
const char* region_to_string(typename MOSFET_BSIM1<P>::Region region) {
    switch (region) {
        case MOSFET_BSIM1<P>::Region::CUTOFF:
            return "CUTOFF";
        case MOSFET_BSIM1<P>::Region::LINEAR:
            return "LINEAR";
        case MOSFET_BSIM1<P>::Region::SATURATION:
            return "SATURATION";
    }
    return "UNKNOWN";
}

bool should_log_debug(const std::string& device_name) {
    const auto& cfg = get_bsim1_debug_config();
    if (!cfg.enabled) {
        return false;
    }
    if (cfg.device_filter.empty()) {
        return true;
    }
    return cfg.device_filter == device_name;
}
}  // namespace

//==============================================================================
// Constructor
//==============================================================================
template<Polarity P>
MOSFET_BSIM1<P>::MOSFET_BSIM1(DeviceId id, const std::string& name,
                              NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                              const MOSFETBsim1Params& params)
    : Device(id, name, device_type)
    , nodes_{drain, gate, source, bulk}
    , params_(params)
{
    if (!params_.validate()) {
        throw std::runtime_error("MOSFET_BSIM1: Invalid parameters for " + name);
    }

    Real leff = params_.get_leff();
    Real weff = params_.get_weff();

    spdlog::debug("Created MOSFET_BSIM1 {} ({}) (D={}, G={}, S={}, B={}, "
                 "W={:.3e}m, L={:.3e}m, Leff={:.3e}m, Weff={:.3e}m)",
                 name, Traits::type_string, drain, gate, source, bulk,
                 params_.w, params_.l, leff, weff);
}

//==============================================================================
// Region determination
//==============================================================================
template<Polarity P>
typename MOSFET_BSIM1<P>::Region MOSFET_BSIM1<P>::get_region(
    Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {

    Real vth = get_threshold_voltage(v_bs_eff, v_ds_eff);

    if (v_gs_eff < vth) {
        return Region::CUTOFF;
    }

    Real vdsat = compute_saturation_voltage(v_gs_eff, vth, params_.get_leff());

    if (v_ds_eff < vdsat) {
        return Region::LINEAR;
    } else {
        return Region::SATURATION;
    }
}

//==============================================================================
// Threshold voltage
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::get_threshold_voltage(Real v_bs_eff, Real v_ds_eff) const {
    Real leff = params_.get_leff();

    // Body effect: ΔVth = K1×sqrt(PHI - Vbs) - K2×(PHI - Vbs)
    // For P-channel, Vsb is typically positive (bulk lower than source)
    Real phi_term;
    if constexpr (P == Polarity::N) {
        phi_term = params_.phi - v_bs_eff;  // PHI - Vbs
    } else {
        phi_term = params_.phi + v_bs_eff;  // PHI + Vsb
    }

    Real arg = std::max(0.01, phi_term);
    Real sqrt_phi = std::sqrt(arg);
    Real body_effect = params_.k1 * sqrt_phi - params_.k2 * phi_term;

    // DIBL effect
    Real eta_eff = compute_eta_effect(v_ds_eff, leff);
    Real dibl_effect = eta_eff * v_ds_eff;

    // Threshold voltage
    Real vth;
    if constexpr (P == Polarity::N) {
        // N-channel: Vth = VFB + PHI + body_effect - DIBL
        vth = params_.vfb + params_.phi + body_effect - dibl_effect;
    } else {
        // P-channel: |Vth| = |VFB| + PHI + body_effect - DIBL
        vth = std::abs(params_.vfb) + params_.phi + body_effect - dibl_effect;
        vth = std::max(0.1, vth);  // Minimum threshold for numerical stability
    }

    return vth;
}

//==============================================================================
// ETA effect (DIBL)
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::compute_eta_effect(Real v_ds_eff, Real leff) const {
    // ETA short-channel effect: η(L) = ETA × (1 + X2E/L + X3E/L^2)
    Real eta = params_.eta;

    if (params_.x2e != 0.0 || params_.x3e != 0.0) {
        Real leff_um = leff * 1e6;  // m → µm
        eta = params_.eta * (1.0 + params_.x2e / leff_um + params_.x3e / (leff_um * leff_um));
    }

    return eta;
}

//==============================================================================
// Effective mobility
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::compute_effective_mobility(Real v_gs_eff, Real v_ds_eff, Real leff) const {
    // Base mobility with short-channel effects
    Real muz = params_.muz;

    if (params_.x2mz != 0.0) {
        Real leff_um = leff * 1e6;  // m → µm
        Real ms = params_.x2ms / leff_um;
        Real factor = std::pow(1.0 + ms, params_.x2m);
        muz = params_.muz / factor;
    }

    // Mobility degradation coefficients
    Real u0_eff = params_.u0;
    Real u1_eff = params_.u1;

    if (params_.x2u0 != 0.0) {
        Real leff_um = leff * 1e6;
        u0_eff = params_.u0 * (1.0 + params_.x2u0 / leff_um);
    }

    if (params_.x2u1 != 0.0) {
        Real leff_um = leff * 1e6;
        u1_eff = params_.u1 * (1.0 + params_.x2u1 / leff_um);
    }

    // Mobility degradation: μeff = MUZ / (1 + U0×Vgs + U1×Vgs^2)
    Real vgs_abs = std::abs(v_gs_eff);
    Real denom = 1.0 + u0_eff * vgs_abs + u1_eff * vgs_abs * vgs_abs;
    Real mu_eff = muz / std::max(1.0, denom);

    return mu_eff;
}

//==============================================================================
// Saturation voltage
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::compute_saturation_voltage(Real v_gs_eff, Real vth, Real leff) const {
    Real vgt = v_gs_eff - vth;

    if (vgt <= 0.0) {
        return 0.0;
    }

    // Effective mobility at Vgs
    Real mu_eff = compute_effective_mobility(v_gs_eff, 0.0, leff);
    Real mu_si = mu_eff * 1e-4;  // cm^2/V-s → m^2/V-s

    // Saturation field: Ec = VSAT / μeff
    Real e_sat = params_.vsat / (mu_si + 1e-10);
    Real e_sat_l = e_sat * leff;

    // X3MS: saturation voltage short-channel correction
    Real ms_correction = 1.0;
    if (params_.x3ms != 0.0) {
        Real normalized = std::abs(params_.x3ms) > 1.0 ? params_.x3ms * 1e-6 : params_.x3ms;
        ms_correction = 1.0 + normalized / std::max(1e-6, leff);
        ms_correction = std::clamp(ms_correction, 0.8, 1.2);
    }

    // Vdsat = Vgt / (1 + Vgt / (Esat × L × ms_correction))
    Real denom = 1.0 + vgt / (e_sat_l * ms_correction + 1e-10);
    Real vdsat = vgt / denom;

    return std::max(0.0, vdsat);
}

//==============================================================================
// Subthreshold current
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::compute_subthreshold_current(
    Real v_gs_eff, Real v_ds_eff, Real v_bs_eff, Real vth) const {

    Real vt = k_boltzmann * temperature_ / q_electron;  // Thermal voltage

    // Subthreshold slope factor: n = N0 + NB×|Vbs| + ND×Vds
    Real n = params_.n0 + params_.nb * std::abs(v_bs_eff) + params_.nd * v_ds_eff;
    n = std::max(1.0, n);

    Real vgt = v_gs_eff - vth;
    Real exp_arg = vgt / (n * vt);
    exp_arg = std::clamp(exp_arg, -50.0, 50.0);  // Avoid overflow

    Real isub_norm = std::exp(exp_arg);

    // DIBL effect in subthreshold
    Real eta_eff = compute_eta_effect(v_ds_eff, params_.get_leff());
    Real dibl_factor = std::exp(eta_eff * v_ds_eff / (n * vt));

    return isub_norm * dibl_factor;
}

//==============================================================================
// Drain current
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::get_drain_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real leff = params_.get_leff();
    Real weff = params_.get_weff();
    Real cox = eps_ox / params_.get_tox_si();

    Real vth = get_threshold_voltage(v_bs_eff, v_ds_eff);
    Real vgt = v_gs_eff - vth;

    Real vt = k_boltzmann * temperature_ / q_electron;

    // Deep cutoff check
    if (vgt < -5.0 * vt) {
        return 0.0;
    }

    // Effective mobility and transconductance parameter
    Real mu_eff = compute_effective_mobility(v_gs_eff, v_ds_eff, leff);
    Real mu_si = mu_eff * 1e-4;  // cm^2/V-s → m^2/V-s
    Real beta = mu_si * cox * (weff / leff);

    Real ids = 0.0;

    if (vgt < 0.0) {
        // Subthreshold region
        Real isub_norm = compute_subthreshold_current(v_gs_eff, v_ds_eff, v_bs_eff, vth);

        if constexpr (P == Polarity::N) {
            ids = beta * isub_norm * 1e-6;  // Scale subthreshold current
        } else {
            // P-channel: Isub = β × Vt^2 × isub_norm × (1 - exp(-Vsd/Vt))
            Real i_scale = beta * vt * vt;
            ids = i_scale * isub_norm;
            Real vds_factor = 1.0 - std::exp(-v_ds_eff / vt);
            ids = ids * vds_factor;
        }

    } else {
        // Strong inversion
        Real vdsat = compute_saturation_voltage(v_gs_eff, vth, leff);

        if (v_ds_eff < vdsat) {
            // Linear region: Id = β × [(Vgs - Vth)×Vds - 0.5×Vds^2]
            ids = beta * (vgt * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);

        } else {
            // Saturation region: Id = 0.5 × β × Vdsat^2
            ids = 0.5 * beta * vdsat * vdsat;

            // Channel length modulation
            Real lambda_eff = std::max(0.0, params_.eta);
            Real delta_vds = std::max(0.0, v_ds_eff - vdsat);
            ids = ids * (1.0 + lambda_eff * delta_vds);
        }
    }

    return std::max(0.0, ids);
}

//==============================================================================
// Transconductances (numerical derivatives)
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::get_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real delta = 1e-6;

    Real ids_plus = get_drain_current(v_gs_eff + delta, v_ds_eff, v_bs_eff);
    Real ids_minus = get_drain_current(v_gs_eff - delta, v_ds_eff, v_bs_eff);

    Real gm = (ids_plus - ids_minus) / (2.0 * delta);

    return std::max(0.0, gm);
}

template<Polarity P>
Real MOSFET_BSIM1<P>::get_output_conductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real delta = 1e-6;

    Real ids_plus = get_drain_current(v_gs_eff, v_ds_eff + delta, v_bs_eff);
    Real ids_minus = get_drain_current(v_gs_eff, v_ds_eff - delta, v_bs_eff);

    Real gds = (ids_plus - ids_minus) / (2.0 * delta);

    return std::max(0.0, gds);
}

template<Polarity P>
Real MOSFET_BSIM1<P>::get_bulk_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real delta = 1e-6;

    Real ids_plus = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff + delta);
    Real ids_minus = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff - delta);

    Real gmb = (ids_plus - ids_minus) / (2.0 * delta);

    return std::max(0.0, gmb);
}

//==============================================================================
// Gate capacitances (Meyer model)
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::get_cgs(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real leff = params_.get_leff();
    Real weff = params_.get_weff();
    Real cox = eps_ox / params_.get_tox_si();

    Real vth = get_threshold_voltage(v_bs_eff, v_ds_eff);
    Region region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);

    Real cgs_intrinsic = 0.0;

    if (region == Region::CUTOFF) {
        cgs_intrinsic = 0.0;
    } else if (region == Region::LINEAR) {
        if constexpr (P == Polarity::N) {
            cgs_intrinsic = (2.0 / 3.0) * cox * weff * leff;
        } else {
            cgs_intrinsic = 0.5 * cox * weff * leff;
        }
    } else {  // SATURATION
        cgs_intrinsic = (2.0 / 3.0) * cox * weff * leff;
    }

    // Add overlap capacitance
    cgs_intrinsic += params_.cgso * weff;

    return cgs_intrinsic;
}

template<Polarity P>
Real MOSFET_BSIM1<P>::get_cgd(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real leff = params_.get_leff();
    Real weff = params_.get_weff();
    Real cox = eps_ox / params_.get_tox_si();

    Real vth = get_threshold_voltage(v_bs_eff, v_ds_eff);
    Region region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);

    Real cgd_intrinsic = 0.0;

    if (region == Region::CUTOFF) {
        cgd_intrinsic = 0.0;
    } else if (region == Region::LINEAR) {
        if constexpr (P == Polarity::N) {
            cgd_intrinsic = (2.0 / 3.0) * cox * weff * leff;
        } else {
            cgd_intrinsic = 0.5 * cox * weff * leff;
        }
    } else {  // SATURATION
        cgd_intrinsic = 0.0;  // Channel pinched off at drain
    }

    // Add overlap capacitance
    cgd_intrinsic += params_.cgdo * weff;

    return cgd_intrinsic;
}

template<Polarity P>
Real MOSFET_BSIM1<P>::get_cgb(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real leff = params_.get_leff();
    Real weff = params_.get_weff();
    Real cox = eps_ox / params_.get_tox_si();

    Real vth = get_threshold_voltage(v_bs_eff, v_ds_eff);
    Region region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);

    Real cgb_intrinsic = 0.0;

    if (region == Region::CUTOFF) {
        // Cutoff: Cgb = Cox × W × L (channel not formed)
        cgb_intrinsic = cox * weff * leff;
    } else {
        // Strong inversion: Cgb ≈ 0 (channel shields bulk)
        cgb_intrinsic = 0.0;
    }

    // Add overlap capacitance
    if constexpr (P == Polarity::N) {
        cgb_intrinsic += params_.cgbo * weff;
    } else {
        cgb_intrinsic += params_.cgbo * leff;
    }

    return cgb_intrinsic;
}

//==============================================================================
// Junction capacitances
//==============================================================================
template<Polarity P>
Real MOSFET_BSIM1<P>::get_cbd(Real v_bd_eff) const {
    // Junction voltage (positive = reverse bias for N, opposite for P)
    Real vj;
    if constexpr (P == Polarity::N) {
        vj = v_bd_eff;  // vbd for N-channel
    } else {
        vj = -v_bd_eff;  // vdb for P-channel
    }

    if (params_.cbd > 0.0 && P == Polarity::N) {
        // N-channel: use specified value with voltage dependence
        if (params_.cj == 0.0) {
            return params_.cbd;
        }
    }

    if (vj < 0.0) {
        // Forward bias: linear approximation
        Real cj_forward = params_.cbd * (1.0 - vj / params_.pb);
        return std::max(params_.cbd * 0.1, cj_forward);
    }

    if (vj >= params_.pb * 0.9) {
        vj = params_.pb * 0.9;  // Clamp near breakdown
    }

    Real factor = 1.0 - vj / params_.pb;
    Real cj = params_.cbd / std::pow(factor, params_.mj);

    // N-channel: add area/perimeter contributions
    if constexpr (P == Polarity::N) {
        if (params_.cj > 0.0 && params_.ad > 0.0) {
            cj = params_.cj * params_.ad / std::pow(std::max(0.01, factor), params_.mj);

            if (params_.pd > 0.0 && params_.cjsw > 0.0) {
                cj += params_.cjsw * params_.pd / std::pow(std::max(0.01, factor), params_.mjsw);
            }
        }
    }

    return cj;
}

template<Polarity P>
Real MOSFET_BSIM1<P>::get_cbs(Real v_bs_eff) const {
    // Junction voltage
    Real vj;
    if constexpr (P == Polarity::N) {
        vj = v_bs_eff;  // vbs for N-channel
    } else {
        vj = v_bs_eff;  // vsb for P-channel (already positive for reverse bias)
    }

    if (params_.cbs > 0.0 && P == Polarity::N) {
        if (params_.cj == 0.0) {
            return params_.cbs;
        }
    }

    if (vj < 0.0) {
        Real cj_forward = params_.cbs * (1.0 - vj / params_.pb);
        return std::max(params_.cbs * 0.1, cj_forward);
    }

    if (vj >= params_.pb * 0.9) {
        vj = params_.pb * 0.9;
    }

    Real factor = 1.0 - vj / params_.pb;
    Real cj = params_.cbs / std::pow(factor, params_.mj);

    if constexpr (P == Polarity::N) {
        if (params_.cj > 0.0 && params_.as > 0.0) {
            cj = params_.cj * params_.as / std::pow(std::max(0.01, factor), params_.mj);

            if (params_.ps > 0.0 && params_.cjsw > 0.0) {
                cj += params_.cjsw * params_.ps / std::pow(std::max(0.01, factor), params_.mjsw);
            }
        }
    }

    return cj;
}

//==============================================================================
// DC Stamps
//==============================================================================
template<Polarity P>
void MOSFET_BSIM1<P>::stamp_dc_linear(MNASystem& mna,
                                       const DenseVector& node_voltages) const {
    // BSIM1 is nonlinear, no linear contribution
    (void)mna;
    (void)node_voltages;
}

template<Polarity P>
void MOSFET_BSIM1<P>::stamp_dc_nonlinear(MNASystem& mna,
                                          const DenseVector& node_voltages) const {
    NodeId d = drain();
    NodeId g = gate();
    NodeId s = source();
    NodeId b = bulk();

    // Get node voltages
    Real vd = (d > 0) ? node_voltages[d - 1] : 0.0;
    Real vg = (g > 0) ? node_voltages[g - 1] : 0.0;
    Real vs = (s > 0) ? node_voltages[s - 1] : 0.0;
    Real vb = (b > 0) ? node_voltages[b - 1] : 0.0;

    // Compute effective voltages based on polarity
    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    // Get drain current and small-signal parameters
    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gds = get_output_conductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gmb = get_bulk_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);

    if (should_log_debug(name_)) {
        Real leff = params_.get_leff();
        Real vth = get_threshold_voltage(v_bs_eff, v_ds_eff);
        Real vdsat = compute_saturation_voltage(v_gs_eff, vth, leff);
        auto region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);
        spdlog::info(
            "BSIM1_DEBUG dev={} type={} region={} vgs_eff={:.6g} vds_eff={:.6g} vbs_eff={:.6g} "
            "vth={:.6g} vdsat={:.6g} ids={:.6g} gm={:.6g} gds={:.6g} gmb={:.6g}",
            name_, Traits::type_string, region_to_string<P>(region),
            v_gs_eff, v_ds_eff, v_bs_eff, vth, vdsat, ids, gm, gds, gmb);
    }

    // Norton equivalent: I_norton = Ids - gm×Vgs_eff - gds×Vds_eff - gmb×Vbs_eff
    Real i_norton = ids - gm * v_gs_eff - gds * v_ds_eff - gmb * v_bs_eff;

    // Stamp conductances based on polarity
    if constexpr (P == Polarity::N) {
        // N-channel: gm (gate-source transconductance)
        if (d > 0 && g > 0) mna.G.add_entry(d - 1, g - 1, gm);
        if (d > 0 && s > 0) mna.G.add_entry(d - 1, s - 1, -gm);
        if (s > 0 && g > 0) mna.G.add_entry(s - 1, g - 1, -gm);
        if (s > 0) mna.G.add_entry(s - 1, s - 1, gm);

        // gds (drain-source)
        if (d > 0) mna.G.add_entry(d - 1, d - 1, gds);
        if (d > 0 && s > 0) mna.G.add_entry(d - 1, s - 1, -gds);
        if (s > 0 && d > 0) mna.G.add_entry(s - 1, d - 1, -gds);
        if (s > 0) mna.G.add_entry(s - 1, s - 1, gds);

        // gmb (bulk transconductance)
        if (d > 0 && b > 0) mna.G.add_entry(d - 1, b - 1, gmb);
        if (d > 0 && s > 0) mna.G.add_entry(d - 1, s - 1, -gmb);
        if (s > 0 && b > 0) mna.G.add_entry(s - 1, b - 1, -gmb);
        if (s > 0) mna.G.add_entry(s - 1, s - 1, gmb);
    } else {
        // P-channel: adapted stamps for source-referenced voltages
        // Transconductance: gm (gate-source)
        if (s > 0 && g > 0) {
            mna.G.add_entry(s - 1, g - 1, gm);
            mna.G.add_entry(g - 1, s - 1, gm);
        }
        if (s > 0) mna.G.add_entry(s - 1, s - 1, gm);
        if (g > 0) mna.G.add_entry(g - 1, g - 1, gm);

        // Output conductance: gds (drain-source)
        if (s > 0 && d > 0) {
            mna.G.add_entry(s - 1, d - 1, gds);
            mna.G.add_entry(d - 1, s - 1, gds);
        }
        if (s > 0) mna.G.add_entry(s - 1, s - 1, gds);
        if (d > 0) mna.G.add_entry(d - 1, d - 1, gds);

        // Bulk transconductance: gmb
        if (s > 0 && b > 0) {
            mna.G.add_entry(s - 1, b - 1, gmb);
            mna.G.add_entry(b - 1, s - 1, gmb);
        }
        if (s > 0) mna.G.add_entry(s - 1, s - 1, gmb);
        if (b > 0) mna.G.add_entry(b - 1, b - 1, gmb);
    }

    // Norton equivalent current
    if (d > 0) mna.b[d - 1] -= i_norton;
    if (s > 0) mna.b[s - 1] += i_norton;
}

//==============================================================================
// Transient stamp
//==============================================================================
template<Polarity P>
void MOSFET_BSIM1<P>::stamp_transient(MNASystem& mna,
                                       const DenseVector& node_voltages,
                                       Real timestep,
                                       IntegrationMethod method,
                                       const BDFCoefficients* bdf_coef,
                                       const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // First stamp DC nonlinear contribution
    stamp_dc_nonlinear(mna, node_voltages);

    NodeId d = drain();
    NodeId g = gate();
    NodeId s = source();
    NodeId b = bulk();

    Real vd = (d > 0) ? node_voltages[d - 1] : 0.0;
    Real vg = (g > 0) ? node_voltages[g - 1] : 0.0;
    Real vs = (s > 0) ? node_voltages[s - 1] : 0.0;
    Real vb = (b > 0) ? node_voltages[b - 1] : 0.0;

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    // Compute additional voltages for capacitor connections
    Real vgd = vg - vd;
    Real vgb = vg - vb;
    Real vbd = vb - vd;

    // Get capacitances
    Real cgs = get_cgs(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgd = get_cgd(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgb = get_cgb(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cbd = get_cbd(vbd);
    Real cbs = get_cbs(v_bs_eff);

    // Backward Euler companion model
    Real geq_cgs = cgs / timestep;
    Real ieq_cgs = geq_cgs * v1_prev_;

    Real geq_cgd = cgd / timestep;
    Real ieq_cgd = geq_cgd * v2_prev_;

    Real geq_cgb = cgb / timestep;
    Real ieq_cgb = geq_cgb * v3_prev_;

    Real geq_cbd = cbd / timestep;
    Real ieq_cbd = geq_cbd * v4_prev_;

    Real geq_cbs = cbs / timestep;
    Real ieq_cbs = geq_cbs * v5_prev_;

    auto add_rhs = [&](NodeId node, Real value) {
        if (node > 0) {
            mna.b[node - 1] += value;
        }
    };

    // Stamp Cgs (gate-source)
    if (g > 0) mna.G.add_entry(g - 1, g - 1, geq_cgs);
    if (g > 0 && s > 0) mna.G.add_entry(g - 1, s - 1, -geq_cgs);
    if (s > 0 && g > 0) mna.G.add_entry(s - 1, g - 1, -geq_cgs);
    if (s > 0) mna.G.add_entry(s - 1, s - 1, geq_cgs);
    add_rhs(g, ieq_cgs);
    add_rhs(s, -ieq_cgs);

    // Stamp Cgd (gate-drain)
    if (g > 0) mna.G.add_entry(g - 1, g - 1, geq_cgd);
    if (g > 0 && d > 0) mna.G.add_entry(g - 1, d - 1, -geq_cgd);
    if (d > 0 && g > 0) mna.G.add_entry(d - 1, g - 1, -geq_cgd);
    if (d > 0) mna.G.add_entry(d - 1, d - 1, geq_cgd);
    add_rhs(g, ieq_cgd);
    add_rhs(d, -ieq_cgd);

    // Stamp Cgb (gate-bulk)
    if (g > 0) mna.G.add_entry(g - 1, g - 1, geq_cgb);
    if (g > 0 && b > 0) mna.G.add_entry(g - 1, b - 1, -geq_cgb);
    if (b > 0 && g > 0) mna.G.add_entry(b - 1, g - 1, -geq_cgb);
    if (b > 0) mna.G.add_entry(b - 1, b - 1, geq_cgb);
    add_rhs(g, ieq_cgb);
    add_rhs(b, -ieq_cgb);

    // Stamp Cbd (bulk-drain)
    if (b > 0) mna.G.add_entry(b - 1, b - 1, geq_cbd);
    if (b > 0 && d > 0) mna.G.add_entry(b - 1, d - 1, -geq_cbd);
    if (d > 0 && b > 0) mna.G.add_entry(d - 1, b - 1, -geq_cbd);
    if (d > 0) mna.G.add_entry(d - 1, d - 1, geq_cbd);
    add_rhs(b, ieq_cbd);
    add_rhs(d, -ieq_cbd);

    // Stamp Cbs (bulk-source)
    if (b > 0) mna.G.add_entry(b - 1, b - 1, geq_cbs);
    if (b > 0 && s > 0) mna.G.add_entry(b - 1, s - 1, -geq_cbs);
    if (s > 0 && b > 0) mna.G.add_entry(s - 1, b - 1, -geq_cbs);
    if (s > 0) mna.G.add_entry(s - 1, s - 1, geq_cbs);
    add_rhs(b, ieq_cbs);
    add_rhs(s, -ieq_cbs);
}

//==============================================================================
// AC stamp
//==============================================================================
template<Polarity P>
void MOSFET_BSIM1<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                               const DenseVector& dc_solution) const {
    NodeId d = drain();
    NodeId g = gate();
    NodeId s = source();
    NodeId b = bulk();

    Real vd = (d > 0) ? dc_solution[d - 1] : 0.0;
    Real vg = (g > 0) ? dc_solution[g - 1] : 0.0;
    Real vs = (s > 0) ? dc_solution[s - 1] : 0.0;
    Real vb = (b > 0) ? dc_solution[b - 1] : 0.0;

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);
    Real vbd = vb - vd;

    // Small-signal parameters
    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gds = get_output_conductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gmb = get_bulk_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);

    // Capacitances
    Real cgs = get_cgs(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgd = get_cgd(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgb = get_cgb(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cbd = get_cbd(vbd);
    Real cbs = get_cbs(v_bs_eff);

    Complex j_omega(0.0, omega);

    // Stamp transconductances
    if (d > 0 && g > 0) sys.Y.add_entry(d - 1, g - 1, Complex(gm, 0.0));
    if (d > 0 && s > 0) sys.Y.add_entry(d - 1, s - 1, Complex(-gm, 0.0));
    if (s > 0 && g > 0) sys.Y.add_entry(s - 1, g - 1, Complex(-gm, 0.0));
    if (s > 0) sys.Y.add_entry(s - 1, s - 1, Complex(gm, 0.0));

    if (d > 0) sys.Y.add_entry(d - 1, d - 1, Complex(gds, 0.0));
    if (d > 0 && s > 0) sys.Y.add_entry(d - 1, s - 1, Complex(-gds, 0.0));
    if (s > 0 && d > 0) sys.Y.add_entry(s - 1, d - 1, Complex(-gds, 0.0));
    if (s > 0) sys.Y.add_entry(s - 1, s - 1, Complex(gds, 0.0));

    if (d > 0 && b > 0) sys.Y.add_entry(d - 1, b - 1, Complex(gmb, 0.0));
    if (d > 0 && s > 0) sys.Y.add_entry(d - 1, s - 1, Complex(-gmb, 0.0));
    if (s > 0 && b > 0) sys.Y.add_entry(s - 1, b - 1, Complex(-gmb, 0.0));
    if (s > 0) sys.Y.add_entry(s - 1, s - 1, Complex(gmb, 0.0));

    // Stamp capacitances
    Complex ycgs = j_omega * cgs;
    Complex ycgd = j_omega * cgd;
    Complex ycgb = j_omega * cgb;
    Complex ycbd = j_omega * cbd;
    Complex ycbs = j_omega * cbs;

    // Cgs
    if (g > 0) sys.Y.add_entry(g - 1, g - 1, ycgs);
    if (g > 0 && s > 0) sys.Y.add_entry(g - 1, s - 1, -ycgs);
    if (s > 0 && g > 0) sys.Y.add_entry(s - 1, g - 1, -ycgs);
    if (s > 0) sys.Y.add_entry(s - 1, s - 1, ycgs);

    // Cgd
    if (g > 0) sys.Y.add_entry(g - 1, g - 1, ycgd);
    if (g > 0 && d > 0) sys.Y.add_entry(g - 1, d - 1, -ycgd);
    if (d > 0 && g > 0) sys.Y.add_entry(d - 1, g - 1, -ycgd);
    if (d > 0) sys.Y.add_entry(d - 1, d - 1, ycgd);

    // Cgb
    if (g > 0) sys.Y.add_entry(g - 1, g - 1, ycgb);
    if (g > 0 && b > 0) sys.Y.add_entry(g - 1, b - 1, -ycgb);
    if (b > 0 && g > 0) sys.Y.add_entry(b - 1, g - 1, -ycgb);
    if (b > 0) sys.Y.add_entry(b - 1, b - 1, ycgb);

    // Cbd
    if (b > 0) sys.Y.add_entry(b - 1, b - 1, ycbd);
    if (b > 0 && d > 0) sys.Y.add_entry(b - 1, d - 1, -ycbd);
    if (d > 0 && b > 0) sys.Y.add_entry(d - 1, b - 1, -ycbd);
    if (d > 0) sys.Y.add_entry(d - 1, d - 1, ycbd);

    // Cbs
    if (b > 0) sys.Y.add_entry(b - 1, b - 1, ycbs);
    if (b > 0 && s > 0) sys.Y.add_entry(b - 1, s - 1, -ycbs);
    if (s > 0 && b > 0) sys.Y.add_entry(s - 1, b - 1, -ycbs);
    if (s > 0) sys.Y.add_entry(s - 1, s - 1, ycbs);
}

//==============================================================================
// Noise sources
//==============================================================================
template<Polarity P>
std::vector<NoiseSource> MOSFET_BSIM1<P>::get_noise_sources(
    const DenseVector& dc_solution, Real frequency) const {

    std::vector<NoiseSource> sources;

    NodeId d = drain();
    NodeId s = source();

    Real vd = (d > 0) ? dc_solution[d - 1] : 0.0;
    Real vg = (gate() > 0) ? dc_solution[gate() - 1] : 0.0;
    Real vs = (s > 0) ? dc_solution[s - 1] : 0.0;
    Real vb = (bulk() > 0) ? dc_solution[bulk() - 1] : 0.0;

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);

    if (gm < 1e-20) {
        return sources;  // Device off, no noise
    }

    // Thermal noise coefficient: 2/3, as SPICE's, for the N device and its
    // mirror the P device alike
    const Real thermal_coef = 2.0 / 3.0;

    // Thermal noise: I_n^2 = 4kT × coef × gm
    Real thermal_psd = 4.0 * k_boltzmann * temperature_ * thermal_coef * gm;

    if (thermal_psd > 0.0 && d > 0 && s > 0) {
        NoiseSource thermal;
        thermal.name = name() + "_thermal";
        thermal.node_p = d;
        thermal.node_n = s;
        thermal.spectral_density = thermal_psd;
        thermal.is_current = true;
        sources.push_back(thermal);
    }

    // Flicker noise: I_n^2 = KF × |Id|^AF / (Cox × Leff × Weff × f^EF)
    if (params_.kf > 0.0 && frequency > 0.0) {
        Real leff = params_.get_leff();
        Real weff = params_.get_weff();
        Real cox = eps_ox / params_.get_tox_si();

        Real flicker_psd = params_.kf * std::pow(std::abs(ids), params_.af) /
                          (cox * leff * weff * std::pow(frequency, params_.af));

        if (flicker_psd > 0.0 && d > 0 && s > 0) {
            NoiseSource flicker;
            flicker.name = name() + "_flicker";
            flicker.node_p = d;
            flicker.node_n = s;
            flicker.spectral_density = flicker_psd;
            flicker.is_current = true;
            sources.push_back(flicker);
        }
    }

    return sources;
}

//==============================================================================
// State management
//==============================================================================
template<Polarity P>
void MOSFET_BSIM1<P>::accept_solution(const DenseVector& node_voltages) {
    NodeId d = drain();
    NodeId g = gate();
    NodeId s = source();
    NodeId b = bulk();

    Real vd = (d > 0) ? node_voltages[d] : 0.0;
    Real vg = (g > 0) ? node_voltages[g] : 0.0;
    Real vs = (s > 0) ? node_voltages[s] : 0.0;
    Real vb = (b > 0) ? node_voltages[b] : 0.0;

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    // Update voltage history
    // v1 = Vgs/Vsg, v2 = Vgd/Vsd, v3 = Vgb, v4 = Vbd/Vdb, v5 = Vbs/Vsb
    v1_prev_ = v_gs_eff;
    v2_prev_ = vg - vd;  // Vgd for both
    v3_prev_ = vg - vb;  // Vgb for both
    v4_prev_ = vb - vd;  // Vbd for both
    v5_prev_ = v_bs_eff;

    // Update charge history for Physical LTE
    Real cgs = get_cgs(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgd = get_cgd(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgb = get_cgb(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cbd = get_cbd(v4_prev_);
    Real cbs = get_cbs(v5_prev_);

    charge_1_prev_ = cgs * v1_prev_;
    charge_2_prev_ = cgd * v2_prev_;
    charge_3_prev_ = cgb * v3_prev_;
    charge_4_prev_ = cbd * v4_prev_;
    charge_5_prev_ = cbs * v5_prev_;
}

template<Polarity P>
void MOSFET_BSIM1<P>::reject_solution() {
    // Keep previous values unchanged for adaptive timestepping
}

template<Polarity P>
Real MOSFET_BSIM1<P>::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    NodeId d = drain();
    NodeId g = gate();
    NodeId s = source();
    NodeId b = bulk();

    Real vd = (d > 0) ? node_voltages[d - 1] : 0.0;
    Real vg = (g > 0) ? node_voltages[g - 1] : 0.0;
    Real vs = (s > 0) ? node_voltages[s - 1] : 0.0;
    Real vb = (b > 0) ? node_voltages[b - 1] : 0.0;

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    Real vgd = vg - vd;
    Real vgb = vg - vb;
    Real vbd = vb - vd;

    // Current capacitances
    Real cgs = get_cgs(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgd = get_cgd(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cgb = get_cgb(v_gs_eff, v_ds_eff, v_bs_eff);
    Real cbd = get_cbd(vbd);
    Real cbs = get_cbs(v_bs_eff);

    // Current charges
    Real q1 = cgs * v_gs_eff;
    Real q2 = cgd * vgd;
    Real q3 = cgb * vgb;
    Real q4 = cbd * vbd;
    Real q5 = cbs * v_bs_eff;

    // Charge differences
    Real dq1 = std::abs(q1 - charge_1_prev_);
    Real dq2 = std::abs(q2 - charge_2_prev_);
    Real dq3 = std::abs(q3 - charge_3_prev_);
    Real dq4 = std::abs(q4 - charge_4_prev_);
    Real dq5 = std::abs(q5 - charge_5_prev_);

    // Q_scale: ngspice-compatible
    Real q_scale = std::max({std::abs(q1), std::abs(charge_1_prev_),
                             std::abs(q2), std::abs(charge_2_prev_),
                             std::abs(q3), std::abs(charge_3_prev_),
                             std::abs(q4), std::abs(charge_4_prev_),
                             std::abs(q5), std::abs(charge_5_prev_)});

    Real denom = std::max(chgtol, q_scale);

    Real lte1 = dq1 / denom;
    Real lte2 = dq2 / denom;
    Real lte3 = dq3 / denom;
    Real lte4 = dq4 / denom;
    Real lte5 = dq5 / denom;

    // RMS combination
    Real lte_total = std::sqrt((lte1*lte1 + lte2*lte2 + lte3*lte3 +
                                lte4*lte4 + lte5*lte5) / 5.0);

    return lte_total;
}

//==============================================================================
// Parameter access
//==============================================================================
template<Polarity P>
std::optional<Real> MOSFET_BSIM1<P>::get_parameter(const std::string& param_name) const {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "w") return params_.w;
    if (param_lower == "l") return params_.l;
    if (param_lower == "muz" || param_lower == "u0") return params_.muz;
    if (param_lower == "vfb") return params_.vfb;
    if (param_lower == "phi") return params_.phi;
    if (param_lower == "k1") return params_.k1;
    if (param_lower == "k2") return params_.k2;
    if (param_lower == "eta") return params_.eta;
    if (param_lower == "vsat") return params_.vsat;

    return std::nullopt;
}

template<Polarity P>
bool MOSFET_BSIM1<P>::set_parameter(const std::string& param_name, Real value) {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "w") {
        if (value > 0.0) {
            params_.w = value;
            return true;
        }
    } else if (param_lower == "l") {
        if (value > 0.0) {
            params_.l = value;
            return true;
        }
    } else if (param_lower == "muz" || param_lower == "u0") {
        if (value > 0.0) {
            params_.muz = value;
            return true;
        }
    } else if (param_lower == "vfb") {
        params_.vfb = value;
        return true;
    } else if (param_lower == "phi") {
        if (value > 0.0) {
            params_.phi = value;
            return true;
        }
    } else if (param_lower == "k1") {
        params_.k1 = value;
        return true;
    } else if (param_lower == "k2") {
        params_.k2 = value;
        return true;
    } else if (param_lower == "eta") {
        params_.eta = value;
        return true;
    } else if (param_lower == "vsat") {
        if (value > 0.0) {
            params_.vsat = value;
            return true;
        }
    }

    return false;
}

template<Polarity P>
std::vector<std::string> MOSFET_BSIM1<P>::get_perturbable_parameters() const {
    return {"W", "L", "MUZ", "VFB", "PHI", "K1", "K2", "ETA", "VSAT"};
}

//==============================================================================
// Explicit template instantiations
//==============================================================================
template class MOSFET_BSIM1<Polarity::N>;
template class MOSFET_BSIM1<Polarity::P>;

} // namespace kirimaspice::devices
