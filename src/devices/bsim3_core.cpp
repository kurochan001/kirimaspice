// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/bsim3_core.cpp
// BSIM3v3.3 DC core evaluator (Ids/Vth/Vdsat)

#include "kirimaspice/devices/bsim3_core.hpp"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

namespace {
constexpr Real kBoltz = 1.380649e-23;        // J/K
constexpr Real qElectron = 1.602176634e-19;  // C
} // namespace

BSIM3Core::BSIM3Core(const BSIM3Params& params)
    : params_(params) {}

Real BSIM3Core::calc_leff() const {
    return params_.get_leff();
}

Real BSIM3Core::calc_weff() const {
    return params_.get_weff();
}

Real BSIM3Core::calc_vth(Real vbs, Real leff, Real weff) const {
    // Based on MOSFET_BSIM3_N::calculate_vth
    Real vth = params_.vth0;

    // 1. Body effect (K1, K2)
    if (std::abs(params_.k1) > 1e-12) {
        // Surface potential phi (use nominal 300K thermal voltage)
        constexpr Real vt300 = 0.0259;   // [V]
        constexpr Real ni = 1.45e10;     // [cm^-3]
        Real phi = 2.0 * vt300 * std::log(params_.nsub / ni);
        Real sqrt_phi = std::sqrt(phi);
        Real sqrt_vbs_phi = std::sqrt(std::abs(vbs) + phi);
        Real delta_vth_k1 = params_.k1 * (sqrt_vbs_phi - sqrt_phi);
        // Temporary debug for BSIM3N_REF-style params
        if (std::abs(params_.tox - 4.0e-9) < 1e-12 &&
            std::abs(params_.vth0 - 0.5) < 1e-6 &&
            std::abs(params_.w - 10.0e-6) < 1e-9 &&
            std::abs(params_.l - 1.0e-6) < 1e-9) {
            spdlog::info(
                "[BSIM3Core_Vth_Debug] vbs={:.3f} V, nsub={:.3e} cm^-3, phi={:.6f} V, "
                "sqrt_phi={:.6f}, sqrt_vbs_phi={:.6f}, delta_vth_k1={:.6f} V, vth_before={:.6f} V",
                vbs, params_.nsub, phi, sqrt_phi, sqrt_vbs_phi, delta_vth_k1, vth);
        }
        vth += delta_vth_k1;
    }

    if (std::abs(params_.k2) > 1e-12) {
        vth -= params_.k2 * vbs;
    }

    // 2. Short-channel effect (DVT0, DVT1, DVT2)
    if (leff > 0.0 && params_.dvt0 > 0.0) {
        Real lt = std::sqrt(params_.dvt1 * leff);
        Real theta0 = std::atan(params_.dvt2 * vbs / lt);
        Real delta_vth_sce = -params_.dvt0 * theta0 / (1.0 + params_.dvt2 * vbs);
        if (std::abs(params_.tox - 4.0e-9) < 1e-12 &&
            std::abs(params_.vth0 - 0.5) < 1e-6 &&
            std::abs(params_.w - 10.0e-6) < 1e-9 &&
            std::abs(params_.l - 1.0e-6) < 1e-9) {
            spdlog::info(
                "[BSIM3Core_Vth_Debug_SCE] vbs={:.3f} V, leff={:.3e} m, dvt0={:.3f}, dvt1={:.3f}, "
                "dvt2={:.3f}, lt={:.6e}, theta0={:.6e}, delta_vth_sce={:.6f} V, vth_before={:.6f} V",
                vbs, leff, params_.dvt0, params_.dvt1, params_.dvt2,
                lt, theta0, delta_vth_sce, vth);
        }
        vth += delta_vth_sce;
    }

    // 3. Narrow width effect (K3, W0)
    // Only apply for effectively narrow devices (Weff < W0).
    if (weff > 0.0 && params_.w0 > 0.0 && std::abs(params_.k3) > 1e-12 && weff < params_.w0) {
        Real ratio = (params_.w0 - weff) / params_.w0; // 0..1
        Real delta_vth_narrow = params_.k3 * ratio;
        vth += delta_vth_narrow;
    }

    if (std::abs(params_.tox - 4.0e-9) < 1e-12 &&
        std::abs(params_.vth0 - 0.5) < 1e-6 &&
        std::abs(params_.w - 10.0e-6) < 1e-9 &&
        std::abs(params_.l - 1.0e-6) < 1e-9) {
        spdlog::info(
            "[BSIM3Core_Vth_Debug_Final] vbs={:.3f} V, vth_final={:.6f} V",
            vbs, vth);
    }

    return vth;
}

Real BSIM3Core::apply_temp_vth(Real vth_base, Real vbs, Real temperature) const {
    // Based on MOSFET_BSIM3_N::apply_temp_vth
    Real delta_temp = temperature - params_.tnom;
    if (std::abs(delta_temp) < 1e-6) {
        return vth_base;
    }

    Real delta_vth = params_.kt1 * delta_temp +
                     params_.kt2 * vbs * delta_temp;

     if (std::abs(params_.tox - 4.0e-9) < 1e-12 &&
         std::abs(params_.vth0 - 0.5) < 1e-6 &&
         std::abs(params_.w - 10.0e-6) < 1e-9 &&
         std::abs(params_.l - 1.0e-6) < 1e-9) {
         spdlog::info(
             "[BSIM3Core_Vth_Temp_Debug] vth_in={:.6f} V, vbs={:.3f} V, T={:.2f} K, "
             "tnom={:.2f} K, delta_temp={:.6f} K, kt1={:.4f}, kt2={:.4f}, delta_vth={:.6f} V",
             vth_base, vbs, temperature, params_.tnom, delta_temp,
             params_.kt1, params_.kt2, delta_vth);
     }

    return vth_base + delta_vth;
}

Real BSIM3Core::calc_mobility(Real vgs, Real vds, Real vbs,
                              Real vth, Real leff, Real /*weff*/) const {
    // Based on MOSFET_BSIM3_N::calculate_mobility
    (void)leff;
    (void)vds;

    Real u0 = params_.u0; // [cm^2/Vs]
    Real vgst = vgs - vth;
    if (vgst <= 0.0) {
        return u0;
    }

    Real eeff = vgst / params_.tox; // [V/m]

    Real ua_term = params_.ua * eeff;
    Real ub_term = params_.ub * eeff * eeff;
    Real uc_term = params_.uc * vbs * eeff;

    Real denom = 1.0 + ua_term + ub_term + uc_term;
    if (denom < 1e-12) {
        denom = 1e-12;
    }

    return u0 / denom;
}

Real BSIM3Core::apply_temp_mobility(Real u0_base, Real temperature) const {
    // Based on MOSFET_BSIM3_N::apply_temp_mobility
    if (params_.tnom <= 0.0) {
        return u0_base;
    }

    Real temp_ratio = temperature / params_.tnom;
    return u0_base * std::pow(temp_ratio, params_.ute);
}

Real BSIM3Core::apply_temp_vsat(Real vsat_base, Real temperature) const {
    // Based on MOSFET_BSIM3_N::apply_temp_vsat
    Real delta_temp = temperature - params_.tnom;
    if (std::abs(delta_temp) < 1e-6) {
        return vsat_base;
    }

    Real vsat_temp = vsat_base - params_.at * delta_temp;
    if (vsat_temp <= 0.0) {
        vsat_temp = 1.0e4;
    }
    return vsat_temp;
}

Real BSIM3Core::calc_vdsat(Real vgs, Real vds, Real vbs, Real vth,
                           Real leff, Real weff, Real temperature) const {
    (void)weff;

    // Simple velocity-saturation-based Vdsat model.
    Real vgst = vgs - vth;
    if (vgst <= 0.0) {
        return 0.0;
    }

    if (leff <= 0.0) {
        return vgst;
    }

    // Effective mobility with temperature dependence
    Real ueff = calc_mobility(vgs, vds, vbs, vth, leff, weff);
    ueff = apply_temp_mobility(ueff, temperature);

    // Saturation velocity with temperature dependence
    Real vsat = apply_temp_vsat(params_.vsat, temperature);

    if (ueff > 1e-12) {
        // Convert mobility to m²/Vs
        Real ueff_si = ueff * 1e-4;
        Real ecrit = 2.0 * vsat / ueff_si;    // [V/m] (Factor 2.0 from BSIM3 model)
        Real vdsat_long = vgst;               // Long-channel Vdsat ≈ Vgst
        Real vs_factor = 1.0 + vgst / (ecrit * leff);
        return vdsat_long / vs_factor;
    }

    return vgst;
}

Real BSIM3Core::calc_dibl(Real vds, Real leff) const {
    if (std::abs(params_.pdiblc1) < 1e-12 || leff < 1e-12) {
        return 0.0;
    }

    Real dibl_factor = params_.pdiblc1 * params_.pdiblc2 * vds;
    Real denom = 1.0 + params_.pdiblc2 * vds / leff;
    return -dibl_factor / denom;
}

Real BSIM3Core::calc_clm_factor(Real vds, Real vdsat, Real leff) const {
    if (vds <= vdsat) {
        return 1.0;
    }
    if (std::abs(params_.pclm) < 1e-12 || leff < 1e-12) {
        return 1.0;
    }

    // Simple linear CLM approximation: Id ∝ (1 + λ·(Vds - Vdsat)).
    // Keep λ small so that saturation-region Id increases only modestly.
    Real vds_eff = vds - vdsat;
    Real lambda = params_.pclm * 0.05; // empirical small-signal slope [1/V]
    Real clm_factor = 1.0 + lambda * vds_eff;
    if (clm_factor < 0.0) {
        clm_factor = 0.0;
    }
    return clm_factor;
}

Real BSIM3Core::calc_vdseff(Real vds, Real vdsat) const {
    // Effective Vds smoothing around Vdsat using BSIM-style delta parameter.
    // This is a simplified adaptation of the BSIM4 Vdseff formula:
    //
    //   T1 = Vdsat - Vds - delta
    //   T2 = sqrt(T1^2 + 4·delta·Vdsat)
    //   if T1 >= 0:
    //       Vdseff = Vdsat - 0.5·(T1 + T2)
    //   else:
    //       Vdseff = Vdsat·(1 - 2·delta/(T2 - T1))
    //
    // and clamped so that 0 <= Vdseff <= Vds.

    if (vds <= 0.0 || vdsat <= 0.0) {
        return 0.0;
    }

    Real delta = params_.delta;
    if (delta <= 0.0) {
        // No smoothing requested: clamp to min(Vds, Vdsat).
        return std::max(0.0, std::min(vds, vdsat));
    }

    Real T1 = vdsat - vds - delta;
    Real T2 = std::sqrt(T1 * T1 + 4.0 * delta * vdsat);
    Real vdseff;

    if (T1 >= 0.0) {
        vdseff = vdsat - 0.5 * (T1 + T2);
    } else {
        Real denom = std::max(T2 - T1, 1.0e-12);
        Real T4 = (2.0 * delta) / denom;
        Real T5 = 1.0 - T4;
        vdseff = vdsat * T5;
    }

    if (vds == 0.0) {
        vdseff = 0.0;
    }

    // Clamp to physical limits
    if (vdseff > vds) {
        vdseff = vds;
    }
    if (vdseff < 0.0) {
        vdseff = 0.0;
    }

    return vdseff;
}

Real BSIM3Core::compute_ids_subthreshold(Real vgs, Real vds, Real vbs, Real vth,
                                         Real leff, Real weff, Real temperature) const {
    (void)vbs; // vbs 依存は Phase C 以降で拡張予定

    // Subthreshold region: exponential tail for Vgs below Vth but above 0V.
    //  - Ids ∝ (W/L) * exp((Vgs - Vth - VOFF)/(n*Vt)) * (1 - exp(-Vds/(n*Vt)))
    //  - n: subthreshold slope factor (NFACtor)
    //  - VOFF: offset voltage near subthreshold

    // Deep accumulation / negative gate bias: treat as effectively zero.
    if (vgs <= 0.0) {
        return 0.0;
    }

    // Thermal voltage
    Real vt = (temperature > 0.0) ? (kBoltz * temperature / qElectron) : 0.0259;

    // Subthreshold slope factor n
    Real n = (params_.nfactor > 0.1) ? params_.nfactor : 1.5;
    Real nvt = n * vt;

    // Base current scaling (~1 µA at W=L=1µm, near threshold) with W/L scaling.
    Real i0 = 1e-6;
    if (leff > 0.0 && weff > 0.0) {
        i0 *= (weff / 1.0e-6) * (1.0e-6 / leff);
    }

    // Gate overdrive (below threshold)
    Real vgst = vgs - vth;
    Real arg = (vgst - params_.voff) / nvt;
    arg = std::clamp(arg, Real(-50.0), Real(50.0));
    Real exp_term = std::exp(arg);

    // Vds dependence: 1 - exp(-Vds_eff / (n*Vt)), with simple saturation.
    Real vds_eff = (vds > 0.0) ? vds : 0.0;
    // Limit Vds in subthreshold to avoid unrealistically large slope.
    Real vds_limit = 2.0 * nvt;
    if (vds_eff > vds_limit) {
        vds_eff = vds_limit;
    }
    Real arg_vds = -vds_eff / nvt;
    if (arg_vds < -50.0) {
        arg_vds = -50.0;
    }
    Real term_vds = 1.0 - std::exp(arg_vds);
    if (term_vds < 0.0) {
        term_vds = 0.0;
    }

    Real ids_sub = i0 * exp_term * term_vds;
    return (ids_sub > 0.0) ? ids_sub : 0.0;
}

Real BSIM3Core::compute_ids_raw(Real vgs, Real vds, Real vbs, Real temperature,
                                Real& vth_out, Real& vdsat_out) const {
    Real leff = calc_leff();
    Real weff = calc_weff();

    vth_out = 0.0;
    vdsat_out = 0.0;

    if (leff <= 0.0 || weff <= 0.0) {
        return 0.0;
    }

    // Threshold voltage (body effect, SCE, narrow width, temperature, DIBL)
    Real vth_base = calc_vth(vbs, leff, weff);
    vth_base = apply_temp_vth(vth_base, vbs, temperature);
    Real dibl = calc_dibl(vds, leff);
    Real vth = vth_base + dibl;
    vth_out = vth;

    Real vgst = vgs - vth;

    // Temporary debug: trace core quantities for the reference BSIM3N_REF card
    // used in scripts/bench/compare_bsim3_ids.py (tox=4e-9, vth0=0.5, W=10u, L=1u).
    // This is limited to a few calls to avoid excessive logging.
    if (std::abs(params_.tox - 4.0e-9) < 1e-12 &&
        std::abs(params_.vth0 - 0.5) < 1e-6 &&
        std::abs(params_.w - 10.0e-6) < 1e-9 &&
        std::abs(params_.l - 1.0e-6) < 1e-9) {
        thread_local static int debug_count = 0;
        if (debug_count < 20) {
            spdlog::info(
                "[BSIM3Core_Debug] vgs={:.3f} V, vds={:.3f} V, vbs={:.3f} V, "
                "leff={:.3e} m, weff={:.3e} m, nsub={:.3e} cm^-3, nch={:.3e} cm^-3, "
                "vth0={:.6f} V, vth_base={:.6f} V, dibl={:.6e} V, vth={:.6f} V, vgst={:.6f} V",
                vgs, vds, vbs,
                leff, weff,
                params_.nsub, params_.nch,
                params_.vth0, vth_base, dibl, vth, vgst);
            ++debug_count;
        }
    }

    // Subthreshold region: BSIM-style exponential tail (Vgs below Vth, Vgs > 0).
    if (vgst <= 0.0) {
        return compute_ids_subthreshold(vgs, vds, vbs, vth, leff, weff, temperature);
    }

    // Strong inversion region
    Real ueff = calc_mobility(vgs, vds, vbs, vth, leff, weff);
    ueff = apply_temp_mobility(ueff, temperature);

    Real vdsat = calc_vdsat(vgs, vds, vbs, vth, leff, weff, temperature);
    vdsat_out = vdsat;

    // Gate oxide capacitance per unit area
    constexpr Real eps_ox = 3.9 * 8.854e-12; // F/m
    Real cox = eps_ox / params_.tox;

    // β = μeff * Cox * W/L (μ in m²/Vs)
    Real ueff_si = ueff * 1e-4;
    Real beta = ueff_si * cox * (weff / leff);

    Real ids = 0.0;

    if (vds <= 0.0) {
        ids = 0.0;
    } else {
        // Recalculate vsat and esat for unified Ids equation
        Real vsat = apply_temp_vsat(params_.vsat, temperature);
        Real esat = (ueff_si > 1e-12) ? (2.0 * vsat / ueff_si) : 1e12;

        // Use effective Vds (Vdseff) for unified current equation
        Real vdseff = calc_vdseff(vds, vdsat);

        // Unified single-equation current (Linear + Saturation + Velocity Saturation)
        // Ids = beta * Vdseff * (Vgst - 0.5 * Abulk * Vdseff) / (1 + Vdseff / (Esat * L))
        // Assuming Abulk = 1 for now (simplified BSIM3)
        
        Real abulk = 1.0;
        Real term_vds = 1.0 + vdseff / (esat * leff);
        
        ids = beta * vdseff * (vgst - 0.5 * abulk * vdseff) / term_vds;

        // CLM applied via actual Vds (affects slope in saturation)
        Real clm_factor = calc_clm_factor(vds, vdsat, leff);
        ids *= clm_factor;
    }

    if (ids < 0.0) {
        ids = 0.0;
    }

    return ids;
}

BSIM3OperatingPoint BSIM3Core::evaluate(Real vgs, Real vds, Real vbs,
                                        Real temperature) const {
    BSIM3OperatingPoint op{};
    Real vth = 0.0;
    Real vdsat = 0.0;

    op.ids = compute_ids_raw(vgs, vds, vbs, temperature, vth, vdsat);
    op.vth = vth;
    op.vdsat = vdsat;
    return op;
}

} // namespace kirimaspice::devices
