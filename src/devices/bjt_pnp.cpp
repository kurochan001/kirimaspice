// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/bjt_pnp.cpp
#include "kirimaspice/devices/bjt_pnp.hpp"
#include "kirimaspice/core/constants.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

BJT_PNP::BJT_PNP(DeviceId id, const std::string& name,
                 NodeId collector, NodeId base, NodeId emitter,
                 Real is, Real beta_f, Real nf, Real isc, Real beta_r, Real nr,
                 Real vaf, Real var, Real ikf, Real ikr,
                 Real rb, Real rbm, Real irb,
                 Real ise, Real ne, Real isc_leak, Real nc,
                 Real cje, Real vje, Real mje, Real cjc, Real vjc, Real mjc,
                 Real tf, Real tr, Real kf, Real af, Real ff,
                 Real xti, Real xtb, Real eg, Real tnom)
    : Device(id, name, DeviceType::BJT_PNP)
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
    , ise_(ise)  // E-B leakage saturation current
    , ne_(ne)    // E-B leakage emission coefficient
    , isc_leak_(isc_leak)  // C-B leakage saturation current
    , nc_(nc)    // C-B leakage emission coefficient
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
    , veb_prev_(0.0)
    , veb_prev2_(0.0)
    , veb_prev3_(0.0)
    , veb_saved_(0.0)
    , veb_saved2_(0.0)
    , veb_saved3_(0.0)
    , charge_eb_(0.0)
    , charge_eb_prev_(0.0)
    , vcb_prev_(0.0)
    , vcb_prev2_(0.0)
    , vcb_prev3_(0.0)
    , vcb_saved_(0.0)
    , vcb_saved2_(0.0)
    , vcb_saved3_(0.0)
    , charge_cb_(0.0)
    , charge_cb_prev_(0.0)
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
        throw std::runtime_error("BJT E-B leakage emission coefficient must be positive: " + name);
    }
    if (nc <= 0.0) {
        throw std::runtime_error("BJT C-B leakage emission coefficient must be positive: " + name);
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

    spdlog::debug("Created BJT_PNP {} (C={}, B={}, E={}, Is={:.6e}A, βF={:.1f}, Cje={:.6e}F, Cjc={:.6e}F)",
                  name, collector, base, emitter, is_, beta_f_, cje_, cjc_);
}

void BJT_PNP::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }

    // Update thermal voltage using utility function
    vt_ = core::compute_thermal_voltage(temp_kelvin);

    // Update saturation currents IS(T), ISC(T), ISE(T), ISC_leak(T)
    // SPICE equation: I_s(T) = I_s(Tnom) * (T/Tnom)^(XTI/N) * exp(-EG/N * (1/Vt(T) - 1/Vt(Tnom)))
    // Using utility function for temperature scaling

    // IS(T): E-B saturation current
    is_ = core::scale_saturation_current(is_tnom_, xti_, eg_, nf_, temp_kelvin, tnom_);

    // ISC(T): C-B saturation current
    isc_ = core::scale_saturation_current(isc_tnom_, xti_, eg_, nr_, temp_kelvin, tnom_);

    // ISE(T): E-B leakage saturation current (if present)
    if (ise_tnom_ > 0.0) {
        ise_ = core::scale_saturation_current(ise_tnom_, xti_, eg_, ne_, temp_kelvin, tnom_);
    }

    // ISC_leak(T): C-B leakage saturation current (if present)
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

Real BJT_PNP::get_base_current(Real veb, Real vcb) const {
    // Gummel-Poon model for PNP with non-ideal currents:
    // Ib = (Is/βF) × (exp(Veb/(NF*Vt)) - 1) + (Isc/βR) × (exp(Vcb/(NR*Vt)) - 1)
    //    + ISE × (exp(Veb/(NE*Vt)) - 1) + ISC × (exp(Vcb/(NC*Vt)) - 1)
    //
    // Note: Base current does NOT include high-current effects (q1/q2).
    // High-current effects are applied only to collector current via qb factor.
    // This allows beta = Ic/Ib = beta_f / qb to decrease when q1 < 1.
    //
    // For PNP, forward bias is Veb > 0 (emitter-base junction)

    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;
    Real ne_vt = ne_ * vt_;
    Real nc_vt = nc_ * vt_;

    // Ideal base current components
    Real i_forward = core::safe_exp_junction(veb, nf_vt, is_ / beta_f_);
    Real i_reverse = core::safe_exp_junction(vcb, nr_vt, isc_ / beta_r_);

    // Non-ideal (leakage) current components
    Real i_eb_leak = core::safe_exp_junction(veb, ne_vt, ise_);
    Real i_cb_leak = core::safe_exp_junction(vcb, nc_vt, isc_leak_);

    return i_forward + i_reverse + i_eb_leak + i_cb_leak;
}

Real BJT_PNP::get_collector_current(Real veb, Real vcb) const {
    // Gummel-Poon model for PNP with base-width modulation (Early effect):
    // Ic = Is * (exp(Veb/(NF*Vt)) - 1) / qb - (Isc/βR) * (exp(Vcb/(NR*Vt)) - 1) / qbr
    // where:
    //   qb  = q1 × (1 - Veb/VAR + Vcb/VAF)  (forward base-width modulation)
    //   qbr = q2 × (1 - Vcb/VAR + Veb/VAF)  (reverse base-width modulation)
    //   q1  = 1 / sqrt(1 + Ic/IKF)  (forward high-current roll-off)
    //   q2  = 1 / sqrt(1 + Ie/IKR)  (reverse high-current roll-off)
    //
    // Note: Positive current flows OUT of collector for PNP (opposite of NPN)
    // Note: qb is in the DENOMINATOR (not numerator), making Ic INCREASE with Vce

    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;

    Real i_forward = core::safe_exp_junction(veb, nf_vt, is_);
    Real i_reverse = core::safe_exp_junction(vcb, nr_vt, isc_ / beta_r_);

    // High-current coefficient q1
    // Gummel-Poon q1 factor: q1 = sqrt(1 + Ic/IKF)
    // Note: q1 > 1 at high currents, making qb larger and Ic smaller (beta roll-off)
    Real q1 = 1.0;
    if (ikf_ > 0.0) {
        Real ic_approx = std::abs(i_forward);
        q1 = std::sqrt(1.0 + ic_approx / ikf_);
    }

    // Gummel-Poon base-width modulation factor (Early effect)
    // qb = q1 × (1 - Veb/VAR + Vcb/VAF)
    Real qb = q1;
    if (vaf_ > 0.0 || var_ > 0.0) {
        Real modulation = 1.0;
        if (vaf_ > 0.0) modulation += vcb / vaf_;  // Note: +Vcb
        if (var_ > 0.0) modulation -= veb / var_;
        if (modulation < 0.01) modulation = 0.01;  // Clamp to prevent division by zero
        qb = q1 * modulation;
    }

    // High-current coefficient q2
    // q2 = sqrt(1 + Ie/IKR)
    Real q2 = 1.0;
    if (ikr_ > 0.0) {
        Real ie_approx = std::abs(i_forward);
        q2 = std::sqrt(1.0 + ie_approx / ikr_);
    }

    Real qbr = q2;
    if (var_ > 0.0 || vaf_ > 0.0) {
        Real modulation = 1.0;
        if (vaf_ > 0.0) modulation += veb / vaf_;
        if (var_ > 0.0) modulation -= vcb / var_;
        if (modulation < 0.01) modulation = 0.01;
        qbr = q2 * modulation;
    }

    // Ic = If/qb - Ir/qbr (qb in denominator)
    return i_forward / qb - i_reverse / qbr;
}

Real BJT_PNP::get_base_resistance(Real veb, Real vcb) const {
    // Gummel-Poon base resistance model:
    // R_b = (RBM + (RB - RBM) × q_b) / (1 + (I_b / IRB)²)
    //
    // Same model as NPN, but using PNP voltage conventions (Veb, Vcb)

    if (rb_ <= 0.0) {
        return 0.0;  // No base resistance
    }

    // Calculate base-width modulation factor q_b (same as in get_collector_current)
    Real qb = 1.0;

    // Forward high-current roll-off: q1 = sqrt(1 + Ic/IKF)
    Real q1 = 1.0;
    if (ikf_ > 0.0) {
        Real nf_vt = nf_ * vt_;
        Real i_forward = core::safe_exp_junction(veb, nf_vt, is_);
        Real ic_approx = std::abs(i_forward);
        q1 = std::sqrt(1.0 + ic_approx / ikf_);
    }

    // Early effect modulation: (1 - Veb/VAR + Vcb/VAF)
    if (vaf_ > 0.0 || var_ > 0.0) {
        Real modulation = 1.0;
        if (vaf_ > 0.0) modulation += vcb / vaf_;
        if (var_ > 0.0) modulation -= veb / var_;

        // Clamp to prevent negative or very small values
        if (modulation < 0.01) {
            modulation = 0.01;
        }
        qb = q1 * modulation;
    } else {
        qb = q1;
    }

    // Calculate base current
    Real ib = get_base_current(veb, vcb);
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

Real BJT_PNP::get_base_conductance_veb(Real veb, Real vcb) const {
    // g_eb = dIb/dVeb = (Is/(βF*NF*Vt)) * exp(Veb/(NF*Vt))
    //                  + (ISE/(NE*Vt)) * exp(Veb/(NE*Vt))  (non-ideal current)
    Real nf_vt = nf_ * vt_;
    Real ne_vt = ne_ * vt_;

    Real g_ideal = core::safe_exp_conductance(veb, nf_vt, is_ / (beta_f_ * nf_vt), gmin_default);
    Real g_leak = core::safe_exp_conductance(veb, ne_vt, ise_ / ne_vt, 0.0);  // No gmin for leakage

    return g_ideal + g_leak;
}

Real BJT_PNP::get_base_conductance_vcb(Real veb, Real vcb) const {
    // g_cb = dIb/dVcb = (Isc/(βR*NR*Vt)) * exp(Vcb/(NR*Vt))
    //                  + (ISC_leak/(NC*Vt)) * exp(Vcb/(NC*Vt))  (non-ideal current)
    // Use 1.001*gmin to break symmetry with g_cc and avoid singular Jacobian
    Real nr_vt = nr_ * vt_;
    Real nc_vt = nc_ * vt_;

    Real g_ideal = core::safe_exp_conductance(vcb, nr_vt, isc_ / (beta_r_ * nr_vt), 1.001 * gmin_default);
    Real g_leak = core::safe_exp_conductance(vcb, nc_vt, isc_leak_ / nc_vt, 0.0);  // No gmin for leakage

    return g_ideal + g_leak;
}

Real BJT_PNP::get_collector_conductance_veb(Real veb, Real vcb) const {
    // g_ec = dIc/dVeb with Early effect
    // Ic = If * (1 + Vce/VAF) - Ir * (1 + Vbc/VAR)
    // Where: Vce = Veb - Vcb, If = Is * (exp(Veb/(NF*Vt)) - 1)
    //
    // dIc/dVeb = dIf/dVeb * (1 + Vce/VAF) + If * (1/VAF) * dVce/dVeb
    //          = [Is/(NF*Vt) * exp(Veb/(NF*Vt))] * (1 + Vce/VAF) + If * (1/VAF)
    //
    // Note: dVce/dVeb = 1, dVbc/dVeb = 0

    Real nf_vt = nf_ * vt_;

    // Base conductance without Early effect
    Real g_base = core::safe_exp_conductance(veb, nf_vt, is_ / nf_vt, gmin_default);

    if (vaf_ <= 0.0) {
        return g_base;  // No Early effect
    }

    // Early effect modulation
    Real vce = veb - vcb;
    Real early_forward = 1.0 + vce / vaf_;
    if (early_forward < 0.1) early_forward = 0.1;
    if (early_forward > 10.0) early_forward = 10.0;

    // Forward current for second term
    Real i_forward = core::safe_exp_junction(veb, nf_vt, is_);

    return g_base * early_forward + i_forward / vaf_;
}

Real BJT_PNP::get_collector_conductance_vcb(Real veb, Real vcb) const {
    // g_cc = dIc/dVcb with Early effect
    // Ic = If * (1 + Vce/VAF) - Ir * (1 + Vbc/VAR)
    // Where: Vce = Veb - Vcb, Vbc = -Vcb, Ir = Isc/βR * (exp(Vcb/(NR*Vt)) - 1)
    //
    // dIc/dVcb = If * (1/VAF) * dVce/dVcb - [dIr/dVcb * (1 + Vbc/VAR) + Ir * (1/VAR) * dVbc/dVcb]
    //          = -If/VAF - [Isc/(βR*NR*Vt) * exp(Vcb/(NR*Vt))] * (1 + Vbc/VAR) + Ir/VAR
    //
    // Note: dVce/dVcb = -1, dVbc/dVcb = -1

    Real nr_vt = nr_ * vt_;

    // Base conductance without Early effect (negative because Ic = If - Ir)
    Real g_base = -core::safe_exp_conductance(vcb, nr_vt, isc_ / (beta_r_ * nr_vt), gmin_default);

    Real result = g_base;

    // Add Early effect terms
    if (vaf_ > 0.0) {
        Real nf_vt = nf_ * vt_;
        Real i_forward = core::safe_exp_junction(veb, nf_vt, is_);
        result -= i_forward / vaf_;
    }

    if (var_ > 0.0) {
        Real vbc = -vcb;
        Real early_reverse = 1.0 + vbc / var_;
        if (early_reverse < 0.1) early_reverse = 0.1;
        if (early_reverse > 10.0) early_reverse = 10.0;

        // Apply Early effect modulation to reverse conductance
        result *= early_reverse;

        // Add second term
        Real i_reverse = core::safe_exp_junction(vcb, nr_vt, isc_ / beta_r_);
        result += i_reverse / var_;
    }

    return result;
}

void BJT_PNP::stamp_gmin(MNASystem& mna) const {
    // Add small conductance gmin between each terminal pair for numerical stability
    // This prevents singular matrix when BJT is in cut-off

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

void BJT_PNP::stamp_dc_nonlinear(MNASystem& mna,
                                 const DenseVector& node_voltages) const {
    // Newton-Raphson linearization of full Ebers-Moll model for PNP:
    //
    // Base current:      Ib = (Is/βF) * (exp(Veb/(NF*Vt)) - 1) + (Isc/βR) * (exp(Vcb/(NR*Vt)) - 1)
    // Collector current: Ic = Is * (exp(Veb/(NF*Vt)) - 1) - (Isc/βR) * (exp(Vcb/(NR*Vt)) - 1)
    //
    // Linearization:
    //   Ib = g_eb * Veb + g_cb * Vcb + Ib_eq
    //   Ic = g_ec * Veb + g_cc * Vcb + Ic_eq
    //
    // Where:
    //   g_eb = dIb/dVeb, g_cb = dIb/dVcb
    //   g_ec = dIc/dVeb, g_cc = dIc/dVcb
    //   Ib_eq = Ib - g_eb * Veb - g_cb * Vcb
    //   Ic_eq = Ic - g_ec * Veb - g_cc * Vcb
    //
    // MNA stamps (KCL: current OUT of node is positive):
    //   Base node:      Ib = (g_eb + g_cb) * Vb - g_eb * Ve - g_cb * Vc + Ib_eq
    //   Collector node: Ic = (g_ec + g_cc) * Vb - g_ec * Ve - g_cc * Vc + Ic_eq
    //   Emitter node:   -(Ib + Ic) = -(g_eb + g_cb + g_ec + g_cc) * Vb + (g_eb + g_ec) * Ve + (g_cb + g_cc) * Vc - (Ib_eq + Ic_eq)

    NodeId nc = nodes_[0];  // Collector
    NodeId nb = nodes_[1];  // Base
    NodeId ne = nodes_[2];  // Emitter

    // Get terminal voltages
    // node_voltages is indexed from 0, but NodeId starts from 1 (0=GND)
    Real vc = (nc == NODE_GND) ? 0.0 : node_voltages[nc - 1];
    Real vb = (nb == NODE_GND) ? 0.0 : node_voltages[nb - 1];
    Real ve = (ne == NODE_GND) ? 0.0 : node_voltages[ne - 1];

    // APPROACH 2: ngspice-style type multiplier method
    // Reference: ngspice/src/spicelib/devices/bjt/bjtload.c
    //
    // Key insight: Use type = -1 for PNP to reverse junction voltage polarity,
    // then use IDENTICAL NPN formulas for currents and conductances.
    // This unifies NPN/PNP implementation through mathematical sign convention.
    //
    // For PNP: vbe = type * (vb - ve) = -(vb - ve) = ve - vb
    //          vbc = type * (vb - vc) = -(vb - vc) = vc - vb

    constexpr Real type = -1.0;  // PNP type multiplier (NPN = +1.0)

    // Junction voltages with type multiplier (ngspice method)
    Real vbe = type * (vb - ve);  // For PNP: vbe = ve - vb
    Real vbc = type * (vb - vc);  // For PNP: vbc = vc - vb

    // Calculate currents using NPN formulas (Ebers-Moll model)
    // Base current: Ib = Is/βF * (exp(Vbe/(NF*Vt)) - 1) + Isc/βR * (exp(Vbc/(NR*Vt)) - 1)
    // Collector current: Ic = Is * (exp(Vbe/(NF*Vt)) - 1) - Isc/βR * (exp(Vbc/(NR*Vt)) - 1)

    Real nf_vt = nf_ * vt_;
    Real nr_vt = nr_ * vt_;

    // Use named constants for overflow/cutoff limits
    // Base-Emitter exponential term
    Real exp_vbe = 0.0;
    if (vbe < core::JUNCTION_CUTOFF_FACTOR * nf_vt) {
        exp_vbe = 0.0;  // Deep cut-off
    } else if (vbe / nf_vt > core::EXP_OVERFLOW_LIMIT) {
        exp_vbe = std::exp(core::EXP_OVERFLOW_LIMIT);  // Limit exponential
    } else {
        exp_vbe = std::exp(vbe / nf_vt);
    }

    // Base-Collector exponential term
    Real exp_vbc = 0.0;
    if (vbc < core::JUNCTION_CUTOFF_FACTOR * nr_vt) {
        exp_vbc = 0.0;  // Deep cut-off
    } else if (vbc / nr_vt > core::EXP_OVERFLOW_LIMIT) {
        exp_vbc = std::exp(core::EXP_OVERFLOW_LIMIT);  // Limit exponential
    } else {
        exp_vbc = std::exp(vbc / nr_vt);
    }

    // Currents (NPN formulas)
    Real ib = (is_ / beta_f_) * (exp_vbe - 1.0) + (isc_ / beta_r_) * (exp_vbc - 1.0);
    Real ic = is_ * (exp_vbe - 1.0) - (isc_ / beta_r_) * (exp_vbc - 1.0);

    // Conductances (NPN formulas)
    Real g_be = std::max((is_ / (beta_f_ * nf_vt)) * exp_vbe, gmin_default);
    Real g_bc = std::max((isc_ / (beta_r_ * nr_vt)) * exp_vbc, 1.001 * gmin_default);
    Real g_ce = std::max((is_ / nf_vt) * exp_vbe, gmin_default);
    Real g_cc = -std::max((isc_ / (beta_r_ * nr_vt)) * exp_vbc, 1.001 * gmin_default);

    // Equivalent current sources (apply type multiplier)
    Real ib_eq = type * (ib - g_be * vbe - g_bc * vbc);
    Real ic_eq = type * (ic - g_ce * vbe - g_cc * vbc);

    // Convert to MNA indices
    auto nc_idx = (nc == NODE_GND) ? -1 : static_cast<int>(nc - 1);
    auto nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);
    auto ne_idx = (ne == NODE_GND) ? -1 : static_cast<int>(ne - 1);

    spdlog::trace("{} stamp_nonlinear (Approach 2: ngspice type multiplier):", name_);
    spdlog::trace("  Voltages: Vb={:.6f}V, Vc={:.6f}V, Ve={:.6f}V", vb, vc, ve);
    spdlog::trace("  Junctions (type*V): Vbe={:.6f}V, Vbc={:.6f}V", vbe, vbc);
    spdlog::trace("  Currents (NPN-style): Ib={:.6e}A, Ic={:.6e}A", ib, ic);
    spdlog::trace("  Conductances: g_be={:.6e}S, g_bc={:.6e}S, g_ce={:.6e}S, g_cc={:.6e}S",
                 g_be, g_bc, g_ce, g_cc);
    spdlog::trace("  Equiv currents (type*I_eq): Ib_eq={:.6e}A, Ic_eq={:.6e}A", ib_eq, ic_eq);
    spdlog::trace("  MNA indices: nc_idx={}, nb_idx={}, ne_idx={}", nc_idx, nb_idx, ne_idx);

    // MNA stamp: IDENTICAL to NPN (ngspice method)
    // The type multiplier in equivalent currents handles PNP polarity

    // Stamp Base node
    if (nb_idx >= 0) {
        mna.G.add_entry(nb_idx, nb_idx, (g_be + g_bc));
        if (ne_idx >= 0) {
            mna.G.add_entry(nb_idx, ne_idx, -g_be);
        }
        if (nc_idx >= 0) {
            mna.G.add_entry(nb_idx, nc_idx, -g_bc);
        }
        mna.b[nb_idx] -= ib_eq;
    }

    // Stamp Collector node
    if (nc_idx >= 0) {
        if (nb_idx >= 0) {
            mna.G.add_entry(nc_idx, nb_idx, (g_ce + g_cc));
        }
        if (ne_idx >= 0) {
            mna.G.add_entry(nc_idx, ne_idx, -g_ce);
        }
        mna.G.add_entry(nc_idx, nc_idx, -g_cc);
        mna.b[nc_idx] -= ic_eq;
    }

    // Stamp Emitter node
    if (ne_idx >= 0) {
        if (nb_idx >= 0) {
            mna.G.add_entry(ne_idx, nb_idx, -(g_be + g_bc + g_ce + g_cc));
        }
        mna.G.add_entry(ne_idx, ne_idx, (g_be + g_ce));
        if (nc_idx >= 0) {
            mna.G.add_entry(ne_idx, nc_idx, (g_bc + g_cc));
        }
        mna.b[ne_idx] += (ib_eq + ic_eq);
    }
}

void BJT_PNP::stamp_transient(MNASystem& mna,
                               const DenseVector& node_voltages,
                               Real timestep,
                               IntegrationMethod method,
                               const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Transient analysis with nonlinear junction capacitance
    //
    // Strategy (same as NPN, but for PNP junctions E-B and C-B):
    // 1. Stamp DC nonlinear model (Ebers-Moll)
    // 2. Stamp E-B junction capacitance companion model (if Cje > 0)
    // 3. Stamp C-B junction capacitance companion model (if Cjc > 0)
    //
    // Companion model for each junction (linearization at previous timestep):
    //   I_C = g_eq × V + i_eq
    //
    // where g_eq and i_eq depend on the integration method

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

    Real veb = ve - vb;  // E-B junction voltage (PNP)
    Real vcb = vc - vb;  // C-B junction voltage (PNP)

    // Update voltage history for next iteration
    veb_prev_ = veb;
    vcb_prev_ = vcb;

    // Step 2: E-B junction capacitance companion model
    if (cje_ > 0.0) {
        // Evaluate junction capacitance at previous timestep voltage
        Real cje = get_junction_capacitance_eb(veb_prev_);

        // Calculate charge: Q = ∫C(V)dV (for physical LTE)
        charge_eb_ = cje * veb_prev_;

        // Companion model: I_C = g_eq × V + i_eq
        Real g_eq_eb = 0.0;
        Real i_eq_eb = 0.0;

        // Apply integration method
        switch (method) {
        case IntegrationMethod::BACKWARD_EULER:
            g_eq_eb = cje / timestep;
            i_eq_eb = -(cje / timestep) * veb_prev_;
            break;

        case IntegrationMethod::TRAPEZOIDAL: {
            g_eq_eb = 2.0 * cje / timestep;
            Real ic_prev = cje * (veb_prev_ - veb_prev2_) / timestep;
            i_eq_eb = -(2.0 * cje / timestep) * veb_prev_ - ic_prev;
            break;
        }

        case IntegrationMethod::GEAR2:
            if (use_bdf_var_) {
                g_eq_eb = cje * bdf_alpha0_ / bdf_beta0_;
                i_eq_eb = (cje / bdf_beta0_) *
                          (bdf_alpha1_ * veb_prev_ + bdf_alpha2_ * veb_prev2_);
            } else {
                g_eq_eb = 1.5 * cje / timestep;
                i_eq_eb = (-2.0 * cje / timestep) * veb_prev_ +
                          (0.5 * cje / timestep) * veb_prev2_;
            }
            break;

        case IntegrationMethod::GEAR3:
            if (use_bdf_var_) {
                g_eq_eb = cje * bdf_alpha0_ / bdf_beta0_;
                i_eq_eb = (cje / bdf_beta0_) *
                          (bdf_alpha1_ * veb_prev_ + bdf_alpha2_ * veb_prev2_ +
                           bdf_alpha3_ * veb_prev3_);
            } else {
                g_eq_eb = (11.0 / 6.0) * cje / timestep;
                i_eq_eb = (-18.0 / 6.0 * cje / timestep) * veb_prev_ +
                          (9.0 / 6.0 * cje / timestep) * veb_prev2_ +
                          (-2.0 / 6.0 * cje / timestep) * veb_prev3_;
            }
            break;

        case IntegrationMethod::TR_BDF2: {
            constexpr Real gamma = 0.5857864376269049;
            constexpr Real d = gamma * (2.0 - gamma);
            g_eq_eb = (1.0 / d) * cje / timestep;
            i_eq_eb = -(1.0 / d - (1.0 - gamma) * (1.0 - gamma) / d) *
                      (cje / timestep) * veb_prev_;
            break;
        }

        default:
            throw std::runtime_error("BJT_PNP: Unknown integration method in stamp_transient");
        }

        // MNA stamp for E-B capacitor (Emitter+ → Base-)
        auto ne_idx = (ne == NODE_GND) ? -1 : static_cast<int>(ne - 1);
        auto nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);

        if (ne_idx >= 0) {
            mna.G.add_entry(ne_idx, ne_idx, g_eq_eb);
            if (nb_idx >= 0) {
                mna.G.add_entry(ne_idx, nb_idx, -g_eq_eb);
            }
            mna.b[ne_idx] -= i_eq_eb;
        }
        if (nb_idx >= 0) {
            mna.G.add_entry(nb_idx, nb_idx, g_eq_eb);
            if (ne_idx >= 0) {
                mna.G.add_entry(nb_idx, ne_idx, -g_eq_eb);
            }
            mna.b[nb_idx] += i_eq_eb;
        }

        spdlog::trace("{} E-B capacitor: Cje={:.3e}F, g_eq={:.3e}S, i_eq={:.3e}A",
                     name_, cje, g_eq_eb, i_eq_eb);
    }

    // Step 3: C-B junction capacitance companion model
    if (cjc_ > 0.0) {
        // Evaluate junction capacitance at previous timestep voltage
        Real cjc = get_junction_capacitance_cb(vcb_prev_);

        // Calculate charge: Q = ∫C(V)dV
        charge_cb_ = cjc * vcb_prev_;

        // Companion model: I_C = g_eq × V + i_eq
        Real g_eq_cb = 0.0;
        Real i_eq_cb = 0.0;

        // Apply integration method (same as E-B junction)
        switch (method) {
        case IntegrationMethod::BACKWARD_EULER:
            g_eq_cb = cjc / timestep;
            i_eq_cb = -(cjc / timestep) * vcb_prev_;
            break;

        case IntegrationMethod::TRAPEZOIDAL: {
            g_eq_cb = 2.0 * cjc / timestep;
            Real ic_prev = cjc * (vcb_prev_ - vcb_prev2_) / timestep;
            i_eq_cb = -(2.0 * cjc / timestep) * vcb_prev_ - ic_prev;
            break;
        }

        case IntegrationMethod::GEAR2:
            if (use_bdf_var_) {
                g_eq_cb = cjc * bdf_alpha0_ / bdf_beta0_;
                i_eq_cb = (cjc / bdf_beta0_) *
                          (bdf_alpha1_ * vcb_prev_ + bdf_alpha2_ * vcb_prev2_);
            } else {
                g_eq_cb = 1.5 * cjc / timestep;
                i_eq_cb = (-2.0 * cjc / timestep) * vcb_prev_ +
                          (0.5 * cjc / timestep) * vcb_prev2_;
            }
            break;

        case IntegrationMethod::GEAR3:
            if (use_bdf_var_) {
                g_eq_cb = cjc * bdf_alpha0_ / bdf_beta0_;
                i_eq_cb = (cjc / bdf_beta0_) *
                          (bdf_alpha1_ * vcb_prev_ + bdf_alpha2_ * vcb_prev2_ +
                           bdf_alpha3_ * vcb_prev3_);
            } else {
                g_eq_cb = (11.0 / 6.0) * cjc / timestep;
                i_eq_cb = (-18.0 / 6.0 * cjc / timestep) * vcb_prev_ +
                          (9.0 / 6.0 * cjc / timestep) * vcb_prev2_ +
                          (-2.0 / 6.0 * cjc / timestep) * vcb_prev3_;
            }
            break;

        case IntegrationMethod::TR_BDF2: {
            constexpr Real gamma = 0.5857864376269049;
            constexpr Real d = gamma * (2.0 - gamma);
            g_eq_cb = (1.0 / d) * cjc / timestep;
            i_eq_cb = -(1.0 / d - (1.0 - gamma) * (1.0 - gamma) / d) *
                      (cjc / timestep) * vcb_prev_;
            break;
        }

        default:
            throw std::runtime_error("BJT_PNP: Unknown integration method in stamp_transient");
        }

        // MNA stamp for C-B capacitor (Collector+ → Base-)
        auto nc_idx = (nc == NODE_GND) ? -1 : static_cast<int>(nc - 1);
        auto nb_idx = (nb == NODE_GND) ? -1 : static_cast<int>(nb - 1);

        if (nc_idx >= 0) {
            mna.G.add_entry(nc_idx, nc_idx, g_eq_cb);
            if (nb_idx >= 0) {
                mna.G.add_entry(nc_idx, nb_idx, -g_eq_cb);
            }
            mna.b[nc_idx] -= i_eq_cb;
        }
        if (nb_idx >= 0) {
            mna.G.add_entry(nb_idx, nb_idx, g_eq_cb);
            if (nc_idx >= 0) {
                mna.G.add_entry(nb_idx, nc_idx, -g_eq_cb);
            }
            mna.b[nb_idx] += i_eq_cb;
        }

        spdlog::trace("{} C-B capacitor: Cjc={:.3e}F, g_eq={:.3e}S, i_eq={:.3e}A",
                     name_, cjc, g_eq_cb, i_eq_cb);
    }
}

Real BJT_PNP::get_transconductance(Real veb, Real vcb) const {
    // Transconductance: Gm = dIc/dVeb
    // This is the same as get_collector_conductance_veb()
    return get_collector_conductance_veb(veb, vcb);
}

Real BJT_PNP::get_input_conductance(Real veb, Real vcb) const {
    // Input conductance: Gpi = dIb/dVeb
    // This is the same as get_base_conductance_veb()
    return get_base_conductance_veb(veb, vcb);
}

Real BJT_PNP::get_feedback_conductance(Real veb, Real vcb) const {
    // Feedback conductance: Gmu = dIc/dVcb
    // This is the same as get_collector_conductance_vcb()
    // Note: This is typically negative (increasing Vcb decreases Ic)
    return get_collector_conductance_vcb(veb, vcb);
}

Real BJT_PNP::get_input_capacitance(Real veb, Real vcb) const {
    // Input capacitance: Cπ = Cje(Veb) + Cdiff
    // where Cdiff = Tf × Gm (diffusion capacitance)

    Real cje_actual = 0.0;
    if (cje_ > 0.0) {
        // Junction capacitance (same formula as diode)
        constexpr Real FC = 0.5;
        Real fc_vje = FC * vje_;

        if (veb < fc_vje) {
            // Normal region
            Real factor = 1.0 - veb / vje_;
            if (factor <= 0.0) {
                cje_actual = cje_ * 100.0;  // Avoid singularity
            } else {
                cje_actual = cje_ / std::pow(factor, mje_);
            }
        } else {
            // Forward bias: linear extrapolation
            Real cje_fc = cje_ / std::pow(1.0 - FC, mje_);
            Real slope = mje_ * cje_fc / vje_;
            cje_actual = cje_fc + slope * (veb - fc_vje);
        }
    }

    // Diffusion capacitance: Cdiff = Tf × Gm
    Real cdiff = 0.0;
    if (tf_ > 0.0) {
        Real gm = get_transconductance(veb, vcb);
        cdiff = tf_ * gm;
    }

    return cje_actual + cdiff;
}

Real BJT_PNP::get_feedback_capacitance(Real veb, Real vcb) const {
    // Feedback capacitance: Cμ = Cjc(Vcb)

    if (cjc_ == 0.0) {
        return 0.0;
    }

    // Junction capacitance (same formula as diode)
    constexpr Real FC = 0.5;
    Real fc_vjc = FC * vjc_;

    if (vcb < fc_vjc) {
        // Normal region
        Real factor = 1.0 - vcb / vjc_;
        if (factor <= 0.0) {
            return cjc_ * 100.0;  // Avoid singularity
        }
        return cjc_ / std::pow(factor, mjc_);
    } else {
        // Forward bias: linear extrapolation
        Real cjc_fc = cjc_ / std::pow(1.0 - FC, mjc_);
        Real slope = mjc_ * cjc_fc / vjc_;
        return cjc_fc + slope * (vcb - fc_vjc);
    }
}

void BJT_PNP::stamp_ac(ComplexMNASystem& sys, Real omega,
                       const DenseVector& dc_solution) const {
    // Small-signal AC analysis using hybrid-pi model:
    //
    // B --+-- Gπ --+-- E
    //     |        |
    //     Cπ       |
    //     |        |
    //     +---Gmu--+---Cμ--- C
    //     |               |
    //     +--- Gm×Veb ----+
    //
    // where:
    //   Gπ  = input conductance (Gpi)
    //   Cπ  = input capacitance (Cje + Tf×Gm)
    //   Gmu = feedback conductance (dIc/dVcb)
    //   Cμ  = feedback capacitance (Cjc)
    //   Gm  = transconductance (dIc/dVeb)

    NodeId nc_node = nodes_[0];  // Collector
    NodeId nb_node = nodes_[1];  // Base
    NodeId ne_node = nodes_[2];  // Emitter

    // Get DC operating point voltages
    Real v_c = (nc_node == NODE_GND) ? 0.0 : dc_solution[nc_node - 1];
    Real v_b = (nb_node == NODE_GND) ? 0.0 : dc_solution[nb_node - 1];
    Real v_e = (ne_node == NODE_GND) ? 0.0 : dc_solution[ne_node - 1];

    // For PNP: Veb = Ve - Vb, Vcb = Vc - Vb
    Real veb = v_e - v_b;
    Real vcb = v_c - v_b;

    // Calculate small-signal parameters
    Real gm = get_transconductance(veb, vcb);
    Real gpi = get_input_conductance(veb, vcb);
    Real gmu = get_feedback_conductance(veb, vcb);
    Real cpi = get_input_capacitance(veb, vcb);
    Real cmu = get_feedback_capacitance(veb, vcb);

    spdlog::trace("{} stamp_ac: Veb={:.6f}V, Vcb={:.6f}V, Gm={:.6e}S, Gπ={:.6e}S, Gμ={:.6e}S, Cπ={:.6e}F, Cμ={:.6e}F, ω={:.3e}rad/s",
                 name_, veb, vcb, gm, gpi, gmu, cpi, cmu, omega);

    // Convert to MNA indices
    auto nc_idx = (nc_node == NODE_GND) ? -1 : static_cast<int>(nc_node - 1);
    auto nb_idx = (nb_node == NODE_GND) ? -1 : static_cast<int>(nb_node - 1);
    auto ne_idx = (ne_node == NODE_GND) ? -1 : static_cast<int>(ne_node - 1);

    // Admittances
    Complex ypi(gpi, omega * cpi);   // B-E admittance
    Complex ymu(gmu, omega * cmu);   // B-C admittance (feedback)
    Complex gmveb(gm, 0.0);          // Controlled current source (Gm×Veb)

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

    // Stamp controlled current source: Ic = Gm × Veb
    // For PNP, Veb = Ve - Vb (opposite polarity of NPN's Vbe = Vb - Ve)
    // Standard VCCS stamp (ngspice-compatible):
    // Ic flows OUT of collector → MNA convention requires POSITIVE Gm
    // Y[collector, emitter] = +Gm (note: emitter is the POSITIVE terminal for PNP)
    if (nc_idx >= 0) {
        if (ne_idx >= 0) {
            sys.Y.add_entry(nc_idx, ne_idx, +gmveb);  // +Gm × Ve
        }
        if (nb_idx >= 0) {
            sys.Y.add_entry(nc_idx, nb_idx, -gmveb);  // -Gm × Vb
        }
    }
    if (ne_idx >= 0) {
        sys.Y.add_entry(ne_idx, ne_idx, -gmveb);  // -Gm × Ve
        if (nb_idx >= 0) {
            sys.Y.add_entry(ne_idx, nb_idx, +gmveb);  // +Gm × Vb
        }
    }

    // No current source contribution (linearized at DC operating point)
}

std::vector<NoiseSource> BJT_PNP::get_noise_sources(const DenseVector& dc_solution,
                                                      Real frequency) const {
    // BJT PNP noise sources (same as NPN):
    // 1. Shot noise from base current: I_n,B² = 2qI_B (A²/Hz)
    // 2. Shot noise from collector current: I_n,C² = 2qI_C (A²/Hz)
    // 3. Flicker noise: I_n,f² = K_f * I_C^AF / f^FF (A²/Hz)

    std::vector<NoiseSource> sources;

    // Get DC operating point voltages
    NodeId collector_node = nodes_[0];
    NodeId base_node = nodes_[1];
    NodeId emitter_node = nodes_[2];

    Real V_c = (collector_node == NODE_GND) ? 0.0 : dc_solution[collector_node - 1];
    Real V_b = (base_node == NODE_GND) ? 0.0 : dc_solution[base_node - 1];
    Real V_e = (emitter_node == NODE_GND) ? 0.0 : dc_solution[emitter_node - 1];

    Real V_eb = V_e - V_b;  // Emitter-base voltage (PNP uses Veb)
    Real V_cb = V_c - V_b;  // Collector-base voltage (PNP uses Vcb)

    // Calculate DC currents
    Real I_b = get_base_current(V_eb, V_cb);
    Real I_c = get_collector_current(V_eb, V_cb);

    // Shot noise threshold
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

        spdlog::trace("BJT PNP {} base shot noise: I_n² = {:.3e} A²/Hz (I_B = {:.3e} A)",
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

        spdlog::trace("BJT PNP {} collector shot noise: I_n² = {:.3e} A²/Hz (I_C = {:.3e} A)",
                     name_, I_n_C_squared, I_c);
    }

    // Source 3: Flicker noise (1/f noise)
    // Guard against overflow: clamp minimum frequency to prevent huge values when ff_ < 0
    const Real min_frequency = 1e-3; // 1 mHz minimum to avoid overflow
    if (kf_ > 0.0 && std::abs(I_c) > I_threshold && frequency > 0.0) {
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

        spdlog::trace("BJT PNP {} flicker noise: I_n² = {:.3e} A²/Hz (I_C = {:.3e} A, f = {:.3e} Hz)",
                     name_, I_n_f_squared, I_c, frequency);
    }

    return sources;
}

// Junction capacitance calculation (nonlinear capacitance)

Real BJT_PNP::get_junction_capacitance_eb(Real veb) const {
    // E-B junction capacitance (same formula as diode):
    // Cj(V) = Cj0 / (1 - V/Vj)^m  for V < FC*Vj
    // Cj(V) = Cj_FC + (V - FC*Vj) * dCj/dV  for V >= FC*Vj (linear extrapolation)
    //
    // where FC = 0.5 (forward-bias coefficient)

    if (cje_ == 0.0) {
        return 0.0;
    }

    constexpr Real FC = 0.5;
    Real fc_vje = FC * vje_;

    if (veb < fc_vje) {
        // Normal region: Cj(V) = Cj0 / (1 - V/Vj)^m
        Real factor = 1.0 - veb / vje_;
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
        return cje_fc + slope * (veb - fc_vje);
    }
}

Real BJT_PNP::get_junction_capacitance_cb(Real vcb) const {
    // C-B junction capacitance (same formula as diode):
    // Cj(V) = Cj0 / (1 - V/Vj)^m  for V < FC*Vj
    // Cj(V) = Cj_FC + (V - FC*Vj) * dCj/dV  for V >= FC*Vj (linear extrapolation)

    if (cjc_ == 0.0) {
        return 0.0;
    }

    constexpr Real FC = 0.5;
    Real fc_vjc = FC * vjc_;

    if (vcb < fc_vjc) {
        // Normal region: Cj(V) = Cj0 / (1 - V/Vj)^m
        Real factor = 1.0 - vcb / vjc_;
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
        return cjc_fc + slope * (vcb - fc_vjc);
    }
}

// State management for transient analysis

void BJT_PNP::save_state() {
    veb_saved_ = veb_prev_;
    veb_saved2_ = veb_prev2_;
    veb_saved3_ = veb_prev3_;
    vcb_saved_ = vcb_prev_;
    vcb_saved2_ = vcb_prev2_;
    vcb_saved3_ = vcb_prev3_;
    timestep_saved_ = timestep_;
    method_saved_ = last_method_;

    spdlog::trace("{} save_state: veb={:.6f}V, vcb={:.6f}V, timestep={:.3e}s",
                 name_, veb_prev_, vcb_prev_, timestep_);
}

void BJT_PNP::restore_state() {
    veb_prev_ = veb_saved_;
    veb_prev2_ = veb_saved2_;
    veb_prev3_ = veb_saved3_;
    vcb_prev_ = vcb_saved_;
    vcb_prev2_ = vcb_saved2_;
    vcb_prev3_ = vcb_saved3_;
    timestep_ = timestep_saved_;
    last_method_ = method_saved_;

    spdlog::trace("{} restore_state: veb={:.6f}V, vcb={:.6f}V, timestep={:.3e}s",
                 name_, veb_prev_, vcb_prev_, timestep_);
}

void BJT_PNP::accept_solution(const DenseVector& node_voltages) {
    // Update voltage history for multi-step integration methods
    // Shift: V(t-2Δt) ← V(t-Δt) ← V(t)

    (void)node_voltages;  // Suppress unused parameter warning (voltage already saved in stamp_transient)

    if (first_accept_) {
        // First call: just mark as initialized
        first_accept_ = false;
        spdlog::trace("{} accept_solution: first call (timestep={:.3e}s)", name_, timestep_);
        return;
    }

    // Shift E-B voltage history
    veb_prev3_ = veb_prev2_;
    veb_prev2_ = veb_prev_;
    // veb_prev_ is already set in stamp_transient() for next iteration

    // Shift C-B voltage history
    vcb_prev3_ = vcb_prev2_;
    vcb_prev2_ = vcb_prev_;
    // vcb_prev_ is already set in stamp_transient() for next iteration

    // Update charge for physical LTE calculation
    charge_eb_prev_ = charge_eb_;
    charge_cb_prev_ = charge_cb_;

    spdlog::trace("{} accept_solution: veb_prev={:.6f}V, veb_prev2={:.6f}V, veb_prev3={:.6f}V, "
                 "vcb_prev={:.6f}V, vcb_prev2={:.6f}V, vcb_prev3={:.6f}V",
                 name_, veb_prev_, veb_prev2_, veb_prev3_, vcb_prev_, vcb_prev2_, vcb_prev3_);
}

Real BJT_PNP::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    // Physical local truncation error (charge-based):
    // LTE = |Q_EB(t+Δt) - Q_EB(t)| / chgtol + |Q_CB(t+Δt) - Q_CB(t)| / chgtol
    //
    // This provides adaptive timestepping based on charge conservation.

    (void)node_voltages;  // Suppress unused parameter warning (charge already calculated in stamp_transient)

    Real lte_eb = std::abs(charge_eb_ - charge_eb_prev_) / chgtol;
    Real lte_cb = std::abs(charge_cb_ - charge_cb_prev_) / chgtol;
    Real total_lte = lte_eb + lte_cb;

    spdlog::trace("{} physical_lte: lte_eb={:.3e}, lte_cb={:.3e}, total={:.3e} (chgtol={:.3e}C)",
                 name_, lte_eb, lte_cb, total_lte, chgtol);

    return total_lte;
}

// Sensitivity analysis - Parameter access

std::optional<Real> BJT_PNP::get_parameter(const std::string& param_name) const {
    // Saturation currents
    if (param_name == "IS" || param_name == "is") {
        return is_;
    } else if (param_name == "ISC" || param_name == "isc") {
        return isc_;
    }
    // Current gains
    else if (param_name == "BF" || param_name == "bf" ||
             param_name == "BETA_F" || param_name == "beta_f") {
        return beta_f_;
    } else if (param_name == "BR" || param_name == "br" ||
               param_name == "BETA_R" || param_name == "beta_r") {
        return beta_r_;
    }
    // Emission coefficients
    else if (param_name == "NF" || param_name == "nf") {
        return nf_;
    } else if (param_name == "NR" || param_name == "nr") {
        return nr_;
    }
    // Junction capacitances
    else if (param_name == "CJE" || param_name == "cje") {
        return cje_;
    } else if (param_name == "CJC" || param_name == "cjc") {
        return cjc_;
    }

    return std::nullopt;
}

bool BJT_PNP::set_parameter(const std::string& param_name, Real value) {
    // Saturation currents
    if (param_name == "IS" || param_name == "is") {
        if (value <= 0.0) {
            spdlog::warn("BJT_PNP {}: IS must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        is_ = value;
        // If isc is still at default (same as is), update it too
        if (isc_ == is_) {
            isc_ = value;
        }
        spdlog::trace("BJT_PNP {}: set IS = {} A", name_, is_);
        return true;
    } else if (param_name == "ISC" || param_name == "isc") {
        if (value <= 0.0) {
            spdlog::warn("BJT_PNP {}: ISC must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        isc_ = value;
        spdlog::trace("BJT_PNP {}: set ISC = {} A", name_, isc_);
        return true;
    }
    // Current gains
    else if (param_name == "BF" || param_name == "bf" ||
             param_name == "BETA_F" || param_name == "beta_f") {
        if (value <= 0.0) {
            spdlog::warn("BJT_PNP {}: BF must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        beta_f_ = value;
        spdlog::trace("BJT_PNP {}: set BF = {}", name_, beta_f_);
        return true;
    } else if (param_name == "BR" || param_name == "br" ||
               param_name == "BETA_R" || param_name == "beta_r") {
        if (value <= 0.0) {
            spdlog::warn("BJT_PNP {}: BR must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        beta_r_ = value;
        spdlog::trace("BJT_PNP {}: set BR = {}", name_, beta_r_);
        return true;
    }
    // Emission coefficients
    else if (param_name == "NF" || param_name == "nf") {
        if (value <= 0.0) {
            spdlog::warn("BJT_PNP {}: NF must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        nf_ = value;
        spdlog::trace("BJT_PNP {}: set NF = {}", name_, nf_);
        return true;
    } else if (param_name == "NR" || param_name == "nr") {
        if (value <= 0.0) {
            spdlog::warn("BJT_PNP {}: NR must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        nr_ = value;
        spdlog::trace("BJT_PNP {}: set NR = {}", name_, nr_);
        return true;
    }
    // Junction capacitances
    else if (param_name == "CJE" || param_name == "cje") {
        if (value < 0.0) {
            spdlog::warn("BJT_PNP {}: CJE must be non-negative (got {}), not updated",
                        name_, value);
            return false;
        }
        cje_ = value;
        spdlog::trace("BJT_PNP {}: set CJE = {} F", name_, cje_);
        return true;
    } else if (param_name == "CJC" || param_name == "cjc") {
        if (value < 0.0) {
            spdlog::warn("BJT_PNP {}: CJC must be non-negative (got {}), not updated",
                        name_, value);
            return false;
        }
        cjc_ = value;
        spdlog::trace("BJT_PNP {}: set CJC = {} F", name_, cjc_);
        return true;
    }

    return false;
}

std::vector<std::string> BJT_PNP::get_perturbable_parameters() const {
    // Return main parameters for sensitivity analysis
    // (Capacitances CJE/CJC are excluded as they are typically less significant for DC sensitivity)
    return {"IS", "BF", "BR", "NF"};
}

} // namespace kirimaspice::devices
