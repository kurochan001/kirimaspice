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
///    Id = Kp × (W/L) × ((Vgs_eff - Vth_eff) × Vds_eff - Vds_eff²/2) × (1 + LAMBDA × Vds_eff)
///
/// 3. Saturation (Vgs_eff > Vth_eff, Vds_eff >= Vgs_eff - Vth_eff):
///    Id = (Kp/2) × (W/L) × (Vgs_eff - Vth_eff)² × (1 + LAMBDA × Vds_eff)
///
/// with the threshold of the body effect,
///    Vth_eff = VTO_eff + GAMMA × (sqrt(PHI - Vbs_eff) - sqrt(PHI)),
/// and for Vbs_eff > 0, sqrt(PHI) - Vbs_eff / (2 sqrt(PHI)), not below 0,
/// in place of sqrt(PHI - Vbs_eff) (threshold()). KP, PHI and VTO are
/// those at the device's temperature, by SPICE's level-1 laws
/// (set_temperature).
///
/// For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs, Vbs_eff = Vb - Vs, Vth_eff = Vth
/// For P-channel: Vgs_eff = Vs - Vg, Vds_eff = Vs - Vd, Vbs_eff = Vs - Vb, Vth_eff = -Vth
///
/// The bulk is the source, and GAMMA and LAMBDA are 0, until set_body
/// gives them (the parser does, for every level-1 element). The bulk
/// junctions of SPICE's level 1 are not modelled.
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
    NodeId bulk() const { return bulk_; }

    /// The bulk node, GAMMA (body-effect coefficient, V^0.5), PHI (surface
    /// potential, V) and LAMBDA (channel-length modulation, 1/V) of SPICE's
    /// level 1
    void set_body(NodeId bulk, Real gamma, Real phi, Real lambda);

    /// Set the threshold voltage VTO at TNOM (for parameter sweeps); the
    /// device takes it at its present temperature
    void set_threshold_voltage(Real vth);

    /// Set the temperature: KP(T) = KP (T/TNOM)^UTE, and PHI(T) and VTO(T)
    /// through the band gap, as SPICE's level 1 (the equations in the
    /// implementation)
    void set_temperature(Real temp_kelvin);

    /// Get operating point currents and conductances (for debugging/validation)
    /// @param v_gs_eff: Effective gate-source voltage (vgs for N, vsg for P)
    /// @param v_ds_eff: Effective drain-source voltage (vds for N, vsd for P)
    /// The current from drain to source of the effective device (VGS, VDS
    /// for N; VSG, VSD for P) and its derivatives with respect to those two
    /// voltages, in both modes: for VDS < 0 drain and source change roles,
    /// as in SPICE, so the current is -Id(VGD, -VDS). The DC, AC and noise
    /// stamps all take it from here.
    struct ChannelCurrent {
        Real id = 0.0;
        Real gm = 0.0;   ///< d id / d VGS_eff
        Real gds = 0.0;  ///< d id / d VDS_eff
        Real gmb = 0.0;  ///< d id / d VBS_eff
    };
    ChannelCurrent channel_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff = 0.0) const;

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

    /// The transient history (MOSFET_Base) and the voltages of the last
    /// evaluation, from which the step limiting starts: save_state and
    /// restore_state carry both, reset_state sets both to their initial
    /// values, the limiting then starting from 0 V as for a new device
    void save_state() override;
    void restore_state() override;
    void reset_state() override;

    /// Limiting of VGS (core::limit_fet_gate_step) and VDS
    /// (core::limit_fet_drain_step) between Newton iterations, as SPICE's
    /// level 1 does; on by default
    void set_voltage_limiting(bool enable) override { enable_voltage_limiting_ = enable; }
    bool limited_in_last_stamp() const override { return limited_in_last_stamp_; }

    /// Physical local truncation error (charge-based for adaptive timestepping)
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Sensitivity analysis: Parameter access. KP and VTO are the card's, at
    // TNOM, in both; set_parameter scales what it sets to the present
    // temperature
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

protected:
    /// The threshold in the device's own terms, -Vth for P-channel: the card
    /// gives VTO with the device's sign, -0.7 V for an enhancement P device
    /// and +0.5 V for a depletion one, which then conducts at Vsg = 0, as
    /// in ngspice-47
    Real get_effective_threshold() const {
        if constexpr (Traits::use_abs_threshold) {
            return -vth_;
        } else {
            return vth_;
        }
    }

    /// The threshold at Vbs_eff in the device's own terms, and its slope
    struct Threshold {
        Real vth = 0.0;
        Real dvth_dvbs = 0.0;
    };
    Threshold threshold(Real v_bs_eff) const;

    /// The current and derivatives of the device in forward mode (Vds_eff >= 0)
    ChannelCurrent forward_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Vbs_eff from the terminal voltages: vb - vs for N, vs - vb for P
    Real effective_vbs(Real vb, Real vs) const {
        return (P == Polarity::N) ? vb - vs : vs - vb;
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

    std::vector<NodeId> nodes_;  // [drain, gate, source], and bulk when it is another node
    NodeId bulk_;                // the source until set_body
    Real gamma_body_ = 0.0;      // GAMMA [V^0.5]
    bool enable_voltage_limiting_ = true;
    mutable Real vgs_last_eval_ = 0.0;   // VGS_eff of the last Newton evaluation
    mutable Real vds_last_eval_ = 0.0;   // VDS_eff of the last Newton evaluation
    Real vgs_last_eval_saved_ = 0.0;     // the two at save_state
    Real vds_last_eval_saved_ = 0.0;
    mutable bool limited_in_last_stamp_ = false;
    Real phi_tnom_ = 0.6;        // PHI [V] of the card, at TNOM
    Real phi_ = 0.6;             // PHI [V] at the present temperature
    Real vbi_eff_ = 0.0;         // VTO_eff - GAMMA sqrt(PHI) at the present temperature
    Real lambda_ = 0.0;          // LAMBDA [1/V]
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
    Real temperature_ = 300.15;  // The present temperature [K]: TNOM until set_temperature

    // Derived parameter
    Real beta_;  // beta = Kp * W/L [A/V²]
};

// Explicit template instantiation declarations (definitions in .cpp)
extern template class MOSFET_Level1<Polarity::N>;
extern template class MOSFET_Level1<Polarity::P>;

// Type aliases for backward compatibility
using MOSFET_N_Level1 = MOSFET_Level1<Polarity::N>;
using MOSFET_P_Level1 = MOSFET_Level1<Polarity::P>;

// Primary type aliases
using MOSFET_N = MOSFET_Level1<Polarity::N>;
using MOSFET_P = MOSFET_Level1<Polarity::P>;

} // namespace kirimaspice::devices
