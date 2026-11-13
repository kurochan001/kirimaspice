// include/kirimaspice/devices/bjt_base.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// BJT Base class: Common functionality for NPN and PNP bipolar junction transistors
///
/// This base class provides:
/// - Common parameters (Is, βF, NF, Isc, βR, NR, capacitances, noise)
/// - State variable management for two junctions (forward and reverse)
/// - BDF coefficient caching for variable-timestep integration
/// - Common utility methods (save/restore state, LTE estimation, parameter access)
///
/// Derived classes (BJT_NPN, BJT_PNP) implement:
/// - Voltage polarity mapping (Vbe/Vbc vs Veb/Vcb)
/// - MNA stamping with correct signs
/// - Junction-specific voltage extraction
class BJT_Base : public Device {
public:
    // Constructor: Initialize common BJT parameters
    BJT_Base(DeviceId id, const std::string& name, DeviceType type,
             NodeId collector, NodeId base, NodeId emitter,
             Real is,      // Saturation current [A]
             Real beta_f,  // Forward current gain
             Real nf,      // Forward emission coefficient
             Real isc,     // Collector saturation current [A]
             Real beta_r,  // Reverse current gain
             Real nr,      // Reverse emission coefficient
             Real cje,     // Forward junction capacitance [F]
             Real vje,     // Forward junction potential [V]
             Real mje,     // Forward grading coefficient
             Real cjc,     // Reverse junction capacitance [F]
             Real vjc,     // Reverse junction potential [V]
             Real mjc,     // Reverse grading coefficient
             Real tf,      // Forward transit time [s]
             Real tr,      // Reverse transit time [s]
             Real kf,      // Flicker noise coefficient
             Real af,      // Flicker noise current exponent
             Real ff);     // Flicker noise frequency exponent

    ~BJT_Base() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return (cje_ > 0.0 || cjc_ > 0.0 || tf_ > 0.0 || tr_ > 0.0); }

    // Common accessors
    Real saturation_current() const { return is_; }
    Real forward_beta() const { return beta_f_; }
    Real forward_emission_coefficient() const { return nf_; }
    Real collector_saturation_current() const { return isc_; }
    Real reverse_beta() const { return beta_r_; }
    Real reverse_emission_coefficient() const { return nr_; }
    Real thermal_voltage() const { return vt_; }
    NodeId collector() const { return nodes_[0]; }
    NodeId base() const { return nodes_[1]; }
    NodeId emitter() const { return nodes_[2]; }

    /// Set temperature for thermal voltage calculation
    void set_temperature(Real temp_kelvin);

    /// State management for transient analysis (common for both NPN and PNP)
    void save_state() override;
    void restore_state() override;

    /// Get total charge for LTE estimation (sum of both junctions)
    Real get_charge() const override { return charge_fwd_ + charge_rev_; }

    /// Physical local truncation error (charge-based for adaptive timestepping)
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    /// Sensitivity analysis: Parameter access (common implementation)
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

protected:
    // Node connectivity
    std::vector<NodeId> nodes_;  // [collector, base, emitter]

    // Model parameters
    Real is_;      // Saturation current [A]
    Real beta_f_;  // Forward current gain (hFE)
    Real nf_;      // Forward emission coefficient
    Real isc_;     // Collector saturation current [A]
    Real beta_r_;  // Reverse current gain
    Real nr_;      // Reverse emission coefficient
    Real vt_;      // Thermal voltage [V] = kT/q
    Real cje_;     // Forward junction capacitance [F]
    Real vje_;     // Forward junction potential [V]
    Real mje_;     // Forward grading coefficient
    Real cjc_;     // Reverse junction capacitance [F]
    Real vjc_;     // Reverse junction potential [V]
    Real mjc_;     // Reverse grading coefficient
    Real tf_;      // Forward transit time [s]
    Real tr_;      // Reverse transit time [s]
    Real kf_;      // Flicker noise coefficient
    Real af_;      // Flicker noise current exponent
    Real ff_;      // Flicker noise frequency exponent

    // Physical constants
    static constexpr Real k_boltzmann = 1.380649e-23;    // Boltzmann constant [J/K]
    static constexpr Real q_electron = 1.602176634e-19;  // Elementary charge [C]
    static constexpr Real gmin_default = 1e-12;          // Minimum conductance [S]

    // State variables for transient analysis - Forward junction (B-E for NPN, E-B for PNP)
    mutable Real v_fwd_prev_;   // V_fwd(t) - voltage at previous timestep
    mutable Real v_fwd_prev2_;  // V_fwd(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real v_fwd_prev3_;  // V_fwd(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real v_fwd_saved_;  // Saved V_fwd for restore
    mutable Real v_fwd_saved2_; // Saved V_fwd(t-Δt) for restore
    mutable Real v_fwd_saved3_; // Saved V_fwd(t-2Δt) for restore
    mutable Real charge_fwd_;      // Q_fwd = ∫Cje(V)dV (current charge)
    mutable Real charge_fwd_prev_; // Q_fwd(t-Δt) - charge at previous timestep (for physical LTE)

    // State variables for transient analysis - Reverse junction (B-C for NPN, C-B for PNP)
    mutable Real v_rev_prev_;   // V_rev(t) - voltage at previous timestep
    mutable Real v_rev_prev2_;  // V_rev(t-Δt) - voltage at t-Δt (for BDF2)
    mutable Real v_rev_prev3_;  // V_rev(t-2Δt) - voltage at t-2Δt (for BDF3)
    mutable Real v_rev_saved_;  // Saved V_rev for restore
    mutable Real v_rev_saved2_; // Saved V_rev(t-Δt) for restore
    mutable Real v_rev_saved3_; // Saved V_rev(t-2Δt) for restore
    mutable Real charge_rev_;      // Q_rev = ∫Cjc(V)dV (current charge)
    mutable Real charge_rev_prev_; // Q_rev(t-Δt) - charge at previous timestep (for physical LTE)

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

    /// Get junction capacitance at given voltage (common formula for both junctions)
    /// @param v_junction: Junction voltage
    /// @param cj0: Zero-bias capacitance
    /// @param vj: Junction potential
    /// @param m: Grading coefficient
    /// @return Junction capacitance [F]
    Real get_junction_capacitance(Real v_junction, Real cj0, Real vj, Real m) const;

    /// Update state variables after accepting a timestep (common logic)
    /// @param v_fwd_new: New forward junction voltage
    /// @param v_rev_new: New reverse junction voltage
    void update_junction_states(Real v_fwd_new, Real v_rev_new);
};

} // namespace kirimaspice::devices
