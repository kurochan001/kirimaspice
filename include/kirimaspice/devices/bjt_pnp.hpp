// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/bjt_pnp.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// BJT PNP: Nonlinear 3-terminal bipolar junction transistor (PNP type)
/// Full Ebers-Moll model for PNP:
///
/// For PNP transistors, current directions are opposite to NPN:
/// - Positive current flows OUT of the device (emitter → collector)
/// - Junction voltages use different polarity: Veb (E-B), Vcb (C-B)
///
/// Base current:       Ib = (Is/βF) × (exp(Veb/(NF×Vt)) - 1) + (Isc/βR) × (exp(Vcb/(NR×Vt)) - 1)
/// Collector current:  Ic = Is × (exp(Veb/(NF×Vt)) - 1) - Isc/βR × (exp(Vcb/(NR×Vt)) - 1)
/// Emitter current:    Ie = -(Ic + Ib)
///
/// Where:
///   Is  = E-B saturation current [A]
///   Isc = C-B saturation current [A] (default: same as Is)
///   βF  = forward current gain (hFE)
///   βR  = reverse current gain (default: 1.0)
///   NF  = forward emission coefficient
///   NR  = reverse emission coefficient (default: 1.0)
///   Vt  = thermal voltage = kT/q ≈ 25.85mV at T=300K
///   Veb = Ve - Vb (E-B junction voltage, positive when forward biased)
///   Vcb = Vc - Vb (C-B junction voltage)
///
/// Newton-Raphson linearization:
///   g_eb = dIb/dVeb, g_cb = dIb/dVcb
///   g_ec = dIc/dVeb, g_cc = dIc/dVcb
class BJT_PNP : public Device {
public:
    BJT_PNP(DeviceId id, const std::string& name,
            NodeId collector, NodeId base, NodeId emitter,
            Real is = 1e-14,     // E-B saturation current [A] at Tnom
            Real beta_f = 100.0, // Forward current gain at Tnom
            Real nf = 1.0,       // Forward emission coefficient
            Real isc = -1.0,     // C-B saturation current [A] (default: same as Is) at Tnom
            Real beta_r = 1.0,   // Reverse current gain at Tnom
            Real nr = 1.0,       // Reverse emission coefficient
            Real vaf = 0.0,      // Forward Early voltage [V] (0 = infinite)
            Real var = 0.0,      // Reverse Early voltage [V] (0 = infinite)
            Real ikf = 0.0,      // Forward knee current [A] (0 = no high-current roll-off)
            Real ikr = 0.0,      // Reverse knee current [A] (0 = no high-current roll-off)
            Real rb = 0.0,       // Zero-bias base resistance [Ω]
            Real rbm = 0.0,      // Minimum base resistance [Ω]
            Real irb = 0.0,      // Base resistance half-value current [A]
            Real ise = 0.0,      // E-B leakage saturation current [A] at Tnom
            Real ne = 1.5,       // E-B leakage emission coefficient (typ. 1.5-2.0)
            Real isc_leak = 0.0, // C-B leakage saturation current [A] at Tnom
            Real nc = 2.0,       // C-B leakage emission coefficient (typ. 1.5-2.0)
            Real cje = 0.0,      // E-B zero-bias junction capacitance [F]
            Real vje = 0.75,     // E-B junction potential [V]
            Real mje = 0.33,     // E-B grading coefficient
            Real cjc = 0.0,      // C-B zero-bias junction capacitance [F]
            Real vjc = 0.75,     // C-B junction potential [V]
            Real mjc = 0.33,     // C-B grading coefficient
            Real tf = 0.0,       // Forward transit time [s]
            Real tr = 0.0,       // Reverse transit time [s]
            Real kf = 0.0,       // Flicker noise coefficient
            Real af = 1.0,       // Flicker noise current exponent
            Real ff = 1.0,       // Flicker noise frequency exponent
            Real xti = 3.0,      // Saturation current temperature exponent
            Real xtb = 0.0,      // Forward/reverse beta temperature exponent
            Real eg = 1.11,      // Energy gap [eV] (default: Silicon)
            Real tnom = 300.15); // Nominal temperature [K] (27°C)

    ~BJT_PNP() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: Nonlinear device requires Newton-Raphson
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        // BJT is nonlinear - no linear contribution
        // Optionally, add small conductance gmin for numerical stability
        stamp_gmin(mna);
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override;

    // Transient analysis: Nonlinear capacitance with companion model
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return (cje_ > 0.0 || cjc_ > 0.0 || tf_ > 0.0 || tr_ > 0.0); }

    // AC analysis: Small-signal linearization
    void stamp_ac(ComplexMNASystem& sys, Real omega,
                  const DenseVector& dc_solution) const override;

    // Accessors
    Real saturation_current() const { return is_; }
    Real forward_beta() const { return beta_f_; }
    Real forward_emission_coefficient() const { return nf_; }
    Real collector_saturation_current() const { return isc_; }
    Real reverse_beta() const { return beta_r_; }
    Real reverse_emission_coefficient() const { return nr_; }
    Real forward_early_voltage() const { return vaf_; }
    Real reverse_early_voltage() const { return var_; }
    NodeId collector() const { return nodes_[0]; }
    NodeId base() const { return nodes_[1]; }
    NodeId emitter() const { return nodes_[2]; }

    /// Set temperature and update temperature-dependent parameters
    /// Updates: Vt, IS(T), ISC(T), ISE(T), ISC_leak(T), BF(T), BR(T)
    void set_temperature(Real temp_kelvin);

    /// Get temperature parameters
    Real get_xti() const { return xti_; }
    Real get_xtb() const { return xtb_; }
    Real get_eg() const { return eg_; }
    Real get_tnom() const { return tnom_; }

    /// Enable/disable voltage limiting
    void set_voltage_limiting(bool enable) override { enable_voltage_limiting_ = enable; }
    bool voltage_limiting_enabled() const { return enable_voltage_limiting_; }

    /// Get operating point currents (for debugging/validation)
    /// @param veb: E-B junction voltage (Ve - Vb)
    /// @param vcb: C-B junction voltage (Vc - Vb)
    Real get_base_current(Real veb, Real vcb) const;
    Real get_collector_current(Real veb, Real vcb) const;

    /// Get conductances (partial derivatives for Newton-Raphson)
    /// g_eb = dIb/dVeb, g_cb = dIb/dVcb
    /// g_ec = dIc/dVeb, g_cc = dIc/dVcb
    Real get_base_conductance_veb(Real veb, Real vcb) const;    // dIb/dVeb
    Real get_base_conductance_vcb(Real veb, Real vcb) const;    // dIb/dVcb
    Real get_collector_conductance_veb(Real veb, Real vcb) const; // dIc/dVeb
    Real get_collector_conductance_vcb(Real veb, Real vcb) const; // dIc/dVcb

    /// AC small-signal parameters
    /// @param veb: DC E-B voltage (Ve - Vb)
    /// @param vcb: DC C-B voltage (Vc - Vb)
    /// @return Gm: transconductance [S] (Gm = dIc/dVeb)
    Real get_transconductance(Real veb, Real vcb) const;

    /// @return Gpi: input conductance [S] (Gpi = dIb/dVeb)
    Real get_input_conductance(Real veb, Real vcb) const;

    /// @return Gmu: feedback conductance [S] (Gmu = dIc/dVcb, typically negative)
    Real get_feedback_conductance(Real veb, Real vcb) const;

    /// @return Base resistance [Ω] (Gummel-Poon model)
    /// R_b = (RBM + (RB - RBM) × q_b) / (1 + (I_b / IRB)²)
    Real get_base_resistance(Real veb, Real vcb) const;

    /// @return Cpi: E-B junction + diffusion capacitance [F]
    Real get_input_capacitance(Real veb, Real vcb) const;

    /// @return Cmu: C-B junction capacitance [F]
    Real get_feedback_capacitance(Real veb, Real vcb) const;

    /// Noise analysis: shot noise + flicker noise
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                                Real frequency) const override;

    /// State management for transient analysis
    void save_state() override;
    void restore_state() override;
    void accept_solution(const DenseVector& node_voltages) override;

    /// Get charge for LTE estimation
    Real get_charge() const override { return charge_eb_ + charge_cb_; }

    /// Physical local truncation error (charge-based for adaptive timestepping)
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    /// Get E-B junction capacitance at given V_EB voltage
    Real get_junction_capacitance_eb(Real veb) const;

    /// Get C-B junction capacitance at given V_CB voltage
    Real get_junction_capacitance_cb(Real vcb) const;

    // Sensitivity analysis: Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

private:
    /// Stamp small conductance gmin for numerical stability
    void stamp_gmin(MNASystem& mna) const;

    std::vector<NodeId> nodes_;  // [collector, base, emitter]
    Real is_;      // E-B saturation current [A]
    Real beta_f_;  // Forward current gain (hFE)
    Real nf_;      // Forward emission coefficient
    Real isc_;     // C-B saturation current [A]
    Real beta_r_;  // Reverse current gain
    Real nr_;      // Reverse emission coefficient
    Real vaf_;     // Forward Early voltage [V]
    Real var_;     // Reverse Early voltage [V]
    Real ikf_;     // Forward knee current [A]
    Real ikr_;     // Reverse knee current [A]
    Real rb_;      // Zero-bias base resistance [Ω]
    Real rbm_;     // Minimum base resistance [Ω]
    Real irb_;     // Base resistance half-value current [A]
    Real ise_;     // E-B leakage saturation current [A]
    Real ne_;      // E-B leakage emission coefficient
    Real isc_leak_; // C-B leakage saturation current [A] (distinct from ideal isc_)
    Real nc_;      // C-B leakage emission coefficient
    Real vt_;      // Thermal voltage [V] = kT/q
    Real cje_;     // E-B zero-bias junction capacitance [F]
    Real vje_;     // E-B junction potential [V]
    Real mje_;     // E-B grading coefficient
    Real cjc_;     // C-B zero-bias junction capacitance [F]
    Real vjc_;     // C-B junction potential [V]
    Real mjc_;     // C-B grading coefficient
    Real tf_;      // Forward transit time [s]
    Real tr_;      // Reverse transit time [s]
    Real kf_;      // Flicker noise coefficient
    Real af_;      // Flicker noise current exponent
    Real ff_;      // Flicker noise frequency exponent

    // Temperature parameters
    Real xti_;     // Saturation current temperature exponent (default: 3.0 for Si)
    Real xtb_;     // Forward/reverse beta temperature exponent (default: 0.0)
    Real eg_;      // Energy gap [eV] (default: 1.11 for Si)
    Real tnom_;    // Nominal temperature [K] (default: 300.15K = 27°C)
    Real is_tnom_; // IS at nominal temperature (reference value)
    Real isc_tnom_; // ISC at nominal temperature (reference value)
    Real ise_tnom_; // ISE at nominal temperature (reference value)
    Real isc_leak_tnom_; // ISC_leak at nominal temperature (reference value)
    Real beta_f_tnom_; // BF at nominal temperature (reference value)
    Real beta_r_tnom_; // BR at nominal temperature (reference value)

    // Physical constants
    static constexpr Real k_boltzmann = 1.380649e-23; // Boltzmann constant [J/K]
    static constexpr Real q_electron = 1.602176634e-19; // Elementary charge [C]
    static constexpr Real gmin_default = 1e-12; // Minimum conductance [S]

    // Voltage limiting
    mutable bool enable_voltage_limiting_ = true;  ///< Enable PN junction voltage limiting

    // State variables for transient analysis - E-B junction
    mutable Real veb_prev_;   // V_EB(t) - voltage at previous timestep
    mutable Real veb_prev2_;  // V_EB(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real veb_prev3_;  // V_EB(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real veb_saved_;  // Saved V_EB for restore
    mutable Real veb_saved2_; // Saved V_EB(t-Δt) for restore
    mutable Real veb_saved3_; // Saved V_EB(t-2Δt) for restore
    mutable Real charge_eb_;      // Q_EB = ∫Cje(V_EB)dV (current charge)
    mutable Real charge_eb_prev_; // Q_EB(t-Δt) - charge at previous timestep (for physical LTE)

    // State variables for transient analysis - C-B junction
    mutable Real vcb_prev_;   // V_CB(t) - voltage at previous timestep
    mutable Real vcb_prev2_;  // V_CB(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real vcb_prev3_;  // V_CB(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real vcb_saved_;  // Saved V_CB for restore
    mutable Real vcb_saved2_; // Saved V_CB(t-Δt) for restore
    mutable Real vcb_saved3_; // Saved V_CB(t-2Δt) for restore
    mutable Real charge_cb_;      // Q_CB = ∫Cjc(V_CB)dV (current charge)
    mutable Real charge_cb_prev_; // Q_CB(t-Δt) - charge at previous timestep (for physical LTE)

    // Common state for transient analysis
    mutable Real timestep_;      // Current timestep (for accept_solution)
    mutable IntegrationMethod last_method_;  // Integration method used in last stamp
    mutable Real timestep_saved_;  // Saved timestep for restore
    mutable IntegrationMethod method_saved_;  // Saved method for restore
    mutable bool first_accept_;  // Flag for first accept_solution() call

    // BDF coefficients for variable-timestep integration (shared for both junctions)
    mutable Real bdf_alpha0_;  // α₀ coefficient
    mutable Real bdf_alpha1_;  // α₁ coefficient
    mutable Real bdf_alpha2_;  // α₂ coefficient
    mutable Real bdf_alpha3_;  // α₃ coefficient (for BDF3)
    mutable Real bdf_beta0_;   // β₀ coefficient
    mutable bool use_bdf_var_;  // Flag: true if using variable-timestep BDF
};

} // namespace kirimaspice::devices
