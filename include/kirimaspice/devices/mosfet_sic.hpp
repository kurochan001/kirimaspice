// include/kirimaspice/devices/mosfet_sic.hpp
// Unified SiC MOSFET template class
#pragma once

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/polarity.hpp"
#include <cmath>

namespace kirimaspice::devices {

/**
 * @brief SiC MOSFET (Silicon Carbide Power MOSFET) device template class
 *
 * Based on Shichman-Hodges Level 1 model with SiC-specific enhancements.
 *
 * **Operating regions**:
 * 1. Cutoff (Vgs_eff < Vth_eff):
 *    Id = 0
 *
 * 2. Linear (Vgs_eff > Vth_eff, Vds_eff < Vgs_eff - Vth_eff):
 *    Id = beta × ((Vgs_eff - Vth_eff) × Vds_eff - Vds_eff²/2)
 *    where beta = Kp × (W/L) / (1 + THETA × Vds_eff)
 *
 * 3. Saturation (Vgs_eff > Vth_eff, Vds_eff >= Vgs_eff - Vth_eff):
 *    Id = (beta/2) × (Vgs_eff - Vth_eff)²
 *
 * **SiC-specific features**:
 * - Low on-resistance (Rdrift): Drift region resistance dominates
 * - High breakdown voltage: 600V-1700V typical
 * - High temperature operation: -55°C to 175°C
 * - Fast switching: Low gate charge
 * - Body diode: Built-in PN diode with forward voltage ~3.5V
 *
 * **Key parameters**:
 * - Kp    = process transconductance [A/V²]
 * - THETA = mobility modulation [V⁻¹] (SiC-specific, higher than Si)
 * - Rdrift = drift region resistance [Ω] (SiC-specific, very low)
 * - NFS   = subthreshold current fitting [cm⁻²V⁻¹] (SiC-specific)
 * - Vbd   = breakdown voltage [V] (SiC-specific, 600V-1700V)
 *
 * **Temperature coefficients**:
 * - Threshold voltage: VTO(T) = VTO0 + KT1×(T-Tnom) + KT2×(T-Tnom)²
 * - Transconductance: KP(T) = KP0 × (T/Tnom)^UTE
 * - Drift resistance: Rd(T) = Rd0 × (T/Tnom)^RDTEMP
 *
 * For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs, Vth = +2.5V (default)
 * For P-channel: Vgs_eff = Vs - Vg (= Vsg), Vds_eff = Vs - Vd (= Vsd), Vth = -2.5V (default)
 *
 * SiC MOSFET implementation
 * Unified N/P template
 */
template<Polarity P>
class MOSFET_SiC : public Device {
public:
    /// Polarity traits for compile-time constants
    using Traits = PolarityTraits<P>;

    /// Device type for N-channel or P-channel SiC MOSFET
    static constexpr DeviceType device_type =
        (P == Polarity::N) ? DeviceType::MOSFET_SIC_N : DeviceType::MOSFET_SIC_P;

    /**
     * @brief Construct SiC MOSFET device with specified polarity
     * @param id Device ID
     * @param name Device name
     * @param drain Drain node
     * @param gate Gate node
     * @param source Source node
     * @param kp Process transconductance [A/V²] (default: 2e-5)
     * @param w Channel width [m] (default: 10µm)
     * @param l Channel length [m] (default: 1µm)
     * @param vth Threshold voltage [V] (default: +2.5V for N, -2.5V for P)
     * @param theta Mobility modulation [V⁻¹] (default: 0.1)
     * @param rdrift Drift region resistance [Ω] (default: 10mΩ)
     * @param nfs Subthreshold fitting constant [cm⁻²V⁻¹] (default: 1e11)
     * @param vbd Breakdown voltage [V] (default: 1200V)
     * @param cgso Gate-source overlap capacitance per width [F/m] (default: 1nF/m)
     * @param cgdo Gate-drain overlap capacitance per width [F/m] (default: 1nF/m)
     * @param cgbo Gate-bulk overlap capacitance per length [F/m] (default: 0)
     * @param cjo Zero-bias junction capacitance [F] (default: 1nF)
     * @param pb Built-in potential [V] (default: 0.8V)
     * @param mj Grading coefficient (default: 0.5)
     * @param kf Flicker noise coefficient (default: 0)
     * @param af Flicker noise current exponent (default: 1.0)
     * @param ef Flicker noise frequency exponent (default: 1.0)
     * @param gamma Channel thermal noise coefficient (default: 0.667)
     * @param kt1 VTO temperature coefficient [V/K] (default: -5mV/K)
     * @param kt2 VTO second-order temperature coefficient [V/K²] (default: 0)
     * @param ute Mobility temperature exponent (default: -1.5)
     * @param rdtemp Drift resistance temperature exponent (default: 2.5)
     * @param tnom Nominal temperature [K] (default: 300.15K)
     */
    MOSFET_SiC(DeviceId id, const std::string& name,
               NodeId drain, NodeId gate, NodeId source,
               Real kp = 2e-5,
               Real w = 10e-6,
               Real l = 1e-6,
               Real vth = (P == Polarity::N) ? 2.5 : -2.5,
               Real theta = 0.1,
               Real rdrift = 10e-3,
               Real nfs = 1e11,
               Real vbd = 1200.0,
               Real cgso = 1e-9,
               Real cgdo = 1e-9,
               Real cgbo = 0.0,
               Real cjo = 1e-9,
               Real pb = 0.8,
               Real mj = 0.5,
               Real kf = 0.0,
               Real af = 1.0,
               Real ef = 1.0,
               Real gamma = 0.667,
               Real kt1 = -5e-3,
               Real kt2 = 0.0,
               Real ute = -1.5,
               Real rdtemp = 2.5,
               Real tnom = 300.15);

    ~MOSFET_SiC() override = default;

    // Device interface
    const std::vector<NodeId>& nodes() const override { return nodes_; }
    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return (cgso_ > 0.0 || cgdo_ > 0.0 || cgbo_ > 0.0 || cjo_ > 0.0); }

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
    void reset_state() override;

    // Temperature control
    void set_temperature(Real temp_kelvin);
    Real get_temperature() const { return temperature_; }

    // Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

    // Accessors
    Real process_transconductance() const { return kp_; }
    Real channel_width() const { return w_; }
    Real channel_length() const { return l_; }
    Real threshold_voltage() const { return vth_; }
    Real drift_resistance() const { return rdrift_; }
    Real breakdown_voltage() const { return vbd_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }

    // Physical constants
    static constexpr Real gmin_default = 1e-12;
    static constexpr Real k_boltzmann = 1.380649e-23;

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
    Real compute_temperature_scaled_vth(Real vbs = 0.0) const;
    Real compute_temperature_scaled_kp() const;
    Real compute_temperature_scaled_rdrift() const;

    std::vector<NodeId> nodes_;  ///< [Drain, Gate, Source]

    // Basic MOSFET parameters
    Real kp_;      ///< Process transconductance [A/V²]
    Real w_;       ///< Channel width [m]
    Real l_;       ///< Channel length [m]
    Real vth_;     ///< Threshold voltage [V]
    Real beta_;    ///< Derived: kp * w / l

    // SiC-specific DC parameters
    Real theta_;    ///< Mobility modulation [V⁻¹]
    Real rdrift_;   ///< Drift region resistance [Ω]
    Real nfs_;      ///< Subthreshold fitting constant [cm⁻²V⁻¹]
    Real vbd_;      ///< Breakdown voltage [V]

    // Capacitance parameters
    Real cgso_;     ///< Gate-source overlap capacitance per width [F/m]
    Real cgdo_;     ///< Gate-drain overlap capacitance per width [F/m]
    Real cgbo_;     ///< Gate-bulk overlap capacitance per length [F/m]
    Real cjo_;      ///< Zero-bias junction capacitance [F]
    Real pb_;       ///< Built-in potential [V]
    Real mj_;       ///< Grading coefficient

    // Noise parameters
    Real kf_;       ///< Flicker noise coefficient
    Real af_;       ///< Flicker noise current exponent
    Real ef_;       ///< Flicker noise frequency exponent
    Real gamma_;    ///< Channel thermal noise coefficient

    // Temperature parameters
    Real kt1_;      ///< VTO temperature coefficient [V/K]
    Real kt2_;      ///< VTO second-order temperature coefficient [V/K²]
    Real ute_;      ///< Mobility temperature exponent
    Real rdtemp_;   ///< Drift resistance temperature exponent
    Real tnom_;     ///< Nominal temperature [K]
    Real temperature_ = 300.15; ///< Operating temperature [K]

    // Voltage history for transient analysis
    mutable Real vgs_prev_ = 0.0;
    mutable Real vgd_prev_ = 0.0;
    mutable Real vds_prev_ = 0.0;
};

// Explicit template instantiation declarations
extern template class MOSFET_SiC<Polarity::N>;
extern template class MOSFET_SiC<Polarity::P>;

// Type aliases for backward compatibility
using MOSFET_SiC_N = MOSFET_SiC<Polarity::N>;
using MOSFET_SiC_P = MOSFET_SiC<Polarity::P>;

} // namespace kirimaspice::devices
