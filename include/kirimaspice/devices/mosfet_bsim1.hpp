// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mosfet_bsim1.hpp
// Unified MOSFET BSIM1 template class
#pragma once

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/polarity.hpp"
#include "kirimaspice/devices/mosfet_bsim1_params.hpp"
#include <cmath>

namespace kirimaspice::devices {

/**
 * @brief MOSFET BSIM1 (Level 9) device template class
 *
 * Berkeley Short-channel IGFET Model v1 with unified N/P-channel implementation.
 *
 * **BSIM1 improvements over Level 3**:
 * - Better short-channel effect modeling (X2E, X3E, X2MZ parameters)
 * - Enhanced DIBL modeling (Drain-Induced Barrier Lowering)
 * - Improved subthreshold behavior (N0, NB, ND parameters)
 * - Accurate mobility degradation with short-channel effects
 * - Velocity saturation effects (VSAT parameter)
 * - Better body effect modeling (K1, K2 parameters)
 *
 * **Operating regions**:
 * 1. Cutoff (Vgs_eff < Vth_eff): Id = 0 (subthreshold current for weak inversion)
 * 2. Linear (Vgs_eff > Vth_eff, Vds_eff < Vdsat): Id = β × [(Vgs_eff - Vth_eff) × Vds_eff - 0.5 × Vds_eff²]
 * 3. Saturation (Vgs_eff > Vth_eff, Vds_eff >= Vdsat): Id = 0.5 × β × Vdsat² × (1 + λ × (Vds_eff - Vdsat))
 *
 * For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs, Vbs_eff = Vb - Vs
 * For P-channel: Vgs_eff = Vs - Vg (= vsg), Vds_eff = Vs - Vd (= vsd), Vbs_eff = Vs - Vb (= vsb)
 *
 * Reference: BSIM1 User's Manual, UC Berkeley (1988)
 *
 * Unified N/P template
 */
template<Polarity P>
class MOSFET_BSIM1 : public Device {
public:
    /// Polarity traits for compile-time constants
    using Traits = PolarityTraits<P>;

    /// Device type for N-channel or P-channel BSIM1 MOSFET
    static constexpr DeviceType device_type =
        (P == Polarity::N) ? DeviceType::MOSFET_BSIM1_N : DeviceType::MOSFET_BSIM1_P;

    /// Operating region
    enum class Region {
        CUTOFF,     ///< Below threshold
        LINEAR,     ///< Triode region
        SATURATION  ///< Pinch-off region
    };

    /**
     * @brief Construct BSIM1 MOSFET device with specified polarity
     * @param id Device ID
     * @param name Device name
     * @param drain Drain node
     * @param gate Gate node
     * @param source Source node
     * @param bulk Bulk (substrate) node
     * @param params BSIM1 model parameters
     */
    MOSFET_BSIM1(DeviceId id, const std::string& name,
                 NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                 const MOSFETBsim1Params& params);

    ~MOSFET_BSIM1() override = default;

    // Device interface
    const std::vector<NodeId>& nodes() const override { return nodes_; }
    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return true; }  // Has capacitances

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

    void accept_solution(const DenseVector& node_voltages) override;
    void reject_solution();
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

    // Accessors
    const MOSFETBsim1Params& params() const { return params_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }
    NodeId bulk() const { return nodes_[3]; }

    /// Get operating region for given effective voltages
    Region get_region(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get threshold voltage with body effect and DIBL
    Real get_threshold_voltage(Real v_bs_eff, Real v_ds_eff) const;

    /// Get drain current (always positive, represents |Id|)
    Real get_drain_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get transconductance gm = dIds/dVgs
    Real get_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get output conductance gds = dIds/dVds
    Real get_output_conductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get bulk transconductance gmb = dIds/dVbs
    Real get_bulk_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    // Gate capacitances
    Real get_cgs(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;
    Real get_cgd(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;
    Real get_cgb(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    // Junction capacitances
    Real get_cbd(Real v_bd_eff) const;
    Real get_cbs(Real v_bs_eff) const;

    // Temperature support
    void set_temperature(Real temp_kelvin) {
        temperature_ = temp_kelvin;
    }

    // Physical constants
    static constexpr Real gmin_default = 1e-12;
    static constexpr Real k_boltzmann = 1.380649e-23;
    static constexpr Real q_electron = 1.602176634e-19;
    static constexpr Real eps_0 = 8.854187817e-12;
    static constexpr Real eps_si = 11.7 * eps_0;
    static constexpr Real eps_ox = 3.9 * eps_0;

protected:
    /// Get effective threshold voltage (|Vth| for P-channel)
    Real get_effective_threshold() const {
        if constexpr (Traits::use_abs_threshold) {
            return std::abs(params_.vfb);
        } else {
            return params_.vfb;
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
    std::vector<NodeId> nodes_;  ///< [Drain, Gate, Source, Bulk]
    MOSFETBsim1Params params_;   ///< Model parameters
    Real temperature_ = 300.15;  ///< Temperature [K] (default: 27°C)

    // State variables for transient analysis (voltage history)
    // Named generically: v1 = Vgs(N)/Vsg(P), v2 = Vds(N)/Vsd(P), etc.
    mutable Real v1_prev_ = 0.0;  // vgs/vsg
    mutable Real v2_prev_ = 0.0;  // vgd/vsd (actually vg-vd for N, vs-vd for P)
    mutable Real v3_prev_ = 0.0;  // vgb
    mutable Real v4_prev_ = 0.0;  // vbd/vdb
    mutable Real v5_prev_ = 0.0;  // vbs/vsb

    // Charge history for Physical LTE
    mutable Real charge_1_prev_ = 0.0;  // gate-source
    mutable Real charge_2_prev_ = 0.0;  // gate-drain
    mutable Real charge_3_prev_ = 0.0;  // gate-bulk
    mutable Real charge_4_prev_ = 0.0;  // bulk-drain / drain-bulk
    mutable Real charge_5_prev_ = 0.0;  // bulk-source / source-bulk

    // Helper functions
    Real compute_effective_mobility(Real v_gs_eff, Real v_ds_eff, Real leff) const;
    Real compute_saturation_voltage(Real v_gs_eff, Real vth, Real leff) const;
    Real compute_eta_effect(Real v_ds_eff, Real leff) const;
    Real compute_subthreshold_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff, Real vth) const;
};

// Explicit template instantiation declarations
extern template class MOSFET_BSIM1<Polarity::N>;
extern template class MOSFET_BSIM1<Polarity::P>;

// Type aliases for backward compatibility
using MOSFET_BSIM1_N = MOSFET_BSIM1<Polarity::N>;
using MOSFET_BSIM1_P = MOSFET_BSIM1<Polarity::P>;

} // namespace kirimaspice::devices
