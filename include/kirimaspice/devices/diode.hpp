// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/diode.hpp
#pragma once

#include <optional>
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Diode: Nonlinear 2-terminal semiconductor device
/// Shockley equation: I = Is * (exp(V / (N*Vt)) - 1)
/// where:
///   Is = saturation current [A]
///   N  = emission coefficient (ideality factor)
///   Vt = thermal voltage = kT/q ≈ 25.85mV at T=300K
///
/// Newton-Raphson linearization:
///   g_eq = dI/dV = (Is/(N*Vt)) * exp(V/(N*Vt))
///   I_eq = I(V) - g_eq * V
class Diode : public Device {
public:
    Diode(DeviceId id, const std::string& name,
          NodeId anode, NodeId cathode,
          Real is = 1e-14,   // Saturation current [A] at Tnom
          Real n = 1.0,      // Emission coefficient
          Real cj0 = 0.0,    // Zero-bias junction capacitance [F]
          Real vj = 0.7,     // Junction potential [V]
          Real m = 0.5,      // Grading coefficient
          Real xti = 3.0,    // Saturation current temperature exponent
          Real eg = 1.11,    // Energy gap [eV] (default: Silicon)
          Real tnom = 300.15); // Nominal temperature [K] (27°C)

    ~Diode() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: Nonlinear device requires Newton-Raphson
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        // Diode is nonlinear - no linear contribution; its GMIN is in
        // parallel with the junction in stamp_dc_nonlinear
        (void)mna;
        (void)node_voltages;
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override;

    // Transient analysis: Nonlinear device with junction capacitance
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return cj0_ > 0.0; } // Dynamic if junction capacitance present

    // AC analysis: Small-signal linearization
    void stamp_ac(ComplexMNASystem& sys, Real omega,
                  const DenseVector& dc_solution) const override;

    // Accessors
    Real saturation_current() const { return is_; }
    Real emission_coefficient() const { return n_; }
    Real junction_capacitance() const { return cj0_; }
    Real junction_potential() const { return vj_; }
    Real grading_coefficient() const { return m_; }
    NodeId anode() const { return nodes_[0]; }
    NodeId cathode() const { return nodes_[1]; }

    /// Set temperature and update temperature-dependent parameters
    /// Updates: Vt, IS(T), VJ(T) and CJO(T), by SPICE's diode laws
    void set_temperature(Real temp_kelvin);

    /// Get temperature parameters
    Real get_xti() const { return xti_; }
    Real get_eg() const { return eg_; }
    Real get_tnom() const { return tnom_; }

    /// Enable/disable voltage limiting
    void set_voltage_limiting(bool enable) override { enable_voltage_limiting_ = enable; }
    bool voltage_limiting_enabled() const { return enable_voltage_limiting_; }
    bool limited_in_last_stamp() const override { return limited_in_last_stamp_; }

    /// The junction's current Is (exp(V/(N Vt)) - 1), and the conductance
    /// of the junction with its GMIN in parallel, which the DC, transient
    /// and AC stamps take (SPICE's diode)
    Real get_current(Real voltage) const;
    Real get_conductance(Real voltage) const;

    /// AC small-signal parameters
    /// @param voltage: DC operating point voltage (anode - cathode)
    /// @return gd: small-signal conductance [S]
    Real get_small_signal_conductance(Real voltage) const;

    /// @param voltage: DC operating point voltage (anode - cathode)
    /// @return Cj: junction capacitance [F]
    Real get_junction_capacitance(Real voltage) const;

    /// Noise analysis: shot noise
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                                Real frequency) const override;

    // State management for transient analysis
    void save_state() override;
    void restore_state() override;
    void accept_solution(const DenseVector& node_voltages) override;
    void reset_state() override;

    Real get_charge() const override { return charge_; }

    // Physical quantity-based error estimation
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Sensitivity analysis: Parameter access. IS, CJ0 and VJ are the card's,
    // at TNOM, in both; set_parameter scales what it sets to the present
    // temperature
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

private:
    /// Stamp small conductance gmin for numerical stability

    std::vector<NodeId> nodes_;  // [anode, cathode]
    Real is_;   // Saturation current [A] (temperature-dependent)
    Real n_;    // Emission coefficient (ideality factor)
    Real vt_;   // Thermal voltage [V] = kT/q
    Real cj0_;  // Zero-bias junction capacitance [F]
    Real vj_;   // Junction potential [V]
    Real m_;    // Grading coefficient

    // Temperature parameters
    Real xti_;   // Saturation current temperature exponent (default: 3.0 for Si)
    Real eg_;    // Energy gap [eV] (default: 1.11 for Si)
    Real tnom_;  // Nominal temperature [K] (default: 300.15K = 27°C)
    Real is_tnom_; // IS at nominal temperature (reference value)
    Real cj0_tnom_;  // CJO at TNOM
    Real vj_tnom_;   // VJ at TNOM
    Real temp_;    // The present temperature [K]: TNOM until set_temperature

    // State variables for transient analysis
    mutable Real voltage_prev_;  // V(t) - voltage at previous timestep
    mutable Real voltage_prev2_; // V(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real voltage_prev3_; // V(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real voltage_saved_; // Saved voltage for restore
    mutable Real voltage_saved2_; // Saved V(t-Δt) for restore
    mutable Real voltage_saved3_; // Saved V(t-2Δt) for restore
    mutable Real charge_;        // Q = ∫Cj(V)dV (current charge)
    mutable Real charge_prev_;   // Q(t-Δt) - charge at previous timestep (for physical LTE)
    mutable Real timestep_;      // Current timestep (for accept_solution)
    mutable IntegrationMethod last_method_;  // Integration method used in last stamp
    mutable Real timestep_saved_;  // Saved timestep for restore
    mutable IntegrationMethod method_saved_;  // Saved method for restore
    mutable bool first_accept_;  // Flag for first accept_solution() call

    // The companion of the junction capacitance (device_utils::
    // capacitor_companion): the capacitor's current at the last accepted
    // point, which the trapezoidal rule needs, the BDF and TR-BDF2
    // coefficients of the last stamp, and whether a transient step was stamped since the last
    // accepted point
    mutable Real current_prev_ = 0.0;
    mutable Real current_saved_ = 0.0;
    mutable std::optional<BDFCoefficients> cap_bdf_;
    mutable std::optional<TR_BDF2_Coefficients> cap_tr_bdf2_;
    mutable bool cap_stamped_ = false;

    // Physical constants
    static constexpr Real k_boltzmann = 1.380649e-23; // Boltzmann constant [J/K]
    static constexpr Real q_electron = 1.602176634e-19; // Elementary charge [C]
    static constexpr Real gmin_default = 1e-12; // Minimum conductance [S]

    // Voltage limiting
    mutable bool enable_voltage_limiting_ = true;  ///< Enable PN junction voltage limiting
    mutable Real v_last_eval_ = 0.0;  ///< Junction voltage of the last Newton evaluation
    mutable bool limited_in_last_stamp_ = false;  ///< The last stamp limited the junction voltage
};

} // namespace kirimaspice::devices
