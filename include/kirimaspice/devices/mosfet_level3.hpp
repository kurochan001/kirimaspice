// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mosfet_level3.hpp
// Unified MOSFET Level 3 template class
#pragma once

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/polarity.hpp"
#include "kirimaspice/devices/mosfet_level3_params.hpp"
#include <cmath>

namespace kirimaspice::devices {

/**
 * @brief MOSFET Level 3 (Semi-empirical model) device template class
 *
 * Level 3 model with short-channel effects, narrow-width effects, and mobility
 * degradation. Based on SPICE3 User's Manual, UC Berkeley.
 *
 * **Operating regions**:
 * 1. Cutoff (Vgs_eff < Vth_eff):
 *    Id = 0
 *
 * 2. Linear (Vgs_eff > Vth_eff, Vds_eff < Vdsat):
 *    Id = β × [(Vgs_eff - Vth_eff) × Vds_eff - 0.5 × Vds_eff²]
 *
 * 3. Saturation (Vgs_eff > Vth_eff, Vds_eff >= Vdsat):
 *    Id = 0.5 × β × Vdsat² × (1 + λ × (Vds_eff - Vdsat))
 *
 * **Key parameters**:
 * - η (eta)    = static feedback (DIBL effect) [dimensionless]
 * - κ (kappa)  = saturation field factor [dimensionless]
 * - δ (delta)  = width effect on threshold [dimensionless]
 * - θ (theta)  = mobility degradation [1/V]
 * - vmax       = maximum drift velocity [m/s]
 *
 * For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs, Vth > 0
 * For P-channel: Vgs_eff = Vs - Vg, Vds_eff = Vs - Vd, |Vth| used
 *
 * Unified N/P template
 */
template<Polarity P>
class MOSFET_Level3 : public Device {
public:
    /// Polarity traits for compile-time constants
    using Traits = PolarityTraits<P>;

    /// Device type for N-channel or P-channel Level 3 MOSFET
    static constexpr DeviceType device_type =
        (P == Polarity::N) ? DeviceType::MOSFET_LEVEL3_N : DeviceType::MOSFET_LEVEL3_P;

    /// Operating region
    enum class Region {
        CUTOFF,     ///< Below threshold
        LINEAR,     ///< Triode region
        SATURATION  ///< Pinch-off region
    };

    /**
     * @brief Construct Level 3 MOSFET device with specified polarity
     * @param id Device ID
     * @param name Device name
     * @param drain Drain node
     * @param gate Gate node
     * @param source Source node
     * @param bulk Bulk (substrate) node
     * @param params Level 3 model parameters
     */
    MOSFET_Level3(DeviceId id, const std::string& name,
                  NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                  const MOSFETLevel3Params& params);

    ~MOSFET_Level3() override = default;

    // Device interface
    const std::vector<NodeId>& nodes() const override { return nodes_; }
    bool is_linear() const override { return false; }
    bool is_dynamic() const override {
        return (params_.cgso > 0.0 || params_.cgdo > 0.0 || params_.cgbo > 0.0 ||
                params_.cbd > 0.0 || params_.cbs > 0.0);
    }

    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override;

    void stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                        Real timestep, IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    void stamp_ac(ComplexMNASystem& sys, Real omega,
                 const DenseVector& dc_solution) const override;

    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                               Real frequency) const override;

    void save_state() override;
    void restore_state() override;
    void accept_solution(const DenseVector& node_voltages) override;
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

    // Accessors
    const MOSFETLevel3Params& parameters() const { return params_; }
    Real threshold_voltage_nominal() const { return params_.vto; }
    Real oxide_capacitance() const { return cox_; }
    Real transconductance_parameter() const { return beta_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }
    NodeId bulk() const { return nodes_[3]; }

    /// Get threshold voltage with body effect
    Real get_threshold_voltage(Real v_bs_eff) const;

    /// Get effective mobility with degradation
    Real get_effective_mobility(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get saturation voltage
    Real get_vdsat(Real v_gs_eff, Real v_bs_eff) const;

    /// Get channel length modulation factor
    Real get_channel_length_modulation(Real v_ds_eff, Real vdsat) const;

    /// Get operating region
    Region get_region(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get drain current (always positive, represents |Id|)
    Real get_drain_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get transconductance gm = dIds/dVgs
    Real get_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get output conductance gds = dIds/dVds
    Real get_output_conductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get bulk transconductance gmb = dIds/dVbs
    Real get_bulk_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    // Capacitance methods (Meyer model)
    Real get_gate_source_capacitance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;
    Real get_gate_drain_capacitance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;
    Real get_gate_bulk_capacitance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;
    Real get_bulk_source_capacitance(Real v_bs_eff) const;
    Real get_bulk_drain_capacitance(Real v_bd_eff) const;

    // Physical constants
    static constexpr Real gmin_default = 1e-12;
    static constexpr Real k_boltzmann = 1.380649e-23;
    static constexpr Real q_electron = 1.602176634e-19;
    static constexpr Real eps_si = 1.03594e-10;  // ε_Si = 11.7 × ε_0
    static constexpr Real eps_ox = 3.453143e-11; // ε_ox = 3.9 × ε_0

protected:
    /// Get effective threshold voltage (|Vth| for P-channel)
    Real get_effective_threshold() const {
        if constexpr (Traits::use_abs_threshold) {
            return std::abs(params_.vto);
        } else {
            return params_.vto;
        }
    }

    /// Convert terminal voltages to effective Vgs, Vds, Vbs
    /// For N-channel: v_gs_eff = vg - vs, v_ds_eff = vd - vs, v_bs_eff = vb - vs
    /// For P-channel: v_gs_eff = vs - vg (= vsg), v_ds_eff = vs - vd (= vsd), v_bs_eff = vs - vb (= vsb)
    void compute_effective_voltages(Real vd, Real vg, Real vs, Real vb,
                                    Real& v_gs_eff, Real& v_ds_eff, Real& v_bs_eff) const {
        if constexpr (P == Polarity::N) {
            v_gs_eff = vg - vs;
            v_ds_eff = vd - vs;
            v_bs_eff = vb - vs;
        } else {
            v_gs_eff = vs - vg;  // vsg
            v_ds_eff = vs - vd;  // vsd
            v_bs_eff = vs - vb;  // vsb
        }
    }

private:
    void stamp_gmin(MNASystem& mna) const;

    /// Terminal voltages from a vector with node k at element k - 1
    void terminal_voltages(const DenseVector& v, Real& vd, Real& vg, Real& vs, Real& vb) const;

    std::vector<NodeId> nodes_;  ///< [Drain, Gate, Source, Bulk]
    MOSFETLevel3Params params_;  ///< Model parameters

    // Derived parameters
    Real cox_;    ///< Oxide capacitance per unit area [F/m²]
    Real beta_;   ///< Transconductance parameter [A/V²]
    Real fs_;     ///< Surface state factor

    // State variables (for BDF integration)
    // Named generically: v1 = Vgs(N)/Vsg(P), v2 = Vgd(N)/Vsd(P), etc.
    mutable Real v1_prev_ = 0.0, v1_prev2_ = 0.0, v1_prev3_ = 0.0;
    mutable Real v1_saved_ = 0.0, v1_saved2_ = 0.0, v1_saved3_ = 0.0;
    mutable Real charge_1_ = 0.0, charge_1_prev_ = 0.0;

    mutable Real v2_prev_ = 0.0, v2_prev2_ = 0.0, v2_prev3_ = 0.0;
    mutable Real v2_saved_ = 0.0, v2_saved2_ = 0.0, v2_saved3_ = 0.0;
    mutable Real charge_2_ = 0.0, charge_2_prev_ = 0.0;

    mutable Real v3_prev_ = 0.0, v3_prev2_ = 0.0, v3_prev3_ = 0.0;
    mutable Real v3_saved_ = 0.0, v3_saved2_ = 0.0, v3_saved3_ = 0.0;
    mutable Real charge_3_ = 0.0, charge_3_prev_ = 0.0;

    mutable Real vbs_prev_ = 0.0, vbs_prev2_ = 0.0, vbs_prev3_ = 0.0;
    mutable Real vbs_saved_ = 0.0, vbs_saved2_ = 0.0, vbs_saved3_ = 0.0;
    mutable Real charge_bs_ = 0.0, charge_bs_prev_ = 0.0;

    mutable Real vbd_prev_ = 0.0, vbd_prev2_ = 0.0, vbd_prev3_ = 0.0;
    mutable Real vbd_saved_ = 0.0, vbd_saved2_ = 0.0, vbd_saved3_ = 0.0;
    mutable Real charge_bd_ = 0.0, charge_bd_prev_ = 0.0;

    mutable Real timestep_ = 0.0;
    mutable IntegrationMethod last_method_ = IntegrationMethod::BACKWARD_EULER;
    mutable Real timestep_saved_ = 0.0;
    mutable IntegrationMethod method_saved_ = IntegrationMethod::BACKWARD_EULER;
    mutable bool first_accept_ = true;

    // BDF coefficients cache
    mutable Real bdf_alpha0_ = 0.0, bdf_alpha1_ = 0.0, bdf_alpha2_ = 0.0, bdf_alpha3_ = 0.0;
    mutable Real bdf_beta0_ = 0.0;
    mutable bool use_bdf_var_ = false;
};

// Explicit template instantiation declarations
extern template class MOSFET_Level3<Polarity::N>;
extern template class MOSFET_Level3<Polarity::P>;

// Type aliases for backward compatibility
using MOSFET_Level3_N = MOSFET_Level3<Polarity::N>;
using MOSFET_Level3_P = MOSFET_Level3<Polarity::P>;

} // namespace kirimaspice::devices
