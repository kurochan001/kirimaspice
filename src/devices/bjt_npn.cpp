// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/bjt_npn.cpp
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

BJT_NPN::BJT_NPN(DeviceId id, const std::string& name,
                 NodeId collector, NodeId base, NodeId emitter,
                 Real is, Real beta_f, Real nf, Real isc, Real beta_r, Real nr,
                 Real vaf, Real var, Real ikf, Real ikr,
                 Real rb, Real rbm, Real irb,
                 Real ise, Real ne, Real isc_leak, Real nc,
                 Real cje, Real vje, Real mje, Real cjc, Real vjc, Real mjc,
                 Real tf, Real tr, Real kf, Real af, Real ff,
                 Real xti, Real xtb, Real eg, Real tnom)
    : Device(id, name, DeviceType::BJT_NPN)
    , nodes_{collector, base, emitter}
    , is_(is)
    , beta_f_(beta_f)
    , nf_(nf)
    , isc_(isc < 0.0 ? is : isc)  // Default: same as Is
    , beta_r_(beta_r)
    , nr_(nr)
    , vaf_(vaf)  // Forward Early voltage
    , var_(var)  // Reverse Early voltage
    , ikf_(ikf)  // Forward knee current
    , ikr_(ikr)  // Reverse knee current
    , rb_(rb)    // Zero-bias base resistance
    , rbm_(rbm)  // Minimum base resistance
    , irb_(irb)  // Base resistance half-value current
    , ise_(ise)  // B-E leakage saturation current
    , ne_(ne)    // B-E leakage emission coefficient
    , isc_leak_(isc_leak)  // B-C leakage saturation current
    , nc_(nc)    // B-C leakage emission coefficient
    , vt_(k_boltzmann * 300.15 / q_electron)  // Default: T=300.15K (27°C)
    , cje_(cje)
    , vje_(vje)
    , mje_(mje)
    , cjc_(cjc)
    , vjc_(vjc)
    , mjc_(mjc)
    , tf_(tf)
    , tr_(tr)
    , kf_(kf)
    , af_(af)
    , ff_(ff)
    // Temperature parameters
    , xti_(xti)
    , xtb_(xtb)
    , eg_(eg)
    , tnom_(tnom)
    , is_tnom_(is)
    , isc_tnom_(isc < 0.0 ? is : isc)
    , ise_tnom_(ise)
    , isc_leak_tnom_(isc_leak)
    , beta_f_tnom_(beta_f)
    , beta_r_tnom_(beta_r)
    // State variables for transient analysis (nonlinear capacitance)
    , vbe_prev_(0.0)
    , vbe_prev2_(0.0)
    , vbe_prev3_(0.0)
    , vbe_saved_(0.0)
    , vbe_saved2_(0.0)
    , vbe_saved3_(0.0)
    , charge_be_(0.0)
    , charge_be_prev_(0.0)
    , vbc_prev_(0.0)
    , vbc_prev2_(0.0)
    , vbc_prev3_(0.0)
    , vbc_saved_(0.0)
    , vbc_saved2_(0.0)
    , vbc_saved3_(0.0)
    , charge_bc_(0.0)
    , charge_bc_prev_(0.0)
    , timestep_(0.0)
    , last_method_(IntegrationMethod::BACKWARD_EULER)
    , timestep_saved_(0.0)
    , method_saved_(IntegrationMethod::BACKWARD_EULER)
    , first_accept_(true)
    , bdf_alpha0_(0.0)
    , bdf_alpha1_(0.0)
    , bdf_alpha2_(0.0)
    , bdf_alpha3_(0.0)
    , bdf_beta0_(0.0)
    , use_bdf_var_(false) {

    if (is <= 0.0) {
        throw std::runtime_error("BJT saturation current must be positive: " + name);
    }
    if (beta_f <= 0.0) {
        throw std::runtime_error("BJT forward beta must be positive: " + name);
    }
    if (nf <= 0.0) {
        throw std::runtime_error("BJT forward emission coefficient must be positive: " + name);
    }
    if (isc_ <= 0.0) {
        throw std::runtime_error("BJT collector saturation current must be positive: " + name);
    }
    if (beta_r <= 0.0) {
        throw std::runtime_error("BJT reverse beta must be positive: " + name);
    }
    if (nr <= 0.0) {
        throw std::runtime_error("BJT reverse emission coefficient must be positive: " + name);
    }
    if (ne <= 0.0) {
        throw std::runtime_error("BJT B-E leakage emission coefficient must be positive: " + name);
    }
    if (nc <= 0.0) {
        throw std::runtime_error("BJT B-C leakage emission coefficient must be positive: " + name);
    }
    if (ise < 0.0 || isc_leak < 0.0) {
        throw std::runtime_error("BJT leakage saturation currents must be non-negative: " + name);
    }
    if (cje < 0.0 || cjc < 0.0) {
        throw std::runtime_error("BJT junction capacitance must be non-negative: " + name);
    }
    if ((vje <= 0.0 && cje > 0.0) || (vjc <= 0.0 && cjc > 0.0)) {
        throw std::runtime_error("BJT junction potential must be positive when capacitance > 0: " + name);
    }
    if ((mje <= 0.0 && cje > 0.0) || (mjc <= 0.0 && cjc > 0.0)) {
        throw std::runtime_error("BJT grading coefficient must be positive when capacitance > 0: " + name);
    }
    if (tf < 0.0 || tr < 0.0) {
        throw std::runtime_error("BJT transit time must be non-negative: " + name);
    }

    spdlog::debug("Created BJT_NPN {} (C={}, B={}, E={}, Is={:.6e}A, βF={:.1f}, Cje={:.6e}F, Cjc={:.6e}F)",
                  name, collector, base, emitter, is_, beta_f_, cje_, cjc_);
}

void BJT_NPN::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }

    // Update thermal voltage using utility function
    vt_ = core::compute_thermal_voltage(temp_kelvin);

    // Update saturation currents IS(T), ISC(T), ISE(T), ISC_leak(T)
    // SPICE equation: I_s(T) = I_s(Tnom) * (T/Tnom)^(XTI/N) * exp(-EG/N * (1/Vt(T) - 1/Vt(Tnom)))
    // Using utility function for temperature scaling

    // IS(T): B-E saturation current
    is_ = core::scale_saturation_current(is_tnom_, xti_, eg_, nf_, temp_kelvin, tnom_);

    // ISC(T): B-C saturation current
    isc_ = core::scale_saturation_current(isc_tnom_, xti_, eg_, nr_, temp_kelvin, tnom_);

    // ISE(T): B-E leakage saturation current (if present)
    if (ise_tnom_ > 0.0) {
        ise_ = core::scale_saturation_current(ise_tnom_, xti_, eg_, ne_, temp_kelvin, tnom_);
    }

    // ISC_leak(T): B-C leakage saturation current (if present)
    if (isc_leak_tnom_ > 0.0) {
        isc_leak_ = core::scale_saturation_current(isc_leak_tnom_, xti_, eg_, nc_, temp_kelvin, tnom_);
    }

    // Update forward and reverse beta
    // SPICE equation: BETA(T) = BETA(Tnom) * (T/Tnom)^XTB
    // Using mobility scaling utility (same mathematical form)
    beta_f_ = core::scale_mobility_temperature(beta_f_tnom_, xtb_, temp_kelvin, tnom_);
    beta_r_ = core::scale_mobility_temperature(beta_r_tnom_, xtb_, temp_kelvin, tnom_);

    spdlog::debug("{} set temperature to {:.2f}K (Vt={:.6f}V, IS={:.6e}A, BF={:.2f})",
                  name_, temp_kelvin, vt_, is_, beta_f_);
}

Real BJT_NPN::get_base_current(Real vbe, Real vbc) const {
    // Gummel-Poon model with non-ideal currents:
    // Ib = (Is/βF) × (exp(Vbe/(NF*Vt)) - 1) + (Isc/βR) × (exp(Vbc/(NR*Vt)) - 1)
    //    + ISE × (exp(Vbe/(NE*Vt)) - 1) + ISC × (exp(Vbc/(NC*Vt)) - 1)
    //
    // Note: Base current does NOT include high-current effects (q1/q2).
    // High-current effects are applied only to collector current via qb factor.
    // This allows beta = Ic/Ib = beta_f / qb to decrease when q1 < 1.

    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;
    Real ne_vt = ne_ * vt_;
    Real nc_vt = nc_ * vt_;

    // Ideal base current components
    Real i_forward = core::safe_exp_junction(vbe, nf_vt, is_ / beta_f_);
    Real i_reverse = core::safe_exp_junction(vbc, nr_vt, isc_ / beta_r_);

    // Non-ideal (leakage) current components
    Real i_be_leak = core::safe_exp_junction(vbe, ne_vt, ise_);
    Real i_bc_leak = core::safe_exp_junction(vbc, nc_vt, isc_leak_);

    return i_forward + i_reverse + i_be_leak + i_bc_leak;
}

Real BJT_NPN::get_collector_current(Real vbe, Real vbc) const {
    // Gummel-Poon model with base-width modulation (Early effect):
    // Ic = Is * (exp(Vbe/(NF*Vt)) - 1) / qb - (Isc/βR) * (exp(Vbc/(NR*Vt)) - 1) / qbr
    // where:
    //   qb  = q1 × (1 - Vbe/VAR + Vbc/VAF)  (forward base-width modulation)
    //   qbr = q2 × (1 - Vbc/VAR + Vbe/VAF)  (reverse base-width modulation)
    //
    // Note: qb is in the DENOMINATOR (not numerator), making Ic INCREASE with Vce
    //
    // For simple Ebers-Moll compatibility:
    //   q1 = q2 = 1.0 (no high-current effects yet)
    //
    // Gummel-Poon Early effect (base-width modulation)

    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;

    // Forward component: If = Is * (exp(Vbe/(NF*Vt)) - 1)
    Real i_forward = core::safe_exp_junction(vbe, nf_vt, is_);

    // Reverse component: Ir = (Isc/βR) * (exp(Vbc/(NR*Vt)) - 1)
    // Note: We SUBTRACT this from collector current
    Real i_reverse = core::safe_exp_junction(vbc, nr_vt, isc_ / beta_r_);

    // High-current effects (Webster effect, beta roll-off)
    // q1 = 1 / sqrt(1 + Ic/IKF)  (forward knee current)
    // q2 = 1 / sqrt(1 + Ie/IKR)  (reverse knee current)
    //
    // NOTE: This requires iteration since Ic depends on q1, and q1 depends on Ic.
    // We use the previous iteration's current for q1/q2 calculation (linearization).
    // For now, we use a simplified approach: assume Ic ≈ i_forward for q1 calculation.

    Real q1 = 1.0;
    Real q2 = 1.0;

    // Forward high-current roll-off
    // Gummel-Poon q1 factor: q1 = sqrt(1 + Ic/IKF)
    // Note: q1 > 1 at high currents, making qb larger and Ic smaller (beta roll-off)
    if (ikf_ > 0.0) {
        // Approximate: use i_forward (without qb modulation) for q1 calculation
        Real ic_approx = std::abs(i_forward);
        q1 = std::sqrt(1.0 + ic_approx / ikf_);
    }

    // Reverse high-current roll-off
    // q2 = sqrt(1 + Ie/IKR)
    if (ikr_ > 0.0) {
        // Ie ≈ Ic + Ib ≈ i_forward (approximation for reverse mode)
        Real ie_approx = std::abs(i_forward);
        q2 = std::sqrt(1.0 + ie_approx / ikr_);
    }

    // Gummel-Poon base-width modulation factor (Early effect)
    // qb = q1 × (1 - Vbe/VAR + Vbc/VAF)
    // Note: positive Vbc (reverse active) increases qb, negative Vbc (forward active) decreases qb
    Real qb = q1;
    if (vaf_ > 0.0 || var_ > 0.0) {
        Real modulation = 1.0;
        if (vaf_ > 0.0) modulation += vbc / vaf_;  // Note: +Vbc (not -Vbc)
        if (var_ > 0.0) modulation -= vbe / var_;

        // Clamp modulation to prevent negative or very small values
        if (modulation < 0.01) {
            modulation = 0.01;  // Prevents division by zero
        }
        qb = q1 * modulation;
    }

    // Reverse base-width modulation factor
    // qbr = q2 × (1 - Vbc/VAR + Vbe/VAF)
    Real qbr = q2;
    if (var_ > 0.0 || vaf_ > 0.0) {
        Real modulation = 1.0;
        if (vaf_ > 0.0) modulation += vbe / vaf_;
        if (var_ > 0.0) modulation -= vbc / var_;

        if (modulation < 0.01) {
            modulation = 0.01;
        }
        qbr = q2 * modulation;
    }

    // Ic = If/qb - Ir/qbr (qb in denominator)
    return i_forward / qb - i_reverse / qbr;
}

Real BJT_NPN::get_base_resistance(Real vbe, Real vbc) const {
    // Gummel-Poon base resistance model:
    // R_b = (RBM + (RB - RBM) × q_b) / (1 + (I_b / IRB)²)
    //
    // Where:
    //   RB:  Zero-bias (maximum) base resistance [Ω]
    //   RBM: Minimum base resistance at high currents [Ω]
    //   IRB: Base resistance half-value current [A]
    //   q_b: Base-width modulation factor (same as used in collector current)
    //   I_b: Base current [A]
    //
    // Physical interpretation:
    //   - At low currents (I_b → 0): R_b → RB × q_b (maximum resistance)
    //   - At high currents (I_b >> IRB): R_b → RBM × q_b (minimum resistance)
    //   - q_b > 1 due to Early effect and high-current effects
    //
    // If RB = 0, no base resistance model is used.

    if (rb_ <= 0.0) {
        return 0.0;  // No base resistance
    }

    // Calculate base-width modulation factor q_b (same as in get_collector_current)
    Real qb = 1.0;

    // Forward high-current roll-off: q1 = sqrt(1 + Ic/IKF)
    Real q1 = 1.0;
    if (ikf_ > 0.0) {
        // Use forward current approximation for q1 calculation
        Real nf_vt = nf_ * vt_;
        Real i_forward = core::safe_exp_junction(vbe, nf_vt, is_);
        Real ic_approx = std::abs(i_forward);
        q1 = std::sqrt(1.0 + ic_approx / ikf_);
    }

    // Early effect modulation: (1 - Vbe/VAR + Vbc/VAF)
    if (vaf_ > 0.0 || var_ > 0.0) {
        Real modulation = 1.0;
        if (vaf_ > 0.0) modulation += vbc / vaf_;
        if (var_ > 0.0) modulation -= vbe / var_;

        // Clamp to prevent negative or very small values
        if (modulation < 0.01) {
            modulation = 0.01;
        }
        qb = q1 * modulation;
    } else {
        qb = q1;
    }

    // Calculate base current
    Real ib = get_base_current(vbe, vbc);
    Real ib_abs = std::abs(ib);

    // Calculate current-dependent term: 1 / (1 + (I_b / IRB)²)
    Real current_factor = 1.0;
    if (irb_ > 0.0) {
        Real ib_ratio = ib_abs / irb_;
        current_factor = 1.0 / (1.0 + ib_ratio * ib_ratio);
    }

    // R_b = (RBM + (RB - RBM) × q_b) × current_factor
    Real rb_effective = rbm_ + (rb_ - rbm_) * qb;
    return rb_effective * current_factor;
}

Real BJT_NPN::get_base_conductance_vbe(Real vbe, Real vbc) const {
    // g_be = dIb/dVbe = (Is/(βF*NF*Vt)) * exp(Vbe/(NF*Vt))
    //                  + (ISE/(NE*Vt)) * exp(Vbe/(NE*Vt))  (non-ideal current)
    Real nf_vt = nf_ * vt_;
    Real ne_vt = ne_ * vt_;

    Real g_ideal = core::safe_exp_conductance(vbe, nf_vt, is_ / (beta_f_ * nf_vt), gmin_default);
    Real g_leak = core::safe_exp_conductance(vbe, ne_vt, ise_ / ne_vt, 0.0);  // No gmin for leakage

    return g_ideal + g_leak;
}

Real BJT_NPN::get_base_conductance_vbc(Real vbe, Real vbc) const {
    // g_bc = dIb/dVbc = (Isc/(βR*NR*Vt)) * exp(Vbc/(NR*Vt))
    //                  + (ISC_leak/(NC*Vt)) * exp(Vbc/(NC*Vt))  (non-ideal current)
    // Use 1.001*gmin to break symmetry with g_cc and avoid singular Jacobian
    Real nr_vt = nr_ * vt_;
    Real nc_vt = nc_ * vt_;

    Real g_ideal = core::safe_exp_conductance(vbc, nr_vt, isc_ / (beta_r_ * nr_vt), 1.001 * gmin_default);
    Real g_leak = core::safe_exp_conductance(vbc, nc_vt, isc_leak_ / nc_vt, 0.0);  // No gmin for leakage

    return g_ideal + g_leak;
}

Real BJT_NPN::get_collector_conductance_vbe(Real vbe, Real vbc) const {
    // g_ce = dIc/dVbe with Gummel-Poon base-width modulation + high-current effects
    // Ic = Is * (exp(Vbe/(NF*Vt)) - 1) / qb - (Isc/βR) * (exp(Vbc/(NR*Vt)) - 1) / qbr
    // where qb = q1 × (1 - Vbe/VAR + Vbc/VAF)
    //       q1 = sqrt(1 + Ic/IKF)  (high-current roll-off, Gummel-Poon model)
    //
    // dIc/dVbe = d(If/qb)/dVbe - d(Ir/qbr)/dVbe
    //
    // For d(If/qb)/dVbe, we need the quotient rule:
    //   d(If/qb)/dVbe = [dIf/dVbe × qb - If × dqb/dVbe] / qb²
    //
    // where:
    //   dqb/dVbe = dq1/dVbe × modulation + q1 × dmodulation/dVbe
    //   dmodulation/dVbe = -1/VAR (Early effect)
    //   dq1/dVbe = dq1/dIc × dIc/dVbe (high-current effect)
    //
    // To avoid implicit equation, we use linearization: dq1/dVbe ≈ dq1/dIf × dIf/dVbe
    // This is valid for Newton-Raphson iteration (use previous iteration's value).
    //
    // Gummel-Poon conductance with base-width modulation + high-current effects

    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;

    // Forward current component
    Real i_forward = core::safe_exp_junction(vbe, nf_vt, is_);
    Real g_forward_base = core::safe_exp_conductance(vbe, nf_vt, is_ / nf_vt, gmin_default);

    // Reverse current component
    Real i_reverse = core::safe_exp_junction(vbc, nr_vt, isc_ / beta_r_);

    // High-current coefficient q1
    // New Gummel-Poon definition: q1 = sqrt(1 + Ic/IKF)
    Real q1 = 1.0;
    Real dq1_dif = 0.0;  // dq1/dIf derivative
    if (ikf_ > 0.0) {
        Real ic_approx = std::abs(i_forward);
        q1 = std::sqrt(1.0 + ic_approx / ikf_);
        // dq1/dIf = d/dIf[sqrt(1 + If/IKF)] = 1 / (2 × IKF × q1)
        dq1_dif = 1.0 / (2.0 * ikf_ * q1);
    }

    // Early effect modulation
    Real modulation = 1.0;
    Real dmodulation_dvbe = 0.0;
    if (vaf_ > 0.0) modulation += vbc / vaf_;
    if (var_ > 0.0) {
        modulation -= vbe / var_;
        dmodulation_dvbe = -1.0 / var_;
    }
    if (modulation < 0.01) modulation = 0.01;

    Real qb = q1 * modulation;

    // dqb/dVbe = dq1/dIf × dIf/dVbe × modulation + q1 × dmodulation/dVbe
    Real dqb_dvbe = dq1_dif * g_forward_base * modulation + q1 * dmodulation_dvbe;

    // Calculate qbr (reverse base-width modulation)
    Real q2 = 1.0;
    if (ikr_ > 0.0) {
        Real ie_approx = std::abs(i_forward);
        q2 = std::sqrt(1.0 + ie_approx / ikr_);
    }

    Real modulation_r = 1.0;
    Real dmodulation_r_dvbe = 0.0;
    if (vaf_ > 0.0) {
        modulation_r += vbe / vaf_;
        dmodulation_r_dvbe = 1.0 / vaf_;
    }
    if (var_ > 0.0) modulation_r -= vbc / var_;
    if (modulation_r < 0.01) modulation_r = 0.01;

    Real qbr = q2 * modulation_r;

    // Total conductance: dIc/dVbe
    // First term: d(If/qb)/dVbe = [dIf/dVbe × qb - If × dqb/dVbe] / qb²
    Real g_ce = (g_forward_base * qb - i_forward * dqb_dvbe) / (qb * qb);

    // Reverse term: -Ir/qbr² × dqbr/dVbe
    Real dqbr_dvbe = q2 * dmodulation_r_dvbe;
    g_ce -= (i_reverse / (qbr * qbr)) * dqbr_dvbe;

    return g_ce;
}

Real BJT_NPN::get_collector_conductance_vbc(Real vbe, Real vbc) const {
    // g_cc = dIc/dVbc with Gummel-Poon base-width modulation + high-current effects
    // Ic = Is * (exp(Vbe/(NF*Vt)) - 1) / qb - (Isc/βR) * (exp(Vbc/(NR*Vt)) - 1) / qbr
    // where qb = q1 × (1 - Vbe/VAR + Vbc/VAF)
    //       qbr = q2 × (1 - Vbc/VAR + Vbe/VAF)
    //       q2 = 1 / sqrt(1 + Ie/IKR)  (reverse high-current roll-off)
    //
    // dIc/dVbc = d(If/qb)/dVbc - d(Ir/qbr)/dVbc
    //          = -(If/qb²) × dqb/dVbc - [dIr/dVbc × qbr - Ir × dqbr/dVbc] / qbr²
    //
    // where:
    //   dqb/dVbc = q1 × dmodulation/dVbc (q1 doesn't directly depend on Vbc)
    //   dmodulation/dVbc = 1/VAF (Early effect)
    //   dqbr/dVbc = dq2/dVbc × modulation_r + q2 × dmodulation_r/dVbc
    //   dmodulation_r/dVbc = -1/VAR
    //   dq2/dVbc = dq2/dIr × dIr/dVbc (high-current effect)
    //
    // Gummel-Poon conductance with base-width modulation + high-current effects

    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;

    // Forward current component
    Real i_forward = core::safe_exp_junction(vbe, nf_vt, is_);

    // Reverse current component
    Real i_reverse = core::safe_exp_junction(vbc, nr_vt, isc_ / beta_r_);
    Real g_reverse_base = core::safe_exp_conductance(vbc, nr_vt, isc_ / (beta_r_ * nr_vt), gmin_default);

    // High-current coefficient q1 (Gummel-Poon model)
    Real q1 = 1.0;
    if (ikf_ > 0.0) {
        Real ic_approx = std::abs(i_forward);
        q1 = std::sqrt(1.0 + ic_approx / ikf_);
    }

    // Early effect modulation (forward)
    Real modulation = 1.0;
    Real dmodulation_dvbc = 0.0;
    if (vaf_ > 0.0) {
        modulation += vbc / vaf_;
        dmodulation_dvbc = 1.0 / vaf_;
    }
    if (var_ > 0.0) modulation -= vbe / var_;
    if (modulation < 0.01) modulation = 0.01;

    Real qb = q1 * modulation;

    // dqb/dVbc = q1 × dmodulation/dVbc (assuming q1 doesn't directly depend on Vbc)
    Real dqb_dvbc = q1 * dmodulation_dvbc;

    // High-current coefficient q2 (Gummel-Poon model)
    Real q2 = 1.0;
    Real dq2_dir = 0.0;  // dq2/dIr derivative
    if (ikr_ > 0.0) {
        Real ie_approx = std::abs(i_forward);
        q2 = std::sqrt(1.0 + ie_approx / ikr_);
        // dq2/dIr = d/dIr[sqrt(1 + Ir/IKR)] = 1 / (2 × IKR × q2)
        dq2_dir = 1.0 / (2.0 * ikr_ * q2);
    }

    // Early effect modulation (reverse)
    Real modulation_r = 1.0;
    Real dmodulation_r_dvbc = 0.0;
    if (vaf_ > 0.0) modulation_r += vbe / vaf_;
    if (var_ > 0.0) {
        modulation_r -= vbc / var_;
        dmodulation_r_dvbc = -1.0 / var_;
    }
    if (modulation_r < 0.01) modulation_r = 0.01;

    Real qbr = q2 * modulation_r;

    // dqbr/dVbc = dq2/dIr × dIr/dVbc × modulation_r + q2 × dmodulation_r/dVbc
    Real dqbr_dvbc = dq2_dir * g_reverse_base * modulation_r + q2 * dmodulation_r_dvbc;

    // Total conductance: dIc/dVbc
    // First term (forward): -(If/qb²) × dqb/dVbc
    Real g_cc = -(i_forward / (qb * qb)) * dqb_dvbc;

    // Second term (reverse): -[dIr/dVbc × qbr - Ir × dqbr/dVbc] / qbr²
    g_cc -= (g_reverse_base * qbr - i_reverse * dqbr_dvbc) / (qbr * qbr);

    return g_cc;
}

void BJT_NPN::stamp_gmin(MNASystem& mna, const DenseVector& node_voltages) const {
    // Add small conductance gmin between each terminal pair for numerical stability
    // This prevents singular matrix when BJT is in cut-off
    (void)node_voltages;  // Suppress unused parameter warning (gmin is constant)

    NodeId nc = nodes_[0];  // Collector
    NodeId nb = nodes_[1];  // Base
    NodeId ne = nodes_[2];  // Emitter
    Real g = gmin_default;

    // C-B conductance
    auto nc_idx = (nc == NODE_GND) ? -1 : static_cast<int>(nc - 1);
    auto nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);

    if (nc_idx >= 0) {
        mna.G.add_entry(nc_idx, nc_idx, g);
        if (nb_idx >= 0) {
            mna.G.add_entry(nc_idx, nb_idx, -g);
        }
    }
    if (nb_idx >= 0) {
        mna.G.add_entry(nb_idx, nb_idx, g);
        if (nc_idx >= 0) {
            mna.G.add_entry(nb_idx, nc_idx, -g);
        }
    }

    // B-E conductance
    auto ne_idx = (ne == NODE_GND) ? -1 : static_cast<int>(ne - 1);

    if (nb_idx >= 0) {
        mna.G.add_entry(nb_idx, nb_idx, g);
        if (ne_idx >= 0) {
            mna.G.add_entry(nb_idx, ne_idx, -g);
        }
    }
    if (ne_idx >= 0) {
        mna.G.add_entry(ne_idx, ne_idx, g);
        if (nb_idx >= 0) {
            mna.G.add_entry(ne_idx, nb_idx, -g);
        }
    }

    // C-E conductance (completing all terminal pairs)
    if (nc_idx >= 0) {
        mna.G.add_entry(nc_idx, nc_idx, g);
        if (ne_idx >= 0) {
            mna.G.add_entry(nc_idx, ne_idx, -g);
        }
    }
    if (ne_idx >= 0) {
        mna.G.add_entry(ne_idx, ne_idx, g);
        if (nc_idx >= 0) {
            mna.G.add_entry(ne_idx, nc_idx, -g);
        }
    }
}

void BJT_NPN::stamp_dc_nonlinear(MNASystem& mna,
                                 const DenseVector& node_voltages) const {
    // Newton-Raphson linearization of full Ebers-Moll model:
    //
    // Base current:      Ib = (Is/βF) * (exp(Vbe/(NF*Vt)) - 1) + (Isc/βR) * (exp(Vbc/(NR*Vt)) - 1)
    // Collector current: Ic = Is * (exp(Vbe/(NF*Vt)) - 1) - (Isc/βR) * (exp(Vbc/(NR*Vt)) - 1)
    //
    // Linearization:
    //   Ib = g_be * Vbe + g_bc * Vbc + Ib_eq
    //   Ic = g_ce * Vbe + g_cc * Vbc + Ic_eq
    //
    // Where:
    //   g_be = dIb/dVbe, g_bc = dIb/dVbc
    //   g_ce = dIc/dVbe, g_cc = dIc/dVbc
    //   Ib_eq = Ib - g_be * Vbe - g_bc * Vbc
    //   Ic_eq = Ic - g_ce * Vbe - g_cc * Vbc
    //
    // MNA stamps (KCL: current OUT of node is positive):
    //   Base node:      Ib = (g_be + g_bc) * Vb - g_be * Ve - g_bc * Vc + Ib_eq
    //   Collector node: Ic = (g_ce + g_cc) * Vb - g_ce * Ve - g_cc * Vc + Ic_eq
    //   Emitter node:   -(Ib + Ic) = -(g_be + g_bc + g_ce + g_cc) * Vb + (g_be + g_ce) * Ve + (g_bc + g_cc) * Vc - (Ib_eq + Ic_eq)

    NodeId nc = nodes_[0];  // Collector
    NodeId nb = nodes_[1];  // Base
    NodeId ne = nodes_[2];  // Emitter

    // Get terminal voltages
    // node_voltages is indexed from 0, but NodeId starts from 1 (0=GND)
    Real vc = (nc == NODE_GND) ? 0.0 : node_voltages[nc - 1];
    Real vb = (nb == NODE_GND) ? 0.0 : node_voltages[nb - 1];
    Real ve = (ne == NODE_GND) ? 0.0 : node_voltages[ne - 1];

    Real vbe = vb - ve;
    Real vbc = vb - vc;

    // Limit the step of each junction voltage from its last evaluation
    // (core::limit_junction_step); at convergence the limited voltages equal
    // the junction voltages, so the solution follows the Ebers-Moll equations.
    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;
    Real vbe_limited = vbe;
    Real vbc_limited = vbc;
    if (enable_voltage_limiting_) {
        vbe_limited = core::limit_junction_step(vbe, vbe_last_eval_, nf_vt,
                                                core::compute_vcrit(nf_vt, is_));
        vbc_limited = core::limit_junction_step(vbc, vbc_last_eval_, nr_vt,
                                                core::compute_vcrit(nr_vt, isc_));
    }
    vbe_last_eval_ = vbe_limited;
    vbc_last_eval_ = vbc_limited;
    limited_in_last_stamp_ = (vbe_limited != vbe) || (vbc_limited != vbc);

    // Currents and conductances at the voltages where the junctions are evaluated
    Real ib = get_base_current(vbe_limited, vbc_limited);
    Real ic = get_collector_current(vbe_limited, vbc_limited);
    Real g_be = get_base_conductance_vbe(vbe_limited, vbc_limited);       // dIb/dVbe
    Real g_bc = get_base_conductance_vbc(vbe_limited, vbc_limited);       // dIb/dVbc
    Real g_ce = get_collector_conductance_vbe(vbe_limited, vbc_limited);  // dIc/dVbe
    Real g_cc = get_collector_conductance_vbc(vbe_limited, vbc_limited);  // dIc/dVbc

    // Equivalent current sources: the linearization at the evaluated voltages
    Real ib_eq = ib - g_be * vbe_limited - g_bc * vbc_limited;
    Real ic_eq = ic - g_ce * vbe_limited - g_cc * vbc_limited;

    // Convert to MNA indices
    auto nc_idx = (nc == NODE_GND) ? -1 : static_cast<int>(nc - 1);
    auto nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);
    auto ne_idx = (ne == NODE_GND) ? -1 : static_cast<int>(ne - 1);

    spdlog::trace("{} stamp_nonlinear:", name_);
    spdlog::trace("  Voltages: Vb={:.6f}V, Vc={:.6f}V, Ve={:.6f}V", vb, vc, ve);
    spdlog::trace("  Junctions: Vbe={:.6f}V, Vbc={:.6f}V", vbe, vbc);
    spdlog::trace("  Currents: Ib={:.6e}A, Ic={:.6e}A", ib, ic);
    spdlog::trace("  Conductances: g_be={:.6e}S, g_bc={:.6e}S, g_ce={:.6e}S, g_cc={:.6e}S",
                 g_be, g_bc, g_ce, g_cc);
    spdlog::trace("  Equiv currents: Ib_eq={:.6e}A, Ic_eq={:.6e}A", ib_eq, ic_eq);
    spdlog::trace("  MNA indices: nc_idx={}, nb_idx={}, ne_idx={}", nc_idx, nb_idx, ne_idx);
    spdlog::trace("  G[nc][nc] contribution: -g_cc = {:.6e}S", -g_cc);
    spdlog::trace("  G[nc][nb] contribution: g_ce+g_cc = {:.6e}S", g_ce + g_cc);

    // Stamp Base node (row = nb_idx)
    // KCL: Ib flows OUT of base node (positive)
    // Linearization: Ib = (g_be + g_bc) * Vb - g_be * Ve - g_bc * Vc + Ib_eq
    // MNA stamp (Diode-compliant): mna.b -= i_eq for current flowing OUT
    if (nb_idx >= 0) {
        mna.G.add_entry(nb_idx, nb_idx, (g_be + g_bc));
        if (ne_idx >= 0) {
            mna.G.add_entry(nb_idx, ne_idx, -g_be);
        }
        if (nc_idx >= 0) {
            mna.G.add_entry(nb_idx, nc_idx, -g_bc);
        }
        mna.b[nb_idx] -= ib_eq;  // Fixed: Diode-compliant sign
    }

    // Stamp Collector node (row = nc_idx)
    // KCL: Ic flows OUT of collector node (positive)
    // Linearization: Ic = (g_ce + g_cc) * Vb - g_ce * Ve - g_cc * Vc + Ic_eq
    // MNA stamp (Diode-compliant): mna.b -= i_eq for current flowing OUT
    if (nc_idx >= 0) {
        if (nb_idx >= 0) {
            mna.G.add_entry(nc_idx, nb_idx, (g_ce + g_cc));
        }
        if (ne_idx >= 0) {
            mna.G.add_entry(nc_idx, ne_idx, -g_ce);
        }
        mna.G.add_entry(nc_idx, nc_idx, -g_cc);
        mna.b[nc_idx] -= ic_eq;  // Fixed: Diode-compliant sign
    }

    // Stamp Emitter node (row = ne_idx)
    // KCL: -(Ib + Ic) flows INTO emitter node (negative)
    // Linearization: -(Ib + Ic) = -(g_be + g_bc + g_ce + g_cc) * Vb + (g_be + g_ce) * Ve + (g_bc + g_cc) * Vc - (Ib_eq + Ic_eq)
    // MNA stamp (Diode-compliant): mna.b += i_eq for current flowing IN
    if (ne_idx >= 0) {
        if (nb_idx >= 0) {
            mna.G.add_entry(ne_idx, nb_idx, -(g_be + g_bc + g_ce + g_cc));
        }
        mna.G.add_entry(ne_idx, ne_idx, (g_be + g_ce));
        if (nc_idx >= 0) {
            // NOTE: Ebers-Moll model mathematically yields g_bc = -g_cc,
            // so (g_bc + g_cc) can be exactly zero. This is correct behavior.
            // The sparsity pattern is now fixed (to_csc() preserves zeros),
            // so zero values are acceptable and won't cause pattern oscillation.
            mna.G.add_entry(ne_idx, nc_idx, (g_bc + g_cc));
        }
        mna.b[ne_idx] += (ib_eq + ic_eq);  // Fixed: Diode-compliant sign
    }
}

void BJT_NPN::stamp_transient(MNASystem& mna,
                               const DenseVector& node_voltages,
                               Real timestep,
                               IntegrationMethod method,
                               const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Transient analysis with nonlinear junction capacitance
    //
    // Strategy (same as Diode, but for 2 junctions):
    // 1. Stamp DC nonlinear model (Ebers-Moll)
    // 2. Stamp B-E junction capacitance companion model (if Cje > 0)
    // 3. Stamp B-C junction capacitance companion model (if Cjc > 0)
    //
    // Companion model for each junction (linearization at previous timestep):
    //   I_C = g_eq × V + i_eq
    //
    // where g_eq and i_eq depend on the integration method:
    //   - Backward Euler: g_eq = C/Δt, i_eq = -(C/Δt) × V_prev
    //   - Trapezoidal: g_eq = 2C/Δt, i_eq = -(2C/Δt) × V_prev - I_C,prev
    //   - BDF2: g_eq, i_eq depend on fixed/variable timestep
    //   - BDF3: g_eq, i_eq depend on variable timestep coefficients
    //   - TR-BDF2: 2-stage method (complex)

    // Step 1: Stamp DC nonlinear model (Ebers-Moll)
    stamp_dc_nonlinear(mna, node_voltages);

    // Save timestep and method for accept_solution()
    timestep_ = timestep;
    last_method_ = method;

    // Save BDF coefficients if provided (variable-timestep BDF2/BDF3)
    if (bdf_coef != nullptr) {
        bdf_alpha0_ = bdf_coef->alpha0;
        bdf_alpha1_ = bdf_coef->alpha1;
        bdf_alpha2_ = bdf_coef->alpha2;
        bdf_alpha3_ = bdf_coef->alpha3;
        bdf_beta0_ = bdf_coef->beta0;
        use_bdf_var_ = true;
    } else {
        use_bdf_var_ = false;
    }

    // Get node IDs
    NodeId nc = nodes_[0];  // Collector
    NodeId nb = nodes_[1];  // Base
    NodeId ne = nodes_[2];  // Emitter

    // Get terminal voltages
    Real vc = (nc == NODE_GND) ? 0.0 : node_voltages[nc - 1];
    Real vb = (nb == NODE_GND) ? 0.0 : node_voltages[nb - 1];
    Real ve = (ne == NODE_GND) ? 0.0 : node_voltages[ne - 1];

    Real vbe = vb - ve;  // B-E junction voltage
    Real vbc = vb - vc;  // B-C junction voltage

    // Update voltage history for next iteration
    vbe_prev_ = vbe;
    vbc_prev_ = vbc;

    // Step 2: B-E junction capacitance companion model
    if (cje_ > 0.0) {
        // Evaluate junction capacitance at previous timestep voltage
        Real cje = get_junction_capacitance_be(vbe_prev_);

        // Calculate charge: Q = ∫C(V)dV (for physical LTE)
        // Approximation: Q ≈ C(V) × V (good for small changes)
        charge_be_ = cje * vbe_prev_;

        // Companion model: I_C = g_eq × V + i_eq
        Real g_eq_be = 0.0;
        Real i_eq_be = 0.0;

        // Apply integration method
        switch (method) {
        case IntegrationMethod::BACKWARD_EULER:
            // Backward Euler (BDF1): dV/dt ≈ (V_{n+1} - V_n) / Δt
            // I_C = C × dV/dt ≈ C × (V_{n+1} - V_n) / Δt
            // Linearized: I_C = (C/Δt) × V_{n+1} - (C/Δt) × V_n
            g_eq_be = cje / timestep;
            i_eq_be = -(cje / timestep) * vbe_prev_;
            break;

        case IntegrationMethod::TRAPEZOIDAL: {
            // Trapezoidal rule: dV/dt ≈ (V_{n+1} - V_n) / Δt
            // I_C,avg = (I_C,n + I_C,n+1) / 2 = C × (V_{n+1} - V_n) / Δt
            // Linearized: I_C,n+1 = (2C/Δt) × V_{n+1} - (2C/Δt) × V_n - I_C,n
            g_eq_be = 2.0 * cje / timestep;
            Real ic_prev = cje * (vbe_prev_ - vbe_prev2_) / timestep;  // Previous current
            i_eq_be = -(2.0 * cje / timestep) * vbe_prev_ - ic_prev;
            break;
        }

        case IntegrationMethod::GEAR2:
            if (use_bdf_var_) {
                // Variable-timestep BDF2: dV/dt ≈ (α₀V_{n+1} + α₁V_n + α₂V_{n-1}) / β₀
                // I_C = C × dV/dt = (C/β₀) × (α₀V_{n+1} + α₁V_n + α₂V_{n-1})
                g_eq_be = cje * bdf_alpha0_ / bdf_beta0_;
                i_eq_be = (cje / bdf_beta0_) *
                          (bdf_alpha1_ * vbe_prev_ + bdf_alpha2_ * vbe_prev2_);
            } else {
                // Fixed-timestep BDF2: dV/dt ≈ (3V_{n+1} - 4V_n + V_{n-1}) / (2Δt)
                // I_C = C × (3V_{n+1} - 4V_n + V_{n-1}) / (2Δt)
                g_eq_be = 1.5 * cje / timestep;
                i_eq_be = (-2.0 * cje / timestep) * vbe_prev_ +
                          (0.5 * cje / timestep) * vbe_prev2_;
            }
            break;

        case IntegrationMethod::GEAR3:
            // Variable-timestep BDF3: dV/dt ≈ (α₀V_{n+1} + α₁V_n + α₂V_{n-1} + α₃V_{n-2}) / β₀
            // I_C = C × dV/dt = (C/β₀) × (α₀V_{n+1} + α₁V_n + α₂V_{n-1} + α₃V_{n-2})
            if (use_bdf_var_) {
                g_eq_be = cje * bdf_alpha0_ / bdf_beta0_;
                i_eq_be = (cje / bdf_beta0_) *
                          (bdf_alpha1_ * vbe_prev_ + bdf_alpha2_ * vbe_prev2_ +
                           bdf_alpha3_ * vbe_prev3_);
            } else {
                // Fixed-timestep BDF3: dV/dt ≈ (11V_{n+1} - 18V_n + 9V_{n-1} - 2V_{n-2}) / (6Δt)
                g_eq_be = (11.0 / 6.0) * cje / timestep;
                i_eq_be = (-18.0 / 6.0 * cje / timestep) * vbe_prev_ +
                          (9.0 / 6.0 * cje / timestep) * vbe_prev2_ +
                          (-2.0 / 6.0 * cje / timestep) * vbe_prev3_;
            }
            break;

        case IntegrationMethod::TR_BDF2: {
            // TR-BDF2: 2-stage method (γ = 2 - √2 ≈ 0.5857864)
            // Stage 1 (Trapezoidal to midpoint γΔt):
            //   V_{n+γ} = V_n + (γΔt/2) × (dV/dt|_n + dV/dt|_{n+γ})
            // Stage 2 (BDF2 from midpoint to endpoint):
            //   V_{n+1} = (1/(γ(2-γ))) × V_{n+γ} - ((1-γ)²/(γ(2-γ))) × V_n
            //
            // Combined linearization:
            constexpr Real gamma = 0.5857864376269049;  // 2 - √2
            constexpr Real d = gamma * (2.0 - gamma);    // Denominator
            g_eq_be = (1.0 / d) * cje / timestep;
            i_eq_be = -(1.0 / d - (1.0 - gamma) * (1.0 - gamma) / d) *
                      (cje / timestep) * vbe_prev_;
            break;
        }

        default:
            throw std::runtime_error("BJT_NPN: Unknown integration method in stamp_transient");
        }

        // MNA stamp for B-E capacitor (Base+ → Emitter-)
        auto nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);
        auto ne_idx = (ne == NODE_GND) ? -1 : static_cast<int>(ne - 1);

        if (nb_idx >= 0) {
            mna.G.add_entry(nb_idx, nb_idx, g_eq_be);
            if (ne_idx >= 0) {
                mna.G.add_entry(nb_idx, ne_idx, -g_eq_be);
            }
            mna.b[nb_idx] -= i_eq_be;
        }
        if (ne_idx >= 0) {
            mna.G.add_entry(ne_idx, ne_idx, g_eq_be);
            if (nb_idx >= 0) {
                mna.G.add_entry(ne_idx, nb_idx, -g_eq_be);
            }
            mna.b[ne_idx] += i_eq_be;
        }

        spdlog::trace("{} B-E capacitor: Cje={:.3e}F, g_eq={:.3e}S, i_eq={:.3e}A",
                     name_, cje, g_eq_be, i_eq_be);
    }

    // Step 3: B-C junction capacitance companion model
    if (cjc_ > 0.0) {
        // Evaluate junction capacitance at previous timestep voltage
        Real cjc = get_junction_capacitance_bc(vbc_prev_);

        // Calculate charge: Q = ∫C(V)dV
        charge_bc_ = cjc * vbc_prev_;

        // Companion model: I_C = g_eq × V + i_eq
        Real g_eq_bc = 0.0;
        Real i_eq_bc = 0.0;

        // Apply integration method (same as B-E junction)
        switch (method) {
        case IntegrationMethod::BACKWARD_EULER:
            g_eq_bc = cjc / timestep;
            i_eq_bc = -(cjc / timestep) * vbc_prev_;
            break;

        case IntegrationMethod::TRAPEZOIDAL: {
            g_eq_bc = 2.0 * cjc / timestep;
            Real ic_prev = cjc * (vbc_prev_ - vbc_prev2_) / timestep;
            i_eq_bc = -(2.0 * cjc / timestep) * vbc_prev_ - ic_prev;
            break;
        }

        case IntegrationMethod::GEAR2:
            if (use_bdf_var_) {
                g_eq_bc = cjc * bdf_alpha0_ / bdf_beta0_;
                i_eq_bc = (cjc / bdf_beta0_) *
                          (bdf_alpha1_ * vbc_prev_ + bdf_alpha2_ * vbc_prev2_);
            } else {
                g_eq_bc = 1.5 * cjc / timestep;
                i_eq_bc = (-2.0 * cjc / timestep) * vbc_prev_ +
                          (0.5 * cjc / timestep) * vbc_prev2_;
            }
            break;

        case IntegrationMethod::GEAR3:
            if (use_bdf_var_) {
                g_eq_bc = cjc * bdf_alpha0_ / bdf_beta0_;
                i_eq_bc = (cjc / bdf_beta0_) *
                          (bdf_alpha1_ * vbc_prev_ + bdf_alpha2_ * vbc_prev2_ +
                           bdf_alpha3_ * vbc_prev3_);
            } else {
                g_eq_bc = (11.0 / 6.0) * cjc / timestep;
                i_eq_bc = (-18.0 / 6.0 * cjc / timestep) * vbc_prev_ +
                          (9.0 / 6.0 * cjc / timestep) * vbc_prev2_ +
                          (-2.0 / 6.0 * cjc / timestep) * vbc_prev3_;
            }
            break;

        case IntegrationMethod::TR_BDF2: {
            constexpr Real gamma = 0.5857864376269049;
            constexpr Real d = gamma * (2.0 - gamma);
            g_eq_bc = (1.0 / d) * cjc / timestep;
            i_eq_bc = -(1.0 / d - (1.0 - gamma) * (1.0 - gamma) / d) *
                      (cjc / timestep) * vbc_prev_;
            break;
        }

        default:
            throw std::runtime_error("BJT_NPN: Unknown integration method in stamp_transient");
        }

        // MNA stamp for B-C capacitor (Base+ → Collector-)
        auto nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);
        auto nc_idx = (nc == NODE_GND) ? -1 : static_cast<int>(nc - 1);

        if (nb_idx >= 0) {
            mna.G.add_entry(nb_idx, nb_idx, g_eq_bc);
            if (nc_idx >= 0) {
                mna.G.add_entry(nb_idx, nc_idx, -g_eq_bc);
            }
            mna.b[nb_idx] -= i_eq_bc;
        }
        if (nc_idx >= 0) {
            mna.G.add_entry(nc_idx, nc_idx, g_eq_bc);
            if (nb_idx >= 0) {
                mna.G.add_entry(nc_idx, nb_idx, -g_eq_bc);
            }
            mna.b[nc_idx] += i_eq_bc;
        }

        spdlog::trace("{} B-C capacitor: Cjc={:.3e}F, g_eq={:.3e}S, i_eq={:.3e}A",
                     name_, cjc, g_eq_bc, i_eq_bc);
    }
}

Real BJT_NPN::get_transconductance(Real vbe, Real vbc) const {
    // Transconductance: Gm = dIc/dVbe
    // This is the same as get_collector_conductance_vbe()
    return get_collector_conductance_vbe(vbe, vbc);
}

Real BJT_NPN::get_input_conductance(Real vbe, Real vbc) const {
    // Input conductance: Gpi = dIb/dVbe
    // This is the same as get_base_conductance_vbe()
    return get_base_conductance_vbe(vbe, vbc);
}

Real BJT_NPN::get_feedback_conductance(Real vbe, Real vbc) const {
    // Feedback conductance: Gmu = dIc/dVbc
    // This is the same as get_collector_conductance_vbc()
    // Note: This is typically negative (increasing Vbc decreases Ic)
    return get_collector_conductance_vbc(vbe, vbc);
}

Real BJT_NPN::get_input_capacitance(Real vbe, Real vbc) const {
    // Input capacitance: Cπ = Cje(Vbe) + Cdiff
    // where Cdiff = Tf × Gm (diffusion capacitance)

    Real cje_actual = 0.0;
    if (cje_ > 0.0) {
        // Junction capacitance (same formula as diode)
        constexpr Real FC = 0.5;
        Real fc_vje = FC * vje_;

        if (vbe < fc_vje) {
            // Normal region
            Real factor = 1.0 - vbe / vje_;
            if (factor <= 0.0) {
                cje_actual = cje_ * 100.0;  // Avoid singularity
            } else {
                cje_actual = cje_ / std::pow(factor, mje_);
            }
        } else {
            // Forward bias: linear extrapolation
            Real cje_fc = cje_ / std::pow(1.0 - FC, mje_);
            Real slope = mje_ * cje_fc / vje_;
            cje_actual = cje_fc + slope * (vbe - fc_vje);
        }
    }

    // Diffusion capacitance: Cdiff = Tf × Gm
    Real cdiff = 0.0;
    if (tf_ > 0.0) {
        Real gm = get_transconductance(vbe, vbc);
        cdiff = tf_ * gm;
    }

    return cje_actual + cdiff;
}

Real BJT_NPN::get_feedback_capacitance(Real vbe, Real vbc) const {
    // Feedback capacitance: Cμ = Cjc(Vbc)

    if (cjc_ == 0.0) {
        return 0.0;
    }

    // Junction capacitance (same formula as diode)
    constexpr Real FC = 0.5;
    Real fc_vjc = FC * vjc_;

    if (vbc < fc_vjc) {
        // Normal region
        Real factor = 1.0 - vbc / vjc_;
        if (factor <= 0.0) {
            return cjc_ * 100.0;  // Avoid singularity
        }
        return cjc_ / std::pow(factor, mjc_);
    } else {
        // Forward bias: linear extrapolation
        Real cjc_fc = cjc_ / std::pow(1.0 - FC, mjc_);
        Real slope = mjc_ * cjc_fc / vjc_;
        return cjc_fc + slope * (vbc - fc_vjc);
    }
}

void BJT_NPN::stamp_ac(ComplexMNASystem& sys, Real omega,
                       const DenseVector& dc_solution) const {
    // Small-signal AC analysis using hybrid-pi model:
    //
    // B --+-- Gπ --+-- E
    //     |        |
    //     Cπ       |
    //     |        |
    //     +---Gmu--+---Cμ--- C
    //     |               |
    //     +--- Gm×Vbe ----+
    //
    // where:
    //   Gπ  = input conductance (Gpi)
    //   Cπ  = input capacitance (Cje + Tf×Gm)
    //   Gmu = feedback conductance (dIc/dVbc)
    //   Cμ  = feedback capacitance (Cjc)
    //   Gm  = transconductance (dIc/dVbe)

    NodeId nc_node = nodes_[0];  // Collector
    NodeId nb_node = nodes_[1];  // Base
    NodeId ne_node = nodes_[2];  // Emitter

    // Get DC operating point voltages
    Real v_c = (nc_node == NODE_GND) ? 0.0 : dc_solution[nc_node - 1];
    Real v_b = (nb_node == NODE_GND) ? 0.0 : dc_solution[nb_node - 1];
    Real v_e = (ne_node == NODE_GND) ? 0.0 : dc_solution[ne_node - 1];

    Real vbe = v_b - v_e;
    Real vbc = v_b - v_c;

    // Calculate small-signal parameters
    Real gm = get_transconductance(vbe, vbc);
    Real gpi = get_input_conductance(vbe, vbc);
    Real gmu = get_feedback_conductance(vbe, vbc);
    Real cpi = get_input_capacitance(vbe, vbc);
    Real cmu = get_feedback_capacitance(vbe, vbc);

    spdlog::trace("{} stamp_ac: Vbe={:.6f}V, Vbc={:.6f}V, Gm={:.6e}S, Gπ={:.6e}S, Gμ={:.6e}S, Cπ={:.6e}F, Cμ={:.6e}F, ω={:.3e}rad/s",
                 name_, vbe, vbc, gm, gpi, gmu, cpi, cmu, omega);

    // Convert to MNA indices
    auto nc_idx = (nc_node == NODE_GND) ? -1 : static_cast<int>(nc_node - 1);
    auto nb_idx = (nb_node == NODE_GND) ? -1 : static_cast<int>(nb_node - 1);
    auto ne_idx = (ne_node == NODE_GND) ? -1 : static_cast<int>(ne_node - 1);

    // Admittances
    Complex ypi(gpi, omega * cpi);   // B-E admittance
    Complex ymu(gmu, omega * cmu);   // B-C admittance (feedback)
    Complex gmvbe(gm, 0.0);          // Controlled current source (Gm×Vbe)

    // Stamp B-E admittance (Gπ + jωCπ)
    if (nb_idx >= 0) {
        sys.Y.add_entry(nb_idx, nb_idx, ypi);
        if (ne_idx >= 0) {
            sys.Y.add_entry(nb_idx, ne_idx, -ypi);
        }
    }
    if (ne_idx >= 0) {
        sys.Y.add_entry(ne_idx, ne_idx, ypi);
        if (nb_idx >= 0) {
            sys.Y.add_entry(ne_idx, nb_idx, -ypi);
        }
    }

    // Stamp B-C admittance (Gμ + jωCμ)
    if (nb_idx >= 0) {
        sys.Y.add_entry(nb_idx, nb_idx, ymu);
        if (nc_idx >= 0) {
            sys.Y.add_entry(nb_idx, nc_idx, -ymu);
        }
    }
    if (nc_idx >= 0) {
        sys.Y.add_entry(nc_idx, nc_idx, ymu);
        if (nb_idx >= 0) {
            sys.Y.add_entry(nc_idx, nb_idx, -ymu);
        }
    }

    // Stamp controlled current source: Ic = Gm × Vbe
    // Standard VCCS stamp (ngspice-compatible):
    // Ic flows OUT of collector → MNA convention requires POSITIVE Gm
    // Y[collector, base] = +Gm (matches ngspice: -gmu+gm where gmu is negative)
    if (nc_idx >= 0) {
        if (nb_idx >= 0) {
            sys.Y.add_entry(nc_idx, nb_idx, +gmvbe);  // +Gm × Vb
        }
        if (ne_idx >= 0) {
            sys.Y.add_entry(nc_idx, ne_idx, -gmvbe);  // -Gm × Ve
        }
    }
    if (ne_idx >= 0) {
        if (nb_idx >= 0) {
            sys.Y.add_entry(ne_idx, nb_idx, -gmvbe);  // -Gm × Vb
        }
        sys.Y.add_entry(ne_idx, ne_idx, +gmvbe);  // +Gm × Ve
    }

    // No current source contribution (linearized at DC operating point)
}

std::vector<NoiseSource> BJT_NPN::get_noise_sources(const DenseVector& dc_solution,
                                                      Real frequency) const {
    // BJT noise sources:
    // 1. Shot noise from base current: I_n,B² = 2qI_B (A²/Hz)
    // 2. Shot noise from collector current: I_n,C² = 2qI_C (A²/Hz)
    // 3. Flicker noise: I_n,f² = K_f * I_C^AF / f^FF (A²/Hz)
    //
    // Physical constants:
    // - q = 1.602176634e-19 C (elementary charge)
    // - I_B, I_C = DC base/collector currents [A]
    //
    // Derivation:
    // - Shot noise arises from quantization of charge carriers
    // - Flicker noise (1/f noise) dominates at low frequencies
    // - Both are uncorrelated noise sources

    std::vector<NoiseSource> sources;

    // Get DC operating point voltages
    NodeId collector_node = nodes_[0];
    NodeId base_node = nodes_[1];
    NodeId emitter_node = nodes_[2];

    Real V_c = (collector_node == NODE_GND) ? 0.0 : dc_solution[collector_node - 1];
    Real V_b = (base_node == NODE_GND) ? 0.0 : dc_solution[base_node - 1];
    Real V_e = (emitter_node == NODE_GND) ? 0.0 : dc_solution[emitter_node - 1];

    Real V_be = V_b - V_e;  // Base-emitter voltage
    Real V_bc = V_b - V_c;  // Base-collector voltage

    // Calculate DC currents
    Real I_b = get_base_current(V_be, V_bc);
    Real I_c = get_collector_current(V_be, V_bc);

    // Shot noise is only significant for forward-active region (I_B > 0, I_C > 0)
    constexpr Real I_threshold = 1e-15;  // 1fA

    // Source 1: Base current shot noise
    if (std::abs(I_b) > I_threshold) {
        Real I_n_B_squared = 2.0 * q_electron * std::abs(I_b);

        NoiseSource shot_base;
        shot_base.name = name_ + "_shot_ib";
        shot_base.node_p = base_node;
        shot_base.node_n = emitter_node;
        shot_base.spectral_density = I_n_B_squared;
        shot_base.is_current = true;

        sources.push_back(shot_base);

        spdlog::trace("BJT {} base shot noise: I_n² = {:.3e} A²/Hz (I_B = {:.3e} A)",
                     name_, I_n_B_squared, I_b);
    }

    // Source 2: Collector current shot noise
    if (std::abs(I_c) > I_threshold) {
        Real I_n_C_squared = 2.0 * q_electron * std::abs(I_c);

        NoiseSource shot_collector;
        shot_collector.name = name_ + "_shot_ic";
        shot_collector.node_p = collector_node;
        shot_collector.node_n = emitter_node;
        shot_collector.spectral_density = I_n_C_squared;
        shot_collector.is_current = true;

        sources.push_back(shot_collector);

        spdlog::trace("BJT {} collector shot noise: I_n² = {:.3e} A²/Hz (I_C = {:.3e} A)",
                     name_, I_n_C_squared, I_c);
    }

    // Source 3: Flicker noise (1/f noise)
    // Only significant if K_f > 0 and I_C > threshold and f > 0
    // Guard against overflow: clamp minimum frequency to prevent huge values when ff_ < 0
    const Real min_frequency = 1e-3; // 1 mHz minimum to avoid overflow
    if (kf_ > 0.0 && std::abs(I_c) > I_threshold && frequency > 0.0) {
        // I_n,f² = K_f * I_C^AF / f^FF (A²/Hz)
        const Real freq_safe = std::max(frequency, min_frequency);
        Real I_n_f_squared = kf_ * std::pow(std::abs(I_c), af_) / std::pow(freq_safe, ff_);
        // Guard against non-finite results
        if (!std::isfinite(I_n_f_squared)) {
            I_n_f_squared = 0.0;
        }

        NoiseSource flicker;
        flicker.name = name_ + "_flicker";
        flicker.node_p = collector_node;
        flicker.node_n = emitter_node;
        flicker.spectral_density = I_n_f_squared;
        flicker.is_current = true;

        sources.push_back(flicker);

        spdlog::trace("BJT {} flicker noise: I_n² = {:.3e} A²/Hz (I_C = {:.3e} A, f = {:.3e} Hz)",
                     name_, I_n_f_squared, I_c, frequency);
    }

    return sources;
}

// Junction capacitance calculation (nonlinear capacitance)

Real BJT_NPN::get_junction_capacitance_be(Real vbe) const {
    // B-E junction capacitance (same formula as diode):
    // Cj(V) = Cj0 / (1 - V/Vj)^m  for V < FC*Vj
    // Cj(V) = Cj_FC + (V - FC*Vj) * dCj/dV  for V >= FC*Vj (linear extrapolation)
    //
    // where FC = 0.5 (forward-bias coefficient)

    if (cje_ == 0.0) {
        return 0.0;
    }

    constexpr Real FC = 0.5;
    Real fc_vje = FC * vje_;

    if (vbe < fc_vje) {
        // Normal region: Cj(V) = Cj0 / (1 - V/Vj)^m
        Real factor = 1.0 - vbe / vje_;
        if (factor <= 0.0) {
            return cje_ * 100.0;  // Avoid singularity (high capacitance)
        }
        return cje_ / std::pow(factor, mje_);
    } else {
        // Forward bias: linear extrapolation
        // Cj_FC = Cj0 / (1 - FC)^m
        Real cje_fc = cje_ / std::pow(1.0 - FC, mje_);
        // dCj/dV = m * Cj_FC / Vj
        Real slope = mje_ * cje_fc / vje_;
        return cje_fc + slope * (vbe - fc_vje);
    }
}

Real BJT_NPN::get_junction_capacitance_bc(Real vbc) const {
    // B-C junction capacitance (same formula as diode):
    // Cj(V) = Cj0 / (1 - V/Vj)^m  for V < FC*Vj
    // Cj(V) = Cj_FC + (V - FC*Vj) * dCj/dV  for V >= FC*Vj (linear extrapolation)

    if (cjc_ == 0.0) {
        return 0.0;
    }

    constexpr Real FC = 0.5;
    Real fc_vjc = FC * vjc_;

    if (vbc < fc_vjc) {
        // Normal region: Cj(V) = Cj0 / (1 - V/Vj)^m
        Real factor = 1.0 - vbc / vjc_;
        if (factor <= 0.0) {
            return cjc_ * 100.0;  // Avoid singularity (high capacitance)
        }
        return cjc_ / std::pow(factor, mjc_);
    } else {
        // Forward bias: linear extrapolation
        // Cj_FC = Cj0 / (1 - FC)^m
        Real cjc_fc = cjc_ / std::pow(1.0 - FC, mjc_);
        // dCj/dV = m * Cj_FC / Vj
        Real slope = mjc_ * cjc_fc / vjc_;
        return cjc_fc + slope * (vbc - fc_vjc);
    }
}

// State management for transient analysis

void BJT_NPN::save_state() {
    vbe_saved_ = vbe_prev_;
    vbe_saved2_ = vbe_prev2_;
    vbe_saved3_ = vbe_prev3_;
    vbc_saved_ = vbc_prev_;
    vbc_saved2_ = vbc_prev2_;
    vbc_saved3_ = vbc_prev3_;
    timestep_saved_ = timestep_;
    method_saved_ = last_method_;

    spdlog::trace("{} save_state: vbe={:.6f}V, vbc={:.6f}V, timestep={:.3e}s",
                 name_, vbe_prev_, vbc_prev_, timestep_);
}

void BJT_NPN::restore_state() {
    vbe_prev_ = vbe_saved_;
    vbe_prev2_ = vbe_saved2_;
    vbe_prev3_ = vbe_saved3_;
    vbc_prev_ = vbc_saved_;
    vbc_prev2_ = vbc_saved2_;
    vbc_prev3_ = vbc_saved3_;
    timestep_ = timestep_saved_;
    last_method_ = method_saved_;

    spdlog::trace("{} restore_state: vbe={:.6f}V, vbc={:.6f}V, timestep={:.3e}s",
                 name_, vbe_prev_, vbc_prev_, timestep_);
}

void BJT_NPN::accept_solution(const DenseVector& node_voltages) {
    // Update voltage history for multi-step integration methods
    // Shift: V(t-2Δt) ← V(t-Δt) ← V(t)

    (void)node_voltages;  // Suppress unused parameter warning (voltage already saved in stamp_transient)

    if (first_accept_) {
        // First call: just mark as initialized
        first_accept_ = false;
        spdlog::trace("{} accept_solution: first call (timestep={:.3e}s)", name_, timestep_);
        return;
    }

    // Shift B-E voltage history
    vbe_prev3_ = vbe_prev2_;
    vbe_prev2_ = vbe_prev_;
    // vbe_prev_ is already set in stamp_transient() for next iteration

    // Shift B-C voltage history
    vbc_prev3_ = vbc_prev2_;
    vbc_prev2_ = vbc_prev_;
    // vbc_prev_ is already set in stamp_transient() for next iteration

    // Update charge for physical LTE calculation
    charge_be_prev_ = charge_be_;
    charge_bc_prev_ = charge_bc_;

    spdlog::trace("{} accept_solution: vbe_prev={:.6f}V, vbe_prev2={:.6f}V, vbe_prev3={:.6f}V, "
                 "vbc_prev={:.6f}V, vbc_prev2={:.6f}V, vbc_prev3={:.6f}V",
                 name_, vbe_prev_, vbe_prev2_, vbe_prev3_, vbc_prev_, vbc_prev2_, vbc_prev3_);
}

Real BJT_NPN::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    // Physical local truncation error (charge-based):
    // LTE = |Q_BE(t+Δt) - Q_BE(t)| / chgtol + |Q_BC(t+Δt) - Q_BC(t)| / chgtol
    //
    // This provides adaptive timestepping based on charge conservation.

    (void)node_voltages;  // Suppress unused parameter warning (charge already calculated in stamp_transient)

    Real lte_be = std::abs(charge_be_ - charge_be_prev_) / chgtol;
    Real lte_bc = std::abs(charge_bc_ - charge_bc_prev_) / chgtol;
    Real total_lte = lte_be + lte_bc;

    spdlog::trace("{} physical_lte: lte_be={:.3e}, lte_bc={:.3e}, total={:.3e} (chgtol={:.3e}C)",
                 name_, lte_be, lte_bc, total_lte, chgtol);

    return total_lte;
}

// ========== Sensitivity Analysis: Parameter Access ==========

std::optional<Real> BJT_NPN::get_parameter(const std::string& param_name) const {
    if (param_name == "IS" || param_name == "is") {
        return is_;
    } else if (param_name == "BF" || param_name == "bf" || param_name == "BETA_F" || param_name == "beta_f") {
        return beta_f_;
    } else if (param_name == "BR" || param_name == "br" || param_name == "BETA_R" || param_name == "beta_r") {
        return beta_r_;
    } else if (param_name == "NF" || param_name == "nf") {
        return nf_;
    } else if (param_name == "NR" || param_name == "nr") {
        return nr_;
    } else if (param_name == "CJE" || param_name == "cje") {
        return cje_;
    } else if (param_name == "CJC" || param_name == "cjc") {
        return cjc_;
    }
    return std::nullopt;  // Parameter not found
}

bool BJT_NPN::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "IS" || param_name == "is") {
        if (value <= 0.0) {
            spdlog::warn("BJT_NPN {}: IS must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        is_ = value;
        // Update isc_ if it was set to default (same as Is)
        if (isc_ == is_) {
            isc_ = value;
        }
        spdlog::trace("BJT_NPN {}: set IS = {} A", name_, is_);
        return true;
    } else if (param_name == "BF" || param_name == "bf" || param_name == "BETA_F" || param_name == "beta_f") {
        if (value <= 0.0) {
            spdlog::warn("BJT_NPN {}: BF must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        beta_f_ = value;
        spdlog::trace("BJT_NPN {}: set BF = {}", name_, beta_f_);
        return true;
    } else if (param_name == "BR" || param_name == "br" || param_name == "BETA_R" || param_name == "beta_r") {
        if (value <= 0.0) {
            spdlog::warn("BJT_NPN {}: BR must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        beta_r_ = value;
        spdlog::trace("BJT_NPN {}: set BR = {}", name_, beta_r_);
        return true;
    } else if (param_name == "NF" || param_name == "nf") {
        if (value <= 0.0) {
            spdlog::warn("BJT_NPN {}: NF must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        nf_ = value;
        spdlog::trace("BJT_NPN {}: set NF = {}", name_, nf_);
        return true;
    } else if (param_name == "NR" || param_name == "nr") {
        if (value <= 0.0) {
            spdlog::warn("BJT_NPN {}: NR must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        nr_ = value;
        spdlog::trace("BJT_NPN {}: set NR = {}", name_, nr_);
        return true;
    } else if (param_name == "CJE" || param_name == "cje") {
        if (value < 0.0) {
            spdlog::warn("BJT_NPN {}: CJE must be non-negative (got {}), not updated",
                        name_, value);
            return false;
        }
        cje_ = value;
        spdlog::trace("BJT_NPN {}: set CJE = {} F", name_, cje_);
        return true;
    } else if (param_name == "CJC" || param_name == "cjc") {
        if (value < 0.0) {
            spdlog::warn("BJT_NPN {}: CJC must be non-negative (got {}), not updated",
                        name_, value);
            return false;
        }
        cjc_ = value;
        spdlog::trace("BJT_NPN {}: set CJC = {} F", name_, cjc_);
        return true;
    }
    return false;  // Parameter not found
}

std::vector<std::string> BJT_NPN::get_perturbable_parameters() const {
    return {"IS", "BF", "BR", "NF"};  // Main parameters for sensitivity analysis
}

} // namespace kirimaspice::devices
