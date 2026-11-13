// src/devices/inductor.cpp
#include "kirimaspice/devices/inductor.hpp"
#include <spdlog/spdlog.h>
#include <iostream>
#include <stdexcept>

namespace kirimaspice::devices {

Inductor::Inductor(DeviceId id, const std::string& name,
                   NodeId np, NodeId nn, Real inductance)
    : Device(id, name, DeviceType::INDUCTOR)
    , nodes_{np, nn}
    , inductance_(inductance)
    , branch_id_(INVALID_BRANCH)  // Reserved for future branch variable support
    , current_prev_(0.0)
    , voltage_prev_(0.0)
    , current_prev2_(0.0)  // For BDF2
    , current_prev3_(0.0)  // For BDF3
    , current_saved_(0.0)
    , voltage_saved_(0.0)
    , current_saved2_(0.0)  // For BDF2
    , current_saved3_(0.0)  // For BDF3
    , flux_(0.0)
    , flux_prev_(0.0)  // For physical LTE estimation
    , last_timestep_(1e-9)  // Default timestep
    , last_method_(IntegrationMethod::BACKWARD_EULER)  // Default method
    , timestep_saved_(1e-9)  // Default timestep for saved state
    , method_saved_(IntegrationMethod::BACKWARD_EULER)  // Default method for saved state
    , first_accept_(true)  // Sprint 2.1 fix: initialize to true
    , bdf_alpha0_(0.0)
    , bdf_alpha1_(0.0)
    , bdf_alpha2_(0.0)
    , bdf_alpha3_(0.0)
    , bdf_beta0_(0.0)
    , use_bdf_var_(false)  // BDF variable-timestep coefficients
    , current_gamma_(0.0)  // TR-BDF2 intermediate state
    , voltage_gamma_(0.0)
    , current_gamma_saved_(0.0)
    , voltage_gamma_saved_(0.0)
    , tr_bdf2_stage_(0)
    , is_coupled_(false) {

    if (inductance <= 0.0) {
        throw std::runtime_error("Inductor inductance must be positive: " + name);
    }

    spdlog::debug("Created inductor {} (np={}, nn={}, L={}H)",
                  name, np, nn, inductance_);
}

void Inductor::stamp_transient(MNASystem& mna,
                               const DenseVector& node_voltages,
                               Real timestep,
                               IntegrationMethod method,
                               const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Save timestep and method for accept_solution
    last_timestep_ = timestep;
    last_method_ = method;

    // Companion model depends on integration method
    //
    // Backward Euler:
    //   V(t+Δt) = (L/Δt) * [I(t+Δt) - I(t)]
    //   I(t+Δt) = (Δt/L) * V(t+Δt) + I(t)
    //   G_eq = Δt/L,  I_eq = I(t)
    //
    // Trapezoidal:
    //   V(t+Δt) = (2L/Δt) * [I(t+Δt) - I(t)] - V(t)
    //   I(t+Δt) = (Δt/2L) * V(t+Δt) + I(t) + (Δt/2L) * V(t)
    //   G_eq = Δt/2L,  I_eq = I(t) + (Δt/2L) * V(t)

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];

    Real g_eq, i_eq;

    if (method == IntegrationMethod::TRAPEZOIDAL) {
        // Trapezoidal method (2nd order)
        g_eq = timestep / (2.0 * inductance_);
        i_eq = current_prev_ + g_eq * voltage_prev_;
    } else if (method == IntegrationMethod::GEAR2) {
        // BDF2 method (2nd order)
        // Variable-timestep BDF2 formula when bdf_coef is provided:
        //   dI/dt ≈ (α₀*I_{n+1} + α₁*I_n + α₂*I_{n-1}) / β₀
        //   V = L * dI/dt => I_{n+1} = (β₀/(α₀*L))*V - (α₁*I_n + α₂*I_{n-1})/α₀
        //   g_eq = β₀/(α₀*L),  i_eq = -(α₁*I_n + α₂*I_{n-1})/α₀
        //
        // Fixed-timestep fallback (uniform dt):
        //   dI/dt ≈ (3I_{n+1} - 4I_n + I_{n-1}) / (2Δt)
        //   I_{n+1} = (2Δt/3L)*V + (4/3)*I_n - (1/3)*I_{n-1}
        //   g_eq = 2Δt/(3L),  i_eq = (4/3)*I_n - (1/3)*I_{n-1}

        if (bdf_coef != nullptr) {
            // Use variable-timestep coefficients
            g_eq = bdf_coef->beta0 / (bdf_coef->alpha0 * inductance_);
            i_eq = -(bdf_coef->alpha1 * current_prev_ + bdf_coef->alpha2 * current_prev2_) / bdf_coef->alpha0;

            // Save BDF coefficients for accept_solution()
            bdf_alpha0_ = bdf_coef->alpha0;
            bdf_alpha1_ = bdf_coef->alpha1;
            bdf_alpha2_ = bdf_coef->alpha2;
            bdf_alpha3_ = 0.0;  // Not used in BDF2
            bdf_beta0_ = bdf_coef->beta0;
            use_bdf_var_ = true;

            spdlog::trace("{} stamp_BDF2_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.3e}, I_prev={:.6e}, I_prev2={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, bdf_coef->alpha0, bdf_coef->alpha1, bdf_coef->alpha2, bdf_coef->beta0,
                         current_prev_, current_prev2_, g_eq, i_eq);
        } else {
            // Fixed-timestep fallback
            g_eq = (2.0 * timestep) / (3.0 * inductance_);  // 2Δt/(3L)
            i_eq = (4.0 / 3.0) * current_prev_ - (1.0 / 3.0) * current_prev2_;
            use_bdf_var_ = false;  // Using fixed-timestep

            spdlog::trace("{} stamp_BDF2_fixed: I_prev={:.6e}, I_prev2={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, current_prev_, current_prev2_, g_eq, i_eq);
        }
    } else if (method == IntegrationMethod::GEAR3) {
        // BDF3 method (3rd order)
        // Variable-timestep BDF3 formula when bdf_coef is provided:
        //   dI/dt ≈ (α₀*I_{n+1} + α₁*I_n + α₂*I_{n-1} + α₃*I_{n-2}) / β₀
        //   V = L * dI/dt => I_{n+1} = (β₀/(α₀*L))*V - (α₁*I_n + α₂*I_{n-1} + α₃*I_{n-2})/α₀
        //   g_eq = β₀/(α₀*L),  i_eq = -(α₁*I_n + α₂*I_{n-1} + α₃*I_{n-2})/α₀
        //
        // Fixed-timestep fallback (uniform dt):
        //   dI/dt ≈ (11I_{n+1} - 18I_n + 9I_{n-1} - 2I_{n-2}) / (6Δt)
        //   I_{n+1} = (6Δt/11L)*V + (18/11)*I_n - (9/11)*I_{n-1} + (2/11)*I_{n-2}
        //   g_eq = 6Δt/(11L),  i_eq = (18/11)*I_n - (9/11)*I_{n-1} + (2/11)*I_{n-2}

        if (bdf_coef != nullptr) {
            // Use variable-timestep coefficients
            g_eq = bdf_coef->beta0 / (bdf_coef->alpha0 * inductance_);
            i_eq = -(bdf_coef->alpha1 * current_prev_ + bdf_coef->alpha2 * current_prev2_ +
                     bdf_coef->alpha3 * current_prev3_) / bdf_coef->alpha0;

            // Save BDF coefficients for accept_solution()
            bdf_alpha0_ = bdf_coef->alpha0;
            bdf_alpha1_ = bdf_coef->alpha1;
            bdf_alpha2_ = bdf_coef->alpha2;
            bdf_alpha3_ = bdf_coef->alpha3;
            bdf_beta0_ = bdf_coef->beta0;
            use_bdf_var_ = true;

            spdlog::trace("{} stamp_BDF3_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, α₃={:.6f}, β₀={:.3e}, I_prev={:.6e}, I_prev2={:.6e}, I_prev3={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, bdf_coef->alpha0, bdf_coef->alpha1, bdf_coef->alpha2, bdf_coef->alpha3, bdf_coef->beta0,
                         current_prev_, current_prev2_, current_prev3_, g_eq, i_eq);
        } else {
            // Fixed-timestep fallback
            g_eq = (6.0 * timestep) / (11.0 * inductance_);  // 6Δt/(11L)
            i_eq = (18.0 / 11.0) * current_prev_ - (9.0 / 11.0) * current_prev2_ + (2.0 / 11.0) * current_prev3_;
            use_bdf_var_ = false;  // Using fixed-timestep

            spdlog::trace("{} stamp_BDF3_fixed: I_prev={:.6e}, I_prev2={:.6e}, I_prev3={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, current_prev_, current_prev2_, current_prev3_, g_eq, i_eq);
        }
    } else if (method == IntegrationMethod::TR_BDF2) {
        // TR-BDF2 method (2nd order, L-stable)
        // Full 2-stage implementation:
        //   Stage 1: Trapezoidal from t_n to t_n + γΔt
        //   Stage 2: Modified BDF2 from t_n to t_n + Δt (using I_n and I_{n+γ})
        //
        // When tr_bdf2_coef is provided, use stage-specific companion models.
        // Otherwise, fall back to BDF2 approximation.

        if (tr_bdf2_coef != nullptr) {
            // Save stage information for accept_solution()
            tr_bdf2_stage_ = tr_bdf2_coef->stage;

            if (tr_bdf2_coef->stage == 1) {
                // Stage 1: Trapezoidal rule with stage timestep γΔt
                // V(t+γΔt) = (2L/γΔt)[I(t+γΔt) - I(t)] - V(t)
                // I(t+γΔt) = (γΔt/2L)V(t+γΔt) + I(t) + (γΔt/2L)V(t)
                // Companion model: I = G_eq * V + I_eq
                //   G_eq = γΔt / (2L)
                //   I_eq = I_n + (γΔt/2L) * V_n

                Real stage_dt = tr_bdf2_coef->stage_dt;  // γΔt
                g_eq = stage_dt / (2.0 * inductance_);
                i_eq = current_prev_ + g_eq * voltage_prev_;

                spdlog::trace("{} stamp_TRBDF2_Stage1: γΔt={:.6e}, I_prev={:.6e}, V_prev={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                             name_, stage_dt, current_prev_, voltage_prev_, g_eq, i_eq);

            } else if (tr_bdf2_coef->stage == 2) {
                // Stage 2: Modified BDF2 with full timestep Δt (using I_n and I_{n+γ})
                // V(t+Δt) = L * [α₀*I_{n+1} + α₁*I_{n+γ} + α₂*I_n] / (β₀*Δt)
                // Companion model: I_{n+1} = G_eq * V_{n+1} + I_eq
                //   G_eq = β₀*Δt / (α₀*L)
                //   I_eq = -[α₁*I_{n+γ} + α₂*I_n] / α₀

                Real full_dt = tr_bdf2_coef->full_dt;  // Δt

                // Save coefficients for accept_solution
                bdf_alpha0_ = tr_bdf2_coef->alpha0;
                bdf_alpha1_ = tr_bdf2_coef->alpha1;
                bdf_alpha2_ = tr_bdf2_coef->alpha2;
                bdf_beta0_ = tr_bdf2_coef->beta0;
                use_bdf_var_ = true;

                g_eq = (bdf_beta0_ * full_dt) / (bdf_alpha0_ * inductance_);
                i_eq = -(bdf_alpha1_ * current_gamma_ + bdf_alpha2_ * current_prev_) / bdf_alpha0_;

                spdlog::trace("{} stamp_TRBDF2_Stage2: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.6f}, Δt={:.6e}, "
                             "I_gamma={:.6e}, I_prev={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                             name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_beta0_, full_dt,
                             current_gamma_, current_prev_, g_eq, i_eq);

            } else {
                throw std::runtime_error("Invalid TR-BDF2 stage: " + std::to_string(tr_bdf2_coef->stage));
            }

        } else {
            // Fallback: Use BDF2 companion model as approximation (fixed-step)
            g_eq = (2.0 * timestep) / (3.0 * inductance_);  // 2Δt/(3L)
            i_eq = (4.0 / 3.0) * current_prev_ - (1.0 / 3.0) * current_prev2_;

            spdlog::trace("{} stamp_TRBDF2_fallback: I_prev={:.6e}, I_prev2={:.6e}, g_eq={:.6e}, I_eq={:.6e}",
                         name_, current_prev_, current_prev2_, g_eq, i_eq);
        }

    } else {
        // Backward Euler method (1st order, default)
        g_eq = timestep / inductance_;
        i_eq = current_prev_;
    }

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
    if (np_idx >= 0) {
        mna.b[np_idx] -= i_eq;  // Note: negative because current flows into np
    }
    if (nn_idx >= 0) {
        mna.b[nn_idx] += i_eq;
    }
}

void Inductor::save_state() {
    current_saved_ = current_prev_;
    voltage_saved_ = voltage_prev_;
    current_saved2_ = current_prev2_;
    current_saved3_ = current_prev3_;
    timestep_saved_ = last_timestep_;
    method_saved_ = last_method_;
    // TR-BDF2 gamma-point state
    current_gamma_saved_ = current_gamma_;
    voltage_gamma_saved_ = voltage_gamma_;
}

void Inductor::restore_state() {
    current_prev_ = current_saved_;
    voltage_prev_ = voltage_saved_;
    current_prev2_ = current_saved2_;
    current_prev3_ = current_saved3_;
    last_timestep_ = timestep_saved_;
    last_method_ = method_saved_;
    // TR-BDF2 gamma-point state
    current_gamma_ = current_gamma_saved_;
    voltage_gamma_ = voltage_gamma_saved_;
}

void Inductor::add_current_delta(Real delta) {
    current_prev_ += delta;
    flux_ += inductance_ * delta;  // keep flux consistent
}

void Inductor::reset_state() {
    // Reset all state variables to initial values
    current_prev_ = 0.0;
    voltage_prev_ = 0.0;
    current_prev2_ = 0.0;
    current_prev3_ = 0.0;
    current_saved_ = 0.0;
    voltage_saved_ = 0.0;
    current_saved2_ = 0.0;
    current_saved3_ = 0.0;
    flux_ = 0.0;
    flux_prev_ = 0.0;  // Reset flux history for temperature sweep
    current_gamma_ = 0.0;  // TR-BDF2 gamma point current
    voltage_gamma_ = 0.0;  // TR-BDF2 gamma point voltage
    current_gamma_saved_ = 0.0;
    voltage_gamma_saved_ = 0.0;
    last_timestep_ = 1e-9;  // Default timestep
    last_method_ = IntegrationMethod::BACKWARD_EULER;
    timestep_saved_ = 1e-9;  // Default timestep for saved state
    method_saved_ = IntegrationMethod::BACKWARD_EULER;  // Default method for saved state

    spdlog::trace("[{}] reset_state: all state variables reset to initial values", name_);
}

void Inductor::accept_solution(const DenseVector& node_voltages) {
    // Update current and voltage for next timestep

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];

    Real v_np = (np == NODE_GND) ? 0.0 : node_voltages[np];
    Real v_nn = (nn == NODE_GND) ? 0.0 : node_voltages[nn];
    Real voltage_new = v_np - v_nn;

    Real current_old = current_prev_;

    // Update current based on integration method
    // Only skip if timestep is effectively zero (e.g., during DC initialization)
    if (last_timestep_ > 0.0) {
        if (last_method_ == IntegrationMethod::TRAPEZOIDAL) {
            // Trapezoidal: I(t+Δt) = I(t) + (Δt/2L)[V(t+Δt) + V(t)]
            current_prev_ = current_prev_ + (last_timestep_ / (2.0 * inductance_)) * (voltage_new + voltage_prev_);
        } else if (last_method_ == IntegrationMethod::GEAR2) {
            // BDF2: Use variable-timestep coefficients if available
            if (use_bdf_var_) {
                // Variable-timestep BDF2:
                // I_{n+1} = (β₀/(α₀*L))*V_{n+1} - (α₁*I_n + α₂*I_{n-1})/α₀
                Real g_eq = bdf_beta0_ / (bdf_alpha0_ * inductance_);
                Real i_eq = -(bdf_alpha1_ * current_prev_ + bdf_alpha2_ * current_prev2_) / bdf_alpha0_;
                Real new_current = g_eq * voltage_new + i_eq;
                current_prev_ = new_current;

                spdlog::trace("{} accept_BDF2_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.3e}, I_new={:.6e}",
                             name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_beta0_, new_current);
            } else {
                // Fixed-timestep BDF2: I_{n+1} = (2Δt/3L)*V_{n+1} + (4/3)*I_n - (1/3)*I_{n-1}
                Real new_current = ((2.0 * last_timestep_) / (3.0 * inductance_)) * voltage_new +
                                   (4.0 / 3.0) * current_prev_ - (1.0 / 3.0) * current_prev2_;
                current_prev_ = new_current;
            }
        } else if (last_method_ == IntegrationMethod::GEAR3) {
            // BDF3: Use variable-timestep coefficients if available
            if (use_bdf_var_) {
                // Variable-timestep BDF3:
                // I_{n+1} = (β₀/(α₀*L))*V_{n+1} - (α₁*I_n + α₂*I_{n-1} + α₃*I_{n-2})/α₀
                Real g_eq = bdf_beta0_ / (bdf_alpha0_ * inductance_);
                Real i_eq = -(bdf_alpha1_ * current_prev_ + bdf_alpha2_ * current_prev2_ + bdf_alpha3_ * current_prev3_) / bdf_alpha0_;
                Real new_current = g_eq * voltage_new + i_eq;
                current_prev_ = new_current;

                spdlog::trace("{} accept_BDF3_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, α₃={:.6f}, β₀={:.3e}, I_new={:.6e}",
                             name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_alpha3_, bdf_beta0_, new_current);
            } else {
                // Fixed-timestep BDF3: I_{n+1} = (6Δt/11L)*V_{n+1} + (18/11)*I_n - (9/11)*I_{n-1} + (2/11)*I_{n-2}
                Real new_current = ((6.0 * last_timestep_) / (11.0 * inductance_)) * voltage_new +
                                   (18.0 / 11.0) * current_prev_ - (9.0 / 11.0) * current_prev2_ + (2.0 / 11.0) * current_prev3_;
                current_prev_ = new_current;
            }
        } else if (last_method_ == IntegrationMethod::TR_BDF2) {
            // TR-BDF2: Two-stage method
            if (tr_bdf2_stage_ == 1) {
                // Stage 1 completed: Save gamma-point solution
                // Do NOT update history yet (we're at t_n + γΔt, not t_n + Δt)
                // Use trapezoidal rule: I(t+γΔt) = (γΔt/2L)V(t+γΔt) + I(t) + (γΔt/2L)V(t)
                Real g_eq = last_timestep_ / (2.0 * inductance_);
                current_gamma_ = g_eq * voltage_new + current_prev_ + g_eq * voltage_prev_;
                voltage_gamma_ = voltage_new;

                spdlog::trace("{} accept_TRBDF2_Stage1: I_gamma={:.6e}, V_gamma={:.6e}, γΔt={:.6e}",
                             name_, current_gamma_, voltage_gamma_, last_timestep_);

                // Early return - history update happens only after Stage 2
                return;

            } else if (tr_bdf2_stage_ == 2) {
                // Stage 2 completed: Normal history update
                // Calculate current using Modified BDF2: I_{n+1} = (β₀Δt/(α₀L))V_{n+1} - (α₁/α₀)I_{n+γ} - (α₂/α₀)I_n
                if (use_bdf_var_) {
                    Real i_n = current_prev_;  // Save I_n before overwriting
                    Real g_eq = (bdf_beta0_ * last_timestep_) / (bdf_alpha0_ * inductance_);
                    current_prev_ = g_eq * voltage_new - (bdf_alpha1_ / bdf_alpha0_) * current_gamma_ -
                                   (bdf_alpha2_ / bdf_alpha0_) * i_n;
                    spdlog::trace("{} accept_TRBDF2_Stage2_var: α₀={:.6f}, α₁={:.6f}, α₂={:.6f}, β₀={:.6f}, I_new={:.6e}",
                                 name_, bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_beta0_, current_prev_);
                } else {
                    Real i_n = current_prev_;  // Save I_n before overwriting
                    Real g_eq = (2.0 * last_timestep_) / (3.0 * inductance_);
                    current_prev_ = g_eq * voltage_new + ((4.0 / 3.0) * i_n - (1.0 / 3.0) * current_prev2_);
                    spdlog::trace("{} accept_TRBDF2_Stage2: I_new={:.6e}",
                                 name_, current_prev_);
                }
            } else {
                // Fallback for TR_BDF2 without stage information (simplified version)
                Real new_current = ((2.0 * last_timestep_) / (3.0 * inductance_)) * voltage_new +
                                   (4.0 / 3.0) * current_prev_ - (1.0 / 3.0) * current_prev2_;
                current_prev_ = new_current;

                spdlog::trace("{} accept_TRBDF2_fallback: I_new={:.6e}",
                             name_, new_current);
            }
        } else {
            // Backward Euler: I(t+Δt) = I(t) + (Δt/L) * V(t+Δt)
            current_prev_ = current_prev_ + (last_timestep_ / inductance_) * voltage_new;
        }
    } else {
        // DC initialization or first call: keep I = 0
        current_prev_ = 0.0;
    }

    // Shift history for BDF methods (must be done AFTER using current values)
    // Note: For TR-BDF2 Stage 1, we returned early above without updating history
    current_prev3_ = current_prev2_;
    current_prev2_ = current_old;  // Save old current before it was updated
    voltage_prev_ = voltage_new;

    // Update flux and save previous flux
    // Sprint 2.1 fix: On first call, initialize flux_prev from current state
    if (first_accept_) {
        // First accept_solution(): Initialize history from DC operating point
        flux_ = inductance_ * current_prev_;
        flux_prev_ = flux_;  // No prediction on first step
        first_accept_ = false;
        spdlog::trace("{} accept_first: flux initialized to {:.6e}", name_, flux_);
    } else {
        // Subsequent calls: Normal history update
        flux_prev_ = flux_;
        flux_ = inductance_ * current_prev_;
    }

    // Debug trace
    const char* method_str = (last_method_ == IntegrationMethod::TRAPEZOIDAL) ? "TRAP" :
                             (last_method_ == IntegrationMethod::GEAR2) ? "BDF2" :
                             (last_method_ == IntegrationMethod::GEAR3) ? "BDF3" : "BE";
    spdlog::trace("[{}] accept_solution: last_timestep={:.9e}, method={}, V_new={:.9e}, I_old={:.9e} -> I_new={:.9e}, I_prev2={:.9e}, I_prev3={:.9e}",
                  name_, last_timestep_, method_str,
                  voltage_new, current_old, current_prev_, current_prev2_, current_prev3_);
}

Real Inductor::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    // If coupled, local flux calculation (L*I) is invalid because Φ = L1*I1 + M*I2.
    // We disable LTE check for coupled inductors to prevent erroneous timestep shrinkage.
    // TODO: Implement proper LTE check in MutualInductance.
    if (is_coupled_) {
        std::cout << "Inductor " << name_ << " is coupled, skipping LTE check." << std::endl;
        return 0.0;
    } else {
        std::cout << "Inductor " << name_ << " is NOT coupled, checking LTE." << std::endl;
    }

    // Physical quantity-based error estimation
    // Predictor-corrector approach for flux Φ = L * I
    //
    // Theory: The actual physical quantity for an inductor is flux Φ = L * I.
    // We use a predictor-corrector method to estimate the local truncation error:
    //   Predictor: Φ_pred = Φ(t) + dt * V(t)  (1st order extrapolation, since dΦ/dt = V)
    //   Corrector: Φ_corr = L * I(t+dt)       (actual computed result from new solution)
    //   Error: ΔΦ = |Φ_corr - Φ_pred|
    //
    // This is more accurate than current-based error estimation because:
    // 1. Flux is the conserved quantity (dΦ/dt = V)
    // 2. Current can have large relative changes while flux changes smoothly
    // 3. This matches ngspice's flux-based error control for inductors
    //
    // Note: We normalize by (chgtol/sec) to make flux error comparable to charge error:
    //   LTE = ΔΦ / (chgtol / 1sec) = ΔΦ * 1sec / chgtol
    //   Units: (Wb) * (1/s) / (C) = (V·s) * (1/s) / (A·s) = V / A (dimensionless ratio)
    //
    // CRITICAL FIX: Compute Φ_corr from node_voltages (new solution) instead of
    // using flux_ (which is not yet updated at LTE calculation time)

    // Get voltage from new solution
    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    Real v_np = (np == NODE_GND) ? 0.0 : node_voltages[np];
    Real v_nn = (nn == NODE_GND) ? 0.0 : node_voltages[nn];
    Real voltage_new = v_np - v_nn;

    // Calculate new current from voltage using integration method
    Real current_new;
    if (last_timestep_ > 0.0) {
        if (last_method_ == IntegrationMethod::TRAPEZOIDAL) {
            // Trapezoidal: I(t+Δt) = I(t) + (Δt/2L)[V(t+Δt) + V(t)]
            current_new = current_prev_ + (last_timestep_ / (2.0 * inductance_)) * (voltage_new + voltage_prev_);
        } else if (last_method_ == IntegrationMethod::GEAR2) {
            // BDF2: I_{n+1} = (2Δt/3L)*V_{n+1} + (4/3)*I_n - (1/3)*I_{n-1}
            current_new = ((2.0 * last_timestep_) / (3.0 * inductance_)) * voltage_new +
                          (4.0 / 3.0) * current_prev_ - (1.0 / 3.0) * current_prev2_;
        } else if (last_method_ == IntegrationMethod::GEAR3) {
            // BDF3: I_{n+1} = (6Δt/11L)*V_{n+1} + (18/11)*I_n - (9/11)*I_{n-1} + (2/11)*I_{n-2}
            current_new = ((6.0 * last_timestep_) / (11.0 * inductance_)) * voltage_new +
                          (18.0 / 11.0) * current_prev_ - (9.0 / 11.0) * current_prev2_ + (2.0 / 11.0) * current_prev3_;
        } else {
            // Backward Euler: I(t+Δt) = I(t) + (Δt/L) * V(t+Δt)
            current_new = current_prev_ + (last_timestep_ / inductance_) * voltage_new;
        }
    } else {
        // DC initialization
        current_new = 0.0;
    }

    // Predictor: Φ_pred = Φ_prev + dt * V_prev (1st order extrapolation)
    Real Phi_pred = flux_prev_ + last_timestep_ * voltage_prev_;

    // Corrector: Φ_corr = L * I_new (actual computed result from new solution)
    Real Phi_corr = inductance_ * current_new;

    // Error: ΔΦ = |Φ_corr - Φ_pred|
    Real Phi_error = std::abs(Phi_corr - Phi_pred);

    // Final: ngspice-compatible scaling (SPICE3f5 heritage)
    // Use additive scaling: max(|Φ|) + trtol
    // This provides smooth transition between absolute (small Φ) and relative (large Φ) error control
    // Note: For inductors, we use chgtol/1sec as flux tolerance (Weber = V·s = A·s²/V)
    // Reference: ngspice src/spicelib/analysis/cktdump.c, Berkeley SPICE3f5
    Real chgtol_per_sec = chgtol / 1.0;  // Convert charge tolerance to flux tolerance
    Real Phi_scale = std::max(std::abs(Phi_corr), std::abs(Phi_pred)) + chgtol_per_sec;

    // Normalized error: LTE = ΔΦ / Phi_scale
    Real lte = Phi_error / Phi_scale;

    spdlog::trace("{} physical_lte: V_new={:.6e}, I_new={:.6e}, Φ_prev={:.6e}, Φ_pred={:.6e}, Φ_corr={:.6e}, ΔΦ={:.6e}, Φ_scale={:.6e}, LTE={:.6e}",
                 name_, voltage_new, current_new, flux_prev_, Phi_pred, Phi_corr, Phi_error, Phi_scale, lte);

    return lte;
}

void Inductor::stamp_ac(ComplexMNASystem& sys,
                       Real omega,
                       const DenseVector& dc_solution) const {
    // AC analysis: Inductor admittance Y_L = 1/(jωL)
    // Z_L = jωL => Y_L = 1/Z_L = 1/(jωL) = -j/(ωL)

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];

    // Complex admittance: Y_L = 1/(jωL) = -j/(ωL)
    // = 0 - j/(ωL)
    Complex j_omega_L(0.0, omega * inductance_);
    Complex Y_L = Complex(1.0, 0.0) / j_omega_L;

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp admittance matrix (same pattern as resistor/capacitor)
    if (np_idx >= 0) {
        sys.Y.add_entry(np_idx, np_idx, Y_L);
        if (nn_idx >= 0) {
            sys.Y.add_entry(np_idx, nn_idx, -Y_L);
        }
    }

    if (nn_idx >= 0) {
        sys.Y.add_entry(nn_idx, nn_idx, Y_L);
        if (np_idx >= 0) {
            sys.Y.add_entry(nn_idx, np_idx, -Y_L);
        }
    }

    spdlog::trace("Inductor {} AC stamp: Y_L = -j/{:.3e} (ω={:.3e} rad/s, L={:.3e} H)",
                  name_, omega * inductance_, omega, inductance_);
}

// ========== Sensitivity Analysis: Parameter Access ==========

std::optional<Real> Inductor::get_parameter(const std::string& param_name) const {
    if (param_name == "inductance" || param_name == "L") {
        return inductance_;
    }
    return std::nullopt;  // Parameter not found
}

bool Inductor::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "inductance" || param_name == "L") {
        if (value <= 0.0) {
            spdlog::warn("Inductor {}: inductance must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        inductance_ = value;
        spdlog::trace("Inductor {}: set inductance = {} H", name_, inductance_);
        return true;
    }
    return false;  // Parameter not found
}

std::vector<std::string> Inductor::get_perturbable_parameters() const {
    return {"inductance"};
}

} // namespace kirimaspice::devices
