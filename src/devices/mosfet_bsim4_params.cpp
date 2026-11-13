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

} // namespace kirimaspice::devices
