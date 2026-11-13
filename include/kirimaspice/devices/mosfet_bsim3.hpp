// include/kirimaspice/devices/mosfet_bsim3.hpp
// Unified MOSFET BSIM3v3.3 template class
// Reference: BSIM3v3.3 User's Manual (UC Berkeley, 2005)
#pragma once

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/polarity.hpp"
#include "kirimaspice/devices/mosfet_bsim3_params.hpp"
#include "kirimaspice/devices/bsim3_core.hpp"
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

/**
 * @brief MOSFET BSIM3v3.3 device template class
 *
 * Berkeley Short-channel IGFET Model v3.3 with unified N/P-channel implementation.
 * Industry-standard compact MOSFET model for IC design.
 *
 * **Key features**:
 * - Accurate short-channel effects (DVT0, DVT1, DVT2)
 * - Velocity saturation (VSAT)
 * - DIBL (Drain-Induced Barrier Lowering)
 * - Mobility degradation (UA, UB, UC)
 * - Subthreshold conduction (NFACTOR, VOFF)
 * - Channel length modulation (PCLM, PDIBLC1, PDIBLC2)
 * - Temperature dependence (KT1, KT2, UTE)
 * - Comprehensive noise models
 *
 * **Operating regions**:
 * 1. Cutoff (subthreshold): exponential Id-Vgs characteristic
 * 2. Linear (triode): Id = β × [(Vgs-Vth)×Vds - 0.5×Vds²]
 * 3. Saturation: Id = 0.5 × β × Vdsat² × (1 + CLM)
 *
 * For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs, Vbs_eff = Vb - Vs
 * For P-channel: Vgs_eff = Vs - Vg (= vsg), Vds_eff = Vs - Vd (= vsd), Vbs_eff = Vs - Vb
 *
 * Reference: BSIM3v3.3 User's Manual (UC Berkeley, 2005)
 *
 * Unified N/P template
 */
template<Polarity P>
class MOSFET_BSIM3 : public Device {
public:
    /// Polarity traits for compile-time constants
    using Traits = PolarityTraits<P>;

    /// Device type for N-channel or P-channel BSIM3 MOSFET
    static constexpr DeviceType device_type =
        (P == Polarity::N) ? DeviceType::MOSFET_BSIM3_N : DeviceType::MOSFET_BSIM3_P;

    /// Operating region
    enum class Region {
        CUTOFF,       ///< Subthreshold
        SUBTHRESHOLD, ///< Weak inversion
        LINEAR,       ///< Triode region
        SATURATION    ///< Pinch-off region
    };

    /**
     * @brief Construct BSIM3 MOSFET device with specified polarity
     * @param id Device ID
     * @param name Device name
     * @param drain Drain node
     * @param gate Gate node
     * @param source Source node
     * @param bulk Bulk (substrate) node
     * @param params BSIM3 model parameters
     */
    MOSFET_BSIM3(DeviceId id, const std::string& name,
                 NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                 const BSIM3Params& params);

    ~MOSFET_BSIM3() override = default;

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

    // State management
    void save_state() const;
    void restore_state() const;
    void accept_solution(const DenseVector& node_voltages, Real time) const;

    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

    // Accessors
    const BSIM3Params& params() const { return params_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }
    NodeId bulk() const { return nodes_[3]; }

    /// Get operating region for given effective voltages
    Region get_region(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get drain current (positive magnitude |Id|)
    /// Uses BSIM3Core for evaluation with polarity transformation
    Real get_drain_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get transconductance gm = dIds/dVgs
    Real get_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get output conductance gds = dIds/dVds
    Real get_output_conductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    /// Get bulk transconductance gmb = dIds/dVbs
    Real get_bulk_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const;

    // Gate capacitances
    void get_gate_capacitances(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff,
                               Real& cgs, Real& cgd, Real& cgb) const;

    // Junction capacitances
    void get_junction_capacitances(Real v_bd_eff, Real v_bs_eff,
                                   Real& cbd, Real& cbs) const;

    // Temperature support
    void set_temperature(Real temp_kelvin) {
        if (temp_kelvin <= 0.0) {
            throw std::runtime_error("Temperature must be positive (in Kelvin)");
        }
        temp_ = temp_kelvin;
        vt_ = k_boltzmann * temp_kelvin / q_electron;
    }

    Real get_temperature() const { return temp_; }

    // Physical constants
    static constexpr Real gmin_default = 1e-12;
    static constexpr Real k_boltzmann = 1.380649e-23;
    static constexpr Real q_electron = 1.602176634e-19;
    static constexpr Real eps_0 = 8.854187817e-12;
    static constexpr Real eps_si = 11.7 * eps_0;
    static constexpr Real eps_ox = 3.9 * eps_0;

protected:
    /// Convert terminal voltages to effective Vgs, Vds, Vbs
    /// For N-channel: v_gs_eff = vg - vs, v_ds_eff = vd - vs, v_bs_eff = vb - vs
    /// For P-channel: v_gs_eff = vs - vg (= vsg), v_ds_eff = vs - vd (= vsd), v_bs_eff = vs - vb
    void compute_effective_voltages(Real vd, Real vg, Real vs, Real vb,
                                    Real& v_gs_eff, Real& v_ds_eff, Real& v_bs_eff) const {
        if constexpr (P == Polarity::N) {
            v_gs_eff = vg - vs;
            v_ds_eff = vd - vs;
            v_bs_eff = vb - vs;
        } else {
            v_gs_eff = vs - vg;  // vsg
            v_ds_eff = vs - vd;  // vsd
            v_bs_eff = vs - vb;  // vsb (note: often referred to as vbs with opposite sign)
        }
    }

    /// Get effective length [m]
    Real calculate_leff() const { return params_.get_leff(); }

    /// Get effective width [m]
    Real calculate_weff() const { return params_.get_weff(); }

private:
    std::vector<NodeId> nodes_;  ///< [Drain, Gate, Source, Bulk]
    BSIM3Params params_;         ///< Model parameters
    BSIM3Core core_;             ///< DC core evaluator (NMOS-oriented)

    // Temperature state
    mutable Real temp_ = 300.15;   ///< Device temperature [K] (default 27°C)
    mutable Real vt_ = 0.0259;     ///< Thermal voltage kT/q [V] (default at 300K)

    // Dynamic device state variables for transient analysis
    // Gate capacitance voltage history
    mutable Real vgs_prev_ = 0.0, vgs_prev2_ = 0.0, vgs_prev3_ = 0.0;
    mutable Real vgd_prev_ = 0.0, vgd_prev2_ = 0.0, vgd_prev3_ = 0.0;
    mutable Real vgb_prev_ = 0.0, vgb_prev2_ = 0.0, vgb_prev3_ = 0.0;

    // Junction capacitance voltage history
    mutable Real vbd_prev_ = 0.0, vbd_prev2_ = 0.0, vbd_prev3_ = 0.0;
    mutable Real vbs_prev_ = 0.0, vbs_prev2_ = 0.0, vbs_prev3_ = 0.0;

    // Charge history (Physical LTE)
    mutable Real charge_gs_ = 0.0, charge_gs_prev_ = 0.0;
    mutable Real charge_gd_ = 0.0, charge_gd_prev_ = 0.0;
    mutable Real charge_gb_ = 0.0, charge_gb_prev_ = 0.0;
    mutable Real charge_bd_ = 0.0, charge_bd_prev_ = 0.0;
    mutable Real charge_bs_ = 0.0, charge_bs_prev_ = 0.0;

    // Saved state (for timestep rejection)
    mutable Real vgs_saved_ = 0.0, vgs_saved2_ = 0.0, vgs_saved3_ = 0.0;
    mutable Real vgd_saved_ = 0.0, vgd_saved2_ = 0.0, vgd_saved3_ = 0.0;
    mutable Real vgb_saved_ = 0.0, vgb_saved2_ = 0.0, vgb_saved3_ = 0.0;
    mutable Real vbd_saved_ = 0.0, vbd_saved2_ = 0.0, vbd_saved3_ = 0.0;
    mutable Real vbs_saved_ = 0.0, vbs_saved2_ = 0.0, vbs_saved3_ = 0.0;
    mutable Real charge_gs_saved_ = 0.0, charge_gd_saved_ = 0.0;
    mutable Real charge_gb_saved_ = 0.0, charge_bd_saved_ = 0.0, charge_bs_saved_ = 0.0;

    mutable bool first_accept_ = true;  ///< First accept flag

    // Integration state (for companion model)
    mutable Real timestep_ = 0.0;
    mutable IntegrationMethod last_method_ = IntegrationMethod::BACKWARD_EULER;

    // Helper functions
    void stamp_gmin(MNASystem& mna) const;

    /// Calculate threshold voltage [V]
    Real calculate_vth(Real v_bs_eff, Real leff, Real weff) const;

    /// Calculate effective mobility [cm²/Vs]
    Real calculate_mobility(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff,
                           Real vth, Real leff, Real weff) const;

    /// Calculate saturation drain-source voltage [V]
    Real calculate_vdsat(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff, Real vth) const;

    /// Calculate DIBL effect [V]
    Real calculate_dibl(Real v_ds_eff, Real leff) const;

    /// Calculate channel length modulation factor
    Real calculate_clm_factor(Real v_ds_eff, Real vdsat) const;

    /// Apply temperature correction to mobility
    Real apply_temp_mobility(Real u0_base) const;

    /// Apply temperature correction to threshold voltage
    Real apply_temp_vth(Real vth_base, Real v_bs_eff) const;

    /// Apply temperature correction to saturation velocity
    Real apply_temp_vsat(Real vsat_base) const;
};

// Explicit template instantiation declarations
extern template class MOSFET_BSIM3<Polarity::N>;
extern template class MOSFET_BSIM3<Polarity::P>;

// Type aliases for backward compatibility
using MOSFET_BSIM3_N = MOSFET_BSIM3<Polarity::N>;
using MOSFET_BSIM3_P = MOSFET_BSIM3<Polarity::P>;

} // namespace kirimaspice::devices
