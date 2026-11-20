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

// include/kirimaspice/devices/mosfet_bsim4_params.hpp
// BSIM4.8.3 MOSFET Model Parameters
// Reference: BSIM4.8.3 User's Manual (UC Berkeley, 2025)
#pragma once

#include <algorithm>

#include "kirimaspice/core/types.hpp"

#include <map>
#include <string>

namespace kirimaspice::devices {

/// BSIM4.8.3 model parameters
/// Default values from BSIM4.8.3 User's Manual Table 7.1
/// Total: ~300 parameters
struct BSIM4Params {
    // Device polarity: +1 for NMOS, -1 for PMOS (ngspice model->type)
    Real type = 1.0;
    Real version = 4.83;    ///< Declared BSIM4 version (4.8.3 default)
    //==========================================================================
    // Process Parameters (デバイスプロセス依存パラメータ)
    //==========================================================================

    Real toxe = 3.0e-9;     ///< Electrical gate oxide thickness [m] (30Å default)
    Real toxp = 0.0;        ///< Physical gate oxide thickness [m] (0=use toxe, ngspice default)
    Real toxm = 0.0;        ///< Gate oxide thickness for C-V model [m] (0=use toxe, ngspice default)
    Real dtox = 0.0;        ///< Oxide thickness of S/D gate-edge extension [m]
    Real epsrox = 3.9;      ///< Dielectric constant of gate oxide relative to vacuum
    Real epsrsub = 11.7;    ///< Substrate relative permittivity (11.7 for Si)
    Real cdsc = 2.4e-4;     ///< Drain/Source to channel coupling capacitance [F/m²]
    Real cdscb = 0.0;       ///< Body-bias sensitivity of cdsc [F/(Vm²)]
    Real cdscd = 0.0;       ///< Drain-bias sensitivity of cdsc [F/(Vm²)]
    Real cit = 0.0;         ///< Interface trap capacitance [F/m²]
    Real nfactor = 1.0;     ///< Subthreshold swing factor
    Real xj = 1.5e-7;       ///< Junction depth [m]
    Real vsat = 8.0e4;      ///< Saturation velocity at tnom [m/s]
    Real at = 3.3e4;        ///< Temperature coefficient of vsat [m/s]
    Real a0 = 1.0;          ///< Non-uniform depletion width effect coefficient
    Real ags = 0.0;         ///< Gate bias coefficient of Abulk
    Real a1 = 0.0;          ///< Non-saturation effect coefficient [V⁻¹]
    Real a2 = 1.0;          ///< Non-saturation effect coefficient
    Real keta = -0.047;     ///< Body-bias coefficient of non-uniform depletion width
    Real nsub = 6.0e16;     ///< Substrate doping concentration [cm⁻³]
    Real ndep = 1.7e17;     ///< Channel doping concentration [cm⁻³]
    Real nsd = 1.0e20;      ///< S/D doping concentration [cm⁻³]
    Real ngate = 0.0;       ///< Poly-gate doping concentration [cm⁻³]
    Real phi = 0.7;         ///< Surface potential parameter [V]
    Real phin = 0.0;        ///< Poly-silicon depletion effect [V] (typically negative, BSIM4.8.3)
    Real gamma1 = 0.0;      ///< Body-effect coefficient near interface [V^0.5]
    Real gamma2 = 0.0;      ///< Body-effect coefficient in bulk [V^0.5]
    Real vbx = 0.0;         ///< Vbs at which gamma changes from gamma1 to gamma2 [V]
    Real vbm = -3.0;        ///< Maximum Vbs [V]
    // Whether these were given on the .MODEL card: when neither K1 nor K2
    // is, they are computed from GAMMA1, GAMMA2, VBX and VBM (BSIM4 manual,
    // section 2.2 and appendix A.18, Notes 1, 3 and 5). K1 and K2 count as
    // given by default, so that values set directly in code are used;
    // bsim4_params_from_model marks them not given when the card has neither.
    bool k1_given = true;
    bool k2_given = true;
    bool gamma1_given = false;
    bool gamma2_given = false;
    bool vbx_given = false;
    // U0 and EU default by TYPE (compute_derived_params); values set in code
    // count as given
    bool u0_given = true;
    bool eu_given = true;
    Real xt = 1.55e-7;      ///< Doping depth [m]
    Real eot = 0.0;         ///< Equivalent oxide thickness [m]
    Real vddeot = 0.0;      ///< Supply voltage for eot extraction [V]
    Real tempeot = 300.15;  ///< Temperature for eot extraction [K]
    Real leffeot = 0.0;     ///< Effective channel length for eot [m]
    Real weffeot = 0.0;     ///< Effective channel width for eot [m]
    Real ados = 1.0;        ///< Gate density of states coefficient A
        Real bdos = 1.0;        ///< Gate density of states coefficient B
    Real vbsc = 0.0;        ///< Body-source critical voltage (BSIM4 internally derived)
    Real Xdep0 = 1e-7;      ///< Zero-bias depletion depth [m]
    Real litl = 0.0;        ///< Characteristic length of CLM and SCBE [m] (derived)
    Real sqrtPhi = 1.0;     ///< Convenience value sqrt(phi)
    Real factor1 = 1.0;     ///< Derived helper for short-channel effects
    Real k1ox = 0.0;        ///< Oxide-thickness corrected body-effect coefficient (derived from k1)
    Real k2ox = 0.0;        ///< Oxide-thickness corrected k2 coefficient (derived from k2)
    // theta0vb0 moved to instance-specific (MOSFET_BSIM4_N/P::theta0vb0_)
    // Real theta0vb0 = 0.0;   ///< DIBL coefficient theta at Vbs=0 (derived) - NOW INSTANCE-SPECIFIC
    Real vbi = 0.7;         ///< Built-in voltage [V] (derived: Vtm0 * log(nsd * ndep / (ni * ni)))

    // Quantum Correction Parameters (量子補正パラメータ - Coxeff calculation)
    Real vtfbphi2 = 0.0;    ///< Vt*(Vfb+phi) for quantum correction [V] (derived)
    Real vtfbphi1 = 0.0;    ///< Field term of MOBMOD 2: 2 or 2.5 (TYPE VTH0 - VFB - phi) [V] (derived)
    Real epssub = 0.0;      ///< Substrate permittivity [F/m] (derived)
    Real coxp = 0.0;        ///< Physical oxide capacitance (toxp-based) [F/m²] (derived)

    //==========================================================================
    // Threshold Voltage Parameters (しきい値電圧パラメータ)
    //==========================================================================

    Real vth0 = 0.7;        ///< Threshold voltage at Vbs=0, long/wide channel [V]
    Real k1 = 0.0;          ///< First-order body-effect coefficient [V^0.5] (BSIM4 default, matches ngspice)
    Real k2 = 0.0;          ///< Second-order body-effect coefficient
    Real k3 = 80.0;         ///< Narrow width effect coefficient
    Real k3b = 0.0;         ///< Body-effect coefficient of k3
    Real w0 = 2.5e-6;       ///< Narrow width parameter [m]
    Real dvt0 = 2.2;        ///< Short-channel effect coefficient 0
    Real dvt1 = 0.53;       ///< Short-channel effect coefficient 1
    Real dvt2 = -0.032;     ///< Short-channel effect coefficient 2 [V⁻¹]
    Real dvt0w = 0.0;       ///< Narrow width effect coefficient 0 [m⁻¹]
    Real dvt1w = 5.3e6;     ///< Narrow width effect coefficient 1 [m⁻¹]
    Real dvt2w = -0.032;    ///< Narrow width effect coefficient 2 [V⁻¹]
    Real drout = 0.56;      ///< DIBL coefficient of output resistance
    Real dsub = 0.56;       ///< DIBL coefficient in linear region
    Real vth0_w = 0.0;      ///< Width dependence of VTH0 [Vm]
    Real vth0_wl = 0.0;     ///< Width and length cross term for VTH0 [Vm²]
    Real lpe0 = 1.74e-7;    ///< Lateral non-uniform doping parameter at Vbs=0 [m] (ngspice default)
    Real lpeb = 0.0;        ///< Lateral non-uniform doping effect on Vth [Vm]
    Real dvtp0 = 0.0;       ///< First parameter for Vth adjustment [m]
    Real dvtp1 = 0.0;       ///< Second parameter for Vth adjustment [V]
    Real dvtp2 = 0.0;       ///< Third parameter for Vth adjustment [V]
    Real dvtp3 = 0.0;       ///< Fourth parameter for Vth adjustment [V]
    Real dvtp4 = 0.0;       ///< Fifth parameter for Vth adjustment [V]
    Real dvtp5 = 0.0;       ///< Sixth parameter for Vth adjustment [V]

    //==========================================================================
    // Mobility Parameters (移動度パラメータ)
    //==========================================================================

    Real u0 = 0.067;        ///< Low-field mobility at tnom [m²/Vs] (NMOS default; see u0_given)
    Real ua = 1.0e-9;       ///< First-order mobility degradation coefficient [m/V]
    // BSIM4 manual, appendix A, and the BSIM4 4.8.3 code (b4set.c): 1.0e-19
    Real ub = 1.0e-19;      ///< Second-order mobility degradation coefficient [(m/V)²]
    // UA, UC and UC1 default by MOBMOD (bsim4_params_from_model); these are
    // the defaults for MOBMOD = 0 (BSIM4 manual, appendix A)
    Real uc = -0.0465e-9;   ///< Body-effect coefficient of mobility degradation [m/V²]
    Real ud = 0.0;          ///< Vds dependence of mobility degradation
    Real up = 0.0;          ///< Mobility channel length coefficient [1/m²]
    Real lp = 1.0e-8;       ///< Mobility channel length exponential coefficient [m]
    Real eu = 1.67;         ///< Exponent of mobility degradation
    Real ucs = 1.67;        ///< Exponent of velocity degradation for source side
    Real voff = -0.08;      ///< Offset voltage in subthreshold region [V]
    Real tvoff = 0.0;       ///< Temperature coefficient of voff [V/K]
    Real voffl = 0.0;       ///< Channel length dependence of voff [Vm]
    Real minv = 0.0;        ///< Vgsteff fitting parameter for moderate inversion
    Real minvcv = 0.0;      ///< Vgsteff fitting parameter for CV in moderate inversion
    Real fprout = 0.0;      ///< Effect of pocket implant on rout degradation
    Real pdits = 0.0;       ///< Impact of drain induced interface traps on rout [Ω·m]
    Real pditsd = 0.0;      ///< Vds dependence of pdits [Ω·m/V]
    Real pditsl = 0.0;      ///< Length dependence of pdits [Ω·m²]
    Real delta = 0.01;      ///< Effective Vds parameter [V]
    Real rdsw = 200.0;      ///< Parasitic resistance per width [Ω·µm]
    Real rdswmin = 0.0;     ///< Minimum rdsw at large gate overdrive [Ω·µm]
    Real rsw = 100.0;       ///< Source parasitic resistance per width [Ω·µm]
    Real rdw = 100.0;       ///< Drain parasitic resistance per width [Ω·µm]
    Real prwg = 1.0;        ///< Gate-bias effect on rdsw [V⁻¹] (BSIM4.8.3 default = 1.0)
    Real prwb = 0.0;        ///< Body-effect on rdsw [V⁻⁰·⁵]
    Real prt = 0.0;         ///< Temperature coefficient of parasitic resistance [Ω·µm]
    Real eta0 = 0.08;       ///< DIBL effect in subthreshold region
    Real etab = -0.07;      ///< Body-bias coefficient of DIBL effect [V⁻¹]

    //==========================================================================
    // Channel Length Modulation & DIBL Parameters (チャネル長変調・DIBLパラメータ)
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
    Real alpha0 = 0.0;      ///< First parameter of impact ionization current [m/V]
    Real alpha1 = 0.0;      ///< Second parameter of impact ionization current [V⁻¹]
    Real beta0 = 0.0;       ///< Second parameter of channel length modulation

    //==========================================================================
    // Temperature Parameters (温度依存性パラメータ)
    //==========================================================================

    Real tnom = 300.15;     ///< Parameter measurement temperature [K] (27°C)
    Real kt1 = -0.11;       ///< Temperature coefficient of Vth [V]
    Real kt1l = 0.0;        ///< Channel length sensitivity of kt1 [Vm]
    Real kt2 = 0.022;       ///< Body-bias coefficient of Vth temperature effect
    Real ute = -1.5;        ///< Mobility temperature exponent
    Real utl = 0.0;         ///< Mobility temperature exponent for high Vgsteff
    Real ua1 = 1.0e-9;      ///< Temperature coefficient of ua [m/V]
    Real ub1 = -1.0e-18;    ///< Temperature coefficient of ub [(m/V)²]
    Real uc1 = -0.056e-9;   ///< Temperature coefficient of uc [m/V²] (MOBMOD = 0)
    Real ud1 = 0.0;         ///< Temperature coefficient of ud
    Real up1 = 0.0;         ///< Temperature coefficient of up
    // Note: 'at' and 'prt' are already defined in Process and Mobility sections

    //==========================================================================
    // Gate Current (ゲート電流パラメータ - BSIM4新規)
    //==========================================================================

    Real igbmod = 0;        ///< Flag for gate-to-bulk tunneling current model
    Real igcmod = 0;        ///< Flag for gate-to-channel tunneling current model
    Real aigbacc = 1.36e-2; ///< Parameter for Igb in accumulation [Ω⁻¹]
    Real bigbacc = 1.71e-3; ///< Parameter for Igb in accumulation [V⁻¹Ω⁻¹]
    Real cigbacc = 0.075;   ///< Parameter for Igb in accumulation [V⁻¹]
    Real nigbacc = 1.0;     ///< Parameter for Igb in accumulation
    Real aigbinv = 1.11e-2; ///< Parameter for Igb in inversion [Ω⁻¹]
    Real bigbinv = 9.49e-4; ///< Parameter for Igb in inversion [V⁻¹Ω⁻¹]
    Real cigbinv = 0.006;   ///< Parameter for Igb in inversion [V⁻¹]
    Real eigbinv = 1.1;     ///< Parameter for Igb in inversion
    Real nigbinv = 3.0;     ///< Parameter for Igb in inversion
    Real aigc = 1.36e-2;    ///< Parameter for Igc [Ω⁻¹]
    Real bigc = 1.71e-3;    ///< Parameter for Igc [V⁻¹Ω⁻¹]
    Real cigc = 0.075;      ///< Parameter for Igc [V⁻¹]
    Real aigs = 1.36e-2;    ///< Parameter for Igs [Ω⁻¹]
    Real bigs = 1.71e-3;    ///< Parameter for Igs [V⁻¹Ω⁻¹]
    Real cigs = 0.075;      ///< Parameter for Igs [V⁻¹]
    Real aigd = 1.36e-2;    ///< Parameter for Igd [Ω⁻¹]
    Real bigd = 1.71e-3;    ///< Parameter for Igd [V⁻¹Ω⁻¹]
    Real cigd = 0.075;      ///< Parameter for Igd [V⁻¹]
    Real dlcig = 0.0;       ///< Delta L for Ig calculation [m]
    Real dlcigd = 0.0;      ///< Delta L for Igd calculation [m]
    Real poxedge = 1.0;     ///< Factor for gate edge Tox
    Real pigcd = 1.0;       ///< Vds impact on Igc and Igs
    Real ntox = 1.0;        ///< Exponent for Tox ratio
    Real toxref = 3.0e-9;   ///< Target oxide thickness for gate current model [m]
    Real phieot = 0.7;      ///< Surface potential used with the eot model [V]

    //==========================================================================
    // GIDL/GISL Parameters (ゲート誘起ドレイン漏れ電流 - BSIM4新規)
    //==========================================================================

    Real gidlmod = 0;       ///< Flag for GIDL model
    Real agidl = 0.0;       ///< Pre-exponential constant for GIDL [Ω⁻¹]
    Real bgidl = 2.3e9;     ///< Exponential constant for GIDL [V/m]
    Real cgidl = 0.5;       ///< Parameter for body-bias effect on GIDL [V³]
    Real egidl = 0.8;       ///< Fitting parameter for Vgs dependence of GIDL
    Real rgidl = 1.0;       ///< Parameter for conductance in GIDL [Ω·m]
    Real kgidl = 0.0;       ///< Parameter for temperature effect on GIDL [V]
    Real fgidl = 1.0;       ///< Parameter for Vgst dependence of GIDL
    Real agisl = 0.0;       ///< Pre-exponential constant for GISL [Ω⁻¹]
    Real bgisl = 2.3e9;     ///< Exponential constant for GISL [V/m]
    Real cgisl = 0.5;       ///< Parameter for body-bias effect on GISL [V³]
    Real egisl = 0.8;       ///< Fitting parameter for Vgs dependence of GISL
    Real rgisl = 1.0;       ///< Parameter for conductance in GISL [Ω·m]
    Real kgisl = 0.0;       ///< Parameter for temperature effect on GISL [V]
    Real fgisl = 1.0;       ///< Parameter for Vgst dependence of GISL
    Real gidlclamp = -1.0e-5; ///< Clamp value for GIDL current scaling
    Real idovvdsc = 1.0e-9;   ///< Clamp for Idov-Vds term in CV/Noise equations

    //==========================================================================
    // STI Parameters (浅溝素子分離 - BSIM4新規)
    //==========================================================================

    Real saref = 1.0e-6;    ///< Reference distance between OD edge and poly [m]
    Real sbref = 1.0e-6;    ///< Reference distance between OD edge and poly [m]
    Real wlod = 0.0;        ///< Width parameter for stress effect on Lod [m]
    Real ku0 = 0.0;         ///< Mobility degradation coefficient for LOD
    Real kvsat = 0.0;       ///< Saturation velocity degradation for LOD
    Real kvth0 = 0.0;       ///< Vth shift for LOD [V]
    Real tku0 = 0.0;        ///< Temperature coefficient of ku0 [K⁻¹]
    Real llodku0 = 0.0;     ///< Length parameter for ku0 [m]
    Real wlodku0 = 0.0;     ///< Width parameter for ku0 [m]
    Real llodvth = 0.0;     ///< Length parameter for kvth0 [m]
    Real wlodvth = 0.0;     ///< Width parameter for kvth0 [m]
    Real lku0 = 0.0;        ///< Length scaling for ku0 [m·m]
    Real wku0 = 0.0;        ///< Width scaling for ku0 [m·m]
    Real pku0 = 0.0;        ///< Cross term for ku0 [m·m]
    Real lkvth0 = 0.0;      ///< Length scaling for kvth0 [m·m]
    Real wkvth0 = 0.0;      ///< Width scaling for kvth0 [m·m]
    Real pkvth0 = 0.0;      ///< Cross term for kvth0 [m·m]
    Real stk2 = 0.0;        ///< K2 shift for stress effect
    Real lodk2 = 1.0;       ///< K2 shift parameter for LOD
    Real steta0 = 0.0;      ///< Eta0 shift for stress effect
    Real lodeta0 = 1.0;     ///< Eta0 shift parameter for LOD

    //==========================================================================
    // Well Proximity Effect (WPE) Parameters (ウェル近接効果 - BSIM4新規)
    //==========================================================================

    Real web = 0.0;         ///< WPE width parameter [m]
    Real wec = 0.0;         ///< WPE width parameter [m]
    Real kvth0we = 0.0;     ///< Vth0 shift for WPE [Vm]
    Real k2we = 0.0;        ///< K2 shift for WPE [V^0.5·m]
    Real ku0we = 0.0;       ///< U0 shift for WPE [m]
    Real scref = 1.0e-6;    ///< Reference distance for WPE [m]
    Real wpemod = 0;        ///< Flag for WPE model

    //==========================================================================
    // Gate Overlap Capacitance Parameters (ゲート重なり容量パラメータ)
    //==========================================================================

    Real cgso = 0.0;        ///< Gate-source overlap capacitance per width [F/m]
    Real cgdo = 0.0;        ///< Gate-drain overlap capacitance per width [F/m]
    Real cgbo = 0.0;        ///< Gate-bulk overlap capacitance per length [F/m]
    Real cgsl = 0.0;        ///< New gate-source overlap capacitance [F/m]
    Real cgdl = 0.0;        ///< New gate-drain overlap capacitance [F/m]
    Real ckappas = 0.6;     ///< S/D to channel coupling coefficient
    Real ckappad = 0.6;     ///< S/D to channel coupling coefficient
    Real cf = 0.0;          ///< Fringing field capacitance [F/m]
    Real clc = 1.0e-7;      ///< Constant term for short-channel Leff CV model [m]
    Real cle = 0.6;         ///< Exponential term for short-channel Leff CV model
    Real vfbcv = -1.0;      ///< Flat-band voltage for C-V model [V]
    Real acde = 1.0;        ///< Exponential coefficient for finite charge thickness
    Real moin = 15.0;       ///< Coefficient for gate-bias-dependent surface potential
    Real noff = 1.0;        ///< C-V turn-on/off parameter
    Real voffcv = 0.0;      ///< C-V lateral shift parameter [V]

    //==========================================================================
    // Junction Diode Parameters (接合ダイオードパラメータ)
    //==========================================================================

    Real cjs = 5.0e-4;      ///< Source bottom junction capacitance per area [F/m²]
    Real cjd = 5.0e-4;      ///< Drain bottom junction capacitance per area [F/m²]
    Real mjs = 0.5;         ///< Source bottom junction grading coefficient
    Real mjd = 0.5;         ///< Drain bottom junction grading coefficient
    Real pbs = 1.0;         ///< Source bottom junction built-in potential [V]
    Real pbd = 1.0;         ///< Drain bottom junction built-in potential [V]
    Real cjsws = 5.0e-10;   ///< Source sidewall junction capacitance [F/m]
    Real cjswd = 5.0e-10;   ///< Drain sidewall junction capacitance [F/m]
    Real mjsws = 0.33;      ///< Source sidewall junction grading coefficient
    Real mjswd = 0.33;      ///< Drain sidewall junction grading coefficient
    Real pbsws = 1.0;       ///< Source sidewall junction built-in potential [V]
    Real pbswd = 1.0;       ///< Drain sidewall junction built-in potential [V]
    Real cjswgs = 0.0;      ///< Source gate-edge sidewall junction capacitance [F/m]
    Real cjswgd = 0.0;      ///< Drain gate-edge sidewall junction capacitance [F/m]
    Real mjswgs = 0.33;     ///< Source gate-edge sidewall grading coefficient
    Real mjswgd = 0.33;     ///< Drain gate-edge sidewall grading coefficient
    Real pbswgs = 1.0;      ///< Source gate-edge sidewall built-in potential [V]
    Real pbswgd = 1.0;      ///< Drain gate-edge sidewall built-in potential [V]
    Real jss = 1.0e-4;      ///< Source bottom junction saturation current density [A/m²]
    Real jsd = 1.0e-4;      ///< Drain bottom junction saturation current density [A/m²]
    Real jsws = 0.0;        ///< Source sidewall junction saturation current [A/m]
    Real jswd = 0.0;        ///< Drain sidewall junction saturation current [A/m]
    Real jswgs = 0.0;       ///< Source gate-edge sidewall saturation current [A/m]
    Real jswgd = 0.0;       ///< Drain gate-edge sidewall saturation current [A/m]
    Real ijthsrev = 0.1;    ///< Source reverse limiting current [A]
    Real ijthsfwd = 0.1;    ///< Source forward limiting current [A]
    Real ijthdrev = 0.1;    ///< Drain reverse limiting current [A]
    Real ijthdfwd = 0.1;    ///< Drain forward limiting current [A]
    Real xjbvs = 1.0;       ///< Source bottom junction reverse-bias power law exponent
    Real xjbvd = 1.0;       ///< Drain bottom junction reverse-bias power law exponent
    Real bvs = 10.0;        ///< Source breakdown voltage [V]
    Real bvd = 10.0;        ///< Drain breakdown voltage [V]
    Real xtis = 3.0;        ///< Source junction current temperature exponent
    Real xtid = 3.0;        ///< Drain junction current temperature exponent

    //==========================================================================
    // Asymmetric S/D Junction Diode Model (非対称S/D接合モデル - BSIM4拡張)
    //==========================================================================

    Real jtss = 0.0;        ///< Source bottom saturation current density [A/m²]
    Real jtsd = 0.0;        ///< Drain bottom saturation current density [A/m²]
    Real jtssws = 0.0;      ///< Source sidewall saturation current [A/m]
    Real jtsswd = 0.0;      ///< Drain sidewall saturation current [A/m]
    Real jtsswgs = 0.0;     ///< Source gate-edge saturation current [A/m]
    Real jtsswgd = 0.0;     ///< Drain gate-edge saturation current [A/m]
    Real njts = 20.0;       ///< Non-ideality factor for tunneling current
    Real njtsd = 20.0;      ///< Non-ideality factor for drain tunneling
    Real njtssw = 20.0;     ///< Non-ideality factor for source sidewall tunneling
    Real njtsswd = 20.0;    ///< Non-ideality factor for drain sidewall tunneling
    Real njtsswg = 20.0;    ///< Non-ideality factor for source gate-edge tunneling
    Real njtsswgd = 20.0;   ///< Non-ideality factor for drain gate-edge tunneling
    Real vtss = 10.0;       ///< Source thermal voltage for tunneling [V]
    Real vtsd = 10.0;       ///< Drain thermal voltage for tunneling [V]
    Real vtssws = 10.0;     ///< Source sidewall thermal voltage [V]
    Real vtsswd = 10.0;     ///< Drain sidewall thermal voltage [V]
    Real vtsswgs = 10.0;    ///< Source gate-edge thermal voltage [V]
    Real vtsswgd = 10.0;    ///< Drain gate-edge thermal voltage [V]

    //==========================================================================
    // Noise Parameters (ノイズパラメータ)
    //==========================================================================

    Real noia = 1.0e20;     ///< Noise parameter A [m⁻¹]
    Real noib = 5.0e4;      ///< Noise parameter B [m⁻¹]
    Real noic = -1.4e-12;   ///< Noise parameter C [V·m]
    Real em = 4.1e7;        ///< Saturation field for noise [V/m]
    Real ntnoi = 1.0;       ///< Noise parameter for thermal noise
    Real lintnoi = 0.0;     ///< Lint offset for noise [m]
    Real ef = 1.0;          ///< Flicker noise frequency exponent
    Real af = 1.0;          ///< Flicker noise current exponent
    Real kf = 0.0;          ///< Flicker noise coefficient
    Real tnoia = 1.5;       ///< Temperature coefficient of noia
    Real tnoib = 3.5;       ///< Temperature coefficient of noib
    Real rnoia = 0.577;     ///< Ratio of noia for saturation/linear
    Real rnoib = 0.5164;    ///< Ratio of noib for saturation/linear

    //==========================================================================
    // Layout-Dependent Parasitics (レイアウト依存寄生 - BSIM4拡張)
    //==========================================================================

    Real dmcg = 0.0;        ///< Distance from gate contact center to S/D contact [m]
    Real dmci = 0.0;        ///< Distance from gate contact center to isolation edge [m]
    Real dmdg = 0.0;        ///< Distance from S/D contact center to gate edge [m]
    Real dmcgt = 0.0;       ///< Distance from gate contact center to gate edge (top) [m]
    Real dwj = 0.0;         ///< Offset of S/D junction depth [m]
    Real xgw = 0.0;         ///< Distance from gate contact to channel edge [m]
    Real xgl = 0.0;         ///< Offset of gate length [m]
    Real ngcon = 1.0;       ///< Number of gate contacts

    //==========================================================================
    // Geometry Scaling Parameters (幾何学的スケーリングパラメータ)
    //==========================================================================

    Real xl = 0.0;          ///< Length offset for CV model [m]
    Real xw = 0.0;          ///< Width offset for CV model [m]
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
    Real nf = 1.0;          ///< Number of device fingers
    Real m = 1.0;           ///< Multiplier (number of parallel devices)
    Real mult_i = 1.0;      ///< Current variability multiplier
    Real mult_q = 1.0;      ///< Charge variability multiplier
    Real mult_fn = 1.0;     ///< Flicker-noise variability multiplier
    Real min = 0;           ///< Flag to select minimum length/width (0=actual, 1=min)
    Real as = 0.0;          ///< Source diffusion area [m²]
    Real ad = 0.0;          ///< Drain diffusion area [m²]
    Real ps = 0.0;          ///< Source diffusion perimeter [m]
    Real pd = 0.0;          ///< Drain diffusion perimeter [m]
    int nrd = 1;            ///< Number of drain diffusion squares
    int nrs = 1;            ///< Number of source diffusion squares
    Real sa = 0.0;          ///< Distance between OD edge and poly of one side [m]
    Real sb = 0.0;          ///< Distance between OD edge and poly of other side [m]
    Real sd = 0.0;          ///< Distance between neighboring fingers [m]
    Real sca = 0.0;         ///< Distance to well proximity [m]
    Real scb = 0.0;         ///< Distance to other well proximity [m]
    Real scc = 0.0;         ///< Distance to third proximity [m]
    Real sc = 0.0;          ///< Distance to well edge [m]
    Real off = 0.0;         ///< Device disable flag (1=disable, 0=enable)
    Real icvds = 0.0;       ///< Initial Vds for transient analysis [V]
    Real icvgs = 0.0;       ///< Initial Vgs for transient analysis [V]
    Real icvbs = 0.0;       ///< Initial Vbs for transient analysis [V]

    //==========================================================================
    // Model Selection Flags (モデル選択フラグ)
    //==========================================================================

    int mobmod = 0;         ///< Mobility model selector (0/1/2)
    int rdsmod = 0;         ///< S/D resistance model selector
    int rgatemod = 0;       ///< Gate resistance model selector
    int rbodymod = 0;       ///< Body resistance model selector
    int capmod = 0;         ///< Capacitance model selector
    int trnqsmod = 0;       ///< Transient NQS model selector
    int acnqsmod = 0;       ///< AC NQS model selector
    int fnoimod = 1;        ///< Flicker noise model selector
    int tnoimod = 0;        ///< Thermal noise model selector
    int permod = 1;         ///< Periodic AC small-signal model selector
    int geomod = 0;         ///< Geometry-dependent parasitics model selector
    int rgeomod = 0;        ///< Source/drain resistance geometry model
    int mtrlmod = 0;        ///< High-k metal gate model selector
    int diomod = 1;         ///< S/D junction diode IV model selector
    int tempmod = 0;        ///< Temperature model selector

    //==========================================================================
    // Numerical Control Parameters (数値制御パラメータ - KirimaSpice拡張)
    //==========================================================================


    //==========================================================================
    // Helper Methods
    //==========================================================================

    /// Calculate effective channel length [m]
    Real get_leff() const {
        return l - 2.0 * lint;
    }

    /// Effective channel width of one finger [m]: W is the width of all NF
    /// fingers together (BSIM4 manual, geometry), and the current of one
    /// finger is multiplied by NF (BSIM4Core::evaluate)
    Real get_weff() const {
        return w / std::max(nf, 1.0) - 2.0 * wint;
    }

    /// Initialize derived parameters from basic parameters
    /// Call this after setting basic parameters like ndep, toxe, etc.
    /// @param temp Temperature in Kelvin
    void compute_derived_params(Real temp = 300.15);

    /// Validate parameter ranges
    /// @return true if all parameters are valid
    bool validate() const {
        if (toxe <= 0.0) return false;
        if (l <= 0.0) return false;
        if (w <= 0.0) return false;
        if (u0 <= 0.0) return false;
        if (vsat <= 0.0) return false;
        if (tnom <= 0.0) return false;
        if (nf < 1.0) return false;
        if (m < 1.0) return false;
        return true;
    }
};

/// BSIM4 parameters from the parameters of a .MODEL card (names in lower
/// case), with the checks of the version and the ranges. The parser and
/// subcircuit expansion both use it, so a model means the same at the top
/// level and inside a subcircuit; the instance values (W, L, M) are set by
/// the caller.
/// The names the model does not take are appended to unused
BSIM4Params bsim4_params_from_model(const std::map<std::string, Real>& model_params,
                                    const std::string& model_name,
                                    std::vector<std::string>* unused = nullptr);

/// Set an instance parameter (W, L, NF, M, AS, AD, PS, PD, NRD, NRS and the
/// MULT_ factors; the name in lower case) and say whether the name is one.
/// The card reader and the element line both set them here, the element line
/// last.
bool set_bsim4_instance_parameter(BSIM4Params& params, const std::string& key, Real value);

} // namespace kirimaspice::devices
