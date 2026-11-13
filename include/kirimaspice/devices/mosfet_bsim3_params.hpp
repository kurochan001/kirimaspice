// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mosfet_bsim3_params.hpp
// BSIM3v3.3 MOSFET Model Parameters
// Reference: BSIM3v3.3 User's Manual (UC Berkeley, 2005)
#pragma once

#include "kirimaspice/core/types.hpp"

namespace kirimaspice::devices {

/// BSIM3v3.3 model parameters
/// Default values from BSIM3v3.3 User's Manual Table 8.1
/// Total: ~100 parameters
struct BSIM3Params {
    //==========================================================================
    // Process Parameters (デバイスプロセス依存パラメータ)
    //==========================================================================

    Real tox = 1.5e-8;      ///< Gate oxide thickness [m] (150Å default)
    Real toxm = 1.5e-8;     ///< Gate oxide thickness for CV model [m]
    Real cdsc = 2.4e-4;     ///< Drain/Source to channel coupling capacitance [F/m²]
    Real cdscb = 0.0;       ///< Body-bias sensitivity of cdsc [F/(Vm²)]
    Real cdscd = 0.0;       ///< Drain-bias sensitivity of cdsc [F/(Vm²)]
    Real cit = 0.0;         ///< Interface trap capacitance [F/m²]
    Real nfactor = 1.0;     ///< Subthreshold swing factor
    Real xj = 1.5e-7;       ///< Junction depth [m]
    Real vsat = 8.0e4;      ///< Saturation velocity at tnom [m/s]
    Real at = 3.3e4;        ///< Temperature coefficient of vsat [m/s]
    Real a0 = 0.0;          ///< Non-uniform depletion width effect coefficient
    Real ags = 0.0;         ///< Gate bias coefficient of Abulk
    Real a1 = 0.0;          ///< Non-saturation effect coefficient [V⁻¹]
    Real a2 = 1.0;          ///< Non-saturation effect coefficient
    Real keta = -0.047;     ///< Body-bias coefficient of non-uniform depletion width
    Real nsub = 6.0e16;     ///< Substrate doping concentration [cm⁻³]
    Real nch = 1.7e17;      ///< Channel doping concentration [cm⁻³] (alias for nsub)
    Real ngate = 0.0;       ///< Poly-gate doping concentration [cm⁻³]
    Real gamma1 = 0.0;      ///< Body-effect coefficient near interface [V^0.5]
    Real gamma2 = 0.0;      ///< Body-effect coefficient in bulk [V^0.5]
    Real vbx = 0.0;         ///< Vbs at which gamma changes from gamma1 to gamma2 [V]
    Real vbm = -3.0;        ///< Maximum Vbs [V]

    //==========================================================================
    // Threshold Voltage Parameters (しきい値電圧パラメータ)
    //==========================================================================

    Real vth0 = 0.7;        ///< Threshold voltage at Vbs=0, long/wide channel [V]
    Real k1 = 0.5;          ///< First-order body-effect coefficient [V^0.5]
    Real k2 = 0.0;          ///< Second-order body-effect coefficient
    Real k3 = 0.0;          ///< Narrow width effect coefficient (set 0 by default to avoid huge Vth shifts)
    Real k3b = 0.0;         ///< Body-effect coefficient of k3
    Real w0 = 2.5e-6;       ///< Narrow width parameter [m]
    Real nlx = 1.74e-7;     ///< Lateral non-uniform doping parameter [m]
    Real dvt0 = 2.2;        ///< Short-channel effect coefficient 0
    Real dvt1 = 0.53;       ///< Short-channel effect coefficient 1
    Real dvt2 = -0.032;     ///< Short-channel effect coefficient 2 [V⁻¹]
    Real dvt0w = 0.0;       ///< Narrow width effect coefficient 0 [m⁻¹]
    Real dvt1w = 5.3e6;     ///< Narrow width effect coefficient 1 [m⁻¹]
    Real dvt2w = -0.032;    ///< Narrow width effect coefficient 2 [V⁻¹]
    Real drout = 0.56;      ///< DIBL coefficient of output resistance in linear region
    Real dsub = 0.56;       ///< DIBL coefficient in linear region (alias for drout)

    //==========================================================================
    // Mobility Parameters (移動度パラメータ)
    //==========================================================================

    Real u0 = 670.0;        ///< Low-field mobility at tnom [cm²/Vs]
    Real ua = 2.25e-9;      ///< First-order mobility degradation coefficient [m/V]
    Real ub = 5.87e-19;     ///< Second-order mobility degradation coefficient [(m/V)²]
    Real uc = -4.65e-11;    ///< Body-effect coefficient of mobility degradation [V⁻¹]
    Real voff = -0.08;      ///< Offset voltage in subthreshold region [V]
    Real delta = 0.01;      ///< Effective Vds parameter [V]
    Real rdsw = 0.0;        ///< Source/drain resistance per width [Ω·µm]
    Real prwg = 0.0;        ///< Gate-bias effect on rdsw [V⁻¹]
    Real prwb = 0.0;        ///< Body-effect on rdsw [V⁻⁰·⁵]
    Real prt = 0.0;         ///< Temperature coefficient of rdsw [Ω·µm]
    Real eta0 = 0.08;       ///< DIBL effect in subthreshold region
    Real etab = -0.07;      ///< Body-bias coefficient of DIBL effect [V⁻¹]

    //==========================================================================
    // Channel Length Modulation Parameters (チャネル長変調パラメータ)
    //==========================================================================

    Real pclm = 1.3;        ///< Channel length modulation parameter
    Real pdiblc1 = 0.39;    ///< First DIBL effect correction parameter
    Real pdiblc2 = 0.0086;  ///< Second DIBL effect correction parameter
    Real pdiblcb = 0.0;     ///< Body-effect coefficient of DIBL correction
    Real pscbe1 = 4.24e8;   ///< First substrate current body-effect parameter [V/m]
    Real pscbe2 = 1.0e-5;   ///< Second substrate current body-effect parameter [m/V]
    Real pvag = 0.0;        ///< Gate dependence of Early voltage
    Real wr = 1.0;          ///< Width dependence of rds
    Real dwg = 0.0;         ///< Gate bias coefficient of Weff [m/V]
    Real dwb = 0.0;         ///< Body bias coefficient of Weff [m/V^0.5]
    Real b0 = 0.0;          ///< Abulk narrow width parameter [m]
    Real b1 = 0.0;          ///< Abulk narrow width parameter [m]

    //==========================================================================
    // Impact Ionization Parameters (衝突イオン化パラメータ)
    //==========================================================================

    Real alpha0 = 0.0;      ///< First parameter of impact ionization current [m/V]
    Real alpha1 = 0.0;      ///< Second parameter of impact ionization current [V⁻¹]
    Real beta0 = 30.0;      ///< Substrate current parameter [V]

    //==========================================================================
    // Gate Overlap Capacitance Parameters (ゲート重なり容量パラメータ)
    //==========================================================================

    Real cgso = 0.0;        ///< Gate-source overlap capacitance per width [F/m]
    Real cgdo = 0.0;        ///< Gate-drain overlap capacitance per width [F/m]
    Real cgbo = 0.0;        ///< Gate-bulk overlap capacitance per length [F/m]
    Real xpart = 0.0;       ///< Channel charge partitioning (0=50/50, 1=0/100)

    //==========================================================================
    // Intrinsic Capacitance Parameters (内部容量パラメータ)
    //==========================================================================

    Real cf = 0.0;          ///< Fringing field capacitance [F/m]
    Real clc = 1.0e-7;      ///< Constant term for short-channel Leff CV model [m]
    Real cle = 0.6;         ///< Exponential term for short-channel Leff CV model
    Real dwc = 0.0;         ///< Width offset for C-V model [m]
    Real dlc = 0.0;         ///< Length offset for C-V model [m]
    Real vfbcv = -1.0;      ///< Flat-band voltage for C-V model [V]
    Real acde = 1.0;        ///< Exponential coefficient for finite charge thickness
    Real moin = 15.0;       ///< Coefficient for gate-bias-dependent surface potential
    Real noff = 1.0;        ///< C-V turn-on/off parameter
    Real voffcv = 0.0;      ///< C-V lateral shift parameter [V]

    //==========================================================================
    // Junction Diode Parameters (接合ダイオードパラメータ)
    //==========================================================================

    Real cj = 5.0e-4;       ///< Bottom junction capacitance per area [F/m²]
    Real mj = 0.5;          ///< Bottom junction grading coefficient
    Real pb = 1.0;          ///< Bottom junction built-in potential [V]
    Real cjsw = 5.0e-10;    ///< Sidewall junction capacitance per perimeter [F/m]
    Real mjsw = 0.33;       ///< Sidewall junction grading coefficient
    Real pbsw = 1.0;        ///< Sidewall junction built-in potential [V]
    Real cjswg = 0.0;       ///< Gate-edge sidewall junction capacitance [F/m]
    Real mjswg = 0.33;      ///< Gate-edge sidewall junction grading coefficient
    Real pbswg = 1.0;       ///< Gate-edge sidewall junction built-in potential [V]
    Real js = 1.0e-4;       ///< Bottom junction saturation current density [A/m²]
    Real jsw = 0.0;         ///< Sidewall junction saturation current density [A/m]
    Real ijth = 0.1;        ///< Diode limiting current [A]
    Real xti = 3.0;         ///< Junction current temperature exponent coefficient

    //==========================================================================
    // Temperature Parameters (温度依存性パラメータ)
    //==========================================================================

    Real tnom = 300.15;     ///< Parameter measurement temperature [K] (27°C)
    Real ute = -1.5;        ///< Mobility temperature exponent
    Real kt1 = -0.11;       ///< Temperature coefficient of Vth [V]
    Real kt1l = 0.0;        ///< Channel length sensitivity of kt1 [Vm]
    Real kt2 = 0.022;       ///< Body-bias coefficient of Vth temperature effect
    Real ua1 = 4.31e-9;     ///< Temperature coefficient of ua [m/V]
    Real ub1 = -7.61e-18;   ///< Temperature coefficient of ub [(m/V)²]
    Real uc1 = -0.056;      ///< Temperature coefficient of uc [V⁻¹]

    //==========================================================================
    // Noise Parameters (ノイズパラメータ)
    //==========================================================================

    Real kf = 0.0;          ///< Flicker noise coefficient
    Real af = 1.0;          ///< Flicker noise current exponent
    Real ef = 1.0;          ///< Flicker noise frequency exponent
    Real noia = 1.0e20;     ///< Noise parameter A [m⁻¹]
    Real noib = 5.0e4;      ///< Noise parameter B [m⁻¹]
    Real noic = -1.4e-12;   ///< Noise parameter C [V·m]
    Real em = 4.1e7;        ///< Saturation field [V/m]

    //==========================================================================
    // Geometry Scaling Parameters (幾何学的スケーリングパラメータ)
    //==========================================================================

    Real lint = 0.0;        ///< Length offset for I-V model [m]
    Real ll = 0.0;          ///< Length reduction parameter [m·m]
    Real llc = 0.0;         ///< Length reduction parameter for C-V [m·m]
    Real lln = 1.0;         ///< Length reduction exponent
    Real lw = 0.0;          ///< Length reduction parameter [m·m]
    Real lwc = 0.0;         ///< Length reduction parameter for C-V [m·m]
    Real lwn = 1.0;         ///< Length reduction exponent
    Real lwl = 0.0;         ///< Length reduction parameter [m²·m]
    Real lwlc = 0.0;        ///< Length reduction parameter for C-V [m²·m]
    Real wint = 0.0;        ///< Width offset for I-V model [m]
    Real wl = 0.0;          ///< Width reduction parameter [m·m]
    Real wlc = 0.0;         ///< Width reduction parameter for C-V [m·m]
    Real wln = 1.0;         ///< Width reduction exponent
    Real ww = 0.0;          ///< Width reduction parameter [m·m]
    Real wwc = 0.0;         ///< Width reduction parameter for C-V [m·m]
    Real wwn = 1.0;         ///< Width reduction exponent
    Real wwl = 0.0;         ///< Width reduction parameter [m²·m]
    Real wwlc = 0.0;        ///< Width reduction parameter for C-V [m²·m]

    //==========================================================================
    // Instance Parameters (インスタンスパラメータ、デバイスごとに設定)
    //==========================================================================

    Real w = 5.0e-6;        ///< Drawn channel width [m] (5µm default)
    Real l = 5.0e-6;        ///< Drawn channel length [m] (5µm default)
    Real as = 0.0;          ///< Source diffusion area [m²]
    Real ad = 0.0;          ///< Drain diffusion area [m²]
    Real ps = 0.0;          ///< Source diffusion perimeter [m]
    Real pd = 0.0;          ///< Drain diffusion perimeter [m]
    int nrd = 1;            ///< Number of drain diffusion squares
    int nrs = 1;            ///< Number of source diffusion squares
    Real off = 0.0;         ///< Device disable flag (1=disable, 0=enable)
    Real icvds = 0.0;       ///< Initial Vds for transient analysis [V]
    Real icvgs = 0.0;       ///< Initial Vgs for transient analysis [V]
    Real icvbs = 0.0;       ///< Initial Vbs for transient analysis [V]

    //==========================================================================
    // Binning Parameters (ビニング用パラメータ、先進的な用途)
    //==========================================================================
    // Note: Binning allows parameter variation with W/L
    // Format: parameter + "0" suffix (e.g., vth00, k10, u00)
    // Not implemented.1 (reserved for future)
    //==========================================================================

    /// Calculate effective channel length [m]
    Real get_leff() const {
        return l - 2.0 * lint;
    }

    /// Calculate effective channel width [m]
    Real get_weff() const {
        return w - 2.0 * wint;
    }

    /// Validate parameter ranges
    /// @return true if all parameters are valid
    bool validate() const {
        if (tox <= 0.0) return false;
        if (l <= 0.0) return false;
        if (w <= 0.0) return false;
        if (u0 <= 0.0) return false;
        if (vsat <= 0.0) return false;
        if (tnom <= 0.0) return false;
        return true;
    }
};

} // namespace kirimaspice::devices
