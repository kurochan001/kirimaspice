// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/capacitor.cpp
#include "kirimaspice/devices/capacitor.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace kirimaspice::devices {

Capacitor::Capacitor(DeviceId id, const std::string& name,
                     NodeId np, NodeId nn, Real capacitance)
    : Device(id, name, DeviceType::CAPACITOR)
    , nodes_{np, nn}
    , capacitance_(capacitance)
    , voltage_prev_(0.0)
    , current_prev_(0.0)
    , voltage_prev2_(0.0)  // For BDF2
    , voltage_prev3_(0.0)  // For BDF3
    , voltage_saved_(0.0)
    , current_saved_(0.0)
    , voltage_saved2_(0.0)  // For BDF2
    , voltage_saved3_(0.0)  // For BDF3
    , charge_(0.0)
    , charge_prev_(0.0)  // For physical LTE estimation
    , timestep_(1e-9)  // Default timestep
    , last_method_(IntegrationMethod::BACKWARD_EULER)  // Default method
    , timestep_saved_(1e-9)  // Initialize saved timestep
    , method_saved_(IntegrationMethod::BACKWARD_EULER)  // Initialize saved method
    , first_accept_(true)  // Sprint 2.1 fix: initialize to true
    , bdf_alpha0_(0.0)
    , bdf_alpha1_(0.0)
    , bdf_alpha2_(0.0)
    , bdf_alpha3_(0.0)
    , bdf_beta0_(0.0)
    , use_bdf_var_(false)  // BDF variable-timestep coefficients
    , voltage_gamma_(0.0)  // TR-BDF2 intermediate state
    , current_gamma_(0.0)
    , voltage_gamma_saved_(0.0)
    , current_gamma_saved_(0.0)
    , tr_bdf2_stage_(0) {

    if (capacitance <= 0.0) {
        throw std::runtime_error("Capacitor capacitance must be positive: " + name);
    }

    spdlog::debug("Created capacitor {} (np={}, nn={}, C={}F)",
                  name, np, nn, capacitance_);
}

void Capacitor::stamp_transient(MNASystem& mna,
                                const DenseVector& node_voltages,
                                Real timestep,
                                IntegrationMethod method,
                                const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Save timestep and method for accept_solution
    timestep_ = timestep;
    last_method_ = method;

    // Companion model depends on integration method
    //
    // Backward Euler:
    //   I(t+Δt) = (C/Δt) * [V(t+Δt) - V(t)]
    //   G_eq = C/Δt,  I_eq = (C/Δt) * V(t)
    //
    // Trapezoidal:
    //   I(t+Δt) = (2C/Δt) * [V(t+Δt) - V(t)] - I(t)
    //   G_eq = 2C/Δt,  I_eq = -(2C/Δt) * V(t) - I(t)

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];

    Real g_eq, i_eq;

    if (method == IntegrationMethod::TRAPEZOIDAL) {
        // Trapezoidal method (2nd order)
        g_eq = 2.0 * capacitance_ / timestep;
        i_eq = -g_eq * voltage_prev_ - current_prev_;

        spdlog::trace("{} stamp_trap: V_prev={:.6e}, I_prev={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                     name_, voltage_prev_, current_prev_, g_eq, i_eq);
    } else if (method == IntegrationMethod::GEAR2) {
        // BDF2 method (2nd order)
        // Variable-timestep BDF2 formula when bdf_coef is provided:
        //   dV/dt ≈ (α₀*V_{n+1} + α₁*V_n + α₂*V_{n-1}) / β₀
        //   I = C * dV/dt = (C*α₀/β₀)*V_{n+1} + (C/β₀)*(α₁*V_n + α₂*V_{n-1})
        //   g_eq = C*α₀/β₀,  i_eq = (C/β₀)*(α₁*V_n + α₂*V_{n-1})
        //
        // Fixed-timestep fallback (uniform dt):
        //   dV/dt ≈ (3V_{n+1} - 4V_n + V_{n-1}) / (2Δt)
        //   g_eq = 3C/(2Δt),  i_eq = (-2C/Δt)*V_n + (C/2Δt)*V_{n-1}

        if (bdf_coef != nullptr) {
            // Save BDF coefficients for accept_solution()
            bdf_alpha0_ = bdf_coef->alpha0;
            bdf_alpha1_ = bdf_coef->alpha1;
            bdf_alpha2_ = bdf_coef->alpha2;
            bdf_alpha3_ = 0.0;  // Not used in BDF2
            bdf_beta0_ = bdf_coef->beta0;
            use_bdf_var_ = true;

            // Use variable-timestep coefficients
            g_eq = capacitance_ * bdf_coef->alpha0 / bdf_coef->beta0;
            i_eq = (capacitance_ / bdf_coef->beta0) *
                   (bdf_coef->alpha1 * voltage_prev_ + bdf_coef->alpha2 * voltage_prev2_);

            spdlog::trace("{} stamp_BDF2_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.3e}, V_prev={:.6e}, V_prev2={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, bdf_coef->alpha0, bdf_coef->alpha1, bdf_coef->alpha2, bdf_coef->beta0,
                         voltage_prev_, voltage_prev2_, g_eq, i_eq);
        } else {
            // Fixed-timestep fallback
            g_eq = 1.5 * capacitance_ / timestep;  // 3C/(2Δt)
            i_eq = (-2.0 * capacitance_ / timestep) * voltage_prev_ +
                   (0.5 * capacitance_ / timestep) * voltage_prev2_;

            spdlog::trace("{} stamp_BDF2_fixed: V_prev={:.6e}, V_prev2={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, voltage_prev_, voltage_prev2_, g_eq, i_eq);
        }
    } else if (method == IntegrationMethod::GEAR3) {
        // BDF3 method (3rd order)
        // Variable-timestep BDF3 formula when bdf_coef is provided:
        //   dV/dt ≈ (α₀*V_{n+1} + α₁*V_n + α₂*V_{n-1} + α₃*V_{n-2}) / β₀
        //   I = C * dV/dt = (C*α₀/β₀)*V_{n+1} + (C/β₀)*(α₁*V_n + α₂*V_{n-1} + α₃*V_{n-2})
        //   g_eq = C*α₀/β₀,  i_eq = (C/β₀)*(α₁*V_n + α₂*V_{n-1} + α₃*V_{n-2})
        //
        // Fixed-timestep fallback (uniform dt):
        //   dV/dt ≈ (11V_{n+1} - 18V_n + 9V_{n-1} - 2V_{n-2}) / (6Δt)
        //   g_eq = 11C/(6Δt),  i_eq = (C/6Δt)*(-18V_n + 9V_{n-1} - 2V_{n-2})

        if (bdf_coef != nullptr) {
            // Save BDF coefficients for accept_solution()
            bdf_alpha0_ = bdf_coef->alpha0;
            bdf_alpha1_ = bdf_coef->alpha1;
            bdf_alpha2_ = bdf_coef->alpha2;
            bdf_alpha3_ = bdf_coef->alpha3;
            bdf_beta0_ = bdf_coef->beta0;
            use_bdf_var_ = true;

            // Use variable-timestep coefficients
            g_eq = capacitance_ * bdf_coef->alpha0 / bdf_coef->beta0;
            i_eq = (capacitance_ / bdf_coef->beta0) *
                   (bdf_coef->alpha1 * voltage_prev_ + bdf_coef->alpha2 * voltage_prev2_ +
                    bdf_coef->alpha3 * voltage_prev3_);

            spdlog::trace("{} stamp_BDF3_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, α₃={:.6f}, β₀={:.3e}, V_prev={:.6e}, V_prev2={:.6e}, V_prev3={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, bdf_coef->alpha0, bdf_coef->alpha1, bdf_coef->alpha2, bdf_coef->alpha3, bdf_coef->beta0,
                         voltage_prev_, voltage_prev2_, voltage_prev3_, g_eq, i_eq);
        } else {
            // Fixed-timestep fallback
            g_eq = (11.0 / 6.0) * capacitance_ / timestep;  // 11C/(6Δt)
            i_eq = (capacitance_ / (6.0 * timestep)) *
                   (-18.0 * voltage_prev_ + 9.0 * voltage_prev2_ - 2.0 * voltage_prev3_);

            spdlog::trace("{} stamp_BDF3_fixed: V_prev={:.6e}, V_prev2={:.6e}, V_prev3={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, voltage_prev_, voltage_prev2_, voltage_prev3_, g_eq, i_eq);
        }
    } else if (method == IntegrationMethod::TR_BDF2) {
        // TR-BDF2 method (2nd order, L-stable)
        // Full 2-stage implementation:
        //   Stage 1: Trapezoidal from t_n to t_n + γΔt
        //   Stage 2: Modified BDF2 from t_n to t_n + Δt (using V_n and V_{n+γ})
        //
        // When tr_bdf2_coef is provided, use stage-specific companion models.
        // Otherwise, fall back to BDF2 approximation.

        if (tr_bdf2_coef != nullptr) {
            // Save stage information for accept_solution()
            tr_bdf2_stage_ = tr_bdf2_coef->stage;

            if (tr_bdf2_coef->stage == 1) {
                // Stage 1: Trapezoidal rule with stage timestep γΔt
                // I(t+γΔt) = (2C/γΔt)[V(t+γΔt) - V(t)] - I(t)
                // Companion model: I = G_eq * V + I_eq
                //   G_eq = 2C / (γΔt)
                //   I_eq = -(2C / γΔt) * V_n - I_n

                Real stage_dt = tr_bdf2_coef->stage_dt;  // γΔt
                g_eq = 2.0 * capacitance_ / stage_dt;
                i_eq = -g_eq * voltage_prev_ - current_prev_;

                spdlog::trace("{} stamp_TRBDF2_Stage1: γΔt={:.6e}, V_prev={:.6e}, I_prev={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                             name_, stage_dt, voltage_prev_, current_prev_, g_eq, i_eq);

            } else if (tr_bdf2_coef->stage == 2) {
                // Stage 2: Modified BDF2 with full timestep Δt (using V_n and V_{n+γ})
                // I(t+Δt) = C * [α₀*V_{n+1} + α₁*V_{n+γ} + α₂*V_n] / (β₀*Δt)
                // Companion model: I = G_eq * V_{n+1} + I_eq
                //   G_eq = C * α₀ / (β₀*Δt)
                //   I_eq = C * [α₁*V_{n+γ} + α₂*V_n] / (β₀*Δt)

                Real full_dt = tr_bdf2_coef->full_dt;  // Δt

                // Save coefficients for accept_solution
                bdf_alpha0_ = tr_bdf2_coef->alpha0;
                bdf_alpha1_ = tr_bdf2_coef->alpha1;
                bdf_alpha2_ = tr_bdf2_coef->alpha2;
                bdf_beta0_ = tr_bdf2_coef->beta0;
                use_bdf_var_ = true;

                g_eq = capacitance_ * bdf_alpha0_ / (bdf_beta0_ * full_dt);
                i_eq = capacitance_ * (bdf_alpha1_ * voltage_gamma_ + bdf_alpha2_ * voltage_prev_) / (bdf_beta0_ * full_dt);

                spdlog::trace("{} stamp_TRBDF2_Stage2: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.6f}, Δt={:.6e}, "
                             "V_gamma={:.6e}, V_prev={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                             name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_beta0_, full_dt,
                             voltage_gamma_, voltage_prev_, g_eq, i_eq);

            } else {
                throw std::runtime_error("Invalid TR-BDF2 stage: " + std::to_string(tr_bdf2_coef->stage));
            }

        } else {
            // Fallback: Use BDF2 companion model as approximation (fixed-step)
            g_eq = 1.5 * capacitance_ / timestep;  // 3C/(2Δt)
            i_eq = (-2.0 * capacitance_ / timestep) * voltage_prev_ +
                   (0.5 * capacitance_ / timestep) * voltage_prev2_;

            spdlog::trace("{} stamp_TRBDF2_fallback: V_prev={:.6e}, V_prev2={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, voltage_prev_, voltage_prev2_, g_eq, i_eq);
        }

    } else {
        // Backward Euler method (1st order, default)
        // I(t+Δt) = (C/Δt)[V(t+Δt) - V(t)]
        g_eq = capacitance_ / timestep;
        i_eq = -g_eq * voltage_prev_;

        spdlog::trace("{} stamp_BE: V_prev={:.6e}, I_prev={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                     name_, voltage_prev_, current_prev_, g_eq, i_eq);
    }

    // Diagnostic code removed after fixing time management bug

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp G matrix (same pattern as resistor)
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, np_idx, g_eq);
        if (nn_idx >= 0) {
            mna.G.add_entry(np_idx, nn_idx, -g_eq);
        }
    }

    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, nn_idx, g_eq);
        if (np_idx >= 0) {
            mna.G.add_entry(nn_idx, np_idx, -g_eq);
        }
    }

    // Stamp RHS (equivalent current source)
    // Note: For Trapezoidal, I_eq can be negative
    // Positive I_eq means current flows FROM the capacitor (np -> nn)
    if (np_idx >= 0) {
        mna.b[np_idx] -= i_eq;  // Current flows OUT of np
    }
    if (nn_idx >= 0) {
        mna.b[nn_idx] += i_eq;  // Current flows INTO nn
    }
}

void Capacitor::save_state() {
    voltage_saved_ = voltage_prev_;
    current_saved_ = current_prev_;
    voltage_saved2_ = voltage_prev2_;
    voltage_saved3_ = voltage_prev3_;
    timestep_saved_ = timestep_;
    method_saved_ = last_method_;
    // TR-BDF2 gamma-point state
    voltage_gamma_saved_ = voltage_gamma_;
    current_gamma_saved_ = current_gamma_;
}

void Capacitor::restore_state() {
    voltage_prev_ = voltage_saved_;
    current_prev_ = current_saved_;
    voltage_prev2_ = voltage_saved2_;
    voltage_prev3_ = voltage_saved3_;
    timestep_ = timestep_saved_;
    last_method_ = method_saved_;
    // TR-BDF2 gamma-point state
    voltage_gamma_ = voltage_gamma_saved_;
    current_gamma_ = current_gamma_saved_;
}

void Capacitor::reset_state() {
    // Reset all state variables to initial values
    voltage_prev_ = 0.0;
    current_prev_ = 0.0;
    voltage_prev2_ = 0.0;
    voltage_prev3_ = 0.0;
    voltage_saved_ = 0.0;
    current_saved_ = 0.0;
    voltage_saved2_ = 0.0;
    voltage_saved3_ = 0.0;
    charge_ = 0.0;
    charge_prev_ = 0.0;  // Reset charge history for temperature sweep
    voltage_gamma_ = 0.0;  // TR-BDF2 gamma point voltage
    current_gamma_ = 0.0;  // TR-BDF2 gamma point current
    voltage_gamma_saved_ = 0.0;
    current_gamma_saved_ = 0.0;
    timestep_ = 1e-9;  // Default timestep
    last_method_ = IntegrationMethod::BACKWARD_EULER;

    spdlog::trace("[{}] reset_state: all state variables reset to initial values", name_);
}

void Capacitor::accept_solution(const DenseVector& node_voltages) {
    // Update voltage and current for next timestep
    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];

    Real v_np = (np == NODE_GND) ? 0.0 : node_voltages[np];
    Real v_nn = (nn == NODE_GND) ? 0.0 : node_voltages[nn];

    Real voltage_new = v_np - v_nn;

    // Diagnostic code removed after fixing time management bug

    // DEBUG LOG - before update
    spdlog::trace("{} accept (before): V_prev={:.6e}, I_prev={:.6e}, V_new={:.6e}",
                 name_, voltage_prev_, current_prev_, voltage_new);

    // Calculate current based on integration method
    if (last_method_ == IntegrationMethod::TRAPEZOIDAL) {
        // Trapezoidal: I(t+Δt) = (2C/Δt)[V(t+Δt) - V(t)] - I(t)
        Real g_eq = 2.0 * capacitance_ / timestep_;
        Real current_new = g_eq * (voltage_new - voltage_prev_) - current_prev_;
        current_prev_ = current_new;

        spdlog::trace("{} accept_trap: I_new={:.6e}, g_eq={:.6e}, dV={:.6e}",
                     name_, current_new, g_eq, voltage_new - voltage_prev_);
    } else if (last_method_ == IntegrationMethod::GEAR2) {
        // BDF2: Use variable-timestep coefficients if available
        if (use_bdf_var_) {
            // Variable-timestep BDF2:
            // I = C * (α₀*V_{n+1} + α₁*V_n + α₂*V_{n-1}) / β₀
            Real coef = capacitance_ / bdf_beta0_;
            current_prev_ = coef * (bdf_alpha0_ * voltage_new +
                                    bdf_alpha1_ * voltage_prev_ +
                                    bdf_alpha2_ * voltage_prev2_);

            spdlog::trace("{} accept_BDF2_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.3e}, I_new={:.6e}",
                         name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_beta0_, current_prev_);
        } else {
            // Fixed-timestep BDF2: I = C * (3V_{n+1} - 4V_n + V_{n-1}) / (2Δt)
            current_prev_ = capacitance_ * (3.0 * voltage_new - 4.0 * voltage_prev_ + voltage_prev2_) / (2.0 * timestep_);

            spdlog::trace("{} accept_BDF2: I_new={:.6e}",
                         name_, current_prev_);
        }
    } else if (last_method_ == IntegrationMethod::GEAR3) {
        // BDF3: Use variable-timestep coefficients if available
        if (use_bdf_var_) {
            // Variable-timestep BDF3:
            // I = C * (α₀*V_{n+1} + α₁*V_n + α₂*V_{n-1} + α₃*V_{n-2}) / β₀
            Real coef = capacitance_ / bdf_beta0_;
            current_prev_ = coef * (bdf_alpha0_ * voltage_new +
                                    bdf_alpha1_ * voltage_prev_ +
                                    bdf_alpha2_ * voltage_prev2_ +
                                    bdf_alpha3_ * voltage_prev3_);

            spdlog::trace("{} accept_BDF3_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, α₃={:.6f}, β₀={:.3e}, I_new={:.6e}",
                         name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_alpha3_, bdf_beta0_, current_prev_);
        } else {
            // Fixed-timestep BDF3: I = C * (11V_{n+1} - 18V_n + 9V_{n-1} - 2V_{n-2}) / (6Δt)
            current_prev_ = capacitance_ * (11.0 * voltage_new - 18.0 * voltage_prev_ +
                                           9.0 * voltage_prev2_ - 2.0 * voltage_prev3_) / (6.0 * timestep_);

            spdlog::trace("{} accept_BDF3: I_new={:.6e}",
                         name_, current_prev_);
        }
    } else if (last_method_ == IntegrationMethod::TR_BDF2) {
        // TR-BDF2: Two-stage method
        if (tr_bdf2_stage_ == 1) {
            // Stage 1 completed: Save gamma-point solution
            // Do NOT update history yet (we're at t_n + γΔt, not t_n + Δt)
            // Use trapezoidal rule: I(t+γΔt) = (2C/γΔt)[V(t+γΔt) - V(t)] - I(t)
            voltage_gamma_ = voltage_new;
            Real g_eq = 2.0 * capacitance_ / timestep_;
            current_gamma_ = g_eq * (voltage_gamma_ - voltage_prev_) - current_prev_;

            spdlog::trace("{} accept_TRBDF2_Stage1: V_gamma={:.6e}, I_gamma={:.6e}, γΔt={:.6e}",
                         name_, voltage_gamma_, current_gamma_, timestep_);

            // Early return - history update happens only after Stage 2
            return;

        } else if (tr_bdf2_stage_ == 2) {
            // Stage 2 completed: Normal history update
            // Calculate current using Modified BDF2: I = C * [α₀*V_{n+1} + α₁*V_{n+γ} + α₂*V_n] / (β₀*Δt)
            if (use_bdf_var_) {
                current_prev_ = capacitance_ * (bdf_alpha0_ * voltage_new + bdf_alpha1_ * voltage_gamma_ +
                                               bdf_alpha2_ * voltage_prev_) / (bdf_beta0_ * timestep_);
                spdlog::trace("{} accept_TRBDF2_Stage2_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.6f}, I_new={:.6e}",
                             name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_beta0_, current_prev_);
            } else {
                current_prev_ = capacitance_ * (voltage_new - voltage_prev_) / timestep_;
                spdlog::trace("{} accept_TRBDF2_Stage2: I_new={:.6e}",
                             name_, current_prev_);
            }
        } else {
            // Fallback for TR_BDF2 without stage information (simplified version)
            current_prev_ = capacitance_ * (3.0 * voltage_new - 4.0 * voltage_prev_ + voltage_prev2_) / (2.0 * timestep_);

            spdlog::trace("{} accept_TRBDF2_fallback: I_new={:.6e}",
                         name_, current_prev_);
        }
    } else {
        // Backward Euler: I(t+Δt) = (C/Δt)[V(t+Δt) - V(t)]
        current_prev_ = capacitance_ * (voltage_new - voltage_prev_) / timestep_;

        spdlog::trace("{} accept_BE: I_new={:.6e}",
                     name_, current_prev_);
    }

    // Shift history for BDF methods (must be done AFTER using current values)
    // Note: For TR-BDF2 Stage 1, we returned early above without updating history
    voltage_prev3_ = voltage_prev2_;
    voltage_prev2_ = voltage_prev_;
    voltage_prev_ = voltage_new;

    // Update charge and save previous charge
    // Sprint 2.1 fix: On first call, initialize charge_prev from current state
    if (first_accept_) {
        // First accept_solution(): Initialize history from DC operating point
        // CRITICAL FIX: DC operating point has no current flow through capacitors
        current_prev_ = 0.0;
        charge_ = capacitance_ * voltage_prev_;
        charge_prev_ = charge_;  // No prediction on first step
        first_accept_ = false;
        spdlog::trace("{} accept_first: charge initialized to {:.6e}, current reset to 0", name_, charge_);
    } else {
        // Subsequent calls: Normal history update
        charge_prev_ = charge_;
        charge_ = capacitance_ * voltage_prev_;
    }

    spdlog::trace("{} accept (after): V_prev={:.6e}, V_prev2={:.6e}, V_prev3={:.6e}, I_prev={:.6e}",
                 name_, voltage_prev_, voltage_prev2_, voltage_prev3_, current_prev_);
}

Real Capacitor::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    // Physical quantity-based error estimation
    // Predictor-corrector approach for charge Q = C * V
    //
    // Theory: The actual physical quantity for a capacitor is charge Q = C * V.
    // We use a predictor-corrector method to estimate the local truncation error:
    //   Predictor: Q_pred = Q(t) + dt * I(t)  (1st order extrapolation)
    //   Corrector: Q_corr = C * V(t+dt)       (actual computed result from new solution)
    //   Error: ΔQ = |Q_corr - Q_pred|
    //
    // This is more accurate than voltage-based error estimation because:
    // 1. Charge is the conserved quantity (dQ/dt = I)
    // 2. Voltage can have large relative changes while charge changes smoothly
    // 3. This matches ngspice's CHGTOL-based error control
    //
    // CRITICAL FIX: Compute Q_corr from node_voltages (new solution) instead of
    // using charge_ (which is not yet updated at LTE calculation time)

    // Get voltage from new solution
    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    Real v_np = (np == NODE_GND) ? 0.0 : node_voltages[np];
    Real v_nn = (nn == NODE_GND) ? 0.0 : node_voltages[nn];
    Real voltage_new = v_np - v_nn;

    // Predictor: Q_pred = Q_prev + dt * I_prev (1st order extrapolation)
    Real Q_pred = charge_prev_ + timestep_ * current_prev_;

    // Corrector: Q_corr = C * V_new (actual computed result from new solution)
    Real Q_corr = capacitance_ * voltage_new;

    // Error: ΔQ = |Q_corr - Q_pred|
    Real Q_error = std::abs(Q_corr - Q_pred);

    // Final: ngspice-compatible scaling (SPICE3f5 heritage)
    // Use additive scaling: max(|Q|) + chgtol
    // This provides smooth transition between absolute (small Q) and relative (large Q) error control
    // Reference: ngspice src/spicelib/analysis/cktdump.c, Berkeley SPICE3f5
    Real Q_scale = std::max(std::abs(Q_corr), std::abs(Q_pred)) + chgtol;

    // Normalized error: LTE = ΔQ / Q_scale
    Real lte = Q_error / Q_scale;

    spdlog::trace("{} physical_lte: V_new={:.6e}, Q_prev={:.6e}, Q_pred={:.6e}, Q_corr={:.6e}, ΔQ={:.6e}, Q_scale={:.6e}, LTE={:.6e}",
                 name_, voltage_new, charge_prev_, Q_pred, Q_corr, Q_error, Q_scale, lte);

    return lte;
}

void Capacitor::stamp_ac(ComplexMNASystem& sys,
                        Real omega,
                        const DenseVector& dc_solution) const {
    // AC analysis: Capacitor admittance Y_C = jωC
    // In frequency domain: I = jωC * V

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];

    // Complex admittance: Y_C = 0 + j(ωC)
    Complex Y_C(0.0, omega * capacitance_);

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp admittance matrix (same pattern as resistor)
    if (np_idx >= 0) {
        sys.Y.add_entry(np_idx, np_idx, Y_C);
        if (nn_idx >= 0) {
            sys.Y.add_entry(np_idx, nn_idx, -Y_C);
        }
    }

    if (nn_idx >= 0) {
        sys.Y.add_entry(nn_idx, nn_idx, Y_C);
        if (np_idx >= 0) {
            sys.Y.add_entry(nn_idx, np_idx, -Y_C);
        }
    }

    spdlog::trace("Capacitor {} AC stamp: Y_C = j*{:.3e} (ω={:.3e} rad/s, C={:.3e} F)",
                  name_, omega * capacitance_, omega, capacitance_);
}

// ========== Sensitivity Analysis: Parameter Access ==========

std::optional<Real> Capacitor::get_parameter(const std::string& param_name) const {
    if (param_name == "capacitance" || param_name == "C") {
        return capacitance_;
    }
    return std::nullopt;  // Parameter not found
}

bool Capacitor::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "capacitance" || param_name == "C") {
        if (value <= 0.0) {
            spdlog::warn("Capacitor {}: capacitance must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        capacitance_ = value;
        spdlog::trace("Capacitor {}: set capacitance = {} F", name_, capacitance_);
        return true;
    }
    return false;  // Parameter not found
}

std::vector<std::string> Capacitor::get_perturbable_parameters() const {
    return {"capacitance"};
}

} // namespace kirimaspice::devices
