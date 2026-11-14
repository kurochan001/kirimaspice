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

// mosfet_bsim4_params.cpp
// BSIM4 parameter initialization and derived parameter calculation

#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include <cmath>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

// Physical constants
static constexpr Real kBoltz = 1.380649e-23;  // Boltzmann constant [J/K]
static constexpr Real qElectron = 1.602176634e-19;  // Electron charge [C]
static constexpr Real epsSi = 11.7 * 8.854187817e-12;  // Silicon permittivity [F/m]
static constexpr Real eps0 = 8.854187817e-12;  // Vacuum permittivity [F/m]
static constexpr Real ni = 1.45e10;  // Intrinsic carrier concentration at 300K [cm⁻³]

void BSIM4Params::compute_derived_params(Real temp) {
    const Real T = (temp > 0) ? temp : tnom;
    const Real Vt = kBoltz * T / qElectron;  // Thermal voltage at operating temperature
    const Real Vtm0 = kBoltz * tnom / qElectron;  // Thermal voltage at NOMINAL temperature (for phi/vbi)

    // ngspice compatibility: if toxm is not specified (<=0), use toxe
    // From ngspice b4ld.c: if (model->BSIM4toxm <= 0.0) model->BSIM4toxm = model->BSIM4toxe;
    if (toxm <= 0.0) {
        toxm = toxe;
        spdlog::info("BSIM4: toxm not specified, using toxm = toxe = {:.6e} m", toxm);
    }

    // ngspice compatibility: if toxp is not specified (<=0), use toxe
    // From ngspice b4temp.c: if (model->BSIM4toxp <= 0.0) model->BSIM4toxp = model->BSIM4toxe;
    // This is critical for Coxeff quantum correction (Tcen calculation uses toxp)
    if (toxp <= 0.0) {
        toxp = toxe;
        spdlog::info("BSIM4: toxp not specified, using toxp = toxe = {:.6e} m", toxp);
    }

    // DEBUG: Print input parameters
    spdlog::info("BSIM4Params::compute_derived_params:");
    spdlog::info("  ndep = {:.6e} cm⁻³", ndep);
    spdlog::info("  toxe = {:.6e} m", toxe);
    spdlog::info("  toxm = {:.6e} m", toxm);
    spdlog::info("  toxp = {:.6e} m", toxp);
    spdlog::info("  xj   = {:.6e} m", xj);
    spdlog::info("  T    = {:.3f} K", T);
    spdlog::info("  Vt   = {:.6e} V", Vt);

    // =========================================================================
    // Poly Depletion Correction (phin)
    // =========================================================================
    // BSIM4.8.3: phin is a MODEL PARAMETER (not calculated from ngate)
    // ngspice b4temp.c uses: pParam->BSIM4phin = model->BSIM4phin + size scalings
    // The default value is 0.0 (set in mosfet_bsim4_params.hpp)
    // NOTE: Previous incorrect implementation computed phin from ngate, causing
    // phi to be wrong (~0.2V instead of ~0.82V) which cascaded to incorrect
    // Xdep0, lt1, Theta0, and Delt_vth values.
    spdlog::info("BSIM4: phin = {:.6e} V (model parameter, default=0)", phin);

    // Calculate surface potential phi
    // BSIM4.8.3 formula (from ngspice b4temp.c line 1300-1301):
    // phi = Vtm0 * log(ndep / ni) + phin + 0.4
    // NOTE: ndep is stored in cm⁻³ (as parsed from NCH parameter)
    // NOTE: This differs from the textbook formula (2*Vt*ln(NA/ni)) due to BSIM4-specific extensions
    // Surface potential phi (include phin per ngspice)
    // phi = Vtm0 * log(ndep / ni) + phin + 0.4
    if (ndep > 0.0) {
        phi = Vtm0 * std::log(ndep / ni) + phin + 0.4;
        sqrtPhi = std::sqrt(std::max(phi, 0.01));  // Guard against negative/zero phi
        spdlog::info("  phi = Vtm0*ln(ndep/ni)+phin+0.4 = {:.6e} V (phin={:.3e})", phi, phin);
        spdlog::info("  sqrtPhi = {:.6e}", sqrtPhi);
    } else {
        spdlog::warn("BSIM4: ndep <= 0, using default phi = 0.7V");
        phi = 0.7;
        sqrtPhi = std::sqrt(phi);
    }

    // Calculate zero-bias depletion depth Xdep0
    // Xdep0 = sqrt(2 * epsSi / (q * ndep)) * sqrtPhi
    // NOTE: ndep must be converted from cm⁻³ to m⁻³ for this formula
    if (ndep > 0.0) {
        const Real ndep_m3 = ndep * 1e6;  // Convert from cm⁻³ to m⁻³
        const Real term1 = 2.0 * epsSi;
        const Real term2 = qElectron * ndep_m3;
        const Real ratio = term1 / term2;
        const Real sqrt_ratio = std::sqrt(ratio);
        Xdep0 = sqrt_ratio * sqrtPhi;
        spdlog::info("  Xdep0 calculation:");
        spdlog::info("    ndep_m3 = {:.6e} m⁻³", ndep_m3);
        spdlog::info("    2*epsSi = {:.6e}", term1);
        spdlog::info("    q*ndep_m3 = {:.6e}", term2);
        spdlog::info("    ratio = {:.6e}", ratio);
        spdlog::info("    sqrt(ratio) = {:.6e}", sqrt_ratio);
        spdlog::info("    Xdep0 = sqrt(ratio)*sqrtPhi = {:.6e} m", Xdep0);
    } else {
        Xdep0 = 1e-7;  // Default fallback
        spdlog::warn("BSIM4: Using default Xdep0 = 1e-7 m");
    }

    // factor1 uses model epsrox (supports high-k)
    if (toxe > 0.0) {
        const Real epsrox_model = epsrox;  // relative permittivity from params
        const Real eps_ratio = epsSi / (epsrox_model * eps0);
        factor1 = std::sqrt(std::max(eps_ratio, 1e-12) * toxe);
        spdlog::trace("BSIM4: factor1 = sqrt((epsSi/(epsrox*eps0))*toxe) = {:.6e}", factor1);
    } else {
        factor1 = 1.0;
        spdlog::warn("BSIM4: toxe <= 0, using default factor1 = 1.0");
    }

    // K1 and K2 (BSIM4 manual, section 2.2, eqs. 2.13-2.17, and appendix
    // A.18, Notes 1, 3 and 5): when the card gives neither, they follow from
    // the doping. gamma1 and gamma2 are the body-effect coefficients for NDEP
    // and NSUB, sqrt(2 q epsSi N) / Coxe (5.753e-12 sqrt(N[cm^-3]) / Coxe, as
    // in the BSIM4 4.8.3 code); VBX is the body bias at which the depletion
    // width is XT, phi - q NDEP XT^2 / (2 epsSi) (7.7348e-4 NDEP XT^2); VBM
    // and VBX are taken as negative. When the card gives only one of K1 and
    // K2, the other takes its default (K1 = 0.53, K2 = -0.0186), as in the
    // BSIM4 4.8.3 code.
    if (k1_given || k2_given) {
        if (!k1_given) {
            k1 = 0.53;
        }
        if (!k2_given) {
            k2 = -0.0186;
        }
    } else if (toxe > 0.0) {
        const Real coxe = epsrox * eps0 / toxe;
        Real vbx_eff = vbx_given ? vbx : phi - 7.7348e-4 * ndep * xt * xt;
        if (vbx_eff > 0.0) {
            vbx_eff = -vbx_eff;
        }
        const Real vbm_eff = (vbm > 0.0) ? -vbm : vbm;
        const Real g1 = gamma1_given ? gamma1 : 5.753e-12 * std::sqrt(ndep) / coxe;
        const Real g2 = gamma2_given ? gamma2 : 5.753e-12 * std::sqrt(nsub) / coxe;
        const Real t0 = g1 - g2;
        const Real t1 = std::sqrt(phi - vbx_eff) - sqrtPhi;
        const Real t2 = std::sqrt(phi * (phi - vbm_eff)) - phi;
        k2 = t0 * t1 / (2.0 * t2 + vbm_eff);
        k1 = g2 - 2.0 * k2 * std::sqrt(phi - vbm_eff);
    }

    // Calculate k1ox (oxide-thickness corrected body-effect coefficient)
    // From ngspice b4temp.c line 1516-1517:
    //   pParam->BSIM4k1ox = pParam->BSIM4k1 * toxe / model->BSIM4toxm;
    spdlog::info("BSIM4: k1ox calculation inputs:");
    spdlog::info("  k1   = {:.6e}", k1);
    spdlog::info("  toxe = {:.6e} m", toxe);
    spdlog::info("  toxm = {:.6e} m", toxm);
    if (toxm > 0.0) {
        k1ox = k1 * toxe / toxm;
        spdlog::info("BSIM4: k1ox = k1 * toxe / toxm = {:.6e} * {:.6e} / {:.6e} = {:.6e}",
                      k1, toxe, toxm, k1ox);
    } else {
        k1ox = k1;  // Fallback if toxm is zero
        spdlog::warn("BSIM4: toxm <= 0, using k1ox = k1 = {:.6e}", k1ox);
    }

    // Calculate k2ox (oxide-thickness corrected k2 coefficient)
    // From ngspice b4temp.c: similar to k1ox calculation
    //   pParam->BSIM4k2ox = pParam->BSIM4k2 * toxe / model->BSIM4toxm;
    spdlog::info("BSIM4: k2ox calculation inputs:");
    spdlog::info("  k2   = {:.6e}", k2);
    spdlog::info("  toxe = {:.6e} m", toxe);
    spdlog::info("  toxm = {:.6e} m", toxm);
    if (toxm > 0.0) {
        k2ox = k2 * toxe / toxm;
        spdlog::info("BSIM4: k2ox = k2 * toxe / toxm = {:.6e} * {:.6e} / {:.6e} = {:.6e}",
                      k2, toxe, toxm, k2ox);
    } else {
        k2ox = k2;  // Fallback if toxm is zero
        spdlog::warn("BSIM4: toxm <= 0, using k2ox = k2 = {:.6e}", k2ox);
    }

    // Calculate theta0vb0 (DIBL coefficient at Vbs=0)
    // From ngspice b4temp.c lines 1519-1529:
    //   tmp = sqrt(epssub / (epsrox * EPS0) * toxe * pParam->BSIM4Xdep0);
    //   T0 = pParam->BSIM4dsub * pParam->BSIM4leff / tmp;
    //   if (T0 < EXP_THRESHOLD) {
    //       T1 = exp(T0);
    //       T2 = T1 - 1.0;
    //       T3 = T2 * T2;
    //       T4 = T3 + 2.0 * T1 * MIN_EXP;
    //       pParam->BSIM4theta0vb0 = T1 / T4;
    //   }
    // ===================================================================
    // theta0vb0 moved to instance-specific calculation
    // ===================================================================
    // theta0vb0 is now calculated in MOSFET_BSIM4_N/P constructors
    // because it depends on the device's effective channel length (leff),
    // which varies per device instance.
    // Reference: ngspice b4temp.c, BSIM4temp() function
    //
    // OLD MODEL-LEVEL CALCULATION (REMOVED):
    // if (Xdep0 > 0.0 && factor1 > 0.0) {
    //     const Real tmp = factor1 * std::sqrt(Xdep0);
    //     const Real T0 = dsub * leff / tmp;
    //     theta0vb0 = exp(T0) / ((exp(T0) - 1)^2 + 2*exp(T0)*MIN_EXP);
    // }
    //
    // This was causing a critical bug where all devices shared the same
    // theta0vb0 value calculated with a hardcoded leff=45nm, resulting in
    // incorrect DIBL modeling and V(col) errors (~79.8% deviation from ngspice).
    // ===================================================================

    // Calculate vbi (built-in voltage)
    // From ngspice b4temp.c line 1316:
    //   pParam->BSIM4vbi = Vtm0 * log(pParam->BSIM4nsd * pParam->BSIM4ndep / (ni * ni));
    // Note: ndep is stored in cm⁻³, nsd defaults to 1e20 cm⁻³
    // CRITICAL FIX: Use Vtm0 (nominal temp) not Vt (operating temp)
    if (ndep > 0.0 && nsd > 0.0) {
        vbi = Vtm0 * std::log(nsd * ndep / (ni * ni));
        spdlog::info("BSIM4: vbi = Vtm0 * log(nsd * ndep / (ni * ni))");
        spdlog::info("  Vtm0 = {:.6e} V", Vtm0);
        spdlog::info("  nsd  = {:.6e} cm⁻³", nsd);
        spdlog::info("  ndep = {:.6e} cm⁻³", ndep);
        spdlog::info("  ni   = {:.6e} cm⁻³", ni);
        spdlog::info("  vbi  = {:.6e} V", vbi);
    } else {
        vbi = 0.7;  // Default value
        spdlog::warn("BSIM4: Cannot calculate vbi (ndep or nsd <= 0), using default vbi = {:.3f}V", vbi);
    }

    // Calculate other derived parameters (Cox) using model epsrox (high-k対応)
    if (toxe > 0.0) {
        const Real cox = epsrox * eps0 / toxe;  // Oxide capacitance per area [F/m²]
        spdlog::trace("BSIM4: Cox = epsrox*eps0/toxe = {:.6e} F/m² (epsrox={:.3f})", cox, epsrox);
    }

    // Quantum Correction Parameters (Coxeff support)

    // Step 1: Calculate flat-band voltage (vfb)
    // vfb = vth0 - phi - k1*sqrtPhi
    const Real vfb = vth0 - phi - k1 * sqrtPhi;

    // Step 2: Calculate vtfbphi2 (ENABLED)
    // Formula from ngspice b4temp.c lines 1786-1788:
    // T3 = type * vth0 - vfb - phi;
    // vtfbphi2 = 4.0 * T3;
    // if (vtfbphi2 < 0.0) vtfbphi2 = 0.0;
    const Real type = this->type;  // +1 for NMOS, -1 for PMOS
    const Real T3 = type * vth0 - vfb - phi;
    vtfbphi2 = 4.0 * T3;
    if (vtfbphi2 < 0.0) {
        vtfbphi2 = 0.0;
        spdlog::debug("BSIM4: vtfbphi2 clamped to 0 (T3={:.6e} was negative)", T3);
    }

    spdlog::trace("BSIM4 vtfbphi2 calculation:");
    spdlog::trace("  vfb  = {:.6e} V", vfb);
    spdlog::trace("  phi  = {:.6e} V", phi);
    spdlog::trace("  vth0 = {:.6e} V", vth0);
    spdlog::trace("  type = {:.1f}", type);
    spdlog::trace("  vtfbphi2 = 4.0 * ({:.1f} * {:.6e} - {:.6e} - {:.6e}) = {:.6e} V",
                  type, vth0, vfb, phi, vtfbphi2);

    // Step 3: Calculate substrate permittivity (epssub)
    // epssub = eps0 * epsrsub (epsrsub = 11.7 for Si by default)
    epssub = eps0 * epsrsub;

    // Step 4: Calculate physical oxide capacitance (coxp) based on toxp
    // coxp = epsrox * eps0 / toxp
    if (toxp > 0.0) {
        coxp = epsrox * eps0 / toxp;
    } else {
        // Fallback to toxe if toxp is not set
        coxp = epsrox * eps0 / toxe;
        spdlog::trace("BSIM4: toxp not set, using toxe for coxp calculation");
    }

    spdlog::trace("BSIM4 Quantum Correction Params:");
    spdlog::trace("  vfb      = {:.6e} V", vfb);
    spdlog::trace("  vtfbphi2 = {:.6e} V", vtfbphi2);
    spdlog::trace("  epssub   = {:.6e} F/m", epssub);
    spdlog::trace("  coxp     = {:.6e} F/m²", coxp);

    // theta0vb0 is now instance-specific, not model-level
    spdlog::debug("BSIM4 derived params: phi={:.3f}V, sqrtPhi={:.3f}, Xdep0={:.3e}m, factor1={:.3e}, k1ox={:.3f}, vbi={:.3f}V (theta0vb0 now per-instance)",
                  phi, sqrtPhi, Xdep0, factor1, k1ox, vbi);
}


BSIM4Params bsim4_params_from_model(const std::map<std::string, Real>& model_params,
                                    const std::string& model_name) {
    BSIM4Params bsim4_params;

    // Which of the parameters that change the computation of others were
    // given on the card (BSIM4 manual, appendix A.18: K1 and K2 from
    // GAMMA1, GAMMA2, VBX and VBM when neither K1 nor K2 is given)
    bsim4_params.k1_given = model_params.count("k1") > 0;
    bsim4_params.k2_given = model_params.count("k2") > 0;
    bsim4_params.gamma1_given = model_params.count("gamma1") > 0;
    bsim4_params.gamma2_given = model_params.count("gamma2") > 0;
    bsim4_params.vbx_given = model_params.count("vbx") > 0;

    for (const auto& [key, value] : model_params) {
        // Process Parameters
        if (key == "version") bsim4_params.version = value;
        else if (key == "tox") bsim4_params.toxe = value;  // TOX is alias for TOXE in BSIM4
        else if (key == "toxe") bsim4_params.toxe = value;
        else if (key == "toxp") bsim4_params.toxp = value;
        else if (key == "toxm") bsim4_params.toxm = value;
        else if (key == "dtox") bsim4_params.dtox = value;
        else if (key == "toxref") bsim4_params.toxref = value;
        else if (key == "ntox") bsim4_params.ntox = value;
        else if (key == "epsrox") bsim4_params.epsrox = value;
        else if (key == "eot") bsim4_params.eot = value;
        else if (key == "vddeot") bsim4_params.vddeot = value;
        else if (key == "tempeot") bsim4_params.tempeot = value;
        else if (key == "leffeot") bsim4_params.leffeot = value;
        else if (key == "weffeot") bsim4_params.weffeot = value;
        else if (key == "phieot") bsim4_params.phieot = value;
        else if (key == "ados") bsim4_params.ados = value;
        else if (key == "bdos") bsim4_params.bdos = value;
        else if (key == "cdsc") bsim4_params.cdsc = value;
        else if (key == "cdscb") bsim4_params.cdscb = value;
        else if (key == "cdscd") bsim4_params.cdscd = value;
        else if (key == "cit") bsim4_params.cit = value;
        else if (key == "nfactor") bsim4_params.nfactor = value;
        else if (key == "xj") bsim4_params.xj = value;
        else if (key == "vsat") bsim4_params.vsat = value;
        else if (key == "at") bsim4_params.at = value;
        else if (key == "a0") bsim4_params.a0 = value;
        else if (key == "ags") bsim4_params.ags = value;
        else if (key == "a1") bsim4_params.a1 = value;
        else if (key == "a2") bsim4_params.a2 = value;
        else if (key == "keta") bsim4_params.keta = value;
        else if (key == "nsub") bsim4_params.nsub = value;
        else if (key == "ndep") bsim4_params.ndep = value;
        else if (key == "nch") bsim4_params.ndep = value;  // NCH is alias for NDEP in BSIM4
        else if (key == "nsd") bsim4_params.nsd = value;
        else if (key == "ngate") bsim4_params.ngate = value;
        else if (key == "phi") bsim4_params.phi = value;
        else if (key == "phin") bsim4_params.phin = value;
        else if (key == "gamma1") bsim4_params.gamma1 = value;
        else if (key == "gamma2") bsim4_params.gamma2 = value;
        else if (key == "vbx") bsim4_params.vbx = value;
        else if (key == "vbm") bsim4_params.vbm = value;
        else if (key == "xt") bsim4_params.xt = value;
        // Threshold Voltage Parameters
        else if (key == "vth0") bsim4_params.vth0 = value;
        else if (key == "vto") bsim4_params.vth0 = value;  // Alias
        else if (key == "k1") bsim4_params.k1 = value;
        else if (key == "k2") bsim4_params.k2 = value;
        else if (key == "k3") bsim4_params.k3 = value;
        else if (key == "k3b") bsim4_params.k3b = value;
        else if (key == "w0") bsim4_params.w0 = value;
        else if (key == "dvt0") bsim4_params.dvt0 = value;
        else if (key == "dvt1") bsim4_params.dvt1 = value;
        else if (key == "dvt2") bsim4_params.dvt2 = value;
        else if (key == "dvt0w") bsim4_params.dvt0w = value;
        else if (key == "dvt1w") bsim4_params.dvt1w = value;
        else if (key == "dvt2w") bsim4_params.dvt2w = value;
        else if (key == "drout") bsim4_params.drout = value;
        else if (key == "dsub") bsim4_params.dsub = value;
        else if (key == "lpe0") bsim4_params.lpe0 = value;
        else if (key == "lpeb") bsim4_params.lpeb = value;
        else if (key == "kt1") bsim4_params.kt1 = value;
        else if (key == "kt1l") bsim4_params.kt1l = value;
        else if (key == "kt2") bsim4_params.kt2 = value;
        // Mobility Parameters
        else if (key == "u0") bsim4_params.u0 = value;
        else if (key == "ua") bsim4_params.ua = value;
        else if (key == "ub") bsim4_params.ub = value;
        else if (key == "uc") bsim4_params.uc = value;
        else if (key == "ud") bsim4_params.ud = value;
        else if (key == "up") bsim4_params.up = value;
        else if (key == "lp") bsim4_params.lp = value;
        else if (key == "eu") bsim4_params.eu = value;
        else if (key == "voff") bsim4_params.voff = value;
        else if (key == "delta") bsim4_params.delta = value;
        else if (key == "rdsw") bsim4_params.rdsw = value;
        else if (key == "rsw") bsim4_params.rsw = value;
        else if (key == "rdw") bsim4_params.rdw = value;
        else if (key == "prwg") bsim4_params.prwg = value;
        else if (key == "prwb") bsim4_params.prwb = value;
        else if (key == "prt") bsim4_params.prt = value;
        else if (key == "eta0") bsim4_params.eta0 = value;
        else if (key == "etab") bsim4_params.etab = value;
        // Channel Length Modulation & DIBL
        else if (key == "pclm") bsim4_params.pclm = value;
        else if (key == "pdiblc1") bsim4_params.pdiblc1 = value;
        else if (key == "pdiblc2") bsim4_params.pdiblc2 = value;
        else if (key == "pdiblcb") bsim4_params.pdiblcb = value;
        else if (key == "pscbe1") bsim4_params.pscbe1 = value;
        else if (key == "pscbe2") bsim4_params.pscbe2 = value;
        else if (key == "pvag") bsim4_params.pvag = value;
        else if (key == "wr") bsim4_params.wr = value;
        else if (key == "dwg") bsim4_params.dwg = value;
        else if (key == "dwb") bsim4_params.dwb = value;
        else if (key == "b0") bsim4_params.b0 = value;
        else if (key == "b1") bsim4_params.b1 = value;
        else if (key == "alpha0") bsim4_params.alpha0 = value;
        else if (key == "alpha1") bsim4_params.alpha1 = value;
        else if (key == "beta0") bsim4_params.beta0 = value;
        // Temperature Parameters
        else if (key == "tnom") bsim4_params.tnom = value + 273.15;  // Celsius to Kelvin
        else if (key == "kt1") bsim4_params.kt1 = value;
        else if (key == "kt1l") bsim4_params.kt1l = value;
        else if (key == "kt2") bsim4_params.kt2 = value;
        else if (key == "ute") bsim4_params.ute = value;
        else if (key == "ua1") bsim4_params.ua1 = value;
        else if (key == "ub1") bsim4_params.ub1 = value;
        else if (key == "uc1") bsim4_params.uc1 = value;
        // Gate Overlap Capacitance
        else if (key == "cgso") bsim4_params.cgso = value;
        else if (key == "cgdo") bsim4_params.cgdo = value;
        else if (key == "cgbo") bsim4_params.cgbo = value;
        // Geometry Scaling
        else if (key == "lint") bsim4_params.lint = value;
        else if (key == "wint") bsim4_params.wint = value;
        else if (key == "xl") bsim4_params.xl = value;
        else if (key == "xw") bsim4_params.xw = value;
        // Instance parameters (will be overridden by M-line parameters)
        else if (key == "w") bsim4_params.w = value;
        else if (key == "l") bsim4_params.l = value;
        else if (key == "nf") bsim4_params.nf = value;
        else if (key == "m") bsim4_params.m = value;
        else if (key == "mult_i") bsim4_params.mult_i = value;
        else if (key == "mult_q") bsim4_params.mult_q = value;
        else if (key == "mult_fn") bsim4_params.mult_fn = value;
        else if (key == "as") bsim4_params.as = value;
        else if (key == "ad") bsim4_params.ad = value;
        else if (key == "ps") bsim4_params.ps = value;
        else if (key == "pd") bsim4_params.pd = value;
        else if (key == "nrd") bsim4_params.nrd = static_cast<int>(value);
        else if (key == "nrs") bsim4_params.nrs = static_cast<int>(value);
        // Level parameter (already processed)
        else if (key == "level") {
            // Already handled, skip
        }
        // Model selection flags
        else if (key == "mobmod") bsim4_params.mobmod = static_cast<int>(value);
        else if (key == "capmod") bsim4_params.capmod = static_cast<int>(value);
        else if (key == "gidlmod") bsim4_params.gidlmod = static_cast<int>(value);
        else if (key == "gidlclamp") bsim4_params.gidlclamp = value;
        else if (key == "idovvds" || key == "idovvdsc") bsim4_params.idovvdsc = value;
        else if (key == "igbmod") bsim4_params.igbmod = static_cast<int>(value);
        else if (key == "igcmod") bsim4_params.igcmod = static_cast<int>(value);
        // Note: Additional BSIM4 parameters (200+ more) will be added.4+
        // For now, warn about unrecognized parameters
        else {
            spdlog::debug("BSIM4 parameter '{}' recognized but not yet implemented", key);
        }
    }

    // Override with instance parameters (W, L from tokens)

    const Real target_bsim4_version = 4.83;
    if (bsim4_params.version <= 0.0) {
        spdlog::warn("BSIM4 model {} declared non-positive version {:.3f}; defaulting to {:.2f}",
                     model_name, bsim4_params.version, target_bsim4_version);
        bsim4_params.version = target_bsim4_version;
    } else if (std::abs(bsim4_params.version - target_bsim4_version) > 1e-3) {
        spdlog::warn("BSIM4 model {} targets version {:.3f}; KirimaSpice currently follows {:.2f}. Results may differ.",
                     model_name, bsim4_params.version, target_bsim4_version);
    }

    if (bsim4_params.u0 > 1.0) {
        Real original_u0 = bsim4_params.u0;
        bsim4_params.u0 *= 1e-4;
        spdlog::warn("BSIM4 model {}: u0={} interpreted as cm^2/Vs. Converted to {:.3e} m^2/Vs (×1e-4).",
                     model_name, original_u0, bsim4_params.u0);
    }

    auto ensure_positive = [&](const char* param, Real min_value, Real& target) {
        if (target <= min_value) {
            spdlog::warn("BSIM4 model {}: {}={} is <= {}. Clamping to {}.",
                         model_name, param, target, min_value, min_value);
            target = min_value;
        }
    };
    ensure_positive("phi", 1e-3, bsim4_params.phi);
    ensure_positive("phieot", 1e-3, bsim4_params.phieot);
    ensure_positive("tempeot", 1.0, bsim4_params.tempeot);

    if (bsim4_params.gidlclamp >= 0.0) {
        spdlog::warn("BSIM4 model {}: gidlclamp={} must be negative. Resetting to -1e-5.",
                     model_name, bsim4_params.gidlclamp);
        bsim4_params.gidlclamp = -1e-5;
    }
    if (bsim4_params.idovvdsc <= 0.0) {
        spdlog::warn("BSIM4 model {}: idovvdsc={} must be positive. Resetting to 1e-9.",
                     model_name, bsim4_params.idovvdsc);
        bsim4_params.idovvdsc = 1e-9;
    }

    // Compute BSIM4 derived parameters before creating device

    return bsim4_params;
}

} // namespace kirimaspice::devices
