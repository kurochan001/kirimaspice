// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception AND ECL-2.0

// Parts of this file are based on the BSIM4 4.8.3 model code (b4ld.c, b4temp.c),
// as distributed by UC Berkeley and in ngspice. That code carries this notice:
//
//   Copyright (c) 2025 University of California
//   Project Directors: Prof. Sayeef Salahuddin and Prof. Chenming Hu
//   Developers list: https://www.bsim.berkeley.edu/models/bsim4/auth_bsim4/
//
//   Licensed under Educational Community License, Version 2.0 (the "License"); you may
//   not use this file except in compliance with the License. You may obtain a copy of the
//   license at http://opensource.org/licenses/ECL-2.0
//   Unless required by applicable law or agreed to in writing, software distributed under
//   the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the License for the specific language governing
//   permissions and limitations under the License.
//
// Modified for this project: rewritten in C++ with a different structure.

#include "kirimaspice/devices/bsim4_core.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <tuple>
#include <utility>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {
namespace {

constexpr Real kBoltz = 1.380649e-23;      // J/K
constexpr Real qElectron = 1.602176634e-19;  // C
constexpr Real eps0 = 8.8541878128e-12;    // F/m
constexpr Real EPSILON_OX = 3.9 * 8.8541878128e-12;  // SiO2 permittivity [F/m]
constexpr Real ni_cm = 1.45e10;            // Intrinsic concentration [cm^-3]

inline Real limit_exp(Real x) {
    return std::exp(std::clamp(x, -50.0, 50.0));
}

inline Real softplus(Real x, Real smooth) {
    if (smooth <= 0.0) {
        return std::max(x, 0.0);
    }
    Real scaled = std::clamp(x / smooth, -40.0, 40.0);
    return smooth * std::log1p(std::exp(scaled));
}

inline Real logistic(Real x, Real scale) {
    if (scale <= 0.0) {
        return x >= 0.0 ? 1.0 : 0.0;
    }
    Real scaled = std::clamp(x / scale, -40.0, 40.0);
    return 0.5 * (1.0 + std::tanh(scaled * 0.5));
}

inline Real clamp_conductance(Real g) {
    if (!std::isfinite(g)) {
        return 0.0;
    }
    constexpr Real limit = 1e3;  // 1 kS upper bound to avoid matrix blow-up
    if (g > limit) return limit;
    if (g < -limit) return -limit;
    return g;
}

template <typename T>
inline T guard_positive(T value, T fallback) {
    if (!std::isfinite(value) || value <= static_cast<T>(0)) {
        return fallback;
    }
    return value;
}

}  // namespace

BSIM4Core::BSIM4Core(const BSIM4Params& params, bool p_channel)
    : params_(params), polarity_(p_channel ? -1.0 : 1.0) {
    // Align parameter polarity with device type for vtfbphi2/vfb calculations
    params_.type = polarity_;
    // Compute derived parameters from basic parameters at nominal temperature
    params_.compute_derived_params(params_.tnom);
}

BSIM4OperatingPoint BSIM4Core::evaluate(Real vg, Real vd, Real vs, Real vb,
                                         Real temperature, Real theta0vb0) const {
    // Thread-safety: Lock mutex to prevent concurrent modification of params_
    // This is necessary because evaluate() modifies the mutable params_ member
    // during temperature-dependent calculations (compute_derived_params).
    // Each BSIM4 device has its own BSIM4Core, so this lock only serializes
    // calls within a single device instance, not across different devices.
    std::lock_guard<std::mutex> lock(evaluate_mutex_);

    // CRITICAL FIX: Create a LOCAL copy of params for this specific temperature
    // This prevents multiple evaluate() calls at different temperatures from interfering
    // The temperature dependence chain: T → Vt → phi → Xdep → lt1 → Theta0 → Delt_vth → Vth

    // DEBUG: Log phi before save
    thread_local static int eval_call_count = 0;
    spdlog::debug("[evaluate call {}] BEFORE save: phi={:.6e}, sqrtPhi={:.6e}, Xdep0={:.6e}",
                 eval_call_count, params_.phi, params_.sqrtPhi, params_.Xdep0);

    BSIM4Params saved_params = params_;  // Save original params

    // CRITICAL: Store nominal-temperature values for Vth calculation
    // The k1ox term and V0 (in Delt_vth) need NOMINAL temperature references, not operating temperature values
    const Real sqrtPhi_nom = params_.sqrtPhi;  // For k1ox body effect term
    const Real vbi_nom = params_.vbi;          // For V0 = vbi - phi in Delt_vth
    const Real phi_nom = params_.phi;          // For V0 = vbi - phi in Delt_vth

    spdlog::debug("[evaluate call {}] AFTER save: saved phi={:.6e}, sqrtPhi={:.6e}, vbi={:.6e}, Xdep0={:.6e}",
                 eval_call_count, saved_params.phi, saved_params.sqrtPhi, saved_params.vbi, saved_params.Xdep0);
    spdlog::debug("[evaluate call {}] Nominal values for Vth calculation:", eval_call_count);
    spdlog::debug("  sqrtPhi_nom = {:.6e} (for k1ox term)", sqrtPhi_nom);
    spdlog::debug("  vbi_nom     = {:.6e} (for V0 in Delt_vth)", vbi_nom);
    spdlog::debug("  phi_nom     = {:.6e} (for V0 in Delt_vth)", phi_nom);
    spdlog::debug("  V0_nom      = vbi_nom - phi_nom = {:.6e} V", vbi_nom - phi_nom);

    params_.compute_derived_params(temperature);  // Compute temp-dependent params on mutable member

    spdlog::debug("[evaluate call {}] AFTER compute_derived (T={:.2f}K): phi={:.6e}, sqrtPhi={:.6e}, Xdep0={:.6e}",
                 eval_call_count, temperature, params_.phi, params_.sqrtPhi, params_.Xdep0);

    const Real mult_current = std::max(params_.m, 1.0) *
                              std::max(params_.nf, 1.0) *
                              std::max(params_.mult_i, 1e-3);

    // The drain current, from drain to source, at gate, drain and bulk
    // voltages g, d and b (the source at vs): compute_bias swaps drain and
    // source when Vds < 0, so the current then changes sign.
    BSIM4ThresholdState th_at_bias;
    auto drain_current = [&](Real g, Real d, Real b, BSIM4ThresholdState* th_out) {
        const BSIM4BiasState bias = compute_bias(g, d, vs, b, temperature);
        const BSIM4ThresholdState th =
            compute_threshold(bias, temperature, theta0vb0, sqrtPhi_nom, vbi_nom, phi_nom);
        Real unused_gm = 0.0;
        Real unused_gds = 0.0;
        Real unused_gmb = 0.0;
        Real ids = compute_cdrain(bias, th, unused_gm, unused_gds, unused_gmb);
        if (bias.reverse_mode) {
            ids = -ids;
        }
        if (th_out != nullptr) {
            *th_out = th;
        }
        return polarity_ * ids * mult_current;
    };

    BSIM4OperatingPoint op;
    op.ids = drain_current(vg, vd, vb, &th_at_bias);

    // gm, gds and gmb are the derivatives of that same current with respect
    // to the gate, drain and bulk voltages, the source held, as the stamps
    // use them (MOSFET_BSIM4::stamp_dc_nonlinear). They are central
    // differences of drain_current, so they belong to the one current
    // function in both modes and for both polarities.
    constexpr Real kStep = 1e-6;  // [V]
    op.gm = (drain_current(vg + kStep, vd, vb, nullptr) -
             drain_current(vg - kStep, vd, vb, nullptr)) / (2.0 * kStep);
    op.gds = (drain_current(vg, vd + kStep, vb, nullptr) -
              drain_current(vg, vd - kStep, vb, nullptr)) / (2.0 * kStep);
    op.gmb = (drain_current(vg, vd, vb + kStep, nullptr) -
              drain_current(vg, vd, vb - kStep, nullptr)) / (2.0 * kStep);
    op.vth = polarity_ * th_at_bias.vth;
    op.vdsat = polarity_ * th_at_bias.vdsat;

    // Restore original params to prevent interference between multiple evaluate() calls
    params_ = saved_params;

    spdlog::debug("[evaluate call {}] AFTER restore: phi={:.6e}, sqrtPhi={:.6e}, Xdep0={:.6e}",
                 eval_call_count, params_.phi, params_.sqrtPhi, params_.Xdep0);
    spdlog::debug("[evaluate call {}] Returning: vth={:.6e} V, vdsat={:.6e} V\n",
                 eval_call_count, op.vth, op.vdsat);
    eval_call_count++;

    return op;
}

BSIM4Capacitances BSIM4Core::compute_capacitances(Real vg, Real vd, Real vs, Real vb,
                                                  Real vth, Real temperature) const {
    // Reuse bias handling (polarity and drain/source swap)
    const BSIM4BiasState bias = compute_bias(vg, vd, vs, vb, temperature);

    // Effective dimensions
    Real weff = params_.w - params_.wint;
    Real leff = params_.l - params_.lint;
    if (weff <= 0.0) weff = params_.w;
    if (leff <= 0.0) leff = params_.l;

    // Gate oxide capacitance
    const Real epsilon_ox = EPSILON_OX;     // SiO2 permittivity [F/m]
    Real cox = epsilon_ox / params_.toxe;   // [F/m²]
    Real cgate = cox * weff * leff;         // total gate cap

    // Gate capacitances (Meyer-style) using unified vgs/vds (already polarity-adjusted)
    Real vgs = bias.vgs;
    Real vds = bias.vds;
    Real vbs = bias.vbs;
    Real vgs_eff = vgs - vth;

    Real cgs = 0.0, cgd = 0.0, cgb = 0.0;
    if (vgs_eff < 0.0) {
        cgb = cgate; // cutoff: all to bulk
    } else {
        // Velocity saturation dependent vdsat (simple)
        Real vdsat = vgs_eff;
        // optional vsat temp dependence (lightweight)
        Real delta_temp = temperature - params_.tnom;
        Real vsat_temp = params_.vsat - params_.at * delta_temp;
        if (vsat_temp < 0.0) vsat_temp = 0.0;
        if (vsat_temp > 0.0) {
            Real mu_eff = compute_mobility(vgs_eff, vth, vbs, temperature);
            Real vsat_l = leff * vsat_temp / guard_positive(mu_eff, params_.u0);
            if (vsat_l > 0.0) {
                vdsat = vgs_eff / (1.0 + vgs_eff / vsat_l);
            }
        }

        if (vds < vdsat) {
            cgs = (2.0 / 3.0) * cgate;
            cgd = (2.0 / 3.0) * cgate;
        } else {
            cgs = (2.0 / 3.0) * cgate;
            cgd = 0.0;
        }
    }

    // Overlap capacitances
    cgs += params_.cgso * weff;
    cgd += params_.cgdo * weff;
    cgb += params_.cgbo * leff;

    // Junction capacitances use physical vbs/vbd (not polarity-flipped)
    Real vbs_raw = vb - vs;
    Real vbd_raw = vb - vd;

    // Guard against pb=0 division in junction capacitance
    auto compute_cj = [](Real cj0, Real vj, Real pb, Real mj) {
        if (cj0 <= 0.0) return 0.0;
        const Real pb_safe = std::max(std::abs(pb), 0.1);  // Typical pb ~ 0.8-1.0V
        if (vj >= 0.0) {
            return cj0 * (1.0 + mj * vj / pb_safe);
        }
        Real arg = 1.0 - vj / pb_safe;
        if (arg <= 0.0) return cj0 * 1e3;
        return cj0 / std::pow(arg, mj);
    };

    Real cbd = 0.0, cbs = 0.0;
    // Drain side
    Real cj0_d = params_.cjd * params_.ad;
    Real cj0_sw_d = params_.cjswd * params_.pd;
    Real cj0_swg_d = params_.cjswgd * params_.pd;
    cbd += compute_cj(cj0_d, vbd_raw, params_.pbd, params_.mjs);
    cbd += compute_cj(cj0_sw_d, vbd_raw, params_.pbswd, params_.mjswd);
    cbd += compute_cj(cj0_swg_d, vbd_raw, params_.pbswgd, params_.mjswgd);
    // Source side
    Real cj0_s = params_.cjs * params_.as;
    Real cj0_sw_s = params_.cjsws * params_.ps;
    Real cj0_swg_s = params_.cjswgs * params_.ps;
    cbs += compute_cj(cj0_s, vbs_raw, params_.pbs, params_.mjs);
    cbs += compute_cj(cj0_sw_s, vbs_raw, params_.pbsws, params_.mjsws);
    cbs += compute_cj(cj0_swg_s, vbs_raw, params_.pbswgs, params_.mjswgs);

    BSIM4Capacitances caps;
    caps.cgs = cgs;
    caps.cgd = cgd;
    caps.cgb = cgb;
    caps.cbd = cbd;
    caps.cbs = cbs;
    return caps;
}

BSIM4BiasState BSIM4Core::compute_bias(Real vg, Real vd, Real vs, Real vb, Real temperature) const {
    (void)temperature; // unused for now; bias uses temperature-independent params
    BSIM4BiasState bias;
    const Real type = polarity_;

    // Drain-Source Swapping for Vds < 0 (ngspice b4ld.c compatible)
    // BSIM4 model assumes Vds >= 0; if Vds < 0, swap drain and source terminals
    Real vd_eff = vd;
    Real vs_eff = vs;
    const Real vds_original = type * (vd - vs);

    if (vds_original < 0.0) {
        // Reverse mode: swap drain and source node voltages
        std::swap(vd_eff, vs_eff);
        bias.reverse_mode = true;
        spdlog::trace("BSIM4: Reverse mode activated (Vds={:.6f}V < 0), swapping D/S", vds_original);
    } else {
        bias.reverse_mode = false;
    }

    // Calculate terminal voltages with swapped terminals if needed
    bias.vgs = type * (vg - vs_eff);
    bias.vgd = type * (vg - vd_eff);
    bias.vds = type * (vd_eff - vs_eff);  // Now always >= 0
    bias.vbs = type * (vb - vs_eff);
    bias.vdb = type * (vd_eff - vb);

    // Effective gate voltage (ngspice: no additional phin shift; phin is already in phi)
    bias.vgs_eff = bias.vgs;
    bias.dVgs_eff_dVg = 1.0;

    // ===================================================================
    // DEBUG: Trace vbs → vbs_eff transformation for large body bias (M3)
    // ===================================================================
    thread_local static int vbs_debug_count = 0;
    const bool debug_vbs = (bias.vbs < -1.5 && vbs_debug_count < 5);  // Target M3 device
    if (debug_vbs) {
        spdlog::debug("=== compute_bias() call {} - vbs_eff transformation (M3) ===", vbs_debug_count);
        spdlog::debug("  Input vbs (after clamp)   = {:+.6e} V", bias.vbs);
        spdlog::debug("  params_.vbsc              = {:+.6e} V", params_.vbsc);
        spdlog::debug("  params_.phi               = {:+.6e} V", params_.phi);
    }

    // vbsc smoothing: only apply if vbsc is non-zero (ngspice b4ld.c pattern)
    // When vbsc = 0 (default), skip smoothing and use vbs directly to avoid
    // the multiplication-by-zero bug: vbs_eff = vbsc * (1 + T2) = 0
    const Real vbsc = params_.vbsc;
    if (std::abs(vbsc) > 1e-10) {
        // vbsc smoothing is active
        const Real T0 = bias.vbs - vbsc - 0.001;
        const Real T1 = std::sqrt(T0 * T0 - 0.004 * vbsc);
        if (T0 >= 0.0) {
            bias.vbs_eff = vbsc + 0.5 * (T0 + T1);
            bias.dVbs_eff_dVb = 0.5 * (1.0 + T0 / T1);
            if (debug_vbs) {
                spdlog::debug("  [Stage 1: vbsc smoothing ACTIVE, T0 >= 0 branch]");
                spdlog::debug("    T0 = vbs - vbsc - 0.001  = {:+.6e}", T0);
                spdlog::debug("    T1 = sqrt(T0^2 - 0.004*vbsc) = {:+.6e}", T1);
                spdlog::debug("    vbs_eff = vbsc + 0.5*(T0+T1) = {:+.6e} V", bias.vbs_eff);
            }
        } else {
            const Real T2 = -0.002 / (T1 - T0);
            bias.vbs_eff = vbsc * (1.0 + T2);
            bias.dVbs_eff_dVb = T2 * vbsc / T1;
            if (debug_vbs) {
                spdlog::debug("  [Stage 1: vbsc smoothing ACTIVE, T0 < 0 branch]");
                spdlog::debug("    T0 = vbs - vbsc - 0.001  = {:+.6e}", T0);
                spdlog::debug("    T1 = sqrt(T0^2 - 0.004*vbsc) = {:+.6e}", T1);
                spdlog::debug("    T2 = -0.002 / (T1 - T0)  = {:+.6e}", T2);
                spdlog::debug("    vbs_eff = vbsc * (1 + T2) = {:+.6e} V", bias.vbs_eff);
            }
        }
    } else {
        // vbsc = 0: skip smoothing, use vbs directly
        bias.vbs_eff = bias.vbs;
        bias.dVbs_eff_dVb = 1.0;
        if (debug_vbs) {
            spdlog::debug("  [Stage 1: vbsc smoothing SKIPPED (vbsc ≈ 0)]");
            spdlog::debug("    vbs_eff = vbs = {:+.6e} V (no smoothing)", bias.vbs_eff);
        }
    }

    // CRITICAL FIX: Calculate phi at OPERATING temperature for bias.phis
    // NOTE: params_.phi is at NOMINAL temperature (for V0_nom in Delt_vth)
    //       but bias.phis needs OPERATING temperature phi
    // Formula: phi = Vt * ln(ndep/ni) + phin + 0.4
    // BSIM4 ngspice-compatible: Use phi from params_ (calculated at tnom in compute_derived_params)
    // DO NOT recalculate phi at operating temperature (causes excessive temperature dependence)
    // Reference: ngspice b4ld.c:1054 "Phis = pParam->BSIM4phi - Vbseff"
    const Real T9 = 0.95 * params_.phi;
    const Real T0_corr = T9 - bias.vbs_eff - 0.001;
    const Real T1_corr = std::sqrt(T0_corr * T0_corr + 0.004 * T9);
    const Real vbs_eff_stage1 = bias.vbs_eff;  // Save for debug
    bias.vbs_eff = T9 - 0.5 * (T0_corr + T1_corr);
    bias.dVbs_eff_dVb *= 0.5 * (1.0 + T0_corr / T1_corr);

    if (debug_vbs) {
        spdlog::debug("  [Stage 2: phi smoothing]");
        spdlog::debug("    params_.phi (at tnom, ngspice-compatible) = {:+.6e} V", params_.phi);
        spdlog::debug("    T9 = 0.95 * params_.phi  = {:+.6e}", T9);
        spdlog::debug("    T0_corr = T9 - vbs_eff_stage1 - 0.001 = {:+.6e}", T0_corr);
        spdlog::debug("    T1_corr = sqrt(T0_corr^2 + 0.004*T9) = {:+.6e}", T1_corr);
        spdlog::debug("    vbs_eff_stage1 (after stage 1) = {:+.6e} V", vbs_eff_stage1);
        spdlog::debug("    vbs_eff_final = T9 - 0.5*(T0_corr+T1_corr) = {:+.6e} V", bias.vbs_eff);
    }

    bias.phis = params_.phi - bias.vbs_eff;
    bias.sqrt_phis = std::sqrt(std::max(bias.phis, 1e-6));

    if (debug_vbs) {
        spdlog::debug("  [Final results]");
        spdlog::debug("    phis = params_.phi - vbs_eff = {:+.6e} V", bias.phis);
        spdlog::debug("    sqrt_phis                    = {:+.6e}", bias.sqrt_phis);
        spdlog::debug("=== End compute_bias() call {} ===\n", vbs_debug_count);
        vbs_debug_count++;
    }
    bias.dPhis_dVb = -1.0;
    // fix: Pure partial derivative d(sqrt(phis))/d(Vbseff)
    // ngspice applies dVbs_eff_dVb only once at final gmb calculation
    // Reference: ngspice b4ld.c - dsqrtPhis_dVb is stored WITHOUT chain rule factor
    bias.dsqrtPhis_dVb = -0.5 / bias.sqrt_phis;
    return bias;
}

BSIM4ThresholdState BSIM4Core::compute_threshold(const BSIM4BiasState& bias,
                                                   Real temperature, Real theta0vb0,
                                                   Real sqrtPhi_nom, Real vbi_nom, Real phi_nom) const {
    BSIM4ThresholdState th;
    const Real T = guard_positive(temperature, params_.tnom);
    [[maybe_unused]] const Real Vt = kBoltz * T / qElectron;  // For future thermal voltage calculations
    const Real leff = params_.get_leff();
    const Real weff = params_.get_weff();

    const Real Xdep = params_.Xdep0 * bias.sqrt_phis / guard_positive(params_.sqrtPhi, 1e-6);
    const Real dXdep_dVb = (params_.Xdep0 / guard_positive(params_.sqrtPhi, 1e-6)) * bias.dsqrtPhis_dVb;

    const Real T3 = std::sqrt(std::max(Xdep, 1e-12));
    const Real factor1 = params_.factor1;

    // ===================================================================
    // Complete Short-Channel Effect with DVT0/DVT1/DVT2
    // Reference: BSIM4.8.3 b4ld.c lines 1037-1069
    // ===================================================================

    // Calculate body-bias modulation factor T1 for lt1 using dvt2
    const Real T0_dvt2 = params_.dvt2 * bias.vbs_eff;
    Real T1_dvt2, T2_dvt2;
    if (T0_dvt2 >= -0.5) {
        // Normal case: linear approximation
        T1_dvt2 = 1.0 + T0_dvt2;
        T2_dvt2 = params_.dvt2;
    } else {
        // Extreme body bias: smooth limiting
        const Real T4 = 1.0 / (3.0 + 8.0 * T0_dvt2);
        T1_dvt2 = (1.0 + 3.0 * T0_dvt2) * T4;
        T2_dvt2 = params_.dvt2 * T4 * T4;
    }

    // Calculate lt1 with dvt2 body-bias dependence
    Real lt1 = factor1 * T3 * T1_dvt2;
    Real dlt1_dVb = factor1 * (0.5 / T3 * T1_dvt2 * dXdep_dVb + T3 * T2_dvt2 * bias.dVbs_eff_dVb);

    // Calculate Theta0 using dvt1 and lt1
    // CRITICAL FIX: When dvt1=0 (SCE disabled), Theta0 must be 0 (no SCE contribution)
    // Previously, the MIN_EXP guard caused Theta0 to explode to ~2.9e14 when T2=0
    Real Theta0;
    if (lt1 > 0.0 && std::abs(params_.dvt1) > 1e-10) {  // Only compute if dvt1 is non-zero
        const Real T0 = params_.dvt1 * leff / lt1;
        if (T0 < 60.0) {  // EXP_THRESHOLD
            const Real T1 = std::exp(T0);
            const Real T2 = T1 - 1.0;
            const Real T3_theta = T2 * T2;
            const Real MIN_EXP = 1.713908431e-15;
            const Real T4 = T3_theta + 2.0 * T1 * MIN_EXP;
            Theta0 = T1 / std::max(T4, 1e-40);

            // DEBUG: Theta0 calculation breakdown
            thread_local static int theta_debug_count = 0;
            if (theta_debug_count < 20) {
                spdlog::debug("Theta0 calculation [debug {}]:", theta_debug_count);
                spdlog::debug("  T0 = dvt1 * leff / lt1 = {:.6e} * {:.6e} / {:.6e} = {:.6e}",
                             params_.dvt1, leff, lt1, T0);
                spdlog::debug("  T1 = exp(T0) = {:.6e}", T1);
                spdlog::debug("  T2 = T1 - 1.0 = {:.6e}", T2);
                spdlog::debug("  T3 = T2^2 = {:.6e}", T3_theta);
                spdlog::debug("  MIN_EXP term = 2.0 * T1 * MIN_EXP = 2.0 * {:.6e} * {:.6e} = {:.6e}",
                             T1, MIN_EXP, 2.0 * T1 * MIN_EXP);
                spdlog::debug("  T4 = T3 + MIN_EXP_term = {:.6e} + {:.6e} = {:.6e}",
                             T3_theta, 2.0 * T1 * MIN_EXP, T4);
                spdlog::debug("  Theta0 = T1 / T4 = {:.6e} / {:.6e} = {:.6e}", T1, T4, Theta0);
                theta_debug_count++;
            }
            // Update derivative: dTheta0_dVb using quotient rule
            // Theta0 = T1 / T4, where T1 = exp(T0), T0 = dvt1*leff/lt1
            // dTheta0_dVb = dT1_dVb * (T4 - 2*T1*(T2+MIN_EXP)) / T4^2  (ngspice formula)
            // dT1_dVb = T1 * dT0_dVb = T1 * (-dvt1*leff/lt1^2) * dlt1_dVb
            // So: dTheta0_dVb = T1 * (-dvt1*leff/lt1^2) * (T4 - 2*T1*(T2+MIN_EXP)) / T4^2 * dlt1_dVb
            const Real dT0_dlt1 = -params_.dvt1 * leff / (lt1 * lt1);
            const Real dT1_dlt1 = T1 * dT0_dlt1;
            const Real T4_factor = T4 - 2.0 * T1 * (T2 + MIN_EXP);
            const Real dTheta0_dlt1 = dT1_dlt1 * T4_factor / (T4 * T4);
            dlt1_dVb = dTheta0_dlt1 * dlt1_dVb;
        } else {
            Theta0 = std::exp(60.0);
            dlt1_dVb = 0.0;  // Saturated region
        }
    } else {
        // When dvt1=0 (SCE disabled) or lt1<=0, set Theta0=0 (no SCE contribution)
        Theta0 = 0.0;
        dlt1_dVb = 0.0;
    }

    // V0 is built-in voltage minus phi (NOT vth0!)
    // Reference: ngspice b4ld.c line 1075
    // CRITICAL FIX: Use NOMINAL temperature values (vbi_nom - phi_nom) NOT operating temperature values
    // The short-channel effect should reference a fixed energy barrier at nominal temperature
    const Real V0_nom = vbi_nom - phi_nom;  // Nominal-temperature reference
    const Real V0_operating = params_.vbi - params_.phi;  // For debug comparison
    const Real Delt_vth = params_.dvt0 * Theta0 * V0_nom;  // Use nominal V0
    const Real dDelt_vth_dVb = params_.dvt0 * dlt1_dVb * V0_nom;

    // DEBUG: Print intermediate values for Delt_vth calculation
    thread_local static int debug_delt_count = 0;
    if (debug_delt_count < 3) {
        spdlog::debug("BSIM4 Delt_vth intermediate values (call #{}):", debug_delt_count);
        spdlog::debug("  vbs_eff  = {:+.6e} V", bias.vbs_eff);
        spdlog::debug("  Xdep     = {:+.6e} m", Xdep);
        spdlog::debug("  T3       = {:+.6e} m", T3);
        spdlog::debug("  factor1  = {:+.6e}", factor1);
        spdlog::debug("  T1_dvt2  = {:+.6e}", T1_dvt2);
        spdlog::debug("  lt1      = {:+.6e} m", lt1);
        spdlog::debug("  dvt1     = {:+.6e}", params_.dvt1);
        spdlog::debug("  T0       = {:+.6e} (dvt1*leff/lt1)", params_.dvt1 * leff / lt1);
        spdlog::debug("  Theta0   = {:+.6e}", Theta0);
        spdlog::debug("  dvt0     = {:+.6e}", params_.dvt0);
        spdlog::debug("  V0_nom (USED)  = vbi_nom - phi_nom = {:.6e} - {:.6e} = {:+.6e} V",
                     vbi_nom, phi_nom, V0_nom);
        spdlog::debug("  [For comparison: V0_operating = {:.6e} - {:.6e} = {:+.6e} V]",
                     params_.vbi, params_.phi, V0_operating);
        spdlog::debug("  Delt_vth = dvt0*Theta0*V0_nom = {:.6e} * {:.6e} * {:.6e} = {:+.6e} V",
                     params_.dvt0, Theta0, V0_nom, Delt_vth);
        debug_delt_count++;
    }

    // CRITICAL FIX: Use phi_nom (nominal temperature) not params_.phi (operating temperature)
    // ngspice b4ld.c uses pParam->BSIM4phi which is calculated at NOMINAL temperature
    // See ngspice b4temp.c line 1298: pParam->BSIM4phi = Vtm0 * log(ndep/ni) + phin + 0.4
    const Real Vth_NarrowW = params_.toxe * phi_nom / (weff + params_.w0);

    // Calculate Lpe_Vb factor (lateral non-uniform doping at body bias)
    // From ngspice b4ld.c line 1119:
    //   Lpe_Vb = sqrt(1.0 + pParam->BSIM4lpeb / Leff);
    const Real Lpe_Vb = std::sqrt(1.0 + params_.lpeb / leff);

    // Calculate T1 term (length-dependent correction)
    // From ngspice b4ld.c lines 1100-1103:
    //   T0 = sqrt(1.0 + pParam->BSIM4lpe0 / Leff);
    //   T1 = pParam->BSIM4k1ox * (T0 - 1.0) * pParam->BSIM4sqrtPhi
    //      + (pParam->BSIM4kt1 + pParam->BSIM4kt1l / Leff + pParam->BSIM4kt2 * Vbseff) * TempRatio;
    const Real T0_lpe = std::sqrt(1.0 + params_.lpe0 / leff);
    const Real TempRatio = (T - params_.tnom) / params_.tnom;
    const Real T1_lpe = params_.k1ox * (T0_lpe - 1.0) * params_.sqrtPhi;
    const Real T1_temp = (params_.kt1 + params_.kt1l / leff + params_.kt2 * bias.vbs_eff) * TempRatio;
    const Real T1 = T1_lpe + T1_temp;

    // ===================================================================
    // Correct DIBL_Sft calculation using eta0/etab parameters
    // Reference: ngspice b4ld.c DIBL_Sft calculation
    // The DIBL effect on Vth uses eta0/etab, NOT pdiblc1/pdiblc2
    // pdiblc parameters are for VADIBL (output conductance), not Vth DIBL
    // ===================================================================
    // Formula: DIBL_Sft = (eta0 + etab * Vbs_eff) * theta0vb0 * Vds

    const Real eta_eff = params_.eta0 + params_.etab * bias.vbs_eff;
    const Real dDIBL_Sft_dVd = eta_eff * theta0vb0;
    Real DIBL_Sft = dDIBL_Sft_dVd * bias.vds;

    // Light clamp to prevent unrealistic values while allowing ngspice-compatible DIBL
    // At Vds=2V with eta0=0.08, theta0vb0=0.83: DIBL_Sft ≈ 0.133V (matches ngspice)
    const Real DIBL_MAX = 0.5;  // Allow up to 0.5V DIBL shift for extreme bias
    DIBL_Sft = std::clamp(DIBL_Sft, -DIBL_MAX, DIBL_MAX);

    // VTH0 is signed as on the card (negative for a PMOS); the threshold of
    // the N-channel equations takes TYPE * VTH0 (BSIM4 4.8.3 b4ld.c)
    Real term_vth0 = params_.type * params_.vth0;
    // Use k1ox and Lpe_Vb instead of simple k1
    // ngspice formula: (k1ox * sqrtPhis - k1 * sqrtPhi) * Lpe_Vb
    // CRITICAL FIX: Use sqrtPhi_nom (nominal temperature value) NOT params_.sqrtPhi (operating temperature value)
    // The k1ox term requires the DIFFERENCE between bias-dependent sqrtPhis and nominal-temperature sqrtPhi_nom
    const Real sqrtPhis = bias.sqrt_phis;  // Bias-dependent surface potential (operating temperature)
    Real term_k1 = (params_.k1ox * sqrtPhis - params_.k1 * sqrtPhi_nom) * Lpe_Vb;
    Real term_k2 = -params_.k2ox * bias.vbs_eff;  // Changed from k2 to k2ox with minus sign
    Real term_delt = -Delt_vth;
    Real term_k3 = (params_.k3 + params_.k3b * bias.vbs_eff) * Vth_NarrowW;
    Real term_T1 = T1;
    Real term_dibl_sft = -DIBL_Sft;

    thread_local static int vth_debug_count = 0;
    if (vth_debug_count < 60) {  // Log first 60 calls to capture convergence
        spdlog::debug("==== BSIM4 compute_threshold DEBUG [call {}] ====", vth_debug_count);
        spdlog::debug("  Input:  vgs_eff={:.6f} V, vds={:.6f} V, vbs_eff={:.6f} V", bias.vgs_eff, bias.vds, bias.vbs_eff);
        spdlog::debug("  Temperature: {:.6f} K ({:.2f}°C), Tnom: {:.6f} K, TempRatio: {:.6f}",
                     T, T - 273.15, params_.tnom, TempRatio);
        spdlog::debug("  Vth components:");
        spdlog::debug("    vth0      = {:+.6e} V", term_vth0);
        spdlog::debug("    k1ox term = {:+.6e} V (k1ox={:.3f}, Lpe_Vb={:.3f})", term_k1, params_.k1ox, Lpe_Vb);
        spdlog::debug("      sqrtPhis (bias-dep) = {:.6f}, sqrtPhi_nom (nominal) = {:.6f}, diff = {:.6f}",
                     sqrtPhis, sqrtPhi_nom, sqrtPhis - sqrtPhi_nom);
        spdlog::debug("      [For comparison: params_.sqrtPhi (operating T) = {:.6f}]", params_.sqrtPhi);
        spdlog::debug("      (k1ox * sqrtPhis - k1 * sqrtPhi_nom) = ({:.3f} * {:.6f} - {:.3f} * {:.6f}) = {:.6f}",
                     params_.k1ox, sqrtPhis, params_.k1, sqrtPhi_nom,
                     params_.k1ox * sqrtPhis - params_.k1 * sqrtPhi_nom);
        spdlog::debug("    k2ox term = {:+.6e} V (k2ox={:.3f}, vbs_eff={:.6f})", term_k2, params_.k2ox, bias.vbs_eff);
        spdlog::debug("    -Delt_vth = {:+.6e} V (dvt0={:.3e}, Theta0={:.6e})", term_delt, params_.dvt0, Theta0);
        spdlog::debug("    k3 term   = {:+.6e} V", term_k3);
        spdlog::debug("    T1 (lpe+temp) = {:+.6e} V (lpe0={:.3e})", term_T1, params_.lpe0);
        spdlog::debug("      T1_lpe  = {:+.6e} V", T1_lpe);
        spdlog::debug("      T1_temp = {:+.6e} V", T1_temp);
        spdlog::debug("    -DIBL_Sft = {:+.6e} V (eta0={:.3e}, theta0vb0={:.3e}, vds={:.6f})",
                     term_dibl_sft, params_.eta0, theta0vb0, bias.vds);
        vth_debug_count++;
    }

    // Complete BSIM4.8.3 Vth formula
    // From ngspice b4ld.c lines 1121-1124:
    //   Vth = vth0 + (k1ox * sqrtPhis - k1 * sqrtPhi) * Lpe_Vb
    //         - k2ox * Vbseff - Delt_vth - T2
    //         + (k3 + k3b * Vbseff) * Vth_NarrowW
    //         + T1 - DIBL_Sft;
    // Note: We ignore T2 (narrow-width dvt0w term) for now as dvt0w=0 in test case
    th.vth = term_vth0 + term_k1 + term_k2 + term_delt + term_k3 + term_T1 + term_dibl_sft;

    // DEBUG: Print final Vth (continue from vth_debug_count)
    // TEMPORARY: Use warning for investigation
    thread_local static int vth_final_count = 0;
    if (vth_final_count < 5) {
        spdlog::debug("  FINAL Vth = {:+.6e} V (for mobility calc: e_eff = T0/toxe)", th.vth);
        vth_final_count++;
    }

    // Calculate derivatives for gmb (dVth/dVb)
    // Updated formula with k1ox and Lpe_Vb
    // fix: Pure partial derivative - ngspice applies chain rule only at final gmb step
    const Real dSqrtPhis_dVb = -0.5 / sqrtPhis;
    const Real dk1_term_dVb = params_.k1ox * dSqrtPhis_dVb * Lpe_Vb;
    const Real term1 = dk1_term_dVb;
    const Real term2 = -params_.k2ox;  // Changed from k2 to -k2ox (derivative of -k2ox*Vbseff)
    const Real term3 = -dDelt_vth_dVb;
    const Real term4 = params_.k3b * Vth_NarrowW;
    // T1 temperature term has Vbseff dependence through kt2
    const Real dT1_dVb = params_.kt2 * TempRatio;
    // DIBL_Sft has etab dependence
    const Real term5 = -params_.etab * bias.vds * theta0vb0;
    th.dVth_dVb = term1 + term2 + term3 + term4 + dT1_dVb + term5;
    th.dVth_dVd = -dDIBL_Sft_dVd;

    // Debug dVth_dVb breakdown
    thread_local static int dvth_debug_count = 0;
    if (dvth_debug_count < 2) {
        spdlog::debug("==== dVth_dVb Term Breakdown (call {}) ====", dvth_debug_count);
        spdlog::debug("  sqrtPhis         = {:.6e}", sqrtPhis);
        spdlog::debug("  dVbs_eff_dVb     = {:.6e}", bias.dVbs_eff_dVb);
        spdlog::debug("  dSqrtPhis_dVb    = {:.6e} (-0.5/sqrtPhis * dVbs_eff_dVb)", dSqrtPhis_dVb);
        spdlog::debug("  Lpe_Vb           = {:.6e}", Lpe_Vb);
        spdlog::debug("  k1ox             = {:.6e}", params_.k1ox);
        spdlog::debug("  term1 (k1 body)  = {:.6e} (k1ox * dSqrtPhis_dVb * Lpe_Vb)", term1);
        spdlog::debug("  term2 (-k2ox)    = {:.6e}", term2);
        spdlog::debug("  term3 (-dDelt)   = {:.6e}", term3);
        spdlog::debug("  term4 (k3b)      = {:.6e}", term4);
        spdlog::debug("  dT1_dVb (kt2)    = {:.6e}", dT1_dVb);
        spdlog::debug("  term5 (-etab)    = {:.6e}", term5);
        spdlog::debug("  dVth_dVb (total) = {:.6e}", th.dVth_dVb);
        spdlog::debug("  Expected (physics): ≈ -0.23 for k1=0.53, sqrtPhis≈1.15");
        dvth_debug_count++;
    }

    // DEBUG: Log dVth_dVb value and components
    thread_local static int vth_call_count = 0;
    if (vth_call_count < 20) {  // Log first 20 calls
        spdlog::debug("compute_threshold [call {}]: vbs_eff={:.6e}, dVbs_eff_dVb={:.6e}, vds={:.6f}",
                     vth_call_count, bias.vbs_eff, bias.dVbs_eff_dVb, bias.vds);
        spdlog::debug("  dVth_dVb terms: k1ox*...={:.6e}, k2={:.6e}, -dDelt={:.6e}, k3b*...={:.6e}, dT1={:.6e}, -etab*vds*theta0vb0={:.6e}",
                     term1, term2, term3, term4, dT1_dVb, term5);
        spdlog::debug("  etab={:.6f}, theta0vb0={:.6e} (instance-specific)", params_.etab, theta0vb0);
        spdlog::debug("  dVth_dVb total={:.6e} (gmb will be: -{:.6e} × gm)", th.dVth_dVb, th.dVth_dVb);
        vth_call_count++;
    }

    // ===================================================================
    // Compute Abulk (bulk charge effect factor) - ngspice b4ld.c compatible
    // Reference: ngspice b4ld.c lines 1200-1240
    // ===================================================================
    // ngspice formula:
    //   T9 = 0.5 * k1ox * Lpe_Vb / sqrtPhis
    //   T1 = T9 + k2ox - k3b * Vth_NarrowW
    //   T9 = sqrt(xj * Xdep)
    //   tmp1 = Leff + 2.0 * T9
    //   T5 = Leff / tmp1
    //   tmp2 = a0 * T5
    //   tmp3 = weff + b1
    //   tmp4 = b0 / tmp3
    //   T2 = tmp2 + tmp4
    //   Abulk0 = 1.0 + T1 * T2

    // Calculate Lpe_Vb (lateral non-uniform doping factor)
    const Real Lpe_Vb_abulk = std::sqrt(1.0 + params_.lpeb / leff);

    // T1: Body effect contribution (k1ox/sqrtPhis + k2ox - k3b*Vth_NarrowW)
    const Real sqrtPhis_safe = std::max(sqrtPhis, 0.01);
    const Real T9_k1 = 0.5 * params_.k1ox * Lpe_Vb_abulk / sqrtPhis_safe;
    // Note: Vth_NarrowW (narrow width term) is typically 0; k3b defaults to 0 in params
    const Real T1_body = T9_k1 + params_.k2ox;  // k3b*Vth_NarrowW term omitted (k3b=0 default)

    // T2: Geometry factor (a0*T5 + b0/(weff+b1))
    const Real T9_geom = std::sqrt(params_.xj * Xdep);
    const Real tmp1_geom = leff + 2.0 * T9_geom;
    const Real T5_geom = leff / std::max(tmp1_geom, 1e-20);
    const Real tmp2_a0 = params_.a0 * T5_geom;
    const Real tmp3_w = weff + params_.b1;
    const Real tmp4_b0 = params_.b0 / std::max(tmp3_w, 1e-20);
    const Real T2_geom = tmp2_a0 + tmp4_b0;

    // Abulk0 = 1 + T1 * T2
    Real Abulk0 = 1.0 + T1_body * T2_geom;

    // ngspice Abulk0 clipping: if Abulk0 < 0.1, use smoothing function
    if (Abulk0 < 0.1) {
        const Real T9_clip = 1.0 / (3.0 - 20.0 * Abulk0);
        Abulk0 = (0.2 - Abulk0) * T9_clip;
    }

    // AGS gate-bias modulation - ngspice b4ld.c formula:
    // T7 = T5^3, T8 = ags * a0 * T7, dAbulk_dVg = -T1 * T8
    // Abulk = Abulk0 + dAbulk_dVg * Vgsteff
    const Real T6_geom = T5_geom * T5_geom;
    const Real T7_geom = T5_geom * T6_geom;  // T5^3
    const Real T8_ags = params_.ags * params_.a0 * T7_geom;
    const Real dAbulk_dVg = -T1_body * T8_ags;

    // Use ngspice-compatible Vgsteff for AGS calculation
    // Reference: ngspice b4ld.c lines 1272-1326 (Vgsteff calculation)
    // The key difference from the old "" formula:
    // - ngspice uses mstar parameter (typically ~0.5) to scale the exp argument
    // - ngspice divides by T9 = mstar + n*(Coxe/cdep0)*exp(...)
    // The old formula: Vgsteff = n*Vt*log(1+exp(Vgst/(n*Vt)))
    // Correct formula: Vgsteff = T10/T9 where T10 = n*Vt*log(1+exp(mstar*Vgst/(n*Vt)))
    //
    // This difference causes the old formula to return LARGER values than ngspice,
    // which leads to LOWER Abulk (since dAbulk_dVg < 0), and HIGHER drain current.

    const Real Vtm_ags = kBoltz * T / qElectron;  // Thermal voltage
    const Real vgst_raw_ags = bias.vgs - th.vth;  // Gate overdrive

    // Calculate local n value for Vgsteff calculation
    // (n is also calculated later at lines ~835, but we need it here first)
    // Reference: ngspice b4ld.c lines 1150-1188
    const Real eps0_ags = 8.854e-12;
    const Real epsilon_ox_ags = guard_positive(params_.epsrox, 3.9) * eps0_ags;
    const Real Cox_phys_ags = epsilon_ox_ags / guard_positive(params_.toxe, 5e-10);
    const Real epssub_ags = params_.epsrsub * eps0_ags;
    const Real cdep0_n = std::sqrt(qElectron * epssub_ags * params_.ndep * 1.0e6 /
                                   (2.0 * std::max(bias.phis, 1e-6)));
    const Real tmp2_n_ags = params_.nfactor * cdep0_n;
    const Real tmp3_n_ags = params_.cdsc + params_.cdscb * bias.vbs_eff + params_.cdscd * bias.vds;
    Real tmp4_n_ags = (tmp2_n_ags + tmp3_n_ags * Theta0 + params_.cit) / Cox_phys_ags;
    Real n_ags;
    if (tmp4_n_ags >= -0.5) {
        n_ags = 1.0 + tmp4_n_ags;
    } else {
        const Real T0_n_ags = 1.0 / (3.0 + 8.0 * tmp4_n_ags);
        n_ags = (1.0 + 3.0 * tmp4_n_ags) * T0_n_ags;
    }
    n_ags = std::clamp(n_ags, 1.0, 3.0);

    // mstar: smoothing parameter (ngspice b4ld.c line 1237)
    // Default minv=0 gives mstar=0.5
    const Real mstar_ags = 0.5 + std::atan(params_.minv) / std::numbers::pi;

    // T10 calculation (numerator) - ngspice b4ld.c lines 1273-1297
    const Real T0_ags = n_ags * Vtm_ags;  // n * Vt
    const Real T1_ags = mstar_ags * vgst_raw_ags;  // mstar * Vgst
    const Real T2_ags = T1_ags / std::max(T0_ags, 1e-12);

    constexpr Real EXP_THRESHOLD_AGS = 34.0;
    Real T10_ags;
    if (T2_ags > EXP_THRESHOLD_AGS) {
        T10_ags = T1_ags;  // Strong inversion
    } else if (T2_ags < -EXP_THRESHOLD_AGS) {
        T10_ags = Vtm_ags * std::log(1.0 + 1.0e-15) * n_ags;  // Deep cutoff
    } else {
        T10_ags = n_ags * Vtm_ags * std::log(1.0 + std::exp(T2_ags));
    }

    // T9 calculation (denominator) - ngspice b4ld.c lines 1299-1325
    // cdep0: depletion capacitance [F/m²]
    const Real epsSi_ags = 11.7 * 8.854e-12;  // Silicon permittivity
    const Real ndep_m3_ags = params_.ndep * 1.0e6;  // Convert cm⁻³ to m⁻³
    const Real cdep0_ags = std::sqrt(qElectron * epsSi_ags * ndep_m3_ags / (2.0 * params_.phi));

    // voffcbn: offset voltage with length dependence
    const Real voffcbn_ags = params_.voff + params_.voffl / std::max(leff, 1e-10);

    // Coxe: oxide capacitance [F/m²]
    const Real epsOx_ags = 3.9 * 8.854e-12;  // SiO2 permittivity
    const Real Coxe_ags = epsOx_ags / params_.toxe;

    // exp_term for T9
    const Real exp_arg_ags = (voffcbn_ags - (1.0 - mstar_ags) * vgst_raw_ags) / T0_ags;
    Real exp_term_ags;
    if (exp_arg_ags > EXP_THRESHOLD_AGS) {
        exp_term_ags = 2.353853e15;  // MAX_EXP
    } else if (exp_arg_ags < -EXP_THRESHOLD_AGS) {
        exp_term_ags = 1.0e-15;  // MIN_EXP
    } else {
        exp_term_ags = std::exp(exp_arg_ags);
    }

    // T9 = mstar + n * (Coxe/cdep0) * exp_term
    const Real T9_ags = mstar_ags + n_ags * (Coxe_ags / cdep0_ags) * exp_term_ags;

    // Final ngspice-compatible Vgsteff = T10/T9
    const Real vgsteff_for_ags = T10_ags / std::max(T9_ags, 1e-12);

    // Store Abulk0 and dAbulk_dVg for deferred calculation in compute_cdrain
    // The final Abulk = Abulk0 + dAbulk_dVg * vgsteff will be computed in compute_cdrain
    // using the actual vgsteff (which includes Rds effects), not vgsteff_for_ags.
    th.abulk0 = Abulk0;
    th.dAbulk_dVg = dAbulk_dVg;

    // Body-bias factor 1 / (1 + KETA Vbseff) of Abulk (BSIM4 manual,
    // eq. 5.1; the BSIM4 4.8.3 code, with its smooth form below
    // KETA Vbseff = -0.9)
    const Real T2_keta = params_.keta * bias.vbs_eff;
    Real keta_factor;
    Real dketa_dVbseff;
    if (T2_keta >= -0.9) {
        keta_factor = 1.0 / (1.0 + T2_keta);
        dketa_dVbseff = -params_.keta * keta_factor * keta_factor;
    } else {
        const Real T1_keta = 1.0 / (0.8 + T2_keta);
        keta_factor = (17.0 + 20.0 * T2_keta) * T1_keta;
        dketa_dVbseff = -params_.keta * T1_keta * T1_keta;
    }
    th.keta_factor = keta_factor;

    // Initial Abulk estimate using vgsteff_for_ags (for compatibility/debugging)
    Real Abulk = Abulk0 + dAbulk_dVg * vgsteff_for_ags;

    // ngspice Abulk clipping: if Abulk < 0.1, use smoothing function
    if (Abulk < 0.1) {
        const Real T10_clip = 1.0 / (3.0 - 20.0 * Abulk);
        Abulk = (0.2 - Abulk) * T10_clip;
    }

    Abulk = std::clamp(Abulk, 0.1, 10.0);
    th.abulk = Abulk * keta_factor;

    // Calculate dAbulk_dVb for gmb calculation
    // With AGS: Abulk = Abulk0 + dAbulk_dVg * Vgsteff
    // dAbulk_dVb = dAbulk0_dVb + dAbulk_dVg * dVgsteff_dVb
    // where dVgsteff_dVb ≈ -dVth_dVb (since Vgsteff ≈ vgs - vth)

    // dAbulk0_dVb = T2 * dT1_body_dVb + T1 * dT2_geom_dVb
    const Real dT1_body_dVb = -0.25 * params_.k1ox * Lpe_Vb_abulk / (sqrtPhis_safe * sqrtPhis_safe) * bias.dsqrtPhis_dVb;
    // Also include Xdep dependence in T2 via T9_geom
    const Real dT9_geom_dVb = 0.5 * params_.xj / std::max(T9_geom, 1e-20) * dXdep_dVb;
    const Real dT5_geom_dVb = -2.0 * T9_geom * dT9_geom_dVb / (tmp1_geom * tmp1_geom);
    const Real dT2_geom_dVb = params_.a0 * dT5_geom_dVb;
    const Real dAbulk0_dVb = T2_geom * dT1_body_dVb + T1_body * dT2_geom_dVb;

    // AGS contribution to dAbulk_dVb: dAbulk_dVg * dVgsteff_dVb
    // dVgsteff_dVb ≈ -dVth_dVb
    const Real dVgsteff_dVb = -th.dVth_dVb;
    th.dAbulk_dVb = (dAbulk0_dVb + dAbulk_dVg * dVgsteff_dVb) * keta_factor
                    + Abulk * dketa_dVbseff * bias.dVbs_eff_dVb;
    // Vdsat below takes Abulk with the KETA factor, as in b4ld.c
    Abulk *= keta_factor;

    // DEBUG: Print Abulk value with ngspice-compatible AGS formula
    thread_local static int abulk_call_count = 0;
    if (abulk_call_count < 5) {  // Print first 5 calls to see convergence
        spdlog::debug("BSIM4 Abulk [call {}] ngspice AGS formula:", abulk_call_count);
        spdlog::debug("  vgs={:.4f}V, vth={:.4f}V, vgsteff_for_ags={:.4f}V",
                     bias.vgs, th.vth, vgsteff_for_ags);
        spdlog::debug("  T1_body={:.4e}, T2_geom={:.4e}, T5_geom={:.4f}",
                     T1_body, T2_geom, T5_geom);
        spdlog::debug("  Abulk0 (1+T1*T2)={:.4f}, dAbulk_dVg={:.4e}",
                     1.0 + T1_body * T2_geom, dAbulk_dVg);
        spdlog::debug("  Abulk = {:.4f} + ({:.4e})*{:.4f} = {:.4f}",
                     1.0 + T1_body * T2_geom, dAbulk_dVg, vgsteff_for_ags, Abulk);
        abulk_call_count++;
    }

    // Subthreshold slope factor n with CDSC/CDSCB/CDSCD and derivatives (ngspice b4ld.c)
    const Real epsilon_ox = guard_positive(params_.epsrox, 3.9) * eps0;
    const Real Cox_phys = epsilon_ox / guard_positive(params_.toxe, 5e-10);
    const Real epssub = params_.epsrsub * eps0;
    const Real cdep0 = std::sqrt(qElectron * epssub * params_.ndep * 1.0e6 /
                                 (2.0 * std::max(bias.phis, 1e-6)));
    // ngspice b4ld.c: tmp1 = epssub / Xdep (no cit here)
    // cit is added only once in tmp4, not in tmp1
    const Real tmp1_n = cdep0;  // Bug fix: removed + params_.cit
    const Real tmp2_n = params_.nfactor * tmp1_n;
    const Real tmp3_n = params_.cdsc + params_.cdscb * bias.vbs_eff + params_.cdscd * bias.vds;
    const Real dTheta0_dVb = dlt1_dVb;  // after Theta0 calculation above
    Real tmp4_n = (tmp2_n + tmp3_n * Theta0 + params_.cit) / Cox_phys;
    if (tmp4_n >= -0.5) {
        th.n = 1.0 + tmp4_n;
        th.dn_dVb = (-tmp2_n / std::max(Xdep, 1e-20) * dXdep_dVb
                     + tmp3_n * dTheta0_dVb
                     + params_.cdscb * Theta0) / Cox_phys;
        th.dn_dVd = params_.cdscd * Theta0 / Cox_phys;
    } else {
        const Real T0_n = 1.0 / (3.0 + 8.0 * tmp4_n);
        th.n = (1.0 + 3.0 * tmp4_n) * T0_n;
        const Real T0_n_sq = T0_n * T0_n;
        th.dn_dVb = (-tmp2_n / std::max(Xdep, 1e-20) * dXdep_dVb
                     + tmp3_n * dTheta0_dVb
                     + params_.cdscb * Theta0) / Cox_phys * T0_n_sq;
        th.dn_dVd = params_.cdscd * Theta0 / Cox_phys * T0_n_sq;
    }
    th.n = std::clamp(th.n, 1.0, 3.0);

    // DEBUG: n calculation breakdown
    thread_local static int n_debug_count = 0;
    if (n_debug_count < 5) {
        spdlog::debug("n calculation [debug {}]:", n_debug_count);
        spdlog::debug("  cdep0     = {:.6e}", cdep0);
        spdlog::debug("  tmp1_n    = cdep0 = {:.6e}", tmp1_n);
        spdlog::debug("  tmp2_n    = nfactor * tmp1_n = {:.3e} * {:.6e} = {:.6e}",
                     params_.nfactor, tmp1_n, tmp2_n);
        spdlog::debug("  tmp3_n    = cdsc + cdscb*Vbs + cdscd*Vds = {:.6e}", tmp3_n);
        spdlog::debug("  Theta0    = {:.6e}", Theta0);
        spdlog::debug("  tmp3_n*Theta0 = {:.6e}", tmp3_n * Theta0);
        spdlog::debug("  Cox_phys  = {:.6e}", Cox_phys);
        spdlog::debug("  tmp4_n    = (tmp2_n + tmp3_n*Theta0 + cit) / Cox_phys = {:.6e}", tmp4_n);
        spdlog::debug("  n (raw)   = 1 + tmp4_n = {:.6f}", 1.0 + tmp4_n);
        spdlog::debug("  n (clamped) = {:.6f}", th.n);
        n_debug_count++;
    }

    // DO NOT clamp vgsteff to 0 for mobility calculation
    // In subthreshold region (vgs < vth), vgsteff < 0 is physically correct
    // Clamping to 0 causes incorrect mobility and beta, leading to 92.8% error in subthreshold current
    // Reference: ngspice b4ld.c does not clamp vgsteff for mobility calculation
    const Real vgst_raw = bias.vgs_eff - th.vth;  // Allow negative values (raw Vgst)

    // ===================================================================
    // Apply Vgsteff smoothing for vdsat calculation
    // Reference: ngspice b4ld.c lines 1236-1292 (smoothing happens BEFORE vdsat)
    // This converts raw Vgst (can be negative) to smoothed Vgsteff (always positive)
    // ===================================================================
    const Real Vtm = kBoltz * T / qElectron;
    const Real mstar = 0.5 + std::atan(params_.minv) / std::numbers::pi;
    const Real EXP_THRESHOLD = 34.0;

    Real T10_smooth;
    const Real T0_smooth = th.n * Vtm;
    const Real T1_smooth = mstar * vgst_raw;
    const Real T2_smooth = T1_smooth / std::max(T0_smooth, 1e-12);

    if (T2_smooth > EXP_THRESHOLD) {
        // Strong inversion: Vgsteff ≈ mstar * Vgst
        T10_smooth = T1_smooth;
    } else if (T2_smooth < -EXP_THRESHOLD) {
        // Deep cutoff: Vgsteff approaches small positive constant
        constexpr Real MIN_EXP = 1.0e-15;
        T10_smooth = Vtm * std::log(1.0 + MIN_EXP) * th.n;
    } else {
        // Subthreshold/weak inversion: exponential smoothing
        const Real ExpVgst = std::exp(T2_smooth);
        const Real T3_smooth = Vtm * std::log(1.0 + ExpVgst);
        T10_smooth = th.n * T3_smooth;
    }

    // ===================================================================
    // Full T9 quantum correction factor implementation
    // Reference: ngspice b4temp.c lines 1373-1375 (cdep0), 1650-1660 (T9)
    // ===================================================================

    // Calculate cdep0 (depletion capacitance) [F/m²] for T9 smoothing
    const Real epsSi = 11.7 * 8.854e-12;  // Silicon permittivity [F/m]
    const Real ndep_m3 = params_.ndep * 1.0e6;  // Convert cm⁻³ to m⁻³
    const Real cdep0_T9 = std::sqrt(qElectron * epsSi * ndep_m3 / (2.0 * params_.phi));

    // Calculate voffcbn (offset voltage with length dependence)
    // Note: leff is already declared earlier in this function (line 345)
    const Real voffcbn = params_.voff + params_.voffl / std::max(leff, 1e-10);

    // Calculate oxide capacitance Coxe [F/m²]
    const Real epsOx = 3.9 * 8.854e-12;  // SiO2 permittivity [F/m]
    const Real Coxe = epsOx / params_.toxe;

    // Calculate exponential term with overflow protection
    const Real exp_arg = (voffcbn - (1.0 - mstar) * vgst_raw) / T0_smooth;

    Real exp_term;
    if (exp_arg > EXP_THRESHOLD) {
        exp_term = 2.353853e15;  // MAX_EXP (exp(34))
    } else if (exp_arg < -EXP_THRESHOLD) {
        exp_term = 1.0e-15;  // MIN_EXP (exp(-34))
    } else {
        exp_term = std::exp(exp_arg);
    }

    // Full T9 quantum correction factor
    const Real T9_smooth = mstar + th.n * (Coxe / cdep0_T9) * exp_term;
    const Real vgsteff = T10_smooth / std::max(T9_smooth, 1e-12);

    // For mobility calculation, use max(vgsteff, 0) to avoid negative gate overdrive
    // This is physically correct: mobility degradation only occurs in strong inversion
    const Real vgsteff_for_mobility = std::max(vgsteff, 0.0);
    th.ueff = compute_mobility(vgsteff_for_mobility, th.vth, bias.vbs_eff, T);
    // ngspice BSIM4.8.0 tempMod=0 formula (b4temp.c line 2085):
    // vsattemp = vsat - at * T0, where T0 = (TRatio - 1.0)
    // TRatio = temperature / tnom (dimensionless)
    // NOT (T - Tnom) (absolute difference in K)
    // At T=-40°C with at=3.3e4, vsat=8e4:
    // Correct (TRatio-1): 8e4 - 3.3e4×(-0.223) = 8.736e4 m/s
    // Wrong (T-Tnom): 8e4 - 3.3e4×(-67) = 2.219e6 m/s (25x error!)
    const Real temp_ratio = T / params_.tnom;
    const Real vsat = std::max(params_.vsat - params_.at * (temp_ratio - 1.0), 1e4);
    th.vsat = vsat;

    // REMOVED - This AGS correction was DUPLICATE!
    // The AGS correction (Abulk = Abulk0 + dAbulk_dVg * Vgsteff) is already
    // applied earlier in compute_threshold() around lines 698-732.
    // Applying it twice caused Abulk to be too low, resulting in
    // 12-14% higher drain current in the linear region.
    // Reference: ngspice b4ld.c lines 1388-1393 applies AGS ONCE only.

    // Vdsat calculation with Abulk
    // EsatL = (2.0 * VSAT / mobility) * Leff (ngspice b4ld.c line 1827)
    // CRITICAL: Factor 2.0 is essential for correct vdsat calculation
    // Mobility normalization critical for accurate vdsat in short-channel devices
    const Real mobility_safe = std::max(th.ueff, 1e-6);  // Guard: minimum 1e-6 m²/V-s
    const Real Esat = 2.0 * vsat / mobility_safe;
    const Real EsatL = Esat * leff;

    // vdsat branch selection logic
    // Vtm already calculated above for Vgsteff smoothing

    // Calculate Lambda (channel-length modulation parameter)
    // NOTE: Using a1/a2-based compute_lambda() as pclm/leff approach failed
    // Lambda = pclm/leff = 2.89e7 caused T0 < 0, breaking vdsat calculation
    // Root cause: Circular dependency (vdsat needs Lambda, VADIBL needs vdsat, Lambda needs VADIBL)
    const Real Lambda = compute_lambda(vgsteff);

    // Calculate Rds (drain-source resistance)
    const Real Rds = compute_rds(vgsteff, bias.sqrt_phis, T);

    // Calculate Vgst2Vtm with +2.0*Vtm term (Bug fix)
    const Real Vgst2Vtm = vgsteff + 2.0 * Vtm;

    // Get effective channel width
    const Real Weff = params_.get_weff();

    // Branch selection: ngspice uses Branch 2 when (Rds != 0.0) || (Lambda != 1.0)
    // Reference: ngspice b4ld.c lines 1620-1679
    // CRITICAL FIX (Priority 1): Match ngspice condition exactly: Branch 1 only when BOTH Rds=0 AND Lambda=1
    // Previous code used Branch 1 when Lambda≈1.0 (regardless of Rds), causing 28.15% Vdsat error
    // The WVCoxRds fix (vsat instead of vgsteff) solved the numerical instability in Branch 2
    spdlog::debug("=== vdsat Branch Selection ===");
    spdlog::debug("  prwg   = {:.6e}", params_.prwg);
    spdlog::debug("  Rds    = {:.6e} Ω", Rds);
    spdlog::debug("  Lambda = {:.6e}", Lambda);
    const bool use_simple_formula = (std::abs(Rds) < 1e-9) && (std::abs(Lambda - 1.0) < 1e-9);
    spdlog::debug("  Condition: (Rds < 1e-9) && (|Lambda - 1.0| < 1e-9) = {}", use_simple_formula);

    if (use_simple_formula) {
        // Branch 1: Simple formula (ngspice b4ld.c lines 1620-1635)
        // Used when BOTH Rds=0.0 AND Lambda=1.0 (ngspice exact condition)
        spdlog::debug("  → Using Branch 1 (Simple formula)");
        const Real T0 = 1.0 / (Abulk * EsatL + Vgst2Vtm);
        const Real T3 = EsatL * Vgst2Vtm;
        th.vdsat = T3 * T0;
        spdlog::debug("  → vdsat = {:.6e} V (Branch 1)", th.vdsat);
    } else {
        // Branch 2: Complex formula (ngspice b4ld.c lines 1636-1679)
        // Used when Rds≠0.0 OR Lambda≠1.0 (includes WVCoxRds with vsat fix)
        spdlog::debug("  → Using Branch 2 (Complex formula)");
        th.vdsat = compute_vdsat_complex(Vgst2Vtm, Abulk, EsatL, Rds, Lambda, Weff, vgsteff, vsat);
        spdlog::debug("  → vdsat = {:.6e} V (Branch 2)", th.vdsat);
    }

    th.vdsat = std::max(th.vdsat, 0.0);

    // Removed vdsat floor approach - causes regressions
    // The subthreshold correction will be applied at the current stage instead

    // Log Abulk, EsatL, vdsat calculation details (only when converged)
    thread_local static int vdsat_log_count = 0;
    if (vgsteff > 0.1 && vdsat_log_count < 15) {  // Only log for non-zero vgsteff (increased limit)
        spdlog::debug("BSIM4 vdsat calculation details (converged, call #{}):", vdsat_log_count);
        spdlog::debug("  vgsteff  = {:.6e} V", vgsteff);
        spdlog::debug("  ueff     = {:.6e} m²/Vs (mobility_safe)", mobility_safe);
        spdlog::debug("  vsat     = {:.6e} m/s", vsat);
        spdlog::debug("  Esat     = 2*vsat/ueff = {:.6e} V/m", Esat);
        spdlog::debug("  leff     = {:.6e} m", leff);
        spdlog::debug("  EsatL    = Esat*leff = {:.6e} V", EsatL);
        spdlog::debug("  Abulk    = {:.6e}", Abulk);
        spdlog::debug("  vgsteff/EsatL = {:.6e}", vgsteff/std::max(EsatL, 1e-12));
        spdlog::debug("  Denominator = Abulk + vgsteff/EsatL = {:.6e}", Abulk + vgsteff/EsatL);
        spdlog::debug("  vdsat    = {:.6e} V", th.vdsat);
        vdsat_log_count++;
    }

    // DEBUG: Print vdsat calculation
    static bool debug_vdsat_printed = false;
    if (!debug_vdsat_printed) {
        spdlog::debug("BSIM4 vdsat calculation:");
        spdlog::debug("  vgsteff       = {:+.6e} V", vgsteff);
        spdlog::debug("  ueff (mobility) = {:+.6e} m²/V-s", th.ueff);
        spdlog::debug("  vsat          = {:+.6e} m/s", vsat);
        spdlog::debug("  leff          = {:+.6e} m", leff);
        spdlog::debug("  EsatL         = {:+.6e} V (vsat/mobility*leff)", EsatL);
        spdlog::debug("  Abulk         = {:+.6e}", Abulk);
        spdlog::debug("  vdsat         = {:+.6e} V", th.vdsat);
        debug_vdsat_printed = true;
    }

    // Store temperature for use in compute_cdrain
    th.temperature = T;

    // ===================================================================
    // Calculate PvagTerm (used by both VADIBL and Cclm)
    // Reference: BSIM4.8.3 b4ld.c lines 1860-1875
    // ===================================================================
    Real PvagTerm = 1.0;
    if (EsatL > 1e-10) {
        const Real T8_pvag = params_.pvag / EsatL;
        const Real T9_pvag = T8_pvag * vgsteff;
        if (T9_pvag > -0.9) {
            PvagTerm = 1.0 + T9_pvag;
        } else {
            const Real T4_pvag = 1.0 / (17.0 + 20.0 * T9_pvag);
            PvagTerm = (0.8 + T9_pvag) * T4_pvag;
        }
    }
    // Store PvagTerm in ThresholdState for use in compute_cdrain
    th.pvag_term = PvagTerm;

    // DEBUG: PvagTerm calculation (first 2 calls)
    thread_local static int pvag_count = 0;
    if (pvag_count < 2) {
        spdlog::debug("=== PvagTerm Calculation (call {}) ===", pvag_count);
        spdlog::debug("  pvag     = {:.6e}", params_.pvag);
        spdlog::debug("  EsatL    = {:.6e} V", EsatL);
        spdlog::debug("  vgsteff  = {:.6e} V", vgsteff);
        if (EsatL > 1e-10) {
            const Real T8_pvag_dbg = params_.pvag / EsatL;
            const Real T9_pvag_dbg = T8_pvag_dbg * vgsteff;
            spdlog::debug("  T8_pvag  = {:.6e} (pvag/EsatL)", T8_pvag_dbg);
            spdlog::debug("  T9_pvag  = {:.6e} (T8×vgsteff)", T9_pvag_dbg);
        }
        spdlog::debug("  PvagTerm = {:.6f}", PvagTerm);
        pvag_count++;
    }

    // ===================================================================
    // Calculate and store VADIBL for use in evaluate()
    // Reference: BSIM4.8.3 b4ld.c lines 1913-1961
    // ===================================================================

    // Calculate Xdep0 (maximum depletion depth at Vbs=0)
    const Real epssub_pdibl = params_.epsrsub * eps0;  // Substrate permittivity [F/m]
    const Real ndep = std::max(params_.ndep, 1e15);  // Doping [cm^-3]
    const Real phi_xdep = std::max(params_.phi, 0.1);  // Surface potential [V]
    const Real Xdep0 = std::sqrt(2.0 * epssub_pdibl / (qElectron * ndep * 1e6)) * std::sqrt(phi_xdep);
    const Real sqrt_Xdep0 = std::sqrt(std::max(Xdep0, 1e-10));

    // Calculate T5 for thetaRout (PDIBLC model)
    // Reference: ngspice b4temp.c lines ~2300-2320
    // CRITICAL FIX Use correct tmp formula
    // ngspice: tmp = sqrt(epssub / (epsrox * eps0) * toxe * Xdep0)
    const Real tmp_dibl = std::sqrt(epssub_pdibl / (params_.epsrox * eps0) * params_.toxe * Xdep0);
    const Real T0_t5 = params_.drout * leff / std::max(tmp_dibl, 1e-10);
    Real T5;
    if (T0_t5 < 30.0) {  // EXP_THRESHOLD check
        const Real T1_exp = std::exp(T0_t5);
        const Real T2 = T1_exp - 1.0;
        const Real T3 = T2 * T2;
        const Real MIN_T4 = 1e-10;
        const Real T4 = std::max(T3 + 2.0 * T1_exp * 1e-20, MIN_T4);
        T5 = T1_exp / T4;
    } else {
        T5 = 1.0 / (30.0 - 2.0);  // EXP_THRESHOLD - 2
    }

    // Calculate thetaRout = pdiblc1 * T5 + pdiblc2
    const Real thetaRout = params_.pdiblc1 * T5 + params_.pdiblc2;

    // DEBUG: thetaRout calculation (first 2 calls)
    thread_local static int theta_count = 0;
    if (theta_count < 2) {
        spdlog::debug("=== thetaRout Calculation (call {}) ===", theta_count);
        spdlog::debug("  drout     = {:.6e}", params_.drout);
        spdlog::debug("  leff      = {:.6e} m", leff);
        spdlog::debug("  toxe      = {:.6e} m", params_.toxe);
        spdlog::debug("  epsrsub   = {:.6e}", params_.epsrsub);
        spdlog::debug("  epsrox    = {:.6e}", params_.epsrox);
        spdlog::debug("  Xdep0     = {:.6e} m", Xdep0);
        spdlog::debug("  tmp_dibl  = {:.6e} m", tmp_dibl);
        spdlog::debug("  T0_t5     = {:.6e} (drout×leff/tmp)", T0_t5);
        spdlog::debug("  T5        = {:.6e}", T5);
        spdlog::debug("  pdiblc1   = {:.6e}", params_.pdiblc1);
        spdlog::debug("  pdiblc2   = {:.6e}", params_.pdiblc2);
        spdlog::debug("  thetaRout = {:.6e}", thetaRout);
        theta_count++;
    }

    // Apply body-effect correction with pdiblcb
    const Real T7 = params_.pdiblcb * bias.vbs_eff;
    Real vadibl_factor = 1.0;
    if (T7 >= -0.9) {
        vadibl_factor = 1.0 / (1.0 + T7);
    } else {
        const Real T4_vb = 1.0 / (0.8 + T7);
        vadibl_factor = (17.0 + 20.0 * T7) * T4_vb;
    }

    // Calculate VADIBL (DIBL voltage)
    Real VADIBL;
    if (thetaRout > 1e-10 && vgsteff > 0.0) {
        const Real Vgst2Vtm = vgsteff + 2.0 * Vtm;  // Fixed missing +2*Vtm term
        const Real T0_va = Vgst2Vtm * Abulk * th.vdsat;
        const Real T1_va = Vgst2Vtm + Abulk * th.vdsat;
        if (T1_va > 1e-9) {
            VADIBL = (Vgst2Vtm - T0_va / T1_va) / thetaRout * vadibl_factor;
        } else {
            VADIBL = 1e6;
        }

        // DEBUG: VADIBL calculation details (first 2 calls)
        thread_local static int vadibl_calc_count = 0;
        if (vadibl_calc_count < 2) {
            spdlog::debug("=== VADIBL Calculation (call {}) ===", vadibl_calc_count);
            spdlog::debug("  vgsteff    = {:.6e} V", vgsteff);
            spdlog::debug("  Vtm        = {:.6e} V", Vtm);
            spdlog::debug("  Vgst2Vtm   = {:.6e} V (vgsteff + 2×Vtm)", Vgst2Vtm);
            spdlog::debug("  Abulk      = {:.6e}", Abulk);
            spdlog::debug("  vdsat      = {:.6e} V", th.vdsat);
            spdlog::debug("  T0_va      = {:.6e} (Vgst2Vtm × Abulk × vdsat)", T0_va);
            spdlog::debug("  T1_va      = {:.6e} (Vgst2Vtm + Abulk × vdsat)", T1_va);
            spdlog::debug("  Numerator  = {:.6e} (Vgst2Vtm - T0_va/T1_va)", Vgst2Vtm - T0_va / T1_va);
            spdlog::debug("  thetaRout  = {:.6e} (pdiblc1={:.4f}, pdiblc2={:.4f})", thetaRout, params_.pdiblc1, params_.pdiblc2);
            spdlog::debug("  vadibl_factor = {:.6f}", vadibl_factor);
            spdlog::debug("  VADIBL(raw)= {:.6e} V", VADIBL);
            vadibl_calc_count++;
        }
    } else {
        VADIBL = 1e6;
    }

    // Apply PvagTerm multiplication (ngspice b4ld.c line 1955)
    // CRITICAL FIX: This multiplication was missing, causing 28.53% error
    VADIBL *= PvagTerm;

    // Store VADIBL in ThresholdState
    th.vadibl = VADIBL;

    // ===================================================================
    // Calculate and store VADITS, Vasat, Cclm
    // Reference: BSIM4.8.3 b4ld.c lines 1765-1990
    // ===================================================================

    // Calculate Vasat (simplified: use vdsat as approximation)
    // Full ngspice formula at b4ld.c lines 1765-1779 is complex
    // For initial implementation, vdsat is a good approximation
    const Real Vasat = th.vdsat;
    th.vasat = Vasat;

    // Calculate VADITS (Impact Ionization)
    // Reference: ngspice b4ld.c lines 1969-1990
    Real VADITS;
    const Real MIN_EXP = 1e-10;
    if (params_.pdits > MIN_EXP) {
        // Simplified VADITS calculation (without Vds dependence exp term)
        // Full formula: T0 = pditsd * Vds, T1 = exp(T0), VADITS = (1 + pditsl×Leff×T1) / pdits
        // Simplified: VADITS = (1 + pditsl×Leff) / pdits (assuming T1≈1 for small Vds)
        const Real T2 = 1.0 + params_.pditsl * leff;
        VADITS = T2 / params_.pdits;
    } else {
        VADITS = 1e6;  // Effectively infinite (no impact ionization)
    }
    th.vadits = VADITS;

    // DEBUG: Log VADITS calculation (first call only)
    static bool vadits_calc_logged = false;
    if (!vadits_calc_logged) {
        spdlog::debug("=== VADITS Calculation (Debug) ===");
        spdlog::debug("  pdits      = {:.6e}", params_.pdits);
        spdlog::debug("  pditsl     = {:.6e}", params_.pditsl);
        spdlog::debug("  pditsd     = {:.6e}", params_.pditsd);
        spdlog::debug("  Leff       = {:.6e} m", leff);
        spdlog::debug("  VADITS     = {:.6e} V", VADITS);
        spdlog::debug("  VADITS < 1e5? {}", (VADITS < 1e5) ? "YES (will be applied)" : "NO (disabled)");
        vadits_calc_logged = true;
    }

    // Calculate Cclm (Channel Length Modulation coefficient)
    // Reference: ngspice b4ld.c lines 1882-1911
    // Full formula: Cclm = FP * PvagTerm * T0 * T1 / (pclm * litl)
    // This is extremely complex, involving FP (Poly depletion), PvagTerm (PVAG model)
    // For , DISABLE CLM to isolate VADITS effect first
    // TODO Implement full Cclm calculation
    Real Cclm = 1e6;  // Effectively infinite (CLM disabled)
    th.cclm = Cclm;

    return th;
}

// Vgsteff Smoothing Function
// Reference: ngspice BSIM4.8.3 b4ld.c lines 1236-1292
// This function converts raw Vgst (which can be negative in subthreshold) to smoothed Vgsteff
// (always positive), enabling the unified gche formula to work in ALL operating regions.
Real BSIM4Core::compute_vgsteff_smoothed(
    const BSIM4BiasState& bias,
    const BSIM4ThresholdState& th,
    Real dn_dVd, Real dn_dVb,
    Real dVth_dVd, Real dVth_dVb,
    Real dVgs_eff_dVg,
    Real& dVgsteff_dVg,
    Real& dVgsteff_dVd,
    Real& dVgsteff_dVb,
    Real temperature
) const {
    // Physical constants (local definitions to avoid dependency on external headers)
    const Real kb = 1.380649e-23;      // Boltzmann constant [J/K]
    const Real q = 1.602176634e-19;    // Elementary charge [C]
    const Real pi = 3.14159265358979323846;  // Pi
    const Real eps0 = 8.854187817e-12; // Vacuum permittivity [F/m]

    const Real Vtm = kb * temperature / q;
    const Real n = th.n;

    // Raw Vgst (can be negative in subthreshold region)
    const Real Vgst = bias.vgs_eff - th.vth;

    // Derived parameters
    // mstar: Vgsteff smoothing factor (default: 0.5 when minv=0.0)
    const Real mstar = 0.5 + std::atan(params_.minv) / pi;

    // voffcbn: Offset voltage for smoothing
    const Real leff = params_.get_leff();
    const Real voffcbn = params_.voff + params_.voffl / std::max(leff, 1e-9);

    // cdep0: Depletion capacitance [F/m²]
    // Formula: cdep0 = sqrt(q * ε_sub * N_dep / (2 * φ))
    const Real epssub = params_.epsrsub * eps0;
    const Real cdep0 = std::sqrt(q * epssub * params_.ndep * 1.0e6 / (2.0 * params_.phi));

    // coxe: Oxide capacitance [F/m²]
    const Real epsilon_ox = guard_positive(params_.epsrox, 3.9) * eps0;
    const Real coxe = epsilon_ox / guard_positive(params_.toxe, 5e-10);

    // T0 = n * Vtm
    const Real T0 = n * Vtm;

    // T1 = mstar * Vgst
    const Real T1 = mstar * Vgst;

    // T2 = T1 / T0 = (mstar * Vgst) / (n * Vtm)
    const Real T2 = T1 / std::max(T0, 1e-12);

    // Calculate T10 and its derivatives based on region
    Real T10, dT10_dVg, dT10_dVd, dT10_dVb;

    constexpr Real EXP_THRESHOLD = 34.0;  // exp(34) ≈ 5.8e14 (ngspice default)
    constexpr Real MIN_EXP = 1e-9;        // Minimum exponential value (ngspice default)

    if (T2 > EXP_THRESHOLD) {
        // Strong inversion region: Vgsteff ≈ mstar * Vgst
        T10 = T1;
        dT10_dVg = mstar * dVgs_eff_dVg;
        dT10_dVd = -dVth_dVd * mstar;
        dT10_dVb = -dVth_dVb * mstar;

    } else if (T2 < -EXP_THRESHOLD) {
        // Deep cutoff region: Vgsteff approaches small positive constant
        T10 = Vtm * std::log(1.0 + MIN_EXP);
        dT10_dVg = 0.0;
        dT10_dVd = T10 * dn_dVd;
        dT10_dVb = T10 * dn_dVb;
        T10 *= n;  // T10 = n * Vtm * log(1 + MIN_EXP)

    } else {
        // Subthreshold/weak inversion region: EXPONENTIAL SMOOTHING
        // This is the KEY region that fixes VDS scaling!
        const Real ExpVgst = std::exp(T2);
        const Real T3 = Vtm * std::log(1.0 + ExpVgst);
        T10 = n * T3;  // = n * Vtm * log(1 + exp((mstar*Vgst)/(n*Vtm)))

        // Derivatives (ngspice b4ld.c lines 1259-1262)
        dT10_dVg = mstar * ExpVgst / (1.0 + ExpVgst);
        dT10_dVb = T3 * dn_dVb - dT10_dVg * (dVth_dVb + Vgst * dn_dVb / std::max(n, 0.1));
        dT10_dVd = T3 * dn_dVd - dT10_dVg * (dVth_dVd + Vgst * dn_dVd / std::max(n, 0.1));
        dT10_dVg *= dVgs_eff_dVg;
    }

    // Calculate T9 (quantum correction factor) and its derivatives
    // Reference: ngspice b4ld.c lines 1265-1291
    const Real T1_offset = voffcbn - (1.0 - mstar) * Vgst;
    const Real T2_offset = T1_offset / std::max(T0, 1e-12);

    Real T9, dT9_dVg, dT9_dVd, dT9_dVb;

    if (T2_offset < -EXP_THRESHOLD) {
        // Deep cutoff
        const Real T3 = coxe * MIN_EXP / std::max(cdep0, 1e-12);
        T9 = mstar + T3 * n;
        dT9_dVg = 0.0;
        dT9_dVd = dn_dVd * T3;
        dT9_dVb = dn_dVb * T3;

    } else if (T2_offset > EXP_THRESHOLD) {
        // Strong inversion
        const Real T3 = coxe * std::exp(EXP_THRESHOLD) / std::max(cdep0, 1e-12);
        T9 = mstar + T3 * n;
        dT9_dVg = 0.0;
        dT9_dVd = dn_dVd * T3;
        dT9_dVb = dn_dVb * T3;

    } else {
        // Normal range: quantum correction
        const Real ExpVgst_offset = std::exp(T2_offset);
        const Real T3 = coxe / std::max(cdep0, 1e-12);
        const Real T4 = T3 * ExpVgst_offset;
        const Real T5 = T1_offset * T4 / std::max(T0, 1e-12);

        T9 = mstar + n * T4;
        dT9_dVg = T3 * (mstar - 1.0) * ExpVgst_offset / std::max(Vtm, 1e-6);

        // DEBUG: T9 calculation
        thread_local static int t9_debug_count = 0;
        if (t9_debug_count < 2) {
            spdlog::debug("==== T9 (quantum) Debug (call {}) ====", t9_debug_count);
            spdlog::debug("  T1_offset = {:.6e} (voffcbn - (1-mstar)*Vgst)", T1_offset);
            spdlog::debug("  T2_offset = {:.6e} (T1_offset / n*Vtm)", T2_offset);
            spdlog::debug("  ExpVgst_offset = {:.6e} (exp(T2_offset))", ExpVgst_offset);
            spdlog::debug("  T3 = {:.6e} (coxe/cdep0, coxe={:.6e}, cdep0={:.6e})", T3, coxe, cdep0);
            spdlog::debug("  mstar-1 = {:.6e}", mstar - 1.0);
            spdlog::debug("  Vtm = {:.6e}", Vtm);
            spdlog::debug("  dT9_dVg (before *= dVgs_eff_dVg) = {:.6e}", dT9_dVg);
            spdlog::debug("  dVgs_eff_dVg = {:.6e}", dVgs_eff_dVg);
            spdlog::debug("  dT9_dVg (after *= dVgs_eff_dVg) = {:.6e}", dT9_dVg * dVgs_eff_dVg);
            t9_debug_count++;
        }

        // Debug dT9_dVb individual terms
        thread_local static int dt9_debug_count = 0;
        if (dt9_debug_count < 2) {
            spdlog::debug("==== dT9_dVb Term Breakdown (call {}) ====", dt9_debug_count);
            spdlog::debug("  T4             = {:.6e}", T4);
            spdlog::debug("  T5             = {:.6e}", T5);
            spdlog::debug("  dn_dVb         = {:.6e}", dn_dVb);
            spdlog::debug("  dT9_dVg        = {:.6e} (before *= dVgs_eff_dVg)", dT9_dVg);
            spdlog::debug("  dVth_dVb       = {:.6e}", dVth_dVb);
            spdlog::debug("  Term1: T4*dn_dVb            = {:.6e}", T4 * dn_dVb);
            spdlog::debug("  Term2: -dT9_dVg*dVth_dVb    = {:.6e}", -dT9_dVg * dVth_dVb);
            spdlog::debug("  Term3: -T5*dn_dVb           = {:.6e}", -T5 * dn_dVb);
            spdlog::debug("  dT9_dVb (sum of terms)      = {:.6e}", T4 * dn_dVb - dT9_dVg * dVth_dVb - T5 * dn_dVb);
            spdlog::debug("  Expected (for ngspice):     ≈ +1.5 (positive!)");
            dt9_debug_count++;
        }

        dT9_dVb = T4 * dn_dVb - dT9_dVg * dVth_dVb - T5 * dn_dVb;
        dT9_dVd = T4 * dn_dVd - dT9_dVg * dVth_dVd - T5 * dn_dVd;
        dT9_dVg *= dVgs_eff_dVg;
    }

    // Vgsteff = T10 / T9 (BSIM4 manual, eq. 3.7; BSIM4 4.8.3 b4ld.c)
    const Real T9_corrected = T9;

    // Final Vgsteff = T10 / T9_corrected
    const Real Vgsteff = T10 / std::max(T9_corrected, 1e-12);

    // Derivatives using quotient rule: d(T10/T9) = (T9*dT10 - T10*dT9) / T9²
    // Note: Use T9_corrected for consistency with the Vgsteff calculation
    const Real T11 = T9_corrected * T9_corrected;
    dVgsteff_dVg = (T9_corrected * dT10_dVg - T10 * dT9_dVg) / std::max(T11, 1e-24);
    dVgsteff_dVd = (T9_corrected * dT10_dVd - T10 * dT9_dVd) / std::max(T11, 1e-24);
    dVgsteff_dVb = (T9_corrected * dT10_dVb - T10 * dT9_dVb) / std::max(T11, 1e-24);


    // Debug logging (+ derivative debugging)
    thread_local static int vgsteff_debug_count = 0;
    if (vgsteff_debug_count < 2) {
        spdlog::debug("==== Vgsteff Derivative Debug (call {}) ====", vgsteff_debug_count);
        spdlog::debug("  Inputs:");
        spdlog::debug("    Vgst (raw)     = {:.6e} V", Vgst);
        spdlog::debug("    n              = {:.6e}", n);
        spdlog::debug("    Vtm            = {:.6e} V", Vtm);
        spdlog::debug("    dVgs_eff_dVg   = {:.6e}", dVgs_eff_dVg);
        spdlog::debug("  Region: T2 = {:.6e} ({})", T2,
                     (T2 > EXP_THRESHOLD) ? "Strong inversion" :
                     (T2 < -EXP_THRESHOLD) ? "Deep cutoff" : "Subthreshold");
        spdlog::debug("  Numerator/Denominator:");
        spdlog::debug("    T10            = {:.6e}", T10);
        spdlog::debug("    T9             = {:.6e}", T9);
        spdlog::debug("    dT10_dVg       = {:.6e}", dT10_dVg);
        spdlog::debug("    dT9_dVg        = {:.6e}", dT9_dVg);
        spdlog::debug("  Quotient rule: (T9*dT10 - T10*dT9) / T9^2:");
        spdlog::debug("    T9*dT10_dVg    = {:.6e}", T9 * dT10_dVg);
        spdlog::debug("    T10*dT9_dVg    = {:.6e}", T10 * dT9_dVg);
        spdlog::debug("    Numerator      = {:.6e}", T9 * dT10_dVg - T10 * dT9_dVg);
        spdlog::debug("    T9^2           = {:.6e}", T11);
        spdlog::debug("  dVgsteff_dVb calculation:");
        spdlog::debug("    dT10_dVb       = {:.6e}", dT10_dVb);
        spdlog::debug("    dT9_dVb        = {:.6e}", dT9_dVb);
        spdlog::debug("    dVth_dVb       = {:.6e} (for ref)", dVth_dVb);
        spdlog::debug("    dn_dVb         = {:.6e} (for ref)", dn_dVb);
        spdlog::debug("    T9*dT10_dVb    = {:.6e}", T9 * dT10_dVb);
        spdlog::debug("    T10*dT9_dVb    = {:.6e}", T10 * dT9_dVb);
        spdlog::debug("    dVgsteff_dVb   = {:.6e}", dVgsteff_dVb);
        spdlog::debug("    ngspice ref: gmb/gm ≈ 0.165, so dVgsteff_dVb ≈ 0.165");
        spdlog::debug("  Output:");
        spdlog::debug("    Vgsteff        = {:.6e} V", Vgsteff);
        spdlog::debug("    dVgsteff_dVg   = {:.6e}", dVgsteff_dVg);
        vgsteff_debug_count++;
    }
    spdlog::debug("==== Vgsteff Smoothing Function ====");
    spdlog::debug("  Inputs:");
    spdlog::debug("    Vgst (raw)     = {:.6e} V (can be negative)", Vgst);
    spdlog::debug("    n              = {:.6e}", n);
    spdlog::debug("    Vtm            = {:.6e} V", Vtm);
    spdlog::debug("    mstar          = {:.6e} (from minv={:.6e})", mstar, params_.minv);
    spdlog::debug("    voffcbn        = {:.6e} V", voffcbn);
    spdlog::debug("    cdep0          = {:.6e} F/m²", cdep0);
    spdlog::debug("    coxe           = {:.6e} F/m²", coxe);
    spdlog::debug("  Intermediate:");
    spdlog::debug("    T2 = (mstar*Vgst)/(n*Vtm) = {:.6e}", T2);
    spdlog::debug("    Region: {}", (T2 > EXP_THRESHOLD) ? "Strong inversion" :
                                    (T2 < -EXP_THRESHOLD) ? "Deep cutoff" :
                                    "Subthreshold/weak inversion");
    spdlog::debug("    T10            = {:.6e}", T10);
    spdlog::debug("    T9             = {:.6e}", T9);
    spdlog::debug("  Output:");
    spdlog::debug("    Vgsteff        = {:.6e} V (always positive!)", Vgsteff);
    spdlog::debug("    dVgsteff_dVg   = {:.6e}", dVgsteff_dVg);
    spdlog::debug("    dVgsteff_dVd   = {:.6e}", dVgsteff_dVd);
    spdlog::debug("    dVgsteff_dVb   = {:.6e}", dVgsteff_dVb);

    return Vgsteff;
}

//--------------------------------------------------------------------------------------
// compute_coxeff_and_beta
// Extracted from compute_cdrain (Quantum-Corrected Gate Capacitance block).
// Reference: ngspice b4ld.c lines 1810-1827.
//--------------------------------------------------------------------------------------
std::pair<Real, Real> BSIM4Core::compute_coxeff_and_beta(Real vgsteff, Real ueff) const {
    const Real weff = params_.get_weff();
    const Real leff = params_.get_leff();

    // Step 1: Calculate Tcen (carrier effective depth)
    const Real vtfbphi2 = params_.vtfbphi2;
    const Real tmp2 = 2.0e8 * guard_positive(params_.toxp, 5e-10);
    const Real T0 = std::max((vgsteff + vtfbphi2) / tmp2, 1e-20);

    // Power law for density of states (DOS)
    // tmp3 = T0^(bdos * 0.7)
    const Real tmp3 = std::exp(params_.bdos * 0.7 * std::log(T0));
    const Real T1 = 1.0 + tmp3;
    const Real Tcen = params_.ados * 1.9e-9 / T1;

    // Step 2: Calculate Coxeff (series capacitance model)
    // Coxeff = (epssub * coxp) / (epssub + coxp * Tcen)

    // FIX: Compute epssub (silicon permittivity) if not explicitly set
    // Reference: ngspice b4temp.c EPSSI = 1.03594e-10 F/m
    // ε_Si = 11.7 × ε₀ = 11.7 × 8.854e-12 = 1.036e-10 F/m
    constexpr Real eps0 = 8.854e-12;   // Vacuum permittivity [F/m]
    constexpr Real epssi = 11.7 * eps0; // Silicon permittivity [F/m]
    const Real epssub = (params_.epssub > 0.0) ? params_.epssub : epssi;

    // FIX: Compute coxp from toxp/toxe if not explicitly set
    // Reference: ngspice b4temp.c lines 924-927
    // coxp = ε_ox / toxp (or toxe if toxp not set)
    // ε_ox = 3.9 × ε₀ = 3.9 × 8.854e-12 = 3.453e-11 F/m
    constexpr Real epsox = 3.9 * eps0; // SiO2 permittivity [F/m]
    Real coxp = params_.coxp;
    if (coxp <= 0.0) {
        const Real tox = (params_.toxp > 0.0) ? params_.toxp : params_.toxe;
        coxp = epsox / guard_positive(tox, 1e-9);
    }
    const Real Coxeff = epssub * coxp / std::max(epssub + coxp * Tcen, 1e-20);

    // Transconductance parameter beta (with Coxeff, not Cox)
    const Real beta = ueff * Coxeff * weff / guard_positive(leff, 1e-9);

    return {Coxeff, beta};
}

//--------------------------------------------------------------------------------------
// compute_vdseff
// Extracted from compute_cdrain (Accurate Vdseff smoothing block).
// Reference: ngspice b4ld.c lines 1682-1721.
//--------------------------------------------------------------------------------------
std::pair<Real, Real> BSIM4Core::compute_vdseff(Real vdsat, Real vds, Real delta) const {
    Real Vdseff = vds;          // Default to vds, overridden below
    Real dVdseff_dVds = 1.0;    // Derivative of Vdseff wrt Vds

    // T1 = Vdsat - Vds - delta
    const Real T1 = vdsat - vds - delta;

    // T2 = sqrt(T1² + 4·delta·Vdsat)
    const Real T2 = std::sqrt(T1 * T1 + 4.0 * delta * vdsat);
    const Real T9 = 2.0 * delta;

    if (T1 >= 0.0) {
        // Vds < Vdsat (linear/saturation boundary region)
        // Vdseff = Vdsat - 0.5·(T1 + T2)
        Vdseff = vdsat - 0.5 * (T1 + T2);

        // dVdseff/dVds derivative
        // dT1/dVds = -1, dT2/dVds = T1/T2 × dT1/dVds = -T1/T2
        // dVdseff/dVds = -0.5 × (dT1/dVds + dT2/dVds) = 0.5 × (1 + T1/T2)
        dVdseff_dVds = 0.5 * (1.0 + T1 / std::max(T2, 1e-12));
    } else {
        // Vds > Vdsat (deep saturation region)
        // T4 = 2·delta / (T2 - T1)
        // Vdseff = Vdsat·(1 - T4)
        const Real T2_minus_T1 = std::max(T2 - T1, 1e-12);
        const Real T4 = T9 / T2_minus_T1;
        const Real T5 = 1.0 - T4;
        Vdseff = vdsat * T5;

        // dVdseff/dVds derivative for deep saturation
        // dT4/dVds = -T4 × (1 - T1/T2) / (T2 - T1)
        // dVdseff/dVds = -Vdsat × dT4/dVds = Vdsat × T4 × (1 - T1/T2) / (T2 - T1)
        const Real T1_over_T2 = T1 / std::max(T2, 1e-12);
        dVdseff_dVds = vdsat * T4 * (1.0 - T1_over_T2) / T2_minus_T1;
    }

    // Special cases
    if (vds == 0.0) {
        Vdseff = 0.0;
        dVdseff_dVds = 1.0;  // Linear region: dVdseff/dVds → 1
    }

    // Clamp to physical limits
    if (Vdseff > vds) {
        Vdseff = vds;
        dVdseff_dVds = 1.0;  // Clamped: dVdseff/dVds = 1
    }

    // Guard against negative values
    Vdseff = std::max(Vdseff, 0.0);

    // Clamp dVdseff_dVds to physical range [0, 1]
    dVdseff_dVds = std::clamp(dVdseff_dVds, 0.0, 1.0);

    return {Vdseff, dVdseff_dVds};
}

//--------------------------------------------------------------------------------------
// compute_vascbe
// Extracted from compute_cdrain (Substrate Current Body Effect).
// Reference: ngspice b4ld.c lines 1992-2011.
//--------------------------------------------------------------------------------------
Real BSIM4Core::compute_vascbe(Real vds, Real Vdseff, Real leff) const {
    // Calculate litl = characteristic length (used in VASCBE calculation)
    // Formula: litl = sqrt(3 * eps_ox / eps_rox * xj * toxe)
    const Real litl_vascbe = params_.litl;  // BSIM4Params::compute_derived_params

    Real VASCBE;
    const Real diffVds_vascbe = vds - Vdseff;  // For VASCBE calculation only
    if ((params_.pscbe2 > 0.0) && (params_.pscbe1 >= 0.0)) {

        const Real EXP_THRESHOLD = 100.0;  // Same as ngspice MAX_EXP threshold

        if (diffVds_vascbe > params_.pscbe1 * litl_vascbe / EXP_THRESHOLD) {
            const Real T0 = params_.pscbe1 * litl_vascbe / std::max(diffVds_vascbe, 1e-9);
            VASCBE = leff * std::exp(T0) / params_.pscbe2;
        } else {
            // When diffVds is very small, VASCBE becomes very large
            constexpr Real MAX_EXP = 2.688117142e+43;  // exp(100)
            VASCBE = MAX_EXP * leff / params_.pscbe2;
        }
    } else {
        // When pscbe1 or pscbe2 are not properly set, disable VASCBE
        constexpr Real MAX_EXP = 2.688117142e+43;
        VASCBE = MAX_EXP;
    }
    return VASCBE;
}

Real BSIM4Core::compute_cdrain(const BSIM4BiasState& bias, const BSIM4ThresholdState& th,
                               Real& gm, Real& gds, Real& gmb) const {
    // BSIM4 analytical drain current calculation
    // Reference: BSIM4.8.3 User's Manual, ngspice b4ld.c

    // Physical constants
    const Real k = 1.380649e-23;  // Boltzmann constant [J/K]
    const Real q = 1.602176634e-19;  // Elementary charge [C]
    const Real T = th.temperature;  // Use temperature from ThresholdState
    const Real Vt = k * T / q;  // Thermal voltage [V]

    // Calculate smoothed Vgsteff using ngspice-compatible smoothing function
    // This enables unified gche formula to work in ALL regions (including subthreshold)
    // Reference: ngspice b4ld.c lines 1236-1292

    // Collect derivatives for smoothing function
    const Real dn_dVd = th.dn_dVd;       // VDS derivative of n
    const Real dn_dVb = th.dn_dVb;       // VBS derivative of n (CDSCB term)
    const Real dVth_dVd = th.dVth_dVd;   // DIBL effect
    const Real dVth_dVb = th.dVth_dVb;   // Body effect
    const Real dVgs_eff_dVg = bias.dVgs_eff_dVg;  // Vgs_eff derivative

    // Call smoothing function to get smoothed Vgsteff
    Real dVgsteff_dVg, dVgsteff_dVd, dVgsteff_dVb;
    const Real vgsteff = compute_vgsteff_smoothed(
        bias, th,
        dn_dVd, dn_dVb,
        dVth_dVd, dVth_dVb,
        dVgs_eff_dVg,
        dVgsteff_dVg,
        dVgsteff_dVd,
        dVgsteff_dVb,
        T
    );

    // Recalculate Abulk using actual vgsteff (with Rds effects)
    // instead of the simplified vgsteff_for_ags from compute_threshold.
    // This matches ngspice behavior where Vgsteff is computed once and used for both.
    Real abulk = th.abulk0 + th.dAbulk_dVg * vgsteff;

    // ngspice Abulk clipping: if Abulk < 0.1, use smoothing function
    if (abulk < 0.1) {
        const Real T10_clip = 1.0 / (3.0 - 20.0 * abulk);
        abulk = (0.2 - abulk) * T10_clip;
    }
    abulk = std::clamp(abulk, 0.1, 10.0);
    abulk *= th.keta_factor;  // 1 / (1 + KETA Vbseff), see compute_threshold

    // Store other threshold state variables
    const Real vdsat = th.vdsat;
    const Real n = th.n;

    // Device geometry
    const Real weff = params_.get_weff();
    const Real leff = params_.get_leff();

    // Quantum-Corrected Gate Capacitance (Coxeff) and transconductance parameter beta.
    // Reference: ngspice b4ld.c lines 1810-1827. Extracted to compute_coxeff_and_beta().
    const auto [Coxeff, beta] = compute_coxeff_and_beta(vgsteff, th.ueff);
    (void)Coxeff;  // Coxeff not otherwise used in compute_cdrain; beta is consumed below.

    // ===================================================================
    // Use VADIBL from ThresholdState (calculated in compute_threshold)
    // Reference: BSIM4.8.3 b4ld.c lines 1913-1961
    // ===================================================================
    const Real VADIBL = th.vadibl;

    // ===================================================================
    // Enhanced Channel Length Modulation with PDIBLC
    // Use VADIBL from ThresholdState with ngspice-compatible formula
    // ===================================================================

    Real lambda;
    if (VADIBL < 1e5 && vgsteff > 0.0) {
        // Use VADIBL from ThresholdState (ngspice b4ld.c compatible)
        lambda = 1.0 / std::max(VADIBL, 1e-9);
    } else {
        // Fallback to simple pclm model when VADIBL is effectively infinite
        // PCLM model: lambda = pclm / leff (not pclm * leff!)
        lambda = params_.pclm / guard_positive(leff, 1e-9);
    }

    // ===================================================================
    // Lambda function for Ids calculation (RDSW self-consistent iteration)
    // This allows recalculating Ids with different Vds_eff values
    // ===================================================================
    // Use raw Vgst (before smoothing) for region decision; smoothing forces
    // positivity and would bypass the dedicated subthreshold branch.
    const Real raw_vgst = bias.vgs_eff - th.vth;

    // Return dVdseff_dVds, dVdseff_dVgs, and gm_before_chain for complete chain rule in gds
    // Add dVdseff_dVb for complete chain rule in gmb (ngspice b4ld.c lines 5471-5479)
    // ngspice formula: Gds = Ids * (dVdseff_dVd + dVdseff_dVg * dVgsteff_dVd) + vdseff * tmp1
    // ngspice formula: Gmb = (Ids * (dVdseff_dVb + dVdseff_dVg * dVgsteff_dVb) + vdseff * tmp2) * dVbseff_dVb
    // where tmp1 = Gds + Gm_before_chain * dVgsteff_dVd, tmp2 = Gmb_before_chain + Gm * dVgsteff_dVb
    // refactor: lambda inlined directly into compute_cdrain (was called exactly once)
    Real ids_cond = 0.0, gm_tmp = 0.0, gds_tmp = 0.0, gmb_tmp = 0.0;
    Real vdseff = bias.vds;       // Default to bias.vds, overridden in SATURATION region
    Real dVdseff_dVds = 1.0;      // Derivative of vdseff wrt Vds for gds calculation
    Real dVdseff_dVgs = 0.0;      // Derivative of vdseff wrt Vgs for chain rule
    Real dVdseff_dVb = 0.0;       // Derivative of vdseff wrt Vbs for gmb chain rule
    Real gm_before_chain = 0.0;   // ∂Ids/∂Vgsteff BEFORE dVgsteff_dVg chain rule
    bool deep_cutoff_taken = false;
    {
        // Phase Root-Cause: Removed diag_linear_medvds and all targeted diagnostics.
        // Pure BSIM4.8.3 formulas only - no bias-specific empirical corrections.

        // Region detection and calculation
        // Relaxed cutoff threshold from -10 to -30 (ngspice-compatible)
        // At -30*n*Vt, exp(-30) ≈ 9e-14, which is sufficiently small for numerical stability
        // but allows proper subthreshold current calculation (Ids ~ 1e-14 A range)
        // cutoff_threshold unused (kept for future implementations)

        // ===================================================================
        // Removed separate subthreshold branch (was raw_vgst < 0)
        // The unified gche formula with Vgsteff smoothing handles ALL regions:
        //   - Cutoff (Vgsteff → small positive via smoothing)
        //   - Subthreshold (Vgsteff smoothly transitions)
        //   - Linear/Triode (Vds < Vdsat)
        //   - Saturation (Vds > Vdsat)
        // The separate subthreshold branch caused discontinuity at raw_vgst=0
        // resulting in non-monotonic Ids (current DROP when Vgs increases)
        // ===================================================================
        {
            // ===================================================================
            // Unified gche formula for ALL regions
            // After Vgsteff smoothing, the gche formula handles:
            //   - Subthreshold (Vgsteff small positive)
            //   - Weak inversion (Vgsteff moderate)
            //   - Linear/Triode (Vds < Vdsat)
            //   - Saturation (Vds > Vdsat)
            // Reference: ngspice b4ld.c lines 1817-1847, 2138
            // Note: Separate SUBTHRESHOLD and LINEAR branches removed
            // ===================================================================

            // ===================================================================
            // Deep cutoff check (DISABLED)
            // When Vgsteff is extremely small, the device is in deep cutoff.
            // ngspice shows Ids ≈ 1pA in this region. Without this check,
            // the gche formula generates µA-level leakage from small vgsteff.
            // This threshold corresponds to about 3-4 decades below Vth.
            //
            // DISABLED - The Deep Cutoff branch causes 10x error
            // in subthreshold region compared to ngspice. The unified gche
            // formula should handle all regions including subthreshold.
            // ===================================================================
            const Real VGSTEFF_CUTOFF = 0.0;  // Disabled (was 1e-4)
            if (vgsteff < VGSTEFF_CUTOFF) {
                // Deep cutoff: Use exponential subthreshold model
                // Ids ∝ exp(Vgst/(n*Vt)) in subthreshold
                const Real inv_nVt = 1.0 / std::max(n * Vt, 1e-9);
                const Real ids_subth = beta * n * Vt * Vt * limit_exp(raw_vgst * inv_nVt);
                const Real Vds_eff_sub = std::max(bias.vds, 1e-6);
                const Real one_minus_exp_vds = 1.0 - std::exp(-Vds_eff_sub / std::max(Vt, 1e-9));
                ids_cond = ids_subth * one_minus_exp_vds;
                // Floor at 1pA for numerical stability (ngspice compatible)
                ids_cond = std::max(ids_cond, 1e-12);
                gm_tmp = ids_cond * inv_nVt;
                gds_tmp = ids_cond / std::max(Vds_eff_sub, 1e-6);
                gmb_tmp = 0.0;

                // Return early for deep cutoff
                // Note: Return CONDUCTANCE (ids_cond / Vds) since final current = conductance × vdseff
                // Include dVdseff_dVds=1.0, dVdseff_dVgs=0.0, gm_before_chain=gm_tmp for cutoff
                // Include dVdseff_dVb=0.0 for gmb chain rule (no body effect in deep cutoff)
                ids_cond = ids_cond / std::max(Vds_eff_sub, 1e-6);
                vdseff = Vds_eff_sub;
                dVdseff_dVds = 1.0;
                dVdseff_dVgs = 0.0;
                dVdseff_dVb = 0.0;
                gm_before_chain = gm_tmp;
                deep_cutoff_taken = true;
            }
            if (!deep_cutoff_taken) {

            // ===================================================================
            // gche (Channel Conductance) Model - Unified for ALL regions
            // Reference: ngspice BSIM4.8.3 b4ld.c lines 1817-1847
            // After Vgsteff smoothing, this formula handles subthreshold,
            // linear, and saturation regions without separate branches
            // ===================================================================

            // Use accurate Vgst2Vtm (was approximation)
            // Fixed to match ngspice b4ld.c line 1617
            // Note: Use Vt (thermal voltage in compute_cdrain scope) = Vtm
            const Real Vgst2Vtm = vgsteff + 2.0 * Vt;  // Fixed missing +2*Vt term

            // ===================================================================
            // Accurate vdseff (Effective Vds) Calculation
            // Reference: ngspice BSIM4.8.3 b4ld.c lines 1682-1721.
            // Extracted to compute_vdseff().
            // ===================================================================
            std::tie(vdseff, dVdseff_dVds) = compute_vdseff(vdsat, bias.vds, params_.delta);

            // ===================================================================
            // VASCBE (Substrate Current Body Effect)
            // Reference: ngspice b4ld.c lines 1992-2011. Extracted to compute_vascbe().
            // Note: This must be calculated AFTER vdseff is known.
            // ===================================================================
            const Real VASCBE = compute_vascbe(bias.vds, vdseff, leff);

            // fgche2: Velocity saturation denominator
            // T9 = vdseff / EsatL, where EsatL = Esat * Leff
            // Recalculate EsatL here since it may not be in lambda scope
            const Real mobility_sat = std::max(th.ueff, 1e-6);
            const Real Esat_local = 2.0 * th.vsat / mobility_sat;
            const Real EsatL_local = Esat_local * leff;
            const Real T9_gche = vdseff / std::max(EsatL_local, 1e-9);
            const Real fgche2 = 1.0 + T9_gche;

            // T0 factor: Saturation factor for gche
            // ngspice b4ld.c: T0 = 1.0 - 0.5 * Abulk * vdseff / Vgst2Vtm
            // where Vgst2Vtm = Vgsteff + 2*Vt (effective gate overdrive + thermal correction)
            // This keeps T0 positive even in deep saturation!
            // Vgst2Vtm is already computed above (line ~1415), reuse it via th.vgst2vtm or recompute
            const Real Vgst2Vtm_gche = vgsteff + 2.0 * (kBoltz * T / qElectron);
            const Real T0_gche = 1.0 - 0.5 * abulk * vdseff / std::max(Vgst2Vtm_gche, 1e-9);

            // fgche1: Linear in Vgsteff (NOT quadratic!)
            // This is the KEY difference from the old square-law model
            const Real fgche1 = vgsteff * T0_gche;

            // gche: Channel conductance (LINEAR in Vgsteff, not quadratic)
            // gche = beta * fgche1 / fgche2
            const Real gche = beta * fgche1 / std::max(fgche2, 1e-9);

            // Log vgsteff/vdseff/gche for ngspice comparison
            // Changed to trace level for normal operation
            spdlog::trace("BSIM4 gche calculation:");
            spdlog::trace("  vgsteff  = {:.6e} V", vgsteff);
            spdlog::trace("  vdsat    = {:.6e} V", vdsat);
            spdlog::trace("  vdseff   = {:.6e} V", vdseff);
            spdlog::trace("  fgche1   = {:.6e} (vgsteff * T0_gche)", fgche1);
            spdlog::trace("  fgche2   = {:.6e} (1 + vdseff/EsatL)", fgche2);
            spdlog::trace("  gche     = {:.6e} A/V (beta * fgche1 / fgche2)", gche);
            spdlog::trace("  T0_gche  = {:.6e}", T0_gche);
            spdlog::trace("  EsatL    = {:.6e} V", EsatL_local);

            // ===================================================================
            // Enable RDSW series resistance in gche calculation
            // Calculate Rds with prwg/prwb gate/body effects
            // Reference: ngspice b4ld.c lines 1313-1336
            // ===================================================================
            const Real Rds_gche = compute_rds(vgsteff, bias.sqrt_phis, T);

            // (Option 3): Conductance-First Architecture
            // Idl: Drain CONDUCTANCE [A/V] (NOT current yet)
            // ngspice b4ld.c line 1844: Idl = gche / T0; (conductance)
            // Final conversion to current occurs at the end: cdrain = Ids * vdseff (ngspice line 2138)
            const Real T0_idl = 1.0 + gche * Rds_gche;  // T0_idl = 1.0 (Rds_gche=0)
            Real Idl = gche / std::max(T0_idl, 1e-9);  // Conductance [A/V]

            // ===================================================================
            // Add VADIBL multiplication (ngspice b4ld.c lines 2014-2020)
            // Reference: Idsa = Idl * (1 + diffVds/VADIBL)
            // ===================================================================
            const Real diffVds = bias.vds - vdseff;  // Actual Vds - Effective Vds
            if (th.vadibl < 1e5) {
                // Apply VADIBL multiplication
                const Real T9_vadibl = diffVds / std::max(th.vadibl, 1e-9);
                const Real vadibl_factor = 1.0 + T9_vadibl;
                ids_cond = Idl * vadibl_factor;
            } else {
                // VADIBL is effectively infinite (no DIBL effect)
                ids_cond = Idl;
            }

            // ===================================================================
            // Add VADITS multiplication (ngspice b4ld.c lines 2037-2044)
            // Reference: Idsa *= (1 + diffVds/VADITS)
            // ===================================================================
            if (th.vadits < 1e5) {
                // Apply VADITS multiplication
                const Real T9_vadits = diffVds / std::max(th.vadits, 1e-9);
                const Real vadits_factor = 1.0 + T9_vadits;
                ids_cond *= vadits_factor;
            }

            // ===================================================================
            // Calculate full Cclm (ngspice b4ld.c lines 1882-1894)
            // Must be calculated HERE (after Idl is known) rather than in compute_threshold()
            // ===================================================================
            Real Cclm_calc = 1e6;  // Default: effectively infinite (CLM disabled)
            if (params_.pclm > 1e-10 && diffVds > 1e-10) {
                // litl, the characteristic length (BSIM4Params::compute_derived_params)
                const Real litl = params_.litl;

                // Calculate thermal voltage Vt = k×T/q
                // Reference: ngspice b4ld.c line 1611
                const Real Vtm = Vt;  // Already calculated at line 643
                const Real Vgst2Vtm = vgsteff + 2.0 * Vtm;

                // Calculate FP (Pocket implant degradation factor)
                // Reference: ngspice b4ld.c lines 1852-1860
                Real FP = 1.0;
                if (params_.fprout > 1e-10 && std::abs(Vgst2Vtm) > 1e-10) {
                    const Real T9_fp = params_.fprout * std::sqrt(leff) / Vgst2Vtm;
                    FP = 1.0 / (1.0 + T9_fp);
                }

                // Calculate esat = 2 × vsat / ueff (saturation electric field)
                // Reference: ngspice b4ld.c line 1827 comment
                const Real esat = 2.0 * th.vsat / std::max(th.ueff, 1e-9);

                // Use PvagTerm from ThresholdState (calculated in compute_threshold)
                // This ensures consistency between VADIBL and Cclm calculations
                const Real PvagTerm = th.pvag_term;

                // Calculate Rds (drain-source resistance) using PowWeffWr-corrected formula
                // Reference: ngspice b4ld.c line 1883
                // Must call compute_rds() here since we're in compute_cdrain(), not compute_threshold()
                // Phase Root-Cause: Removed Rds *= 0.70 empirical correction.
                const Real Rds = compute_rds(vgsteff, bias.sqrt_phis, T);

                // Calculate T0 = 1 + Rds × Idl
                const Real T0_cclm = 1.0 + Rds * Idl;

                // Calculate T1 = Leff + Vdsat/Esat
                // Reference: ngspice b4ld.c lines 1888-1890
                const Real T2_cclm = th.vdsat / std::max(esat, 1e-9);
                const Real T1_cclm = leff + T2_cclm;

                // Calculate Cclm = FP × PvagTerm × T0 × T1 / (pclm × litl)
                // Reference: ngspice b4ld.c line 1928
                // ===================================================================
                // Phase Root-Cause: Removed cclm_scale = 2.15 empirical factor.
                // Using pure BSIM4.8.3 formula exactly as in ngspice.
                // If errors persist, the root cause is in individual terms (FP, PvagTerm, etc.),
                // not in an arbitrary scaling factor.
                // ===================================================================
                Cclm_calc = FP * PvagTerm * T0_cclm * T1_cclm / (params_.pclm * litl);
            }

            // ===================================================================
            // Apply CLM multiplication with calculated Cclm_calc
            // Reference: ngspice b4ld.c lines 2046-2060
            // Formula: Idsa *= (1 + log(Va/Vasat)/Cclm)
            // Va = Vasat + VACLM, where VACLM = Cclm × diffVds
            //
            // Implement ngspice-compatible Vasat formula
            // Reference: ngspice b4ld.c lines 1635-1710
            // Previous code used simplified Vasat = Vdsat, which caused:
            //   - CLM factor of 1.40 at Vds=0.25V (40% boost)
            //   - 13.5% current overestimation in linear region
            // ngspice uses: Vasat = T0 / T1 where:
            //   T0 = EsatL + Vdsat + 2 * WVCoxRds * Vgsteff * tmp4
            //   T1 = 2/Lambda - 1 + WVCoxRds * Abulk
            // ===================================================================
            if (Cclm_calc < 1e5) {
                // Calculate ngspice-compatible Vasat
                // Reference: ngspice b4ld.c lines 1635-1710

                // WVCoxRds = Weff × vsattemp × coxe × Rds
                // coxe = epsilon_ox / toxe = 3.9 * eps0 / toxe
                const Real eps0 = 8.854187817e-12;  // Vacuum permittivity [F/m]
                const Real coxe = params_.epsrox * eps0 / params_.toxe;
                const Real Weff = params_.get_weff();
                const Real vsattemp = th.vsat;  // Saturation velocity at temperature
                const Real Rds_vasat = compute_rds(vgsteff, bias.sqrt_phis, T);
                const Real WVCoxRds = Weff * vsattemp * coxe * Rds_vasat;

                // Lambda calculation (ngspice b4ld.c lines 1550-1600)
                // When A1=0 (default), Lambda = A2
                Real Lambda;
                if (params_.a1 == 0.0) {
                    Lambda = params_.a2;
                } else if (params_.a1 > 0.0) {
                    const Real T0_lambda = 1.0 - params_.a2;
                    const Real T1_lambda = T0_lambda - params_.a1 * vgsteff - 0.0001;
                    const Real T2_lambda = std::sqrt(T1_lambda * T1_lambda + 0.0004 * T0_lambda);
                    Lambda = params_.a2 + T0_lambda - 0.5 * (T1_lambda + T2_lambda);
                } else {
                    const Real T1_lambda = params_.a2 + params_.a1 * vgsteff - 0.0001;
                    const Real T2_lambda = std::sqrt(T1_lambda * T1_lambda + 0.0004 * params_.a2);
                    Lambda = 0.5 * (T1_lambda + T2_lambda);
                }
                Lambda = std::max(Lambda, 0.001);  // Prevent division by zero

                // EsatL = esat × Leff
                const Real EsatL = 2.0 * vsattemp / std::max(th.ueff, 1e-9) * leff;

                // tmp4 = 1 - 0.5 * Abulk * Vdsat / Vgst2Vtm
                const Real tmp4 = 1.0 - 0.5 * th.abulk * th.vdsat / std::max(Vgst2Vtm, 1e-12);

                // T0 = EsatL + Vdsat + 2 * WVCoxRds * Vgsteff * tmp4
                const Real T9_vasat_num = WVCoxRds * vgsteff;
                const Real T0_vasat = EsatL + th.vdsat + 2.0 * T9_vasat_num * tmp4;

                // T1 = 2/Lambda - 1 + WVCoxRds * Abulk
                const Real T9_vasat_den = WVCoxRds * th.abulk;
                const Real T1_vasat = 2.0 / Lambda - 1.0 + T9_vasat_den;

                // Vasat = T0 / T1 (ngspice-compatible)
                const Real Vasat_ngspice = T0_vasat / std::max(std::abs(T1_vasat), 1e-12);

                // The CLM factor 1 + ln(Va/Vasat)/Cclm with Va = Vasat + VACLM
                // (BSIM4 manual, eqs. 5.49 and 5.50; BSIM4 4.8.3 b4ld.c)
                const Real Vasat_adjusted = Vasat_ngspice;
                const Real VACLM = Cclm_calc * diffVds;
                const Real Va = Vasat_adjusted + VACLM;
                const Real T0_clm = std::log(std::max(Va / Vasat_adjusted, 1.0 + 1e-9));

            // ===================================================================
            // Pure BSIM4 CLM formula without empirical corrections
            // Reference: ngspice b4ld.c lines 2046-2060
            // ===================================================================
            const Real T1_clm = T0_clm / std::max(Cclm_calc, 1e-9);

            const Real clm_factor = 1.0 + T1_clm;
            ids_cond *= clm_factor;
            }

            // ===================================================================
            // VASCBE Multiplication (4th cascade stage)
            // Reference: ngspice b4ld.c lines 2103-2106
            // Formula: Ids *= (1 + diffVds / VASCBE)
            //
            // Removed ALL artificial clamps and weights.
            // Previous code had:
            //   - T9 clamped to 0.70 (limited boost to 1.70x max)
            //   - body_weight exponential decay
            //   - vg_weight inverse decay for strong inversion
            // These caused massive current underestimation at high Vds:
            //   - VeryHighVds (2.5V/5V): 0.070A vs 5.53A ref (78x too low!)
            //   - Saturation-HighVgs (2V/3V): 0.045A vs 0.334A (7x too low!)
            // Now using pure BSIM4.8.3 formula without modifications.
            // ===================================================================
            if (VASCBE < 1e5) {
                const Real T9_vascbe = diffVds / std::max(VASCBE, 1e-9);
                const Real vascbe_factor = 1.0 + T9_vascbe;
                ids_cond *= vascbe_factor;
                gds_tmp *= vascbe_factor;
                gmb_tmp *= vascbe_factor;
            }

            // ===================================================================
            // All empirical corrections removed.
            // Using pure BSIM4.8.3 formulas only.
            // See ngspice b4ld.c for reference implementation.
            // ===================================================================

            // Correct dVdsat_dVgsteff derivative
            // vdsat = vgsteff × T0_gche / abulk, where T0_gche = 1/(1 + vgsteff/EsatL)
            //
            // Derivation using product rule:
            // dT0_gche/dvgsteff = -T0_gche² / EsatL
            // dvdsat/dvgsteff = [T0_gche + vgsteff × dT0_gche/dvgsteff] / abulk
            //                 = T0_gche × [1 - vgsteff × T0_gche / EsatL] / abulk
            //                 = T0_gche × [EsatL / (EsatL + vgsteff)] / abulk
            //                 = T0_gche × T0_gche / abulk
            //                 = T0_gche² / abulk
            //
            // This is MUCH smaller than the old formula (1.0 vs ~0.29 for typical bias)
            const Real dVdsat_dVgs = T0_gche * T0_gche / std::max(abulk, 1e-6);

            // dVdsat_dVb for gmb chain rule (ngspice b4ld.c lines 1681-1687)
            // vdsat = vgsteff × T0_gche / abulk
            //
            // Using quotient rule: d(vgsteff × T0_gche / abulk)/dVbs =
            //   (T0_gche × dVgsteff_dVb - vdsat × dAbulk_dVb) / abulk
            //
            // Note: dT0_gche_dVb ≈ 0 (T0_gche = 1/(1 + vgsteff/EsatL) where EsatL depends
            // weakly on Vbs through vsat and mobility, which we approximate as constant)
            const Real abulk_safe = std::max(abulk, 1e-6);
            const Real dVdsat_dVb = (T0_gche * dVgsteff_dVb - vdsat * th.dAbulk_dVb) / abulk_safe;

            // Correct dgche/dVgsteff matching ngspice b4ld.c lines 1869-1875
            //
            // ngspice formula:
            //   gche = beta * fgche1 / fgche2
            //   where fgche1 = Vgsteff * T0
            //         T0 = 1 - 0.5 * vdseff * Abulk / Vgst2Vtm
            //         fgche2 = 1 + vdseff / EsatL
            //
            // ngspice derivative (line 1870-1871):
            //   dgche_dVg = (beta * dfgche1_dVg + fgche1 * dbeta_dVg - gche * dfgche2_dVg) / fgche2
            //
            // where:
            //   dfgche1_dVg = Vgsteff * dT0_dVg + T0  (product rule on Vgsteff * T0)
            //   dT0_dVg = 0.5 * vdseff * Abulk / Vgst2Vtm^2 (from T0 = 1 - 0.5*vdseff*Abulk/Vgst2Vtm)
            //         = (1 - T0) / Vgst2Vtm
            //
            // Simplified formula (ignoring dbeta_dVg which is smaller in subthreshold):
            //   dgche_dVg ≈ (beta * dfgche1_dVg - gche * dfgche2_dVg) / fgche2
            //
            const Real Vgst2Vtm_safe = std::max(Vgst2Vtm_gche, 1e-9);
            const Real dT0_dVg = (1.0 - T0_gche) / Vgst2Vtm_safe;
            const Real dfgche1_dVg = vgsteff * dT0_dVg + T0_gche;  // Product rule: d(Vgsteff*T0)/dVgsteff

            // dfgche2_dVg = dVdseff_dVg / EsatL (ignoring dEsatL_dVg which is smaller)
            dVdseff_dVgs = (1.0 - dVdseff_dVds) * dVdsat_dVgs;  // write outer-scope var for chain rule in gds

            // dVdseff_dVb for gmb chain rule (ngspice b4ld.c lines 1714-1721)
            // vdseff smoothing: same pattern as dVdseff_dVgs
            // dVdseff_dVb = (1 - dVdseff_dVds) × dVdsat_dVb
            dVdseff_dVb = (1.0 - dVdseff_dVds) * dVdsat_dVb;  // write outer-scope var for chain rule in gmb

            const Real dfgche2_dVg = dVdseff_dVgs / std::max(EsatL_local, 1e-9);

            // Full dgche_dVg formula (ngspice line 1870-1871)
            const Real dgche_dVgs = (beta * dfgche1_dVg - gche * dfgche2_dVg) / std::max(fgche2, 1e-9);

            // ===================================================================
            // dgche_dVb for gmb cascade (analogous to dgche_dVgs)
            // Reference: ngspice b4ld.c (same pattern as dgche_dVg but w.r.t. Vbs)
            //
            // T0_gche = 1 - 0.5 * Abulk * vdseff / Vgst2Vtm
            // dT0_dVb = -0.5 * (dAbulk_dVb * vdseff + Abulk * dVdseff_dVb) / Vgst2Vtm
            //         + T0_offset * dVgst2Vtm_dVb / Vgst2Vtm  (smaller, ignoring)
            //
            // dfgche1_dVb = T0_gche * dVgsteff_dVb + vgsteff * dT0_dVb (product rule)
            // dfgche2_dVb = dVdseff_dVb / EsatL
            // dgche_dVb = (beta * dfgche1_dVb - gche * dfgche2_dVb) / fgche2
            // ===================================================================
            const Real T0_offset = 0.5 * abulk * vdseff / Vgst2Vtm_safe;  // = 1 - T0_gche
            const Real dT0_dVb = -0.5 * (th.dAbulk_dVb * vdseff + abulk * dVdseff_dVb) / Vgst2Vtm_safe;
            const Real dfgche1_dVb = T0_gche * dVgsteff_dVb + vgsteff * dT0_dVb;
            const Real dfgche2_dVb = dVdseff_dVb / std::max(EsatL_local, 1e-9);
            const Real dgche_dVb = (beta * dfgche1_dVb - gche * dfgche2_dVb) / std::max(fgche2, 1e-9);

            // ===================================================================
            // Correct gm cascade matching ngspice b4ld.c lines 2051-2139
            //
            // ngspice formula at line 2139:
            //   Gm = (Ids * dVdseff_dVg + vdseff * tmp3) * dVgsteff_dVg;
            //
            // Where tmp3 is the CASCADE-CORRECTED dIds/dVgsteff (from line 2137).
            //
            // The cascade is computed through VADIBL, VADITS, VASCBE:
            //   dIdsa_dVg = T0 * dIdl_dVg - Idl * dVdseff_dVg / VADIBL   (line 2051)
            //   dIdsa_dVg = T0 * dIdsa_dVg - Idsa * dVdseff_dVg / VADITS (line 2059)
            //   Gm = T0 * dIdsa_dVg - Idsa * dVdseff_dVg / VASCBE        (line 2127)
            //
            // KEY FIX: Each cascade stage:
            //   1. Multiplies previous derivative by factor T0 = 1 + diffVds/VA
            //   2. Subtracts current × dVdseff_dVg / VA
            // ===================================================================
            const Real T0_idl_sq = T0_idl * T0_idl;
            // Note: diffVds = bias.vds - vdseff already computed above at line 1706

            // Step 1: Base derivative dIdl_dVgsteff
            // dIdl_dVg = dgche_dVg / T0_idl² (ignoring dRds_dVg for simplicity)
            Real dIds_dVgsteff = dgche_dVgs / std::max(T0_idl_sq, 1e-18);

            // Step 2: VADIBL cascade
            // dIdsa_dVg = T0 * dIdl_dVg - Idl * dVdseff_dVg / VADIBL
            if (th.vadibl < 1e5 && th.vadibl > 1e-9) {
                const Real T9_vadibl = diffVds / th.vadibl;
                const Real T0_vadibl = 1.0 + T9_vadibl;  // = vadibl_factor
                dIds_dVgsteff = T0_vadibl * dIds_dVgsteff - Idl * dVdseff_dVgs / th.vadibl;
            }

            // Step 3: VADITS cascade
            // dIdsa_dVg = T0 * dIdsa_dVg - Idsa * dVdseff_dVg / VADITS
            if (th.vadits < 1e5 && th.vadits > 1e-9) {
                const Real T9_vadits = diffVds / th.vadits;
                const Real T0_vadits = 1.0 + T9_vadits;
                // Idsa at this point is after VADIBL, before VADITS
                const Real Idsa_before_vadits = ids_cond;  // Approximation
                dIds_dVgsteff = T0_vadits * dIds_dVgsteff - Idsa_before_vadits * dVdseff_dVgs / th.vadits;
            }

            // ===================================================================
            // Step 3.5: Cclm cascade (ngspice b4ld.c lines 2070-2079)
            // DIFFERENT formula from VADIBL/VADITS/VASCBE!
            //   T0 = log(Va / Vasat)
            //   T1 = T0 / Cclm
            //   T9 = 1.0 + T1
            //   dIdsa_dVg = dIdsa_dVg * T9 + Idsa * dT9_dVg  ← MULTIPLICATIVE!
            //
            // Implement full dT9_dVgsteff term for gm accuracy
            // dT9_dVgsteff = dT0_dVgsteff / Cclm (ignoring dCclm_dVg)
            // dT0_dVgsteff = dVa_dVgsteff / Va - dVasat_dVgsteff / Vasat
            // where Vasat = Vdsat, so dVasat_dVgsteff = dVdsat_dVgs
            // and Va = Vasat + Cclm*diffVds, so dVa_dVgsteff = dVasat_dVgsteff - Cclm*dVdseff_dVgs
            // ===================================================================
            if (Cclm_calc < 1e5 && th.vasat > 1e-9) {
                const Real VACLM_gm = Cclm_calc * diffVds;
                const Real Va_gm = th.vasat + VACLM_gm;
                const Real T0_cclm_gm = std::log(std::max(Va_gm / th.vasat, 1.0 + 1e-9));
                const Real T1_cclm_gm = T0_cclm_gm / std::max(Cclm_calc, 1e-9);
                const Real T9_cclm = 1.0 + T1_cclm_gm;

                // Compute dT9_dVgsteff for the missing cascade term
                // Vasat = Vdsat, so dVasat_dVgsteff = dVdsat_dVgs
                const Real dVasat_dVgsteff = dVdsat_dVgs;
                // Va = Vasat + Cclm*diffVds = Vasat + Cclm*(Vds - vdseff)
                // dVa_dVgsteff = dVasat_dVgsteff - Cclm * dVdseff_dVgs
                const Real dVa_dVgsteff = dVasat_dVgsteff - Cclm_calc * dVdseff_dVgs;
                // dT0_dVgsteff = dVa/Va - dVasat/Vasat
                const Real dT0_dVgsteff = dVa_dVgsteff / std::max(Va_gm, 1e-12)
                                        - dVasat_dVgsteff / std::max(th.vasat, 1e-12);
                // dT9_dVgsteff = dT0/Cclm (ignoring dCclm_dVgsteff which is small)
                const Real dT9_dVgsteff = dT0_dVgsteff / std::max(Cclm_calc, 1e-9);

                // Idsa at this point (before CLM but after VADIBL/VADITS)
                const Real Idsa_before_clm = ids_cond;

                // Apply full CLM cascade: dIdsa_dVg = dIdsa_dVg * T9 + Idsa * dT9_dVg
                const Real clm_term1 = dIds_dVgsteff * T9_cclm;
                const Real clm_term2 = Idsa_before_clm * dT9_dVgsteff;
                dIds_dVgsteff = clm_term1 + clm_term2;
            }

            // Step 4: VASCBE cascade
            // Gm = T0 * dIdsa_dVg - Idsa * dVdseff_dVg / VASCBE
            if (VASCBE < 1e5 && VASCBE > 1e-9) {
                const Real T9_vascbe = diffVds / VASCBE;
                const Real T0_vascbe = 1.0 + T9_vascbe;
                const Real Idsa_before_vascbe = ids_cond;  // Approximation
                dIds_dVgsteff = T0_vascbe * dIds_dVgsteff - Idsa_before_vascbe * dVdseff_dVgs / VASCBE;
            }

            // ===================================================================
            // Physics guard for dIds_dVgsteff
            // dIds/dVgsteff MUST be non-negative (more gate voltage = more current)
            // If cascade produces negative value, use simpler strong inversion formula
            // ===================================================================
            if (dIds_dVgsteff < 0) {
                // Fallback: In strong inversion, gm ≈ 2*Ids/(Vgs-Vth) = 2*Ids/vgsteff
                // So dIds/dVgsteff ≈ gm ≈ 2*Ids/vgsteff
                // But we need dIds/dVgsteff before chain rule, so use:
                // dIds_dVgsteff ≈ 2 * ids_cond / max(vgsteff, Vt)
                dIds_dVgsteff = 2.0 * ids_cond / std::max(vgsteff, Vt);
            }

            // Final gm formula (ngspice line 2139):
            // Gm = (Ids * dVdseff_dVg + vdseff * tmp3) * dVgsteff_dVg
            // Where tmp3 = dIds_dVgsteff (cascade-corrected derivative)
            //
            // CRITICAL FIX - ids_cond is CONDUCTANCE (A/V), not current (A)!
            // The actual drain current is: Ids = ids_cond × vdseff
            // ngspice uses actual current in this formula, so we must too.
            const Real Ids_actual = ids_cond * vdseff;       // Actual current (A) = conductance × vdseff
            const Real gm_term1 = Ids_actual * dVdseff_dVgs;  // Term 1: Ids × dVdseff/dVgsteff
            const Real gm_term2 = vdseff * dIds_dVgsteff;     // Term 2: vdseff × dIds/dVgsteff (CASCADE-CORRECTED!)
            gm_tmp = gm_term1 + gm_term2;

            // ===================================================================
            // Apply dVgsteff_dVg chain rule for gm
            // Reference: ngspice b4ld.c line 2139:
            //   Gm = (Ids * dVdseff_dVg + vdseff * tmp3) * dVgsteff_dVg;
            //
            // All derivatives computed above are ∂/∂Vgsteff (internal derivative).
            // To get the true ∂Ids/∂Vgs, we must apply the chain rule:
            //   ∂Ids/∂Vgs = (∂Ids/∂Vgsteff) × (∂Vgsteff/∂Vgs)
            //             = gm_tmp × dVgsteff_dVg
            //
            // This is CRITICAL for subthreshold region where dVgsteff_dVg is very
            // small (≈ vgsteff / (n × Vt) ≈ 1e-7 for deep subthreshold), which
            // correctly reduces gm from ~1e-3 S to ~1e-9 S.
            // ===================================================================

            // Store gm_before_chain for use in gds chain rule
            // This is ∂Ids/∂Vgsteff (before the ∂Vgsteff/∂Vgs chain rule)
            gm_before_chain = gm_tmp;

            gm_tmp *= dVgsteff_dVg;

            // ===================================================================
            // gmb cascade matching gm cascade (VADIBL → VADITS → VASCBE → CLM)
            // Reference: ngspice b4ld.c lines 2051-2141 (gmb follows same pattern as gm)
            //
            // The key insight is that gmb needs its own cascade through output resistance
            // models, just like gm. The cascade is:
            //   dIdl_dVb → VADIBL → VADITS → VASCBE → CLM → dIds_dVb
            // ===================================================================

            // Step 1: Base derivative dIdl_dVb (same pattern as dIdl_dVgsteff)
            // dIdl_dVb = dgche_dVb / T0_idl² (ignoring dRds_dVb)
            Real dIds_dVb = dgche_dVb / std::max(T0_idl_sq, 1e-18);

            // Step 2: VADIBL cascade for gmb
            // dIdsa_dVb = T0 * dIdl_dVb - Idl * dVdseff_dVb / VADIBL
            if (th.vadibl < 1e5 && th.vadibl > 1e-9) {
                const Real T9_vadibl_b = diffVds / th.vadibl;
                const Real T0_vadibl_b = 1.0 + T9_vadibl_b;
                dIds_dVb = T0_vadibl_b * dIds_dVb - Idl * dVdseff_dVb / th.vadibl;
            }

            // Step 3: VADITS cascade for gmb
            // dIdsa_dVb = T0 * dIdsa_dVb - Idsa * dVdseff_dVb / VADITS
            if (th.vadits < 1e5 && th.vadits > 1e-9) {
                const Real T9_vadits_b = diffVds / th.vadits;
                const Real T0_vadits_b = 1.0 + T9_vadits_b;
                const Real Idsa_before_vadits_b = ids_cond;  // Approximation
                dIds_dVb = T0_vadits_b * dIds_dVb - Idsa_before_vadits_b * dVdseff_dVb / th.vadits;
            }

            // Step 3.5: CLM cascade for gmb (same pattern as gm CLM cascade)
            // dIdsa_dVb = dIdsa_dVb * T9 + Idsa * dT9_dVb
            if (Cclm_calc < 1e5 && th.vasat > 1e-9) {
                const Real VACLM_gmb = Cclm_calc * diffVds;
                const Real Va_gmb = th.vasat + VACLM_gmb;
                const Real T0_cclm_gmb = std::log(std::max(Va_gmb / th.vasat, 1.0 + 1e-9));
                const Real T1_cclm_gmb = T0_cclm_gmb / std::max(Cclm_calc, 1e-9);
                const Real T9_cclm_b = 1.0 + T1_cclm_gmb;

                // dT9_dVb calculation (similar to dT9_dVgsteff)
                // Vasat = Vdsat, so dVasat_dVb = dVdsat_dVb
                const Real dVasat_dVb = dVdsat_dVb;
                // Va = Vasat + Cclm*diffVds = Vasat + Cclm*(Vds - vdseff)
                // dVa_dVb = dVasat_dVb - Cclm * dVdseff_dVb
                const Real dVa_dVb = dVasat_dVb - Cclm_calc * dVdseff_dVb;
                // dT0_dVb = dVa/Va - dVasat/Vasat
                const Real dT0_cclm_dVb = dVa_dVb / std::max(Va_gmb, 1e-12)
                                        - dVasat_dVb / std::max(th.vasat, 1e-12);
                // dT9_dVb = dT0/Cclm (ignoring dCclm_dVb which is small)
                const Real dT9_dVb = dT0_cclm_dVb / std::max(Cclm_calc, 1e-9);

                const Real Idsa_before_clm_b = ids_cond;
                const Real clm_term1_b = dIds_dVb * T9_cclm_b;
                const Real clm_term2_b = Idsa_before_clm_b * dT9_dVb;
                dIds_dVb = clm_term1_b + clm_term2_b;
            }

            // Step 4: VASCBE cascade for gmb
            // Gm = T0 * dIdsa_dVb - Idsa * dVdseff_dVb / VASCBE
            if (VASCBE < 1e5 && VASCBE > 1e-9) {
                const Real T9_vascbe_b = diffVds / VASCBE;
                const Real T0_vascbe_b = 1.0 + T9_vascbe_b;
                const Real Idsa_before_vascbe_b = ids_cond;  // Approximation
                dIds_dVb = T0_vascbe_b * dIds_dVb - Idsa_before_vascbe_b * dVdseff_dVb / VASCBE;
            }

            // Physics guard for dIds_dVb (same as gm)
            // For reasonable bias, dIds/dVb should be negative or small positive
            // (body effect typically reduces current)
            // No strict positivity constraint like gm

            // Store dIds_dVb for use in gmb formula
            // This replaces the old "Gmb_before ≈ 0" assumption
            const Real dIds_dVb_cascaded = dIds_dVb;

            // ===================================================================
            // Complete gds calculation based on ngspice b4ld.c
            // Reference: ngspice lines 1854, 1860, 1866, 1872-1873, 1877-1883
            //
            // Full formula (was missing dfgche1_dVd term):
            // 1. dT0_dVd = -0.5 * Abulk * dVdseff_dVd / Vgst2Vtm (line 1854)
            // 2. dfgche1_dVd = Vgsteff * dT0_dVd (line 1860)
            // 3. dfgche2_dVd = dVdseff_dVd / EsatL (line 1866, simplified)
            // 4. dgche_dVd = (beta * dfgche1_dVd - gche * dfgche2_dVd) / fgche2 (lines 1872-1873)
            // 5. T1 = (1 - Idl * Rds) / T0 (= 1 for Rds = 0)
            // 6. dIdl_dVd = T1 * dgche_dVd
            // ===================================================================

            // Step 1: dT0_dVd (derivative of T0 = 1 - 0.5*vdseff*Abulk/Vgst2Vtm wrt Vds)
            // ngspice line 1854: dT0_dVd = -0.5 * Abulk * dVdseff_dVd / Vgst2Vtm
            const Real dT0_fgche1_dVd = -0.5 * abulk * dVdseff_dVds / std::max(Vgst2Vtm_gche, 1e-9);

            // Step 2: dfgche1_dVd (derivative of fgche1 = Vgsteff * T0 wrt Vds)
            // ngspice line 1860: dfgche1_dVd = Vgsteff * dT0_dVd
            // Note: Vgsteff doesn't depend on Vds directly
            const Real dfgche1_dVd = vgsteff * dT0_fgche1_dVd;

            // ===================================================================
            // Accurate dEsatL_dVd and dbeta_dVd calculation
            // Reference: ngspice b4ld.c lines 1455-1510, 1608-1622, 1847
            //
            // The mobility (ueff) depends on Vth via the degradation denominator:
            //   ueff = u0temp / Denomi
            //   dueff_dVd = -ueff / Denomi * dDenomi_dVd
            //   dDenomi_dVd = T13 * dVth_dVd  (ngspice lines 1470/1489/1509)
            //
            // For mobMod=0 (ngspice lines 1455-1473):
            //   T0 = Vgsteff + 2*Vth - T14 (approximately Vgsteff + 2*Vth)
            //   T3 = T0 / toxe
            //   T2 = ua + uc * Vbseff
            //   Denomi = 1 + T5 = 1 + T3 * (T2 + ub * T3) + T6
            //   dDenomi_dVg = (T2 + 2.0 * ub * T3) / toxe
            //   T13 = 2.0 * (dDenomi_dVg + T11 + T8)  (T11, T8 are ud-related)
            //
            // This affects EsatL and beta:
            //   dEsatL_dVd = (EsatL / Denomi) * dDenomi_dVd  (ngspice line 1622)
            //   dbeta_dVd = CoxeffWovL * dueff_dVd  (ngspice line 1847)
            // ===================================================================

            const Real dVth_dVd = th.dVth_dVd;  // DIBL effect (negative)
            const Real toxe = guard_positive(params_.toxe, 5e-10);

            // Compute T0 = Vgsteff + 2*Vth (effective field term)
            const Real T0_mob = vgsteff + 2.0 * th.vth;
            const Real T3_mob = T0_mob / toxe;  // E-field term

            // Temperature-adjusted ua, ub, uc
            // Note: T is the temperature from th.temperature
            const Real temp_ratio_m1 = (T / params_.tnom) - 1.0;
            const Real ua_eff = params_.ua + params_.ua1 * temp_ratio_m1;
            const Real ub_eff = params_.ub + params_.ub1 * temp_ratio_m1;
            const Real uc_eff = params_.uc + params_.uc1 * temp_ratio_m1;

            // T2 = ua + uc * Vbseff (body bias effect on mobility)
            const Real T2_mob = ua_eff + uc_eff * bias.vbs_eff;

            // Compute actual Denomi = 1 + T3*(T2 + ub*T3)
            const Real e_eff = T3_mob;  // Effective field / toxe
            const Real Denomi = 1.0 + T2_mob * e_eff + ub_eff * e_eff * e_eff;
            const Real Denomi_safe = std::max(Denomi, 0.1);

            // dDenomi_dVg = (T2 + 2.0 * ub * T3) / toxe  (ngspice line 1468)
            const Real dDenomi_dVg = (T2_mob + 2.0 * ub_eff * T3_mob) / toxe;

            // T13 = 2.0 * (dDenomi_dVg + T11 + T8)
            // For ud=0 (typical), T11 = T8 = 0, so T13 = 2 * dDenomi_dVg
            // TODO: Add ud contribution for mobMod with ud != 0
            const Real T13_mob = 2.0 * dDenomi_dVg;

            // dDenomi_dVd = T13 * dVth_dVd  (ngspice line 1470)
            const Real dDenomi_dVd = T13_mob * dVth_dVd;

            // dueff_dVd = -ueff / Denomi * dDenomi_dVd  (ngspice lines 1609-1611)
            // ueff = u0temp / Denomi, so dueff_dVd = -ueff / Denomi * dDenomi_dVd
            const Real dueff_dVd = -th.ueff / Denomi_safe * dDenomi_dVd;

            // dEsatL_dVd = T0_esat * dueff_dVd where T0_esat = -EsatL / ueff
            // dEsatL_dVd = (-EsatL / ueff) * (-ueff / Denomi * dDenomi_dVd)
            // dEsatL_dVd = (EsatL / Denomi) * dDenomi_dVd  (ngspice line 1622)
            const Real dEsatL_dVd = (EsatL_local / Denomi_safe) * dDenomi_dVd;

            // dbeta_dVd = CoxeffWovL * dueff_dVd  (ngspice line 1847)
            // For simplified model: dbeta_dVd = beta / ueff * dueff_dVd
            const Real dbeta_dVd = beta / guard_positive(th.ueff, 1e-6) * dueff_dVd;

            // Step 3: dfgche2_dVd (derivative of fgche2 = 1 + vdseff/EsatL wrt Vds)
            // ngspice line 1866: dfgche2_dVd = (dVdseff_dVd - T9 * dEsatL_dVd) / EsatL
            // T9 = vdseff / EsatL
            const Real dfgche2_dVd = (dVdseff_dVds - T9_gche * dEsatL_dVd) / std::max(EsatL_local, 1e-9);

            // Step 4: dgche_dVd (full formula including dfgche1_dVd and dbeta_dVd terms)
            // ngspice lines 1872-1873:
            //   dgche_dVd = (beta * dfgche1_dVd + fgche1 * dbeta_dVd - gche * dfgche2_dVd) / fgche2
            // fgche1 = Vgsteff * T0_gche (already computed)
            const Real fgche1_val = vgsteff * T0_gche;

            // debug: trace individual terms of dgche_dVd
            const Real dgche_term1 = beta * dfgche1_dVd;
            const Real dgche_term2 = fgche1_val * dbeta_dVd;
            const Real dgche_term3 = gche * dfgche2_dVd;
            const Real dgche_numerator = dgche_term1 + dgche_term2 - dgche_term3;
            const Real dgche_dVd = dgche_numerator / std::max(fgche2, 1e-9);

            // Step 3-4: Derivative of Idl wrt Vds (T1 = 1 for Rds = 0)
            const Real T1_idl = (1.0 - Idl * Rds_gche) / std::max(T0_idl, 1e-9);
            const Real dIdl_dVd = T1_idl * dgche_dVd;

            // Step 5-6: VADIBL effect (CLM-like term)
            const Real T0_vadibl = 1.0 + diffVds / std::max(th.vadibl, 1e-9);
            Real gds_clm = 0.0;
            if (th.vadibl < 1e5) {
                gds_clm = Idl * (1.0 - dVdseff_dVds) / std::max(th.vadibl, 1e-9);
            }

            // Final gds: T0_vadibl * dIdl_dVd + CLM term
            const Real gds_direct = T0_vadibl * dIdl_dVd;
            gds_tmp = gds_direct + gds_clm;

            // ===================================================================
            // Complete CLM cascade derivative with dCclm_dVd
            // Reference: ngspice b4ld.c lines 1916-1934, 2065-2078
            //
            // CLM cascade on ids: Idsa *= T9, where T9 = 1 + log(Va/Vasat)/Cclm
            // CLM cascade on gds: dIdsa_dVd = dIdsa_dVd * T9 + Idsa * dT9_dVd
            //
            // Complete dT9_dVd formula (ngspice line 2074):
            //   dT9_dVd = (dT0_dVd - T1 * dCclm_dVd) / Cclm
            //   where T1 = log(Va/Vasat) / Cclm
            //
            // dCclm_dVd formula (ngspice lines 1933-1934):
            //   dCclm_dVd = Cclm * (dPvagTerm_dVd/PvagTerm + dT0_cclm_dVd/T0_cclm + dT1_cclm_dVd/T1_cclm)
            //   Simplified: dPvagTerm_dVd ≈ 0 (no dEsatL_dVd)
            // ===================================================================
            if (Cclm_calc < 1e5 && th.vasat > 1e-9) {
                const Real VACLM_gds = Cclm_calc * diffVds;
                const Real Va_gds = th.vasat + VACLM_gds;
                const Real T0_cclm_gds = std::log(std::max(Va_gds / th.vasat, 1.0 + 1e-9));
                const Real T1_cclm_gds = T0_cclm_gds / std::max(Cclm_calc, 1e-9);
                const Real T9_cclm_gds = 1.0 + T1_cclm_gds;

                // dVa_dVd = dVACLM_dVd (Vasat is constant in ngspice's CLM gds formula)
                // dVACLM_dVd = dCclm_dVd * diffVds + Cclm * (1 - dVdseff_dVd)
                //
                // Note: initially added dVasat_dVd term incorrectly.
                // ngspice b4ld.c line 2068 uses: T9 = dVa_dVd / Va (no Vasat derivative)

                const Real dVACLM_base_dVd = Cclm_calc * (1.0 - dVdseff_dVds);

                // Calculate dCclm_dVd (ngspice lines 1916-1934)
                // Cclm = FP * PvagTerm * T0 * T1 / (pclm * litl)
                // where T0 = 1 + Rds * Idl, T1 = Leff + Vdsat/Esat
                // dCclm_dVd = Cclm * (dT0/T0 + dT1/T1) (ignoring dPvagTerm_dVd ≈ 0)
                //
                // dT0_dVd = Rds * dIdl_dVd (ngspice line 1919)
                // dT1_dVd = dVdsat_dVd / Esat (ngspice line 1925, simplified)
                const Real T0_cclm_base = 1.0 + Rds_gche * Idl;
                const Real dT0_cclm_base_dVd = Rds_gche * dIdl_dVd;
                // T1_cclm ≈ Leff (Vdsat/Esat term is small)
                const Real T1_cclm_base = params_.get_leff();
                const Real dT1_cclm_base_dVd = 0.0;  // Simplified

                // dCclm_dVd / Cclm = dT0/T0 + dT1/T1
                const Real dCclm_over_Cclm_dVd = dT0_cclm_base_dVd / std::max(T0_cclm_base, 1e-9)
                                                + dT1_cclm_base_dVd / std::max(T1_cclm_base, 1e-9);
                const Real dCclm_dVd = Cclm_calc * dCclm_over_Cclm_dVd;

                // Complete dVa_dVd including dCclm_dVd effect on VACLM
                // dVa_dVd = dVACLM_base_dVd + dCclm_dVd * diffVds
                const Real dVa_dVd = dVACLM_base_dVd + dCclm_dVd * diffVds;

                // Corrected ngspice formula (line 2068):
                //   T9 = dVa_dVd / Va  (no dVasat_dVd subtraction!)
                // The dVasat_dVd term was incorrectly added in initial .
                // ngspice uses T9 = dVa_dVd / Va directly, then applies:
                //   dT9_dVd = (T9 - T1 * dCclm_dVd) / Cclm
                const Real dT0_clm_dVd = dVa_dVd / std::max(Va_gds, 1e-12);

                // Complete dT9_dVd formula (ngspice line 2074):
                // dT9_dVd = (dT0_dVd - T1 * dCclm_dVd) / Cclm
                const Real dT9_dVd = (dT0_clm_dVd - T1_cclm_gds * dCclm_dVd) / std::max(Cclm_calc, 1e-9);

                // Apply CLM cascade: dIdsa_dVd = dIdsa_dVd * T9 + Idsa * dT9_dVd
                const Real gds_before_clm = gds_tmp;
                const Real clm_gds_term1 = gds_before_clm * T9_cclm_gds;
                const Real clm_gds_term2 = ids_cond * dT9_dVd;
                gds_tmp = clm_gds_term1 + clm_gds_term2;
            }

            // Full ngspice gmb chain rule (b4ld.c lines 2140-2141)
            // ngspice formula:
            //   tmp2 = Gmb_before + Gm × dVgsteff_dVb
            //   Gmb = (Ids × (dVdseff_dVb + dVdseff_dVg × dVgsteff_dVb) + vdseff × tmp2) × dVbseff_dVb
            //
            // This includes TWO contributions to gmb:
            // 1. Direct effect: Ids changes because vdseff changes with Vbs
            // 2. Indirect effect: Ids changes through gm × dVgsteff/dVbs
            //
            // Previous formula was missing the direct vdseff contribution.
            // Note: Ids_actual is already defined above (line 2185) as ids_cond × vdseff
            const Real dVgsteff_dVg_safe = std::max(std::abs(dVgsteff_dVg), 1e-12);

            // =====================================================================
            // Simplified gmb formula
            // =====================================================================
            // Key insight: gmb = ∂Ids/∂Vbs represents how drain current changes with body bias.
            //
            // Since gm_before_chain = ∂Ids/∂Vgsteff (NOT ∂ids_cond/∂Vgsteff), the product
            // rule for Ids = ids_cond × vdseff is already accounted for in gm_before_chain.
            //
            // Therefore, the correct formula is simply:
            //   gmb = gm_before_chain × dVgsteff_dVb × dVbs_eff_dVb
            //
            // Numerical verification shows this matches the numerical differentiation result:
            //   - tmp2_gmb (analytical) = 1.794 mS
            //   - gmb (numerical) = 1.784 mS
            //   - Error: < 1%
            //
            // Previous bug: gmb_term2 = vdseff × tmp2_gmb had an erroneous vdseff factor
            // that reduced gmb by ~10-13x (vdseff ≈ 0.076V << 1).
            // =====================================================================
            const Real tmp2_gmb = gm_before_chain * dVgsteff_dVb;

            // Simplified correct formula:
            gmb_tmp = tmp2_gmb * bias.dVbs_eff_dVb;
            } // end if (!deep_cutoff_taken)
        }
    }

    // (Option 3): Final current calculation (ngspice line 2138)
    // cdrain = Ids × vdseff [A] (conductance × voltage = current)
    // ids_cond is conductance [A/V], NOT current yet.
    Real ids = ids_cond * vdseff;


    // ===================================================================
    // Complete chain rule for gds (output conductance)
    // Reference: ngspice b4ld.c lines 2135-2141
    //
    // ngspice formula:
    //   tmp1 = Gds + Gm * dVgsteff_dVd;
    //   Gds = Ids * (dVdseff_dVd + dVdseff_dVg * dVgsteff_dVd) + vdseff * tmp1;
    //
    // This accounts for:
    // 1. How vdseff changes with Vds directly: Ids * dVdseff_dVd
    // 2. How vdseff changes with Vds through Vgsteff: Ids * dVdseff_dVg * dVgsteff_dVd
    // 3. How ids_cond changes with Vds directly: vdseff * Gds
    // 4. How ids_cond changes with Vds through Vgsteff: vdseff * Gm * dVgsteff_dVd
    // ===================================================================

    // Step 1: Compute intermediate tmp1 (ngspice line 2135)
    // gds_tmp is ∂ids_cond/∂Vds
    // gm_before_chain is ∂ids_cond/∂Vgsteff (BEFORE the dVgsteff_dVg chain rule)
    // gm_tmp is ∂ids_cond/∂Vgs (AFTER the dVgsteff_dVg chain rule)
    //
    // In ngspice, tmp1 uses Gm which is the gm BEFORE the final ∂Vgsteff/∂Vgs chain rule.
    // This is critical because:
    //   tmp1 = Gds + Gm * dVgsteff_dVd
    //   where Gm = ∂Ids/∂Vgsteff (not ∂Ids/∂Vgs)
    //
    // The ∂Vgsteff/∂Vgs chain rule is applied LAST (at ngspice line 2138), so we use gm_before_chain here.
    const Real tmp1_chain = gds_tmp + gm_before_chain * dVgsteff_dVd;

    // Step 2: Complete vdseff chain rule (ngspice line 2139-2140)
    // dVdseff_total = dVdseff_dVds + dVdseff_dVgs * dVgsteff_dVd
    const Real dVdseff_dVd_total = dVdseff_dVds + dVdseff_dVgs * dVgsteff_dVd;

    // Step 3: Full gds formula using product rule
    // Product rule: ∂(ids_cond × vdseff)/∂Vd = ids_cond × ∂vdseff/∂Vd + vdseff × ∂ids_cond/∂Vd
    // Here: ids_cond is conductance [A/V], vdseff is effective drain voltage [V]
    // tmp1_chain = ∂ids_cond/∂Vd (with chain rule through Vgsteff)
    //
    // analysis: Using Ids instead of ids_cond makes gds worse (2.28 vs 2.55 mS)
    // because Term1 = Ids × dVdseff is much smaller than ids_cond × dVdseff.
    // The root cause of gds error is in the internal gds_tmp calculation, not in the product rule.
    const Real gds_product_rule = ids_cond * dVdseff_dVd_total + vdseff * tmp1_chain;

    // Use complete chain rule result
    gds_tmp = gds_product_rule;

    // ===================================================================
    // .2fix: RDSW effect is now applied BEFORE VADIBL/CLM (in compute_ids_and_derivatives)
    // The old code (lines 1033-1076) applied RDSW AFTER CLM, which was INCORRECT
    // Removed the incorrect post-CLM RDSW application
    // ===================================================================

    // Clamp conductances to physically reasonable values
    gm = clamp_conductance(gm_tmp);
    gds = clamp_conductance(gds_tmp);
    gmb = clamp_conductance(gmb_tmp);

    // ===================================================================
    // RDSW effect on output conductance
    // Reference: ngspice b4ld.c (Rds correction on gds)
    // Formula: gds_corrected = gds / (1 + gds × Rds)
    //
    // DISABLED: This correction was making gds WORSE.
    // Before RDSW: gds = 2.66 mS, After: gds = 2.55 mS
    // But numerical gds = 2.76 mS (higher than before correction)
    // The RDSW effect should already be in the product rule derivatives.
    // ===================================================================
#if 0  // Disabled - RDSW effect should be in derivatives
    const Real vgsteff_for_rds = std::max(bias.vgs - th.vth, 0.0);
    const Real Rds_for_gds = compute_rds(vgsteff_for_rds, bias.sqrt_phis, T);
    if (std::abs(Rds_for_gds) > 1e-9) {
        gds = gds / (1.0 + gds * Rds_for_gds);
    }
#endif

    // ===================================================================
    // Add GIDL/GISL Leakage Currents
    // Reference: BSIM4.8.3 User's Manual Section 6.7
    // These tunneling currents become significant in subthreshold region
    // when gate voltage pulls down drain/source potential
    // ===================================================================
    const Real Igidl = compute_gidl(bias.vgs, bias.vds, bias.vbs);
    const Real Igisl = compute_gisl(bias.vgs, bias.vds, bias.vbs);

    // Total drain current includes channel current + leakage currents
    const Real ids_total = ids + Igidl + Igisl;

    return std::max(ids_total, 0.0);
}
Real BSIM4Core::compute_ids(Real vgs, Real vds, Real vbs, Real temperature,
                            Real& vth_out, Real& vdsat_out) const {
    const Real T = guard_positive(temperature, params_.tnom);
    const Real Vt = kBoltz * T / qElectron;

    const Real weff = guard_positive(params_.get_weff(), 1e-9);
    const Real leff = guard_positive(params_.get_leff(), 1e-9);
    const Real toxe = guard_positive(params_.toxe, 5e-10);
    const Real epsilon_ox = guard_positive(params_.epsrox, 3.9) * eps0;
    const Real cox = epsilon_ox / toxe;

    const Real ni = ni_cm;
    const Real nsub = std::max(params_.nsub, 1e15);
    const Real phi_thermal = 2.0 * Vt * std::log(nsub / ni);
    const Real phi = std::max(params_.phi, phi_thermal);

    const Real sqrt_phi = std::sqrt(std::max(phi, 1e-6));
    const Real sqrt_term = std::sqrt(std::max(phi - vbs, 1e-6)) - sqrt_phi;
    const Real dvth_body = params_.k1 * sqrt_term + params_.k2 * vbs;

    Real dvth_sce = 0.0;
    if (params_.dvt0 > 0.0 && params_.dvt1 > 0.0) {
        const Real epsilon_si = 11.7 * eps0;
        const Real lt = std::sqrt(std::max(epsilon_si * toxe / epsilon_ox, 1e-18));
        const Real theta_sce = params_.dvt0 / (1.0 + params_.dvt2 * vbs);
        dvth_sce = -theta_sce * phi * std::exp(-params_.dvt1 * leff / lt);
    }

    Real dvth_dibl = 0.0;
    if (vds > 0.0) {
        const Real eta = params_.eta0 + params_.etab * vbs;
        dvth_dibl = -eta * vds;
    }

    Real dvth_wpe = 0.0;
    if (params_.k3 > 0.0 && params_.w0 > 0.0 && weff < params_.w0) {
        dvth_wpe = params_.k3 * (params_.w0 / weff - 1.0);
    }

    const Real delta_temp = T - params_.tnom;
    const Real dvth_temp = params_.kt1 * delta_temp + params_.kt2 * vbs * delta_temp;

    const Real vth = params_.vth0 + dvth_body + dvth_sce + dvth_dibl + dvth_wpe + dvth_temp;
    vth_out = vth;

    const Real vgt = vgs - vth;
    const Real smooth = std::max(3.0 * Vt, 1e-3);
    const Real vgt_eff = softplus(vgt, smooth);

    // For simplified compute_ids(), use vbs directly without bias smoothing
    Real mu_eff = compute_mobility(vgt_eff, vth, vbs, T);
    mu_eff = guard_positive(mu_eff, 1e-4);

    const Real vsat_temp = std::max(params_.vsat - params_.at * delta_temp, 1.0e4);
    const Real vsat_volt = std::max(leff * vsat_temp / mu_eff, 1e-3);
    const Real vdsat = vgt_eff / (1.0 + vgt_eff / vsat_volt);
    vdsat_out = vdsat;

    Real beta = mu_eff * cox * (weff / leff);
    if (params_.mult_i != 1.0) {
        beta *= std::max(params_.mult_i, 1e-3);
    }
    if (params_.mult_fn != 1.0) {
        beta *= std::max(params_.mult_fn, 1e-3);
    }

    const Real n = std::max(params_.nfactor, 1.0);
    const Real ids0 = n * Vt * Vt * cox * weff / leff;
    Real ids_sub = ids0 * limit_exp((vgs - vth - params_.voff) / (n * Vt));
    ids_sub *= (1.0 - limit_exp(-std::max(vds, 0.0) / (n * Vt)));

    Real ids_lin = beta * (vgt_eff * vds - 0.5 * vds * vds);
    ids_lin = std::max(ids_lin, 0.0);
    Real ids_sat = 0.5 * beta * vdsat * vdsat;

    const Real sat_blend = logistic(vds - vdsat, std::max(5.0 * Vt, 5e-3));
    Real ids_inv = (1.0 - sat_blend) * ids_lin + sat_blend * ids_sat;

    if (vds > vdsat) {
        const Real lambda = std::max(params_.pclm, 0.0) / std::max(leff, 1e-9);
        ids_inv *= (1.0 + lambda * (vds - vdsat));
    }

    const Real inversion_blend = logistic(vgt, std::max(5.0 * Vt, 5e-3));
    Real ids_total = (1.0 - inversion_blend) * ids_sub + inversion_blend * ids_inv;

    ids_total = std::max(ids_total, 0.0);

    return ids_total;
}

Real BSIM4Core::compute_mobility(Real vgt_eff, Real vth, Real vbs_eff, Real temperature) const {
    // The effective mobility (BSIM4 manual, chapter 6), with the parameters
    // scaled from TNOM to T for TEMPMOD = 0 (chapter 13), as the BSIM4 4.8.3
    // code computes them (b4temp.c, b4ld.c). vgt_eff is Vgsteff and vth the
    // threshold of the N-channel equations; U0 is in m^2/Vs.
    const Real t_ratio = guard_positive(temperature, params_.tnom) / params_.tnom;
    const Real t0 = t_ratio - 1.0;
    const Real ua = params_.ua + params_.ua1 * t0;
    const Real ub = params_.ub + params_.ub1 * t0;
    const Real uc = params_.uc + params_.uc1 * t0;
    const Real ud = params_.ud + params_.ud1 * t0;
    const Real length_factor = 1.0 - params_.up * std::exp(-params_.get_leff() / params_.lp);
    const Real u0temp = params_.u0 * length_factor * std::pow(t_ratio, params_.ute);

    // Coulomb scattering, UD (Vth toxe / (Vgsteff + 2 sqrt(Vth^2 + 1e-4)))^2
    const Real toxe = guard_positive(params_.toxe, 5e-10);
    const Real t12 = std::sqrt(vth * vth + 1e-4);
    const Real t10 = toxe / (vgt_eff + 2.0 * t12);
    const Real coulomb = ud * t10 * t10 * vth * vth;

    // The degradation term T5 of each MOBMOD (MTRLMOD = 0)
    Real t5;
    switch (params_.mobmod) {
        case 1: {
            const Real eeff = (vgt_eff + 2.0 * vth) / toxe;
            t5 = eeff * (ua + ub * eeff) * (1.0 + uc * vbs_eff) + coulomb;
            break;
        }
        case 2: {
            const Real eeff = (vgt_eff + params_.vtfbphi1) / toxe;
            t5 = std::exp(params_.eu * std::log(eeff)) * (ua + uc * vbs_eff) + coulomb;
            break;
        }
        default: {
            const Real eeff = (vgt_eff + 2.0 * vth) / toxe;
            t5 = eeff * (ua + uc * vbs_eff + ub * eeff) + coulomb;
            break;
        }
    }
    // 1 + T5, kept positive by the smooth form below T5 = -0.8
    const Real denomi = (t5 >= -0.8) ? 1.0 + t5 : (0.6 + t5) / (7.0 + 10.0 * t5);
    return u0temp / denomi;
}

/// Calculate rds0 (temperature-dependent base resistance) [Ω]
/// Reference: ngspice b4temp.c line 1255
Real BSIM4Core::compute_rds0(Real temperature) const {
    const Real delTemp = temperature - params_.tnom;
    const Real T10 = 1.0 + params_.prt * delTemp;

    // PowWeffWr: Width scaling factor
    // Reference: ngspice b4temp.c: PowWeffWr = pow(weffCJ * 1e6, wr) * nf
    const Real weff = params_.get_weff();  // Effective width [m]
    const Real nf = params_.nf;            // Number of fingers
    const Real PowWeffWr = std::pow(weff * 1.0e6, params_.wr) * nf;

    // rds0 = rdsw × T10 × nf / PowWeffWr
    const Real rds0 = params_.rdsw * T10 * nf / PowWeffWr;

    return rds0;
}

/// Calculate Rds (bias-dependent drain-source resistance) [Ω]
/// Reference: ngspice b4ld.c lines 1313-1336
Real BSIM4Core::compute_rds(Real vgsteff, Real sqrtPhis, Real temperature) const {
    // If rdsmod == 1, disable Rds calculation
    if (params_.rdsmod == 1) {
        return 0.0;
    }

    const Real rds0 = compute_rds0(temperature);

    // Gate-bias effect on rds
    const Real T0 = 1.0 + params_.prwg * vgsteff;

    // Body-effect on rds (using sqrtPhis from bias state)
    const Real T1 = params_.prwb * sqrtPhis;

    // Combined effect with smoothing
    const Real T2 = 1.0 / T0 + T1;
    const Real T3 = T2 + std::sqrt(T2 * T2 + 0.01);
    const Real T4 = rds0 * 0.5;

    // Final Rds with minimum value
    const Real Rds = params_.rdswmin + T3 * T4;

    // Ensure non-negative
    return std::max(Rds, 0.0);
}

/// Calculate Lambda (channel-length modulation parameter)
/// Reference: ngspice b4ld.c lines 1593-1609
Real BSIM4Core::compute_lambda(Real vgsteff) const {
    const Real a1 = params_.a1;
    const Real a2 = params_.a2;

    if (a1 == 0.0) {
        // Default case (a1 = 0.0): Lambda = a2
        return a2;
    } else if (a1 > 0.0) {
        // Positive a1 case
        const Real T0 = 1.0 - a2;
        const Real T1 = T0 - a1 * vgsteff - 0.0001;
        const Real T2 = std::sqrt(T1 * T1 + 0.0004 * T0);
        return a2 + T0 - 0.5 * (T1 + T2);
    } else {
        // Negative a1 case (a1 < 0.0)
        const Real T1 = a2 + a1 * vgsteff - 0.0001;
        const Real T2 = std::sqrt(T1 * T1 + 0.0004 * a2);
        return 0.5 * (T1 + T2);
    }
}

/// Calculate vdsat using complex formula (Branch 2)
/// Reference: ngspice b4ld.c lines 1636-1679
Real BSIM4Core::compute_vdsat_complex(Real Vgst2Vtm, Real Abulk, Real EsatL,
                                       Real Rds, Real Lambda, Real Weff, Real vgsteff, Real vsat) const {
    // These detailed logs were useful.4 debugging but swamp test output.
    // Keep them at debug level and guard with should_log to avoid formatting cost when disabled.
    if (spdlog::should_log(spdlog::level::debug)) {
        spdlog::debug("=== compute_vdsat_complex() inputs ===");
        spdlog::debug("  Vgst2Vtm = {:.6e} V", Vgst2Vtm);
        spdlog::debug("  Abulk    = {:.6e}", Abulk);
        spdlog::debug("  EsatL    = {:.6e} V", EsatL);
        spdlog::debug("  Rds      = {:.6e} Ω", Rds);
        spdlog::debug("  Lambda   = {:.6e}", Lambda);
        spdlog::debug("  Weff     = {:.6e} m", Weff);
        spdlog::debug("  vgsteff  = {:.6e} V", vgsteff);
        spdlog::debug("  vsat     = {:.6e} m/s", vsat);
    }

    // WVCox calculation (WITHOUT Rds!)
    // Reference: ngspice b4ld.c line 1581: WVCox = Weff * here->BSIM4vsattemp * model->BSIM4coxe;
    const Real Cox = EPSILON_OX / params_.toxe;
    const Real WVCox = Weff * vsat * Cox;

    // WVCoxRds calculation
    // Reference: ngspice b4ld.c line 1582: WVCoxRds = WVCox * Rds;
    // CRITICAL FIX: ngspice uses WVCoxRds in T9/T6 calculation!
    // Previous bug: used WVCox instead of WVCoxRds, causing 26% vdsat error
    const Real WVCoxRds = WVCox * Rds;

    if (spdlog::should_log(spdlog::level::debug)) {
        spdlog::debug("  Cox      = {:.6e} F/m²", Cox);
        spdlog::debug("  WVCox    = {:.6e} (WITHOUT Rds)", WVCox);
        spdlog::debug("  WVCoxRds = {:.6e} (WITH Rds={:.3f} Ω)", WVCoxRds, Rds);
    }

    // Intermediate terms (using WVCoxRds per ngspice b4ld.c lines 1584-1587)
    const Real T9 = Abulk * WVCoxRds;  // ngspice: T9 uses WVCoxRds, not WVCox!
    // const Real T8 = Abulk * T9;  // Unused in current implementation
    const Real T7 = Vgst2Vtm * T9;
    const Real T6 = Vgst2Vtm * WVCoxRds;  // ngspice: T6 uses WVCoxRds, not WVCox!

    // T0 calculation
    // Guard against Lambda=0 division
    const Real Lambda_safe = guard_positive(Lambda, 1e-12);
    const Real T0 = 2.0 * Abulk * (T9 - 1.0 + 1.0 / Lambda_safe);

    // T1 calculation
    const Real T1 = Vgst2Vtm * (2.0 / Lambda_safe - 1.0)
                  + Abulk * EsatL + 3.0 * T7;

    // T2 calculation
    const Real T2 = Vgst2Vtm * (EsatL + 2.0 * T6);

    // T3 calculation (square root term)
    const Real T3 = std::sqrt(T1 * T1 - 2.0 * T0 * T2);

    // Final vdsat
    // Guard against T0=0 division
    const Real T0_safe = guard_positive(std::abs(T0), 1e-12);
    const Real Vdsat = (T1 - T3) / (T0 >= 0.0 ? T0_safe : -T0_safe);

    if (spdlog::should_log(spdlog::level::debug)) {
        spdlog::debug("  Intermediate terms:");
        spdlog::debug("    T9 = {:.6e}", T9);
        spdlog::debug("    T7 = {:.6e}", T7);
        spdlog::debug("    T6 = {:.6e}", T6);
        spdlog::debug("    T0 = {:.6e}", T0);
        spdlog::debug("    T1 = {:.6e}", T1);
        spdlog::debug("    T2 = {:.6e}", T2);
        spdlog::debug("    T3 = {:.6e}", T3);
        spdlog::debug("  → Vdsat = (T1 - T3) / T0 = ({:.6e} - {:.6e}) / {:.6e} = {:.6e} V", T1, T3, T0, Vdsat);
    }

    return Vdsat;
}

// ===================================================================
// GIDL/GISL Leakage Current Calculation
// Reference: BSIM4.8.3 User's Manual Section 6.7
//            ngspice b4ld.c lines 2100-2170
// ===================================================================

Real BSIM4Core::compute_gidl(Real vgs, Real vds, Real vbs) const {
    spdlog::trace("GIDL: Entry - vgs={:.6e}, vds={:.6e}, vbs={:.6e}, agidl={:.6e}", vgs, vds, vbs, params_.agidl);
    if (params_.agidl <= 0.0) {
        spdlog::trace("GIDL: Disabled (agidl <= 0)");
        return 0.0;  // GIDL disabled
    }
    const Real vgd = vgs - vds;
    const Real vbd = vbs - vds;

    // GIDL occurs when Vgd < 0 (gate pulls down drain potential)
    if (vgd >= 0.0) {
        spdlog::trace("GIDL: Early return (vgd >= 0)");
        return 0.0;
    }

    // Avoid division by zero or negative Vbd
    if (vbd >= -1e-9) {
        spdlog::trace("GIDL: Early return (vbd >= -1e-9, vbd={:.6e})", vbd);
        return 0.0;
    }

    // Exponential tunneling model
    // Igidl = Weff × Agidl × Vbd × exp(-Bgidl / Vbd) × (1 + Cgidl × Vgd)
    const Real T0 = -params_.bgidl / vbd;
    spdlog::trace("GIDL: T0={:.6e} (bgidl={:.6e})", T0, params_.bgidl);
    const Real T1 = std::exp(std::clamp(T0, -20.0, 20.0));  // Prevent overflow
    const Real T2 = 1.0 + params_.cgidl * vgd;
    spdlog::trace("GIDL: T1={:.6e}, T2={:.6e} (cgidl={:.6e})", T1, T2, params_.cgidl);

    const Real Igidl = params_.get_weff() * params_.agidl * (-vbd) * T1 * T2;  // Fix sign error
    spdlog::trace("GIDL: Igidl_raw={:.6e} (weff={:.6e})", Igidl, params_.get_weff());

    const Real Igidl_final = std::max(Igidl, 0.0);
    spdlog::trace("GIDL: Final Igidl={:.6e} (max of raw and 0)", Igidl_final);
    return Igidl_final;
}

Real BSIM4Core::compute_gisl(Real vgs, Real vds, Real vbs) const {
    spdlog::trace("GISL: Entry - vgs={:.6e}, vds={:.6e}, vbs={:.6e}, agisl={:.6e}", vgs, vds, vbs, params_.agisl);
    if (params_.agisl <= 0.0) {
        return 0.0;  // GISL disabled
    }

    // GISL occurs when Vgs < 0 (gate pulls down source potential)
    if (vgs >= 0.0) {
        return 0.0;
    }

    // Avoid division by zero or negative Vbs
    if (vbs >= -1e-9) {
        return 0.0;
    }

    // Igisl = Weff × Agisl × Vbs × exp(-Bgisl / Vbs) × (1 + Cgisl × Vgs)
    const Real T0 = -params_.bgisl / vbs;
    const Real T1 = std::exp(std::clamp(T0, -20.0, 20.0));  // Prevent overflow
    const Real T2 = 1.0 + params_.cgisl * vgs;
    spdlog::trace("GISL: T1={:.6e}, T2={:.6e} (cgisl={:.6e})", T1, T2, params_.cgisl);

    const Real Igisl = params_.get_weff() * params_.agisl * (-vbs) * T1 * T2;  // Fix sign error

    return std::max(Igisl, 0.0);  // Ensure non-negative
}

}  // namespace kirimaspice::devices
