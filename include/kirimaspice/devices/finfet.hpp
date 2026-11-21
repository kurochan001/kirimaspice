// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/finfet.hpp
// Unified FinFET template class
#pragma once

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include "kirimaspice/devices/polarity.hpp"
#include <cmath>

namespace kirimaspice::devices {

/**
 * @brief FinFET (Fin Field-Effect Transistor) device template class
 *
 * FinFET is a 3D multi-gate MOSFET structure with superior short-channel
 * effect control and low power consumption. Used in advanced logic processes
 * (14nm and below).
 *
 * **Key Features**:
 * - 3D Fin structure (vertical channel)
 * - Multi-gate (Gate-All-Around)
 * - Low subthreshold swing (SS)
 * - Excellent DIBL control
 * - Multiple fins in parallel
 *
 * **Simplified Model**:
 * - Shichman-Hodges-like equations
 * - Effective width: Weff = NFin × Hfin
 * - Improved subthreshold characteristics
 * - Temperature scaling
 *
 * **Operating Regions**:
 * - Cutoff: Vgs_eff < Vth_eff
 * - Linear: Vgs_eff > Vth_eff, Vds_eff < Vdsat
 * - Saturation: Vgs_eff > Vth_eff, Vds_eff >= Vdsat
 *
 * For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs, Vth = +0.4V (default)
 * For P-channel: Vgs_eff = Vs - Vg, Vds_eff = Vs - Vd, Vth = -0.4V (default)
 *
 * FinFET implementation (LEVEL=12)
 * Unified N/P template
 */
template<Polarity P>
class FinFET : public Device {
public:
    /// Polarity traits for compile-time constants
    using Traits = PolarityTraits<P>;

    /// Device type for N-channel or P-channel FinFET
    static constexpr DeviceType device_type =
        (P == Polarity::N) ? DeviceType::FINFET_N : DeviceType::FINFET_P;

    /**
     * @brief Construct FinFET device with specified polarity
     * @param id Device ID
     * @param name Device name
     * @param drain Drain node
     * @param gate Gate node
     * @param source Source node
     * @param nfin Number of fins (default: 1)
     * @param hfin Fin height [m] (default: 30nm)
     * @param tfin Fin thickness [m] (default: 7nm)
     * @param vth Threshold voltage [V] (default: +0.4V for N, -0.4V for P)
     * @param u0 Low-field mobility [m²/V-s] (default: 300 cm²/V-s for N, 250 for P)
     * @param vsat Saturation velocity [m/s] (default: 1e5 for N, 8e4 for P)
     * @param ss Subthreshold swing [V/dec] (default: 70 mV/dec for N, 75 for P)
     * @param dibl DIBL coefficient [V/V] (default: 0.05 for N, 0.06 for P)
     * @param tox Gate oxide thickness [m] (default: 1nm)
     * @param cgs Gate-source capacitance [F] (default: 1fF)
     * @param cgd Gate-drain capacitance [F] (default: 0.1fF)
     * @param kf Flicker noise coefficient (default: 0)
     * @param af Flicker noise current exponent (default: 1.0)
     * @param kt1 Vth temperature coefficient [V/K] (default: -1mV/K for N, +1mV/K for P)
     * @param tnom Nominal temperature [K] (default: 300.15K)
     * @param l Gate (channel) length [m] (default: 1um, the default L of the
     *          netlist's MOSFETs)
     */
    FinFET(DeviceId id, const std::string& name,
           NodeId drain, NodeId gate, NodeId source,
           int nfin = 1,
           Real hfin = 30e-9,
           Real tfin = 7e-9,
           Real vth = (P == Polarity::N) ? 0.4 : -0.4,
           Real u0 = (P == Polarity::N) ? 300e-4 : 250e-4,
           Real vsat = (P == Polarity::N) ? 1e5 : 8e4,
           Real ss = (P == Polarity::N) ? 70e-3 : 75e-3,
           Real dibl = (P == Polarity::N) ? 0.05 : 0.06,
           Real tox = 1e-9,
           Real cgs = 1e-15,
           Real cgd = 1e-16,
           Real kf = 0.0,
           Real af = 1.0,
           Real kt1 = (P == Polarity::N) ? -1e-3 : 1e-3,
           Real tnom = 300.15,
           Real l = 1e-6);

    ~FinFET() override = default;

    // Device interface
    const std::vector<NodeId>& nodes() const override { return nodes_; }
    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return (cgs_ > 0.0 || cgd_ > 0.0); }

    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        stamp_gmin(mna);
    }

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
    void reset_state() override {
        v_gs_prev_ = 0.0;
        v_gd_prev_ = 0.0;
        cgs_hist_.reset();
        cgd_hist_.reset();
    }

    // Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

    // Accessors
    int num_fins() const { return nfin_; }
    Real fin_height() const { return hfin_; }
    Real fin_thickness() const { return tfin_; }
    Real effective_width() const { return weff_; }
    Real threshold_voltage() const { return vth_; }
    Real mobility() const { return u0_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }

    /// Set temperature and update temperature-dependent parameters
    void set_temperature(Real temp_kelvin) { temperature_ = temp_kelvin; }

    // Physical constants
    static constexpr Real gmin_default = 1e-12;
    static constexpr Real k_boltzmann = 1.380649e-23;
    static constexpr Real q_electron = 1.602176634e-19;
    static constexpr Real epsilon_0 = 8.854187817e-12;
    static constexpr Real epsilon_ox = 3.9;

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
    void stamp_gmin(MNASystem& mna) const;

    /// The drain current of the model and its derivatives
    struct DrainCurrent {
        Real id = 0.0;   ///< From drain to source for N, from source to drain for P [A]
        Real gm = 0.0;   ///< d id / d Vgs_eff [S]
        Real gds = 0.0;  ///< d id / d Vds_eff, with the DIBL term [S]
    };

    /// The drain current at the effective voltages (VGS and VDS for N, VSG
    /// and VSD for P) and the operating temperature. The DC, transient and
    /// AC stamps and the noise sources all take it from here.
    DrainCurrent drain_current(Real v_gs_eff, Real v_ds_eff) const;

    /// Terminal voltages from a vector with node k at element k - 1
    void terminal_voltages(const DenseVector& v, Real& vd, Real& vg, Real& vs) const;

    std::vector<NodeId> nodes_;  ///< [Drain, Gate, Source]

    // FinFET geometry parameters
    int nfin_;      ///< Number of fins
    Real hfin_;     ///< Fin height [m]
    Real tfin_;     ///< Fin thickness [m]
    Real l_;        ///< Gate (channel) length [m]
    Real weff_;     ///< Effective width: Weff = NFin × Hfin [m]

    // Electrical parameters
    Real vth_;      ///< Threshold voltage [V]
    Real u0_;       ///< Low-field mobility [m²/V-s]
    Real vsat_;     ///< Saturation velocity [m/s]
    Real ss_;       ///< Subthreshold swing [V/dec]
    Real dibl_;     ///< DIBL coefficient [V/V]
    Real tox_;      ///< Gate oxide thickness [m]

    // Derived parameters
    Real cox_;      ///< Oxide capacitance per unit area [F/m²]
    Real beta_;     ///< Gain factor u0 Cox Weff / L [A/V²]

    // Capacitances
    Real cgs_;      ///< Gate-source capacitance [F]
    Real cgd_;      ///< Gate-drain capacitance [F]
    mutable Real v_gs_prev_ = 0.0;  ///< Accepted Vg - Vs
    mutable Real v_gd_prev_ = 0.0;  ///< Accepted Vg - Vd
    device_utils::CapacitorHistory cgs_hist_, cgd_hist_;

    // Noise parameters
    Real kf_;       ///< Flicker noise coefficient
    Real af_;       ///< Flicker noise current exponent

    // Temperature parameters
    Real kt1_;      ///< Vth temperature coefficient [V/K]
    Real tnom_;     ///< Nominal temperature [K]
    mutable Real temperature_ = 300.15; ///< Operating temperature [K]
};

// Explicit template instantiation declarations
extern template class FinFET<Polarity::N>;
extern template class FinFET<Polarity::P>;

// Type aliases for backward compatibility
using FinFET_N = FinFET<Polarity::N>;
using FinFET_P = FinFET<Polarity::P>;

} // namespace kirimaspice::devices
