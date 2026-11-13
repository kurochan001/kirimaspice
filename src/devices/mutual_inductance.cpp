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

    // Determinant of inductance matrix (clamped for numerical safety)
    Real det = L1 * L2 - M * M;
    Real k_eff = std::abs(M) / std::sqrt(std::max(Real(1e-30), L1 * L2));
    // Avoid singular matrix at k≈1. Clamp relative to L1*L2 with k-aware guard (even softer for k→1).
    Real det_min = (1e-12 + 1e-4 * (1.0 - k_eff)) * L1 * L2;
    if (det < det_min) det = det_min;

    // Companion conductances derived from coupled inductance matrix
    // dI/dt ≈ (α₀ I_{n+1} + α₁ I_n + α₂ I_{n-1} + α₃ I_{n-2}) / β₀
    // I_{n+1} = (β₀/α₀) L^{-1} V_{n+1} - Σ (α_i/α₀) I_{n+1-i}
    // → G = (β₀/α₀) L^{-1}
    Real scale = beta0 / alpha0;
    Real g11_target = scale * L2 / det;     // coefficient for v1
    Real g22_target = scale * L1 / det;     // coefficient for v2
    Real g12 = -scale * M / det;            // coefficient for v2 into i1
    Real g21 = g12;                         // symmetric

    // Existing self stamps from each inductor (added separately)
    Real gL1 = scale / L1;
    Real gL2 = scale / L2;
    Real delta_g1 = g11_target - gL1;
    Real delta_g2 = g22_target - gL2;

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

    // TR-BDF2 stage2 uses gamma current/voltage for alpha1 term if available
    if (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef) {
        if (tr_bdf2_coef->stage == 2) {
            i1_prev = ind1->current_gamma();
            i2_prev = ind2->current_gamma();
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
}

// After each accepted timestep, recompute coupled inductor currents to keep history
// consistent with the coupled companion model.
void MutualInductance::accept_solution(const DenseVector& node_voltages) {
    if (!L1_ || !L2_) return;
    auto* ind1 = const_cast<Inductor*>(dynamic_cast<const Inductor*>(L1_));
    auto* ind2 = const_cast<Inductor*>(dynamic_cast<const Inductor*>(L2_));
    if (!ind1 || !ind2) return;

    // Node voltages (MNA includes GND at index 0)
    auto node_v = [&](NodeId n) -> Real {
        // node_voltages is 1-based (index 0 = GND) to align with NodeId.
        return (n == NODE_GND) ? 0.0 : node_voltages[n];
    };

    Real v1 = node_v(ind1->np()) - node_v(ind1->nn());
    Real v2 = node_v(ind2->np()) - node_v(ind2->nn());

    Real L1 = ind1->inductance();
    Real L2 = ind2->inductance();
    Real M = mutual_inductance();
    Real det = L1 * L2 - M * M;
    Real k_eff = std::abs(M) / std::sqrt(std::max(Real(1e-30), L1 * L2));
    Real det_guard = (1e-9 + 0.1 * (1.0 - k_eff)) * L1 * L2;
    if (det <= det_guard) return;

    // Use last_timestep from inductors
    Real dt = ind1->last_timestep();
    if (dt <= 0.0) return;
    IntegrationMethod m = ind1->last_method(); // assume both inductors share the same method

    // Effective coefficients consistent with stamp_transient
    Real alpha0 = 1.0, alpha1 = -1.0, alpha2 = 0.0, alpha3 = 0.0;
    Real beta0  = dt;

    if (ind1->use_bdf_var()) {
        alpha0 = ind1->bdf_alpha0();
        beta0  = ind1->bdf_beta0();
    } else if (m == IntegrationMethod::TR_BDF2) {
        // Stage is tracked inside inductor
        int stage = ind1->tr_bdf2_stage();
        if (stage == 1) {
            beta0 = 0.5 * dt;
            alpha0 = 1.0; alpha1 = -1.0; alpha2 = alpha3 = 0.0;
        } else {
            // Modified BDF2 coefficients: α0,α1,α2 saved in bdf fields when stamped
            alpha0 = ind1->bdf_alpha0();
            beta0  = ind1->bdf_beta0() * dt; // bdf_beta0 is normalized; stamp used β0*Δt
        }
    } else if (m == IntegrationMethod::GEAR2) {
        alpha0 = 1.5; alpha1 = -2.0; alpha2 = 0.5; alpha3 = 0.0; beta0 = dt;
    } else if (m == IntegrationMethod::GEAR3) {
        alpha0 = 11.0/6.0; alpha1 = -3.0; alpha2 = 1.5; alpha3 = -1.0/3.0; beta0 = dt;
    } else if (m == IntegrationMethod::TRAPEZOIDAL) {
        alpha0 = 1.0; alpha1 = -1.0; alpha2 = alpha3 = 0.0; beta0 = 0.5 * dt;
    } else { // BACKWARD_EULER and default
        alpha0 = 1.0; alpha1 = -1.0; alpha2 = alpha3 = 0.0; beta0 = dt;
    }

    Real scale = beta0 / alpha0;
    Real di1_dt_coupled = ( L2 * v1 - M * v2) / det;
    Real di2_dt_coupled = (-M * v1 + L1 * v2) / det;
    Real di1_dt_uncoupled = v1 / L1;
    Real di2_dt_uncoupled = v2 / L2;

    // ΔI = (β0/α0) * (dI/dt_coupled - dI/dt_uncoupled)
    Real delta_i1 = scale * (di1_dt_coupled - di1_dt_uncoupled);
    Real delta_i2 = scale * (di2_dt_coupled - di2_dt_uncoupled);

    // For trapezoidal (and TR-BDF2 stage1), add previous-voltage term
    if (m == IntegrationMethod::TRAPEZOIDAL ||
        (m == IntegrationMethod::TR_BDF2 && ind1->tr_bdf2_stage() == 1)) {
        Real v1_prev = ind1->voltage_prev();
        Real v2_prev = ind2->voltage_prev();
        Real g11_target = scale * L2 / det;
        Real g22_target = scale * L1 / det;
        Real g12 = -scale * M / det;
        Real gL1 = scale / L1;
        Real gL2 = scale / L2;
        delta_i1 += (g11_target - gL1) * v1_prev + g12 * v2_prev;
        delta_i2 += g12 * v1_prev + (g22_target - gL2) * v2_prev;
    }

    // Optional damping factor to better match ngspice behavior
    constexpr Real corr = 0.9;
    ind1->add_current_delta(corr * delta_i1);
    ind2->add_current_delta(corr * delta_i2);
}

void MutualInductance::stamp_ac(ComplexMNASystem& sys,
                                 Real omega,
                                 const DenseVector& dc_solution) const {
    // AC analysis: Add jωM cross-coupling
    //
    // Admittance matrix for coupled inductors:
    //   I1 = (V1 - V2) / (jωL1) + (V1) * jωM / (jωL1 * jωL2 - (jωM)²)
    //   I2 = (V2 - V1) / (jωL2) + (V2) * jωM / (jωL1 * jωL2 - (jωM)²)
    //
    // Simplified approach: Add mutual admittance Y_M = jωM between inductors
    //
    // For coupled inductors with nodes (n1p, n1n) and (n2p, n2n):
    //   Y11 = 1/(jωL1) + jωM_eff
    //   Y12 = -jωM_eff
    //   Y21 = -jωM_eff
    //   Y22 = 1/(jωL2) + jωM_eff
    //
    // However, this requires modifying the inductor stamps, which is complex.
    // For , we implement a simplified version that ignores the M term.

    if (!L1_ || !L2_) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: inductors not set", name()));
    }

    Real M = mutual_inductance();

    // Cast to Inductor to access nodes
    auto* ind1 = dynamic_cast<const Inductor*>(L1_);
    auto* ind2 = dynamic_cast<const Inductor*>(L2_);

    if (!ind1 || !ind2) {
        throw std::runtime_error(
            fmt::format("Mutual inductance {}: failed to cast inductors", name()));
    }

    NodeId np1 = ind1->np();
    NodeId nn1 = ind1->nn();
    NodeId np2 = ind2->np();
    NodeId nn2 = ind2->nn();

    // Convert to MNA indices
    auto np1_idx = (np1 == NODE_GND) ? -1 : static_cast<int>(np1 - 1);
    auto nn1_idx = (nn1 == NODE_GND) ? -1 : static_cast<int>(nn1 - 1);
    auto np2_idx = (np2 == NODE_GND) ? -1 : static_cast<int>(np2 - 1);
    auto nn2_idx = (nn2 == NODE_GND) ? -1 : static_cast<int>(nn2 - 1);

    // Mutual admittance: Y_M = jωM = j * omega * M
    Complex Y_M(0.0, omega * M);

    // Add mutual coupling terms
    // I1 += Y_M * V2, I2 += Y_M * V1
    // This is a simplified approximation

    // Cross-coupling from L2 to L1
    if (np1_idx >= 0 && np2_idx >= 0) {
        sys.Y.add_entry(np1_idx, np2_idx, Y_M);
    }
    if (np1_idx >= 0 && nn2_idx >= 0) {
        sys.Y.add_entry(np1_idx, nn2_idx, -Y_M);
    }
    if (nn1_idx >= 0 && np2_idx >= 0) {
        sys.Y.add_entry(nn1_idx, np2_idx, -Y_M);
    }
    if (nn1_idx >= 0 && nn2_idx >= 0) {
        sys.Y.add_entry(nn1_idx, nn2_idx, Y_M);
    }

    // Cross-coupling from L1 to L2
    if (np2_idx >= 0 && np1_idx >= 0) {
        sys.Y.add_entry(np2_idx, np1_idx, Y_M);
    }
    if (np2_idx >= 0 && nn1_idx >= 0) {
        sys.Y.add_entry(np2_idx, nn1_idx, -Y_M);
    }
    if (nn2_idx >= 0 && np1_idx >= 0) {
        sys.Y.add_entry(nn2_idx, np1_idx, -Y_M);
    }
    if (nn2_idx >= 0 && nn1_idx >= 0) {
        sys.Y.add_entry(nn2_idx, nn1_idx, Y_M);
    }

    spdlog::trace("AC stamp: Mutual inductance {}, M={:.3e}H, ωM={:.3e}, omega={}",
                  name(), M, omega * M, omega);
}

} // namespace kirimaspice::devices
