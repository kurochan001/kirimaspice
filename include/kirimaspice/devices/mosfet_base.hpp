// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mosfet_base.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"

namespace kirimaspice::devices {

/// MOSFET Base class: Common state management for all MOSFET variants
///
/// This base class provides:
/// - State variables for transient analysis (G-S, G-D, G-B junctions)
/// - BDF coefficient management for variable-timestep integration
/// - Temperature parameters (KT1, KT2, UTE, TNOM)
/// - Common save_state/restore_state/reset_state implementations
///
/// Derived classes (MOSFET_N, MOSFET_P, MOSFET_Level3_N, BSIM3, BSIM4, etc.)
/// inherit these common features and implement device-specific behavior.
class MOSFET_Base : public Device {
public:
    MOSFET_Base(DeviceId id, const std::string& name, DeviceType type)
        : Device(id, name, type)
        // State variables for transient analysis - G-S junction
        , vgs_prev_(0.0)
        , vgs_prev2_(0.0)
        , vgs_prev3_(0.0)
        , vgs_saved_(0.0)
        , vgs_saved2_(0.0)
        , vgs_saved3_(0.0)
        , charge_gs_(0.0)
        , charge_gs_prev_(0.0)
        // State variables for transient analysis - G-D junction
        , vgd_prev_(0.0)
        , vgd_prev2_(0.0)
        , vgd_prev3_(0.0)
        , vgd_saved_(0.0)
        , vgd_saved2_(0.0)
        , vgd_saved3_(0.0)
        , charge_gd_(0.0)
        , charge_gd_prev_(0.0)
        // State variables for transient analysis - G-B junction
        , vgb_prev_(0.0)
        , vgb_prev2_(0.0)
        , vgb_prev3_(0.0)
        , vgb_saved_(0.0)
        , vgb_saved2_(0.0)
        , vgb_saved3_(0.0)
        , charge_gb_(0.0)
        , charge_gb_prev_(0.0)
        // Common transient state
        , timestep_(0.0)
        , last_method_(IntegrationMethod::BACKWARD_EULER)
        , timestep_saved_(0.0)
        , method_saved_(IntegrationMethod::BACKWARD_EULER)
        , first_accept_(true)
        // BDF coefficients
        , bdf_alpha0_(0.0)
        , bdf_alpha1_(0.0)
        , bdf_alpha2_(0.0)
        , bdf_alpha3_(0.0)
        , bdf_beta0_(0.0)
        , use_bdf_var_(false)
        // Temperature parameters
        , kt1_(0.0)
        , kt2_(0.0)
        , ute_(-1.5)
        , tnom_(300.15) {}

    ~MOSFET_Base() override = default;

    // ===== State Management (common to all MOSFET variants) =====

    /// Save state for potential rollback
    void save_state() override {
        // Save voltage history for all 3 junctions (G-S, G-D, G-B)
        vgs_saved_ = vgs_prev_;
        vgs_saved2_ = vgs_prev2_;
        vgs_saved3_ = vgs_prev3_;

        vgd_saved_ = vgd_prev_;
        vgd_saved2_ = vgd_prev2_;
        vgd_saved3_ = vgd_prev3_;

        vgb_saved_ = vgb_prev_;
        vgb_saved2_ = vgb_prev2_;
        vgb_saved3_ = vgb_prev3_;

        timestep_saved_ = timestep_;
        method_saved_ = last_method_;
    }

    /// Restore state from saved values
    void restore_state() override {
        // Restore voltage history for all 3 junctions (G-S, G-D, G-B)
        vgs_prev_ = vgs_saved_;
        vgs_prev2_ = vgs_saved2_;
        vgs_prev3_ = vgs_saved3_;

        vgd_prev_ = vgd_saved_;
        vgd_prev2_ = vgd_saved2_;
        vgd_prev3_ = vgd_saved3_;

        vgb_prev_ = vgb_saved_;
        vgb_prev2_ = vgb_saved2_;
        vgb_prev3_ = vgb_saved3_;

        timestep_ = timestep_saved_;
        last_method_ = method_saved_;
    }

    /// Reset state to initial conditions
    void reset_state() override {
        // Reset all voltage history
        vgs_prev_ = vgs_prev2_ = vgs_prev3_ = 0.0;
        vgs_saved_ = vgs_saved2_ = vgs_saved3_ = 0.0;

        vgd_prev_ = vgd_prev2_ = vgd_prev3_ = 0.0;
        vgd_saved_ = vgd_saved2_ = vgd_saved3_ = 0.0;

        vgb_prev_ = vgb_prev2_ = vgb_prev3_ = 0.0;
        vgb_saved_ = vgb_saved2_ = vgb_saved3_ = 0.0;

        // Reset charge history
        charge_gs_ = charge_gs_prev_ = 0.0;
        charge_gd_ = charge_gd_prev_ = 0.0;
        charge_gb_ = charge_gb_prev_ = 0.0;

        // Reset transient state
        timestep_ = timestep_saved_ = 0.0;
        last_method_ = method_saved_ = IntegrationMethod::BACKWARD_EULER;
        first_accept_ = true;

        // Reset BDF coefficients
        bdf_alpha0_ = bdf_alpha1_ = bdf_alpha2_ = bdf_alpha3_ = 0.0;
        bdf_beta0_ = 0.0;
        use_bdf_var_ = false;
    }

    /// Get total charge for LTE estimation
    Real get_charge() const override {
        return charge_gs_ + charge_gd_ + charge_gb_;
    }

    // ===== Temperature Parameter Accessors =====

    Real get_kt1() const { return kt1_; }
    Real get_kt2() const { return kt2_; }
    Real get_ute() const { return ute_; }
    Real get_tnom() const { return tnom_; }

protected:
    // ===== State variables for transient analysis - G-S junction =====
    mutable Real vgs_prev_;       ///< V_GS(t) - voltage at previous timestep
    mutable Real vgs_prev2_;      ///< V_GS(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real vgs_prev3_;      ///< V_GS(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real vgs_saved_;      ///< Saved V_GS for restore
    mutable Real vgs_saved2_;     ///< Saved V_GS(t-Δt) for restore
    mutable Real vgs_saved3_;     ///< Saved V_GS(t-2Δt) for restore
    mutable Real charge_gs_;      ///< Q_GS = ∫Cgs(V)dV (current charge)
    mutable Real charge_gs_prev_; ///< Q_GS(t-Δt) - charge at previous timestep

    // ===== State variables for transient analysis - G-D junction =====
    mutable Real vgd_prev_;       ///< V_GD(t) - voltage at previous timestep
    mutable Real vgd_prev2_;      ///< V_GD(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real vgd_prev3_;      ///< V_GD(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real vgd_saved_;      ///< Saved V_GD for restore
    mutable Real vgd_saved2_;     ///< Saved V_GD(t-Δt) for restore
    mutable Real vgd_saved3_;     ///< Saved V_GD(t-2Δt) for restore
    mutable Real charge_gd_;      ///< Q_GD = ∫Cgd(V)dV (current charge)
    mutable Real charge_gd_prev_; ///< Q_GD(t-Δt) - charge at previous timestep

    // ===== State variables for transient analysis - G-B junction =====
    mutable Real vgb_prev_;       ///< V_GB(t) - voltage at previous timestep
    mutable Real vgb_prev2_;      ///< V_GB(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real vgb_prev3_;      ///< V_GB(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real vgb_saved_;      ///< Saved V_GB for restore
    mutable Real vgb_saved2_;     ///< Saved V_GB(t-Δt) for restore
    mutable Real vgb_saved3_;     ///< Saved V_GB(t-2Δt) for restore
    mutable Real charge_gb_;      ///< Q_GB = ∫Cgb(V)dV (current charge)
    mutable Real charge_gb_prev_; ///< Q_GB(t-Δt) - charge at previous timestep

    // ===== Common transient state =====
    mutable Real timestep_;               ///< Current timestep (for accept_solution)
    mutable IntegrationMethod last_method_; ///< Integration method used in last stamp
    mutable Real timestep_saved_;         ///< Saved timestep for restore
    mutable IntegrationMethod method_saved_; ///< Saved method for restore
    mutable bool first_accept_;           ///< Flag for first accept_solution() call

    // ===== BDF coefficients for variable-timestep integration =====
    mutable Real bdf_alpha0_;  ///< α₀ coefficient
    mutable Real bdf_alpha1_;  ///< α₁ coefficient
    mutable Real bdf_alpha2_;  ///< α₂ coefficient
    mutable Real bdf_alpha3_;  ///< α₃ coefficient (for BDF3)
    mutable Real bdf_beta0_;   ///< β₀ coefficient
    mutable bool use_bdf_var_; ///< Flag: true if using variable-timestep BDF

    // ===== Temperature parameters =====
    Real kt1_;   ///< VTO temperature coefficient [V/K]
    Real kt2_;   ///< VTO second-order temperature coefficient [V/K²]
    Real ute_;   ///< Mobility temperature exponent (default: -1.5)
    Real tnom_;  ///< Nominal temperature [K] (default: 300.15K = 27°C)

    // ===== Physical constants =====
    static constexpr Real k_boltzmann = 1.380649e-23; ///< Boltzmann constant [J/K]
    static constexpr Real T_nominal = 300.15;         ///< Nominal temperature (K, 27°C)
    static constexpr Real gmin_default = 1e-12;       ///< Minimum conductance [S]

    /// Helper: Update BDF coefficients from external coefficients
    void update_bdf_coefficients(const BDFCoefficients* bdf_coef) {
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
    }

    /// Helper: Scale threshold voltage with temperature
    /// @return VTO(T) = VTO(Tnom) + KT1×(T-Tnom) + KT2×(T-Tnom)²
    Real scale_vth_with_temperature(Real vth_tnom, Real temp_kelvin) const {
        return core::scale_vth_temperature(vth_tnom, kt1_, kt2_, temp_kelvin, tnom_);
    }

    /// Helper: Scale mobility/transconductance with temperature
    /// @return KP(T) = KP(Tnom) × (T/Tnom)^UTE
    Real scale_kp_with_temperature(Real kp_tnom, Real temp_kelvin) const {
        return core::scale_mobility_temperature(kp_tnom, ute_, temp_kelvin, tnom_);
    }
};

} // namespace kirimaspice::devices
