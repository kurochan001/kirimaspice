// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mosfet_level1.hpp
// Unified MOSFET Level 1 (Shichman-Hodges) template class
#pragma once
#include "kirimaspice/devices/mosfet_base.hpp"
#include "kirimaspice/devices/polarity.hpp"

#ifdef LINEAR
#undef LINEAR
#endif

namespace kirimaspice::devices {

/// MOSFET Level 1 (Shichman-Hodges) template class
/// Unified implementation for both N-channel and P-channel
///
/// Shichman-Hodges Level 1 model:
///
/// Operating regions:
/// 1. Cutoff (Vgs_eff < Vth_eff):
///    Id = 0
///
/// 2. Linear (Vgs_eff > Vth_eff, Vds_eff < Vgs_eff - Vth_eff):
///    Id = Kp × (W/L) × ((Vgs_eff - Vth_eff) × Vds_eff - Vds_eff²/2)
///
/// 3. Saturation (Vgs_eff > Vth_eff, Vds_eff >= Vgs_eff - Vth_eff):
///    Id = (Kp/2) × (W/L) × (Vgs_eff - Vth_eff)²
///
/// For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs, Vth_eff = Vth
/// For P-channel: Vgs_eff = Vs - Vg, Vds_eff = Vs - Vd, Vth_eff = |Vth|
///
/// Note: This is a simplified 3-terminal model (drain, gate, source).
/// Body/substrate is assumed to be connected to source (Vbs = 0).
template<Polarity P>
class MOSFET_Level1 : public MOSFET_Base {
public:
    /// Polarity traits for compile-time constants
    using Traits = PolarityTraits<P>;

    /// Operating region enumeration
    enum class Region { CUTOFF, LINEAR, SATURATION };

    MOSFET_Level1(DeviceId id, const std::string& name,
                  NodeId drain, NodeId gate, NodeId source,
                  Real kp = 2e-5,      // Process transconductance [A/V²] at Tnom
                  Real w = 10e-6,      // Channel width [m]
                  Real l = 1e-6,       // Channel length [m]
                  Real vth = (P == Polarity::N) ? 0.7 : -0.7,  // Threshold voltage [V]
                  Real cgso = 0.0,     // G-S overlap capacitance per width [F/m]
                  Real cgdo = 0.0,     // G-D overlap capacitance per width [F/m]
                  Real cgbo = 0.0,     // G-B overlap capacitance per length [F/m]
                  Real kf = 0.0,       // Flicker noise coefficient
                  Real af = 1.0,       // Flicker noise current exponent
                  Real ef = 1.0,       // Flicker noise frequency exponent
                  Real gamma = 0.667,  // Channel thermal noise coefficient
                  Real kt1 = 0.0,      // VTO temperature coefficient [V/K]
                  Real kt2 = 0.0,      // VTO second-order temperature coefficient [V/K²]
                  Real ute = -1.5,     // Mobility temperature exponent
                  Real tnom = 300.15); // Nominal temperature [K] (27°C)

    ~MOSFET_Level1() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: Nonlinear device requires Newton-Raphson
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
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
    bool is_dynamic() const override { return (cgso_ > 0.0 || cgdo_ > 0.0 || cgbo_ > 0.0); }

    // AC analysis: Small-signal linearization
    void stamp_ac(ComplexMNASystem& sys, Real omega,
                  const DenseVector& dc_solution) const override;

    // Accessors
    Real process_transconductance() const { return kp_; }
    Real channel_width() const { return w_; }
    Real channel_length() const { return l_; }
    Real threshold_voltage() const { return vth_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }

    /// Set threshold voltage (for temperature compensation or parameter sweeps)
    void set_threshold_voltage(Real vth);

    /// Set temperature and update temperature-dependent parameters
    void set_temperature(Real temp_kelvin);

    /// Get operating point currents and conductances (for debugging/validation)
    /// @param v_gs_eff: Effective gate-source voltage (vgs for N, vsg for P)
    /// @param v_ds_eff: Effective drain-source voltage (vds for N, vsd for P)
    Real get_drain_current(Real v_gs_eff, Real v_ds_eff) const;
    Real get_transconductance(Real v_gs_eff, Real v_ds_eff) const;
    Real get_output_conductance(Real v_gs_eff, Real v_ds_eff) const;

    /// Get operating region (for debugging)
    Region get_region(Real v_gs_eff, Real v_ds_eff) const;

    /// AC small-signal capacitances
    Real get_gate_source_capacitance(Real v_gs_eff, Real v_ds_eff) const;
    Real get_gate_drain_capacitance(Real v_gs_eff, Real v_ds_eff) const;

    /// Noise analysis: channel thermal noise + flicker noise
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                                Real frequency) const override;

    /// Accept solution for transient analysis (updates voltage history)
    void accept_solution(const DenseVector& node_voltages) override;

    /// Physical local truncation error (charge-based for adaptive timestepping)
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Sensitivity analysis: Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

protected:
    /// Get effective threshold voltage (|Vth| for P-channel)
    Real get_effective_threshold() const {
        if constexpr (Traits::use_abs_threshold) {
            return std::abs(vth_);
        } else {
            return vth_;
        }
    }

    /// Convert terminal voltages to effective Vgs and Vds
    /// For N-channel: vgs = vg - vs, vds = vd - vs
    /// For P-channel: vgs_eff = vs - vg (= vsg), vds_eff = vs - vd (= vsd)
    void compute_effective_voltages(Real vd, Real vg, Real vs,
                                    Real& v_gs_eff, Real& v_ds_eff) const {
        if constexpr (P == Polarity::N) {
            v_gs_eff = vg - vs;
            v_ds_eff = vd - vs;
        } else {
            v_gs_eff = vs - vg;  // vsg
            v_ds_eff = vs - vd;  // vsd
        }
    }

private:
    /// Stamp small conductance gmin for numerical stability
    void stamp_gmin(MNASystem& mna) const;

    std::vector<NodeId> nodes_;  // [drain, gate, source]
    Real kp_;    // Process transconductance parameter [A/V²]
    Real w_;     // Channel width [m]
    Real l_;     // Channel length [m]
    Real vth_;   // Threshold voltage [V] (negative for P-channel)
    Real cgso_;  // G-S overlap capacitance per width [F/m]
    Real cgdo_;  // G-D overlap capacitance per width [F/m]
    Real cgbo_;  // G-B overlap capacitance per length [F/m]
    Real kf_;    // Flicker noise coefficient
    Real af_;    // Flicker noise current exponent
    Real ef_;    // Flicker noise frequency exponent
    Real gamma_; // Channel thermal noise coefficient

    // Temperature reference values
    Real vth_tnom_; // VTO at nominal temperature (reference value)
    Real kp_tnom_;  // KP at nominal temperature (reference value)

    // Derived parameter
    Real beta_;  // beta = Kp * W/L [A/V²]
};

// Explicit template instantiation declarations (definitions in .cpp)
extern template class MOSFET_Level1<Polarity::N>;
extern template class MOSFET_Level1<Polarity::P>;

// Type aliases for backward compatibility
using MOSFET_N_Level1 = MOSFET_Level1<Polarity::N>;
using MOSFET_P_Level1 = MOSFET_Level1<Polarity::P>;

// Step 5.1: Primary type aliases (replaces mosfet_n.hpp/mosfet_p.hpp)
using MOSFET_N = MOSFET_Level1<Polarity::N>;
using MOSFET_P = MOSFET_Level1<Polarity::P>;

} // namespace kirimaspice::devices
