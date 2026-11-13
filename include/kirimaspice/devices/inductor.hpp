// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/inductor.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Inductor: Linear 2-terminal dynamic device
/// V = L * dI/dt
/// Backward Euler companion model (Norton equivalent):
/// I = G_eq * V + I_eq
/// where G_eq = Δt/L, I_eq = I_prev + Δt/L * V_prev
class Inductor : public Device {
public:
    Inductor(DeviceId id, const std::string& name,
             NodeId np, NodeId nn, Real inductance);

    ~Inductor() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: inductor is short circuit
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        // Short circuit in DC analysis
        // Treat as very small resistor (R = 1μΩ → G = 1e6 S)
        // Note: Too large values (>1e9) cause numerical conditioning issues
        // and prevent Newton-Raphson convergence (max_change=0 symptom)
        // 1e6 provides good balance: small voltage drop (<0.1μV) + stable convergence
        Real g_large = 1e6;  // Large conductance (1μΩ resistance)

        NodeId np = nodes_[0];
        NodeId nn = nodes_[1];

        // Convert node IDs to MNA indices (GND=0 → no entry, node_id → index = id-1)
        auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
        auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

        // Stamp conductance matrix
        if (np_idx >= 0) {
            mna.G.add_entry(np_idx, np_idx, g_large);
            if (nn_idx >= 0) {
                mna.G.add_entry(np_idx, nn_idx, -g_large);
            }
        }

        if (nn_idx >= 0) {
            mna.G.add_entry(nn_idx, nn_idx, g_large);
            if (np_idx >= 0) {
                mna.G.add_entry(nn_idx, np_idx, -g_large);
            }
        }
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Inductor is linear
    }

    // Transient analysis: Backward Euler companion model
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp admittance Y_L = 1/(jωL)
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

    Real get_flux() const override { return flux_; }

    // Physical quantity-based error estimation
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Accessors
    Real inductance() const { return inductance_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    BranchId branch_id() const { return branch_id_; }

    void set_branch_id(BranchId id) { branch_id_ = id; }

    // Sensitivity analysis: Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

    // Accessor for coupled elements
    Real current_prev() const { return current_prev_; }
    Real current_prev2() const { return current_prev2_; }
    Real current_prev3() const { return current_prev3_; }
    Real current_gamma() const { return current_gamma_; }
    Real voltage_prev() const { return voltage_prev_; }
    Real voltage_gamma() const { return voltage_gamma_; }
    Real last_timestep() const { return last_timestep_; }
    IntegrationMethod last_method() const { return last_method_; }
    int tr_bdf2_stage() const { return tr_bdf2_stage_; }
    // Apply delta to current and keep flux consistent
    void add_current_delta(Real delta);
    void set_current_prev(Real i) { current_prev_ = i; }
    void set_current_prev2(Real i) { current_prev2_ = i; }
    void set_current_prev3(Real i) { current_prev3_ = i; }
    void set_voltage_prev(Real v) { voltage_prev_ = v; }
    void set_coupled(bool c) { is_coupled_ = c; }

    // BDF state accessors for MutualInductance correction
    bool use_bdf_var() const { return use_bdf_var_; }
    Real bdf_alpha0() const { return bdf_alpha0_; }
    Real bdf_beta0() const { return bdf_beta0_; }

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    Real inductance_;    // Inductance (H)
    BranchId branch_id_; // Branch current variable index (reserved for future use)

    // State variables
    mutable Real current_prev_;  // I(t) - current at previous timestep
    mutable Real voltage_prev_;  // V(t) - voltage at previous timestep (for Trapezoidal)
    mutable Real current_prev2_; // I(t-Δt) - current at t-Δt (for BDF2)
    mutable Real current_prev3_; // I(t-2Δt) - current at t-2Δt (for BDF3)
    mutable Real current_saved_; // Saved current for restore
    mutable Real voltage_saved_; // Saved voltage for restore
    mutable Real current_saved2_; // Saved I(t-Δt) for restore
    mutable Real current_saved3_; // Saved I(t-2Δt) for restore
    mutable Real flux_;          // Φ = L * I (current)
    mutable Real flux_prev_;     // Φ(t-Δt) - flux at previous timestep (for physical LTE)
    mutable Real last_timestep_; // Last timestep used (for accept_solution)
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
    mutable Real current_gamma_;  // I(t + γΔt) - current at gamma point (Stage 1 solution)
    mutable Real voltage_gamma_;  // V(t + γΔt) - voltage at gamma point (Stage 1 solution)
    mutable Real current_gamma_saved_;  // Saved gamma current for restore
    mutable Real voltage_gamma_saved_;  // Saved gamma voltage for restore
    mutable int tr_bdf2_stage_;   // Current TR-BDF2 stage (1 or 2, saved from stamp_transient)
    mutable bool is_coupled_;     // Flag: true if part of a mutual inductance pair
};

} // namespace kirimaspice::devices
