// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/mutual_inductance.cpp
#include "kirimaspice/devices/mutual_inductance.hpp"
#include "kirimaspice/devices/inductor.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace kirimaspice::devices {

MutualInductance::MutualInductance(DeviceId id, const std::string& name,
                                   const std::string& inductor1_name,
                                   const std::string& inductor2_name,
                                   Real coupling_coefficient)
    : Device(id, name, DeviceType::MUTUAL_INDUCTANCE)
    , inductor1_name_(inductor1_name)
    , inductor2_name_(inductor2_name)
    , k_(coupling_coefficient)
    , L1_(nullptr)
    , L2_(nullptr) {

    // Validate coupling coefficient
    if (k_ < 0.0 || k_ > 1.0) {
        throw std::invalid_argument(
            fmt::format("Mutual inductance {}: coupling coefficient k={} out of range [0, 1]",
                       name, k_));
    }

    spdlog::debug("Created MutualInductance {} (L1={}, L2={}, k={})",
                  name, inductor1_name_, inductor2_name_, k_);
}

void MutualInductance::set_inductors(const Device* L1, const Device* L2) {
    // Verify that L1 and L2 are inductors
    if (L1->type() != DeviceType::INDUCTOR) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: {} is not an inductor",
                       name(), inductor1_name_));
    }
    if (L2->type() != DeviceType::INDUCTOR) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: {} is not an inductor",
                       name(), inductor2_name_));
    }

    L1_ = L1;
    L2_ = L2;

    spdlog::debug("Mutual inductance {}: resolved L1={}, L2={}, M={:.3e}H",
                  name(), inductor1_name_, inductor2_name_, mutual_inductance());
}

Real MutualInductance::mutual_inductance() const {
    if (!L1_ || !L2_) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: inductors not set", name()));
    }

    // Cast to Inductor to access inductance()
    auto* ind1 = dynamic_cast<const Inductor*>(L1_);
    auto* ind2 = dynamic_cast<const Inductor*>(L2_);

    if (!ind1 || !ind2) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: failed to cast inductors", name()));
    }

    Real L1_val = ind1->inductance();
    Real L2_val = ind2->inductance();

    // Mark inductors as coupled to disable invalid local LTE check
    const_cast<Inductor*>(ind1)->set_coupled(true);
    const_cast<Inductor*>(ind2)->set_coupled(true);

    // M = k * sqrt(L1 * L2)
    return k_ * std::sqrt(L1_val * L2_val);
}

void MutualInductance::stamp_transient(MNASystem& mna,
                                        const DenseVector& node_voltages,
                                        Real timestep,
                                        IntegrationMethod method,
                                        const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    (void)method; (void)bdf_coef; (void)tr_bdf2_coef;

    if (!L1_ || !L2_) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: inductors not set", name()));
    }

    auto* ind1 = dynamic_cast<const Inductor*>(L1_);
    auto* ind2 = dynamic_cast<const Inductor*>(L2_);

    if (!ind1 || !ind2) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: failed to cast inductors", name()));
    }

    Real L1 = ind1->inductance();
    Real L2 = ind2->inductance();
    Real M = mutual_inductance();
    static bool debug_once = true;
    if (debug_once) {
        spdlog::info("MutualInductance {} first stamp: L1={:.3e}, L2={:.3e}, M={:.3e}",
                     name(), L1, L2, M);
        debug_once = false;
    }

    // Generalized coefficients for linear multi-inductor companion model
    // dI/dt ≈ (α0 I_{n+1} + α1 I_n + α2 I_{n-1} + α3 I_{n-2}) / β0
    // => I_{n+1} = (β0/α0) L^{-1} V_{n+1} - (α1/α0) I_n - (α2/α0) I_{n-1} - (α3/α0) I_{n-2}
    Real alpha0 = 1.0, alpha1 = -1.0, alpha2 = 0.0, alpha3 = 0.0;
    Real beta0  = timestep; // BE default (β0 = Δt)
    bool need_hist2 = false, need_hist3 = false;

    switch (method) {
        case IntegrationMethod::GEAR2:
            if (bdf_coef) {
                alpha0 = bdf_coef->alpha0; alpha1 = bdf_coef->alpha1; alpha2 = bdf_coef->alpha2; beta0 = bdf_coef->beta0;
            } else {
                // BDF2: (3I_{n+1} - 4I_n + I_{n-1}) / (2Δt)
                // => alpha0=3, alpha1=-4, alpha2=1, beta0=2Δt
                alpha0 = 3.0; alpha1 = -4.0; alpha2 = 1.0; beta0 = 2.0 * timestep;
            }
            need_hist2 = true;
            break;
        case IntegrationMethod::GEAR3:
            if (bdf_coef) {
                alpha0 = bdf_coef->alpha0; alpha1 = bdf_coef->alpha1; alpha2 = bdf_coef->alpha2; alpha3 = bdf_coef->alpha3; beta0 = bdf_coef->beta0;
            } else {
                // BDF3: (11I_{n+1} - 18I_n + 9I_{n-1} - 2I_{n-2}) / (6Δt)
                alpha0 = 11.0; alpha1 = -18.0; alpha2 = 9.0; alpha3 = -2.0; beta0 = 6.0 * timestep;
            }
            need_hist2 = need_hist3 = true;
            break;
        case IntegrationMethod::TR_BDF2:
            if (tr_bdf2_coef && tr_bdf2_coef->stage == 2) {
                // Stage 2 behaves like BDF2 with custom coefficients and full_dt
                alpha0 = tr_bdf2_coef->alpha0;
                alpha1 = tr_bdf2_coef->alpha1; // multiplies I_gamma
                alpha2 = tr_bdf2_coef->alpha2; // multiplies I_n
                beta0  = tr_bdf2_coef->beta0 * tr_bdf2_coef->full_dt;
                need_hist2 = true;
            } else {
                // Stage1 (trap) : use trapezoidal-like scale (γΔt/2)
                Real stage_dt = tr_bdf2_coef ? tr_bdf2_coef->stage_dt : timestep;
                alpha0 = 1.0; alpha1 = -1.0; alpha2 = 0.0; alpha3 = 0.0; beta0 = stage_dt * 0.5;
            }
            break;
        case IntegrationMethod::TRAPEZOIDAL:
            alpha0 = 1.0; alpha1 = -1.0; alpha2 = 0.0; alpha3 = 0.0; beta0 = 0.5 * timestep;
            break;
        default: // BACKWARD_EULER など
            alpha0 = 1.0; alpha1 = -1.0; alpha2 = 0.0; alpha3 = 0.0; beta0 = timestep;
            break;
    }

    // A group of inductors that K lines couple among themselves: its lead
    // stamps the companion of the group's whole inductance matrix, as below
    // for a pair, and its other K lines nothing (CoupledGroup)
    if (group_) {
        if (!lead_) {
            stamped_ = false;
            return;
        }
        const auto& inductors = group_->inductors;
        const std::size_t n = inductors.size();
        const auto gamma = group_->inverse();
        const Real scale = beta0 / alpha0;
        const Real coeff1 = -alpha1 / alpha0, coeff2 = -alpha2 / alpha0, coeff3 = -alpha3 / alpha0;
        const bool trapezoidal_like =
            method == IntegrationMethod::TRAPEZOIDAL ||
            (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef && tr_bdf2_coef->stage == 1);
        const bool second_stage = method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef &&
                                  tr_bdf2_coef->stage == 2;
        auto idx = [](NodeId node) { return (node == NODE_GND) ? -1 : static_cast<int>(node - 1); };
        std::vector<const Inductor*> ind(n);
        std::vector<int> np(n), nn(n);
        std::vector<Real> v_prev(n), base(n);
        for (std::size_t i = 0; i < n; ++i) {
            ind[i] = static_cast<const Inductor*>(inductors[i]);
            np[i] = idx(ind[i]->np());
            nn[i] = idx(ind[i]->nn());
            v_prev[i] = ind[i]->voltage_prev();
            // The history each inductor stamps of its own (Inductor::
            // stamp_transient); TR-BDF2's second stage takes I(t + gamma h)
            // and I(t)
            const Real i1 = second_stage ? ind[i]->current_gamma() : ind[i]->current_prev();
            const Real i2 = second_stage ? ind[i]->current_prev() : (need_hist2 ? ind[i]->current_prev2() : 0.0);
            const Real i3 = need_hist3 ? ind[i]->current_prev3() : 0.0;
            base[i] = coeff1 * i1 + coeff2 * i2 + coeff3 * i3;
        }
        group_g_.assign(n, std::vector<Real>(n, 0.0));
        group_dg_.assign(n, std::vector<Real>(n, 0.0));
        group_i_ = base;
        group_di_.assign(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const Real g_self = scale / ind[i]->inductance();
            for (std::size_t j = 0; j < n; ++j) {
                group_g_[i][j] = scale * gamma[i][j];
                group_dg_[i][j] = group_g_[i][j] - ((i == j) ? g_self : 0.0);
            }
            if (trapezoidal_like) {
                for (std::size_t j = 0; j < n; ++j) {
                    group_i_[i] += group_g_[i][j] * v_prev[j];
                }
                group_di_[i] = group_i_[i] - (base[i] + g_self * v_prev[i]);
            }
        }
        // The current dG_ij v_j from np_i to nn_i, and the history di_i
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                const Real g = group_dg_[i][j];
                if (g == 0.0) continue;
                if (np[i] >= 0 && np[j] >= 0) mna.G.add_entry(np[i], np[j], g);
                if (np[i] >= 0 && nn[j] >= 0) mna.G.add_entry(np[i], nn[j], -g);
                if (nn[i] >= 0 && np[j] >= 0) mna.G.add_entry(nn[i], np[j], -g);
                if (nn[i] >= 0 && nn[j] >= 0) mna.G.add_entry(nn[i], nn[j], g);
            }
            if (np[i] >= 0) mna.b[static_cast<std::size_t>(np[i])] -= group_di_[i];
            if (nn[i] >= 0) mna.b[static_cast<std::size_t>(nn[i])] += group_di_[i];
        }
        stamped_ = true;
        stamped_dt_ = second_stage || (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef)
                          ? tr_bdf2_coef->full_dt
                          : timestep;
        return;
    }

    // Companion conductances derived from coupled inductance matrix
    // dI/dt ≈ (α₀ I_{n+1} + α₁ I_n + α₂ I_{n-1} + α₃ I_{n-2}) / β₀
    // I_{n+1} = (β₀/α₀) L^{-1} V_{n+1} - Σ (α_i/α₀) I_{n+1-i}
    // → G = (β₀/α₀) L^{-1}
    const InverseInductance inverse = inverse_inductance();
    Real scale = beta0 / alpha0;
    Real g11_target = scale * inverse.g11;  // coefficient for v1
    Real g22_target = scale * inverse.g22;  // coefficient for v2
    Real g12 = scale * inverse.g12;         // coefficient for v2 into i1
    Real g21 = g12;                         // symmetric

    // Existing self stamps from each inductor (added separately)
    Real gL1 = scale / L1;
    Real gL2 = scale / L2;
    Real delta_g1 = scale * inverse.delta11;
    Real delta_g2 = scale * inverse.delta22;

    NodeId np1 = ind1->np();
    NodeId nn1 = ind1->nn();
    NodeId np2 = ind2->np();
    NodeId nn2 = ind2->nn();

    auto idx = [](NodeId n){ return (n == NODE_GND) ? -1 : static_cast<int>(n - 1); };
    int np1_idx = idx(np1), nn1_idx = idx(nn1);
    int np2_idx = idx(np2), nn2_idx = idx(nn2);

    auto add_self = [&](int p, int n, Real g_delta) {
        if (g_delta == 0.0) return;
        if (p >= 0) {
            mna.G.add_entry(p, p, g_delta);
            if (n >= 0) mna.G.add_entry(p, n, -g_delta);
        }
        if (n >= 0) {
            mna.G.add_entry(n, n, g_delta);
            if (p >= 0) mna.G.add_entry(n, p, -g_delta);
        }
    };

    // Adjust self conductances so total matches coupled model
    add_self(np1_idx, nn1_idx, delta_g1);
    add_self(np2_idx, nn2_idx, delta_g2);

    // Cross coupling as VCCS: i1 += g12 * v2, i2 += g21 * v1
    auto add_vccs = [&](int p_out, int n_out, int p_ctrl, int n_ctrl, Real g){
        if (g == 0.0) return;
        if (p_out >= 0 && p_ctrl >= 0) mna.G.add_entry(p_out, p_ctrl,  g);
        if (p_out >= 0 && n_ctrl >= 0) mna.G.add_entry(p_out, n_ctrl, -g);
        if (n_out >= 0 && p_ctrl >= 0) mna.G.add_entry(n_out, p_ctrl, -g);
        if (n_out >= 0 && n_ctrl >= 0) mna.G.add_entry(n_out, n_ctrl,  g);
    };

    add_vccs(np1_idx, nn1_idx, np2_idx, nn2_idx, g12);
    add_vccs(np2_idx, nn2_idx, np1_idx, nn1_idx, g21);

    // RHS history currents (matrix form analog of single inductor i_eq)
    auto add_b = [&](int row, Real val) {
        if (row >= 0) mna.b[static_cast<std::size_t>(row)] += val;
    };

    // history currents from inductor states (include cross-history for coupled model)
    Real i1_prev = ind1->current_prev();
    Real i2_prev = ind2->current_prev();
    Real i1_prev2 = need_hist2 ? ind1->current_prev2() : 0.0;
    Real i2_prev2 = need_hist2 ? ind2->current_prev2() : 0.0;
    Real i1_prev3 = need_hist3 ? ind1->current_prev3() : 0.0;
    Real i2_prev3 = need_hist3 ? ind2->current_prev3() : 0.0;

    // TR-BDF2's second stage: alpha1 multiplies I(t + gamma h) and alpha2
    // I(t), as in Inductor::stamp_transient
    if (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef) {
        if (tr_bdf2_coef->stage == 2) {
            i1_prev = ind1->current_gamma();
            i2_prev = ind2->current_gamma();
            i1_prev2 = ind1->current_prev();
            i2_prev2 = ind2->current_prev();
        }
    }

    Real coeff1 = -alpha1 / alpha0;
    Real coeff2 = -alpha2 / alpha0;
    Real coeff3 = -alpha3 / alpha0;

    // Helper: inject to each node pair (similar pattern to self conductance)
    auto stamp_hist = [&](int p, int n, Real ihist) {
        // Follow inductor sign convention: positive ihist is current from p -> n
        if (p >= 0) add_b(p, -ihist);
        if (n >= 0) add_b(n, ihist);
    };

    // ------------------------------------------------------------
    // History current correction
    //
    // Self inductors already stamp their own history terms (i_eq).
    // To avoid double counting, only the *difference* between the
    // coupled companion model and the uncoupled self stamps is added.
    // For BDF (Gear2/Gear3) this difference is zero; for trapezoidal-
    // like stages the delta captures (delta_g * V_prev + cross V terms).
    // ------------------------------------------------------------

    // Base history that *each* inductor has already injected (self terms only)
    Real i1_base = coeff1 * i1_prev + coeff2 * i1_prev2 + coeff3 * i1_prev3;
    Real i2_base = coeff1 * i2_prev + coeff2 * i2_prev2 + coeff3 * i2_prev3;

    // Target history for the coupled model.
    // BDF-family methods do not couple past currents across inductors;
    // coupling appears only through the conductance matrix on V_{n+1}.
    Real i1_target = i1_base;
    Real i2_target = i2_base;

    // Trapezoidal-like stages include previous voltages
    if (method == IntegrationMethod::TRAPEZOIDAL ||
        (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef && tr_bdf2_coef->stage == 1)) {
        Real v1_prev = ind1->voltage_prev();
        Real v2_prev = ind2->voltage_prev();
        i1_target += g11_target * v1_prev + g12 * v2_prev;
        i2_target += g21 * v1_prev + g22_target * v2_prev;

        // Self inductors already contributed gL*V_prev on their own.
        i1_base += gL1 * v1_prev;
        i2_base += gL2 * v2_prev;
    }

    // Only add the delta that is missing from the uncoupled stamps
    Real delta_i1 = i1_target - i1_base;
    Real delta_i2 = i2_target - i2_base;

    stamp_hist(np1_idx, nn1_idx, delta_i1);
    stamp_hist(np2_idx, nn2_idx, delta_i2);

    // Keep the coupled companion model of this step for get_physical_lte
    stamped_ = true;
    stamped_g11_ = g11_target;
    stamped_g12_ = g12;
    stamped_g22_ = g22_target;
    stamped_i1_ = i1_target;
    stamped_i2_ = i2_target;
    stamped_dt_ = (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef) ? tr_bdf2_coef->full_dt
                                                                         : timestep;
    stamped_dg11_ = delta_g1;
    stamped_dg22_ = delta_g2;
    stamped_di1_ = delta_i1;
    stamped_di2_ = delta_i2;
}

Real MutualInductance::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    auto node_voltage = [&](NodeId n) { return (n == NODE_GND) ? 0.0 : node_voltages[n]; };
    if (group_) {
        // The group's fluxes L I, predicted and from the stamped companion
        if (!lead_ || !stamped_ || stamped_dt_ <= 0.0) {
            return 0.0;
        }
        const std::size_t n = group_->inductors.size();
        const auto lm = group_->inductance_matrix();
        std::vector<Real> v(n), i_new(n, 0.0), i_old(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto* ind = static_cast<const Inductor*>(group_->inductors[i]);
            v[i] = node_voltage(ind->np()) - node_voltage(ind->nn());
            i_old[i] = ind->current_prev();
        }
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) i_new[i] += group_g_[i][j] * v[j];
            i_new[i] += group_i_[i];
        }
        Real worst = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const auto* ind = static_cast<const Inductor*>(group_->inductors[i]);
            Real pred = stamped_dt_ * ind->voltage_prev(), corr = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                pred += lm[i][j] * i_old[j];
                corr += lm[i][j] * i_new[j];
            }
            worst = std::max(worst, Inductor::flux_lte(pred, corr, chgtol));
        }
        return worst;
    }
    auto* ind1 = dynamic_cast<const Inductor*>(L1_);
    auto* ind2 = dynamic_cast<const Inductor*>(L2_);
    if (!ind1 || !ind2 || !stamped_ || stamped_dt_ <= 0.0) {
        return 0.0;
    }

    // node_voltages is indexed by node id (index 0 = ground)
    auto node_v = [&](NodeId n) { return (n == NODE_GND) ? 0.0 : node_voltages[n]; };
    const Real v1 = node_v(ind1->np()) - node_v(ind1->nn());
    const Real v2 = node_v(ind2->np()) - node_v(ind2->nn());

    // Currents of the new solution from the coupled companion model
    const Real i1 = stamped_g11_ * v1 + stamped_g12_ * v2 + stamped_i1_;
    const Real i2 = stamped_g12_ * v1 + stamped_g22_ * v2 + stamped_i2_;

    const Real L1 = ind1->inductance();
    const Real L2 = ind2->inductance();
    const Real M = k_ * std::sqrt(L1 * L2);

    // dΦ/dt = V: predict each flux from the previous point (the inductors
    // still hold I_n and V_n until the step is accepted)
    const Real i1_n = ind1->current_prev();
    const Real i2_n = ind2->current_prev();
    const Real phi1_pred = L1 * i1_n + M * i2_n + stamped_dt_ * ind1->voltage_prev();
    const Real phi2_pred = M * i1_n + L2 * i2_n + stamped_dt_ * ind2->voltage_prev();
    const Real phi1_corr = L1 * i1 + M * i2;
    const Real phi2_corr = M * i1 + L2 * i2;

    return std::max(Inductor::flux_lte(phi1_pred, phi1_corr, chgtol),
                    Inductor::flux_lte(phi2_pred, phi2_corr, chgtol));
}

// Each inductor has accepted the current of its own companion
// (Inductor::accept_solution); the solution also carries the current that
// this device stamped on top of it (stamp_transient), which is added here.
// The two together are the coupled companion of the step, the same model
// as get_physical_lte evaluates, for every method and stage.
void MutualInductance::accept_solution(const DenseVector& node_voltages) {
    if (!L1_ || !L2_ || !stamped_) return;  // no step stamped (the start under UIC)
    if (group_) {
        // The lead adds to each inductor of the group the current it stamped
        if (!lead_) return;
        auto node_voltage = [&](NodeId n) { return (n == NODE_GND) ? 0.0 : node_voltages[n]; };
        const std::size_t n = group_->inductors.size();
        std::vector<Real> v(n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto* ind = static_cast<const Inductor*>(group_->inductors[i]);
            v[i] = node_voltage(ind->np()) - node_voltage(ind->nn());
        }
        for (std::size_t i = 0; i < n; ++i) {
            Real delta = group_di_[i];
            for (std::size_t j = 0; j < n; ++j) delta += group_dg_[i][j] * v[j];
            const_cast<Inductor*>(static_cast<const Inductor*>(group_->inductors[i]))->add_current_delta(delta);
        }
        return;
    }
    auto* ind1 = const_cast<Inductor*>(dynamic_cast<const Inductor*>(L1_));
    auto* ind2 = const_cast<Inductor*>(dynamic_cast<const Inductor*>(L2_));
    if (!ind1 || !ind2) return;

    // node_voltages is indexed by node id (index 0 = ground)
    auto node_v = [&](NodeId n) { return (n == NODE_GND) ? 0.0 : node_voltages[n]; };
    const Real v1 = node_v(ind1->np()) - node_v(ind1->nn());
    const Real v2 = node_v(ind2->np()) - node_v(ind2->nn());

    ind1->add_current_delta(stamped_dg11_ * v1 + stamped_g12_ * v2 + stamped_di1_);
    ind2->add_current_delta(stamped_g12_ * v1 + stamped_dg22_ * v2 + stamped_di2_);
}

MutualInductance::InverseInductance MutualInductance::inverse_inductance() const {
    auto* ind1 = dynamic_cast<const Inductor*>(L1_);
    auto* ind2 = dynamic_cast<const Inductor*>(L2_);
    if (!ind1 || !ind2) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: inductors not set", name()));
    }
    const Real L1 = ind1->inductance();
    const Real L2 = ind2->inductance();
    const Real M = mutual_inductance();

    // Determinant of inductance matrix (clamped for numerical safety)
    Real det = L1 * L2 - M * M;
    Real k_eff = std::abs(M) / std::sqrt(std::max(Real(1e-30), L1 * L2));
    // Avoid singular matrix at k≈1. Clamp relative to L1*L2 with k-aware guard (even softer for k→1).
    // The inverse is then that of the matrix with this determinant, whose
    // coupling is a little below k: dividing the adjugate for the given M by
    // the clamped determinant would drop the finite part of the inverse,
    // 1/(L1 + L2) along the common flux (the magnetizing inductance).
    Real det_min = (1e-12 + 1e-4 * (1.0 - k_eff)) * L1 * L2;
    Real m = M;
    if (det < det_min) {
        det = det_min;
        m = std::copysign(std::sqrt(L1 * L2 - det_min), M);
    }

    InverseInductance inverse;
    inverse.g11 = L2 / det;
    inverse.g22 = L1 / det;
    inverse.g12 = -m / det;
    inverse.delta11 = inverse.g11 - 1.0 / L1;
    inverse.delta22 = inverse.g22 - 1.0 / L2;
    return inverse;
}

void MutualInductance::stamp_ac(ComplexMNASystem& sys,
                                 Real omega,
                                 const DenseVector& dc_solution) const {
    (void)dc_solution;
    // The coupled pair has the admittance L^{-1} / (j omega), L its
    // inductance matrix [[L1, M], [M, L2]], between the inductor voltages
    // v1 = V(np1) - V(nn1) and v2 = V(np2) - V(nn2). Each inductor stamps
    // 1/(j omega L) of its own (Inductor::stamp_ac); this device adds the rest
    // of the diagonal and the coupling, as stamp_transient does with the
    // factor of its step.
    if (!L1_ || !L2_) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: inductors not set", name()));
    }
    auto* ind1 = dynamic_cast<const Inductor*>(L1_);
    auto* ind2 = dynamic_cast<const Inductor*>(L2_);
    if (!ind1 || !ind2) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: failed to cast inductors", name()));
    }

    // At omega = 0 the circuit is that of the DC analysis, where the coupling
    // stamps nothing (stamp_dc_linear) and each inductor is the short of its
    // DC model (Inductor::stamp_ac): v = j omega (L i) is 0 whatever the
    // currents are, not 1/0
    if (omega == 0.0) {
        return;
    }

    const Complex factor = Complex(1.0, 0.0) / Complex(0.0, omega);
    auto idx = [](NodeId n) { return (n == NODE_GND) ? -1 : static_cast<int>(n - 1); };

    // A group: its lead adds (L^-1 - diag(1/L_i)) / (j omega) of the group
    if (group_) {
        if (!lead_) return;
        const std::size_t n = group_->inductors.size();
        const auto gamma = group_->inverse();
        for (std::size_t i = 0; i < n; ++i) {
            const auto* a = static_cast<const Inductor*>(group_->inductors[i]);
            const int pi = idx(a->np()), ni = idx(a->nn());
            for (std::size_t j = 0; j < n; ++j) {
                const auto* b = static_cast<const Inductor*>(group_->inductors[j]);
                const int pj = idx(b->np()), nj = idx(b->nn());
                const Complex y = factor * (gamma[i][j] - ((i == j) ? 1.0 / a->inductance() : 0.0));
                if (pi >= 0 && pj >= 0) sys.Y.add_entry(pi, pj, y);
                if (pi >= 0 && nj >= 0) sys.Y.add_entry(pi, nj, -y);
                if (ni >= 0 && pj >= 0) sys.Y.add_entry(ni, pj, -y);
                if (ni >= 0 && nj >= 0) sys.Y.add_entry(ni, nj, y);
            }
        }
        return;
    }

    const InverseInductance inverse = inverse_inductance();
    const int np1 = idx(ind1->np()), nn1 = idx(ind1->nn());
    const int np2 = idx(ind2->np()), nn2 = idx(ind2->nn());

    // Current y * (V(p_ctrl) - V(n_ctrl)) from p_out to n_out
    auto add_transadmittance = [&](int p_out, int n_out, int p_ctrl, int n_ctrl, Complex y) {
        if (p_out >= 0 && p_ctrl >= 0) sys.Y.add_entry(p_out, p_ctrl, y);
        if (p_out >= 0 && n_ctrl >= 0) sys.Y.add_entry(p_out, n_ctrl, -y);
        if (n_out >= 0 && p_ctrl >= 0) sys.Y.add_entry(n_out, p_ctrl, -y);
        if (n_out >= 0 && n_ctrl >= 0) sys.Y.add_entry(n_out, n_ctrl, y);
    };
    add_transadmittance(np1, nn1, np1, nn1, factor * inverse.delta11);
    add_transadmittance(np2, nn2, np2, nn2, factor * inverse.delta22);
    add_transadmittance(np1, nn1, np2, nn2, factor * inverse.g12);
    add_transadmittance(np2, nn2, np1, nn1, factor * inverse.g12);

    spdlog::trace("AC stamp: Mutual inductance {}, M={:.3e}H, omega={}",
                  name(), mutual_inductance(), omega);
}

std::vector<std::vector<Real>> MutualInductance::CoupledGroup::inductance_matrix() const {
    const std::size_t n = inductors.size();
    std::vector<std::vector<Real>> l(n, std::vector<Real>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) {
        l[i][i] = static_cast<const Inductor*>(inductors[i])->inductance();
    }
    for (const auto& c : couplings) {
        const Real m = c.k->coupling_coefficient() * std::sqrt(l[c.i][c.i] * l[c.j][c.j]);
        l[c.i][c.j] += m;
        l[c.j][c.i] += m;
    }
    return l;
}

std::vector<std::vector<Real>> MutualInductance::CoupledGroup::inverse() const {
    auto a = inductance_matrix();
    const std::size_t n = a.size();
    for (std::size_t i = 0; i < n; ++i) {
        a[i][i] *= 1.0 + 1e-9;
    }
    std::vector<std::vector<Real>> inv(n, std::vector<Real>(n, 0.0));
    for (std::size_t i = 0; i < n; ++i) inv[i][i] = 1.0;
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        }
        if (!(std::abs(a[pivot][col]) > 0.0)) {
            throw std::runtime_error("Coupled inductors: the inductance matrix is singular");
        }
        std::swap(a[col], a[pivot]);
        std::swap(inv[col], inv[pivot]);
        const Real d = a[col][col];
        for (std::size_t k = 0; k < n; ++k) {
            a[col][k] /= d;
            inv[col][k] /= d;
        }
        for (std::size_t r = 0; r < n; ++r) {
            if (r == col) continue;
            const Real f = a[r][col];
            if (f == 0.0) continue;
            for (std::size_t k = 0; k < n; ++k) {
                a[r][k] -= f * a[col][k];
                inv[r][k] -= f * inv[col][k];
            }
        }
    }
    return inv;
}

} // namespace kirimaspice::devices
