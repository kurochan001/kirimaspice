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

#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include <mutex>
#include <utility>

namespace kirimaspice::devices {

struct BSIM4OperatingPoint {
    Real ids = 0.0;    ///< Drain current (A), positive from drain to source
    Real gm = 0.0;     ///< dIds/dVgs (S)
    Real gds = 0.0;    ///< dIds/dVds (S)
    Real gmb = 0.0;    ///< dIds/dVbs (S)
    Real vth = 0.0;    ///< Effective threshold voltage (V)
    Real vdsat = 0.0;  ///< Velocity-saturation drain voltage (V)

    // Conductance-First architecture (Option 3)
    Real ids_conductance = 0.0;  ///< Ids before Vdseff multiplication [A/V] (ngspice line 2118)
    Real vdseff = 0.0;           ///< Effective Vds used in final multiplication [V]
};

struct BSIM4Capacitances {
    Real cgs = 0.0;
    Real cgd = 0.0;
    Real cgb = 0.0;
    Real cbd = 0.0;
    Real cbs = 0.0;
};

struct BSIM4BiasState {
    Real vgs = 0.0;
    Real vgd = 0.0;
    Real vds = 0.0;
    Real vbs = 0.0;
    Real vdb = 0.0;
    Real vgs_eff = 0.0;
    Real dVgs_eff_dVg = 1.0;
    Real vbs_eff = 0.0;
    Real dVbs_eff_dVb = 1.0;
    Real phis = 0.0;
    Real sqrt_phis = 0.0;
    Real dPhis_dVb = 0.0;
    Real dsqrtPhis_dVb = 0.0;
    bool reverse_mode = false;  ///< True if Vds < 0 (drain-source swap applied)
};

struct BSIM4ThresholdState {
    Real vth = 0.0;
    Real dVth_dVb = 0.0;
    Real dVth_dVd = 0.0;
    Real abulk = 1.0;
    Real dAbulk_dVb = 0.0;  ///< Body derivative of Abulk for gmb calculation
    // Store Abulk0 and dAbulk_dVg for deferred Abulk calculation
    Real abulk0 = 1.0;       ///< Base Abulk (at Vgsteff=0)
    Real dAbulk_dVg = 0.0;   ///< AGS sensitivity: dAbulk/dVgsteff
    Real keta_factor = 1.0;  ///< 1 / (1 + KETA Vbseff), the body-bias factor of Abulk
    Real n = 1.0;
    Real dn_dVd = 0.0;  ///< VDS derivative of n factor (CDSCD*Vds dependence)
    Real dn_dVb = 0.0;  ///< VBS derivative of n factor (CDSCB dependence)
    Real ueff = 0.0;
    Real vsat = 0.0;
    Real vdsat = 0.0;
    Real temperature = 300.15;  ///< Operating temperature [K]
    Real vadibl = 1e6;  ///< DIBL voltage [V]
    Real vadits = 1e6;  ///< DITS voltage [V]
    Real vasat = 0.0;   ///< Saturation voltage [V]
    Real cclm = 1e6;    ///< CLM coefficient [dimensionless]
    Real vascbe = 1e6;  ///< SCBE voltage [V]
    Real pvag_term = 1.0;  ///< PVAG model term [dimensionless]
};

/// BSIM4's short-channel factor of a length ratio x, about 1 / (2 (cosh x - 1)),
/// and its derivative with respect to x. The BSIM4 4.8.3 code computes it as
/// e^x / ((e^x - 1)^2 + 2 e^x MIN_EXP), and from x = EXP_THRESHOLD = 34 on
/// takes the constant 1 / (MAX_EXP - 2) with zero derivative, where the factor
/// is about e^-x. Theta0 of Vth takes it of DVT1 Leff / lt1, theta0vb0 of the
/// DIBL shift of DSUB Leff / lt0, and thetaRout of the output resistance of
/// DROUT Leff / lt0, with lt0 = factor1 sqrt(Xdep0).
struct BSIM4ShortChannelFactor {
    Real value = 0.0;
    Real derivative = 0.0;  ///< d value / d x
};
BSIM4ShortChannelFactor bsim4_short_channel_factor(Real x);

/// Lightweight evaluator for BSIM4 DC operating points.
/// This is an intermediate step toward the full BSIM4.8.3 implementation.
///
/// theta0vb0 is now passed as an argument (instance-specific)
class BSIM4Core {
public:
    BSIM4Core(const BSIM4Params& params, bool p_channel);

    /// Evaluate BSIM4 operating point
    /// @param vg Gate voltage [V]
    /// @param vd Drain voltage [V]
    /// @param vs Source voltage [V]
    /// @param vb Bulk voltage [V]
    /// @param temperature Operating temperature [K]
    /// @param theta0vb0 Instance-specific DIBL coefficient at Vbs=0
    /// @return Operating point (ids, gm, gds, gmb, vth, vdsat)
    BSIM4OperatingPoint evaluate(Real vg, Real vd, Real vs, Real vb,
                                   Real temperature, Real theta0vb0) const;

    /// Compute small-signal capacitances (Meyer + junction) for the current bias.
    /// Uses the same polarity handling as evaluate(): vgs/vds are internally
    /// transformed to positive values for P-channel. vth is the threshold as
    /// evaluate() returns it, with the device's sign; the junction voltages
    /// are positive in each junction's forward direction.
    BSIM4Capacitances compute_capacitances(Real vg, Real vd, Real vs, Real vb,
                                           Real vth, Real temperature) const;

private:
    BSIM4BiasState compute_bias(Real vg, Real vd, Real vs, Real vb, Real temperature) const;
    BSIM4ThresholdState compute_threshold(const BSIM4BiasState& bias,
                                           Real temperature, Real theta0vb0,
                                           Real sqrtPhi_nom, Real vbi_nom, Real phi_nom) const;
    Real compute_cdrain(const BSIM4BiasState& bias, const BSIM4ThresholdState& th,
                        Real& gm, Real& gds, Real& gmb) const;
    Real compute_ids(Real vgs, Real vds, Real vbs, Real temperature,
                     Real& vth_out, Real& vdsat_out) const;
    Real compute_mobility(Real vgt_eff, Real vth, Real vbs_eff, Real temperature) const;

    /// rds0 and rdswmin of the internal Rds(V) at the temperature [Ω]
    /// (TEMPMOD = 0, BSIM4 4.8.3 b4temp.c)
    std::pair<Real, Real> compute_rds0(Real temperature) const;

    /// Calculate Rds (bias-dependent drain-source resistance) [Ω]
    /// Reference: ngspice b4ld.c lines 1313-1336
    /// @param vgsteff Effective gate overdrive voltage [V]
    /// @param sqrtPhis Square root of surface potential [V^0.5]
    /// @param temperature Operating temperature [K]
    Real compute_rds(Real vgsteff, Real sqrtPhis, Real temperature) const;

    /// Calculate Lambda (channel-length modulation parameter)
    /// Reference: ngspice b4ld.c lines 1593-1609
    /// @param vgsteff Effective gate overdrive voltage [V]
    /// @return Lambda coefficient (dimensionless)
    Real compute_lambda(Real vgsteff) const;

    /// Calculate vdsat using complex formula (Branch 2)
    /// Reference: ngspice b4ld.c lines 1636-1679
    /// @param Vgst2Vtm Vgsteff + 2.0*Vtm [V]
    /// @param Abulk Bulk charge effect coefficient
    /// @param EsatL Esat × Leff [V]
    /// @param Rds Drain-source resistance [Ω]
    /// @param Lambda Channel-length modulation parameter
    /// @param Weff Effective channel width [m]
    /// @param vgsteff Effective gate overdrive voltage [V]
    /// @param vsat Saturation velocity [m/s] (vsattemp in ngspice)
    /// @return Vdsat (velocity saturation drain voltage) [V]
    Real compute_vdsat_complex(Real Vgst2Vtm, Real Abulk, Real EsatL,
                                Real Rds, Real Lambda, Real Weff, Real vgsteff, Real vsat) const;

    /// Calculate smoothed Vgsteff for unified gche formula
    /// Reference: ngspice b4ld.c lines 1236-1292
    /// This smoothing function enables the unified gche formula to work in ALL regions,
    /// including subthreshold, by converting negative Vgst to small positive Vgsteff.
    /// @param bias Bias state structure
    /// @param th Threshold state structure
    /// @param dn_dVd Derivative of n w.r.t. Vds [V⁻¹]
    /// @param dn_dVb Derivative of n w.r.t. Vbs [V⁻¹]
    /// @param dVth_dVd Derivative of Vth w.r.t. Vds [V/V]
    /// @param dVth_dVb Derivative of Vth w.r.t. Vbs [V/V]
    /// @param dVgs_eff_dVg Derivative of Vgs_eff w.r.t. Vgs [V/V]
    /// @param dVgsteff_dVg Output: Derivative of Vgsteff w.r.t. Vgs [V/V]
    /// @param dVgsteff_dVd Output: Derivative of Vgsteff w.r.t. Vds [V/V]
    /// @param dVgsteff_dVb Output: Derivative of Vgsteff w.r.t. Vbs [V/V]
    /// @param temperature Operating temperature [K]
    /// @return Smoothed Vgsteff (always positive, even in subthreshold) [V]
    Real compute_vgsteff_smoothed(
        const BSIM4BiasState& bias,
        const BSIM4ThresholdState& th,
        Real dn_dVd, Real dn_dVb,
        Real dVth_dVd, Real dVth_dVb,
        Real dVgs_eff_dVg,
        Real& dVgsteff_dVg,
        Real& dVgsteff_dVd,
        Real& dVgsteff_dVb,
        Real temperature
    ) const;

    /// Calculate GIDL current (Gate-Induced Drain Leakage) [A]
    /// Reference: BSIM4.8.3 User's Manual Section 6.7, ngspice b4ld.c lines 2100-2130
    /// @param vgs Gate-source voltage [V]
    /// @param vds Drain-source voltage [V]
    /// @param vbs Bulk-source voltage [V]
    /// @return GIDL current [A] (always non-negative)
    Real compute_gidl(Real vgs, Real vds, Real vbs) const;

    /// Calculate GISL current (Gate-Induced Source Leakage) [A]
    /// Reference: BSIM4.8.3 User's Manual Section 6.7, ngspice b4ld.c lines 2140-2170
    /// @param vgs Gate-source voltage [V]
    /// @param vds Drain-source voltage [V]
    /// @param vbs Bulk-source voltage [V]
    /// @return GISL current [A] (always non-negative)
    Real compute_gisl(Real vgs, Real vds, Real vbs) const;

    /// Compute quantum-corrected Coxeff and transconductance parameter beta.
    /// Reference: ngspice b4ld.c lines 1810-1827.
    /// @param vgsteff Effective gate overdrive [V]
    /// @param ueff    Effective mobility [m²/V·s]
    /// @return pair {Coxeff [F/m²], beta [A/V²]}
    std::pair<Real, Real> compute_coxeff_and_beta(Real vgsteff, Real ueff) const;

    /// Smoothed effective drain voltage Vdseff and its Vds-derivative.
    /// Reference: ngspice b4ld.c lines 1682-1721.
    /// @param vdsat Saturation drain voltage [V]
    /// @param vds   Actual drain-source voltage [V]
    /// @param delta Smoothing parameter
    /// @return pair {Vdseff [V], dVdseff/dVds}
    std::pair<Real, Real> compute_vdseff(Real vdsat, Real vds, Real delta) const;

    /// Substrate current body-effect voltage VASCBE.
    /// Reference: ngspice b4ld.c lines 1992-2011. Must be called after Vdseff is known.
    /// @param vds    Actual drain-source voltage [V]
    /// @param Vdseff Effective drain voltage [V]
    /// @param leff   Effective channel length [m]
    /// @return VASCBE [V]
    Real compute_vascbe(Real vds, Real Vdseff, Real leff) const;

    mutable BSIM4Params params_;  // mutable to allow temperature updates in evaluate()
    Real polarity_;  ///< +1 for N-channel, -1 for P-channel

    /// Mutex to protect evaluate() from concurrent access
    /// The evaluate() method modifies mutable params_ during temperature-dependent
    /// calculations, which would cause data races if called from multiple threads.
    /// This mutex serializes access within a single device instance.
    /// Note: Each BSIM4 device has its own BSIM4Core, so inter-device parallelism
    /// is unaffected. This only prevents concurrent calls on the SAME device.
    mutable std::mutex evaluate_mutex_;
};

}  // namespace kirimaspice::devices
