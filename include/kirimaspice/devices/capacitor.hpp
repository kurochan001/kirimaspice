// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/capacitor.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Capacitor: Linear 2-terminal dynamic device
/// I = C * dV/dt
/// Backward Euler companion model: I = G_eq * V + I_eq
/// where G_eq = C/Δt, I_eq = -C/Δt * V_prev
class Capacitor : public Device {
public:
    Capacitor(DeviceId id, const std::string& name,
              NodeId np, NodeId nn, Real capacitance);

    ~Capacitor() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: capacitor is open circuit
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        // Open circuit in DC analysis
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Capacitor is linear
    }

    // Transient analysis: Backward Euler companion model
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp admittance Y_C = jωC
    void stamp_ac(ComplexMNASystem& sys,
                 Real omega,
                 const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return true; }

    // State management
    void save_state() override;
    void restore_state() override;
    void reset_state() override;
    void accept_solution(const DenseVector& node_voltages) override;

    Real get_charge() const override { return charge_; }

    // Physical quantity-based error estimation
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Accessors
    Real capacitance() const { return capacitance_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }

    // Sensitivity analysis: Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    Real capacitance_;   // Capacitance (F)

    // State variables
    mutable Real voltage_prev_;  // V(t) - voltage at previous timestep
    mutable Real current_prev_;  // I(t) - current at previous timestep (for Trapezoidal)
    mutable Real voltage_prev2_; // V(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real voltage_prev3_; // V(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real voltage_saved_; // Saved voltage for restore
    mutable Real current_saved_; // Saved current for restore
    mutable Real voltage_saved2_; // Saved V(t-Δt) for restore
    mutable Real voltage_saved3_; // Saved V(t-2Δt) for restore
    mutable Real charge_;        // Q = C * V (current)
    mutable Real charge_prev_;   // Q(t-Δt) - charge at previous timestep (for physical LTE)
    mutable Real timestep_;      // Current timestep (for accept_solution)
    mutable IntegrationMethod last_method_;  // Integration method used in last stamp
    mutable Real timestep_saved_;  // Saved timestep for restore
    mutable IntegrationMethod method_saved_;  // Saved method for restore
    mutable bool first_accept_;  // Flag for first accept_solution() call (Sprint 2.1 fix)

    // BDF coefficients for variable-timestep integration (saved from stamp_transient)
    mutable Real bdf_alpha0_;  // α₀ coefficient
    mutable Real bdf_alpha1_;  // α₁ coefficient
    mutable Real bdf_alpha2_;  // α₂ coefficient
    mutable Real bdf_alpha3_;  // α₃ coefficient (for BDF3)
    mutable Real bdf_beta0_;   // β₀ coefficient
    mutable bool use_bdf_var_;  // Flag: true if using variable-timestep BDF

    // TR-BDF2 intermediate state (Stage 1 gamma-point solution)
    mutable Real voltage_gamma_;  // V(t + γΔt) - voltage at gamma point (Stage 1 solution)
    mutable Real current_gamma_;  // I(t + γΔt) - current at gamma point (Stage 1 solution)
    mutable Real voltage_gamma_saved_;  // Saved gamma voltage for restore
    mutable Real current_gamma_saved_;  // Saved gamma current for restore
    mutable int tr_bdf2_stage_;   // Current TR-BDF2 stage (1 or 2, saved from stamp_transient)
};

} // namespace kirimaspice::devices
