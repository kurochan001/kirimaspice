// include/kirimaspice/devices/vdmos.hpp
// Unified VDMOS (Vertical Double-diffused MOSFET) template class
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

/// VDMOS Polarity enumeration for template parameter
enum class VDMOSPolarity { N, P };

/// VDMOS: Vertical Double-diffused MOSFET template class
/// Unified implementation for both N-channel and P-channel
///
/// Operating regions:
/// 1. Cutoff (Vgs_eff < Vth_eff):
///    Id = 0
///
/// 2. Linear (Vgs_eff > Vth_eff, Vds_eff < Vgs_eff - Vth_eff):
///    Id = KP × ((Vgs_eff - Vth_eff) × Vds_eff - Vds_eff²/2) × (1 + LAMBDA × Vds_eff)
///
/// 3. Saturation (Vgs_eff > Vth_eff, Vds_eff >= Vgs_eff - Vth_eff):
///    Id = (KP/2) × (Vgs_eff - Vth_eff)² × (1 + LAMBDA × Vds_eff)
///
/// Key features:
///   - Self-consistent RDS calculation with iterative solution
///   - Voltage-dependent gate-drain capacitance (Miller effect)
///   - Temperature-dependent parameters (VTO, KP, RDS)
///   - Avalanche breakdown modeling
///
/// For N-channel: Vgs_eff = Vg - Vs, Vds_eff = Vd - Vs
/// For P-channel: Vgs_eff = Vs - Vg, Vds_eff = Vs - Vd
///
/// Note: This is a 3-terminal model (drain, gate, source).
/// Body/substrate is assumed to be connected to source (Vbs = 0).
template<VDMOSPolarity Pol>
class VDMOS : public Device {
public:
    /// Polarity sign: +1 for N-channel, -1 for P-channel
    static constexpr Real polarity_sign = (Pol == VDMOSPolarity::N) ? 1.0 : -1.0;
    static constexpr bool is_n_channel = (Pol == VDMOSPolarity::N);

    VDMOS(DeviceId id, const std::string& name,
          NodeId drain, NodeId gate, NodeId source,
          // Basic VDMOS parameters
          Real vto = 4.0,        // Threshold voltage [V] (3-5V typical)
          Real kp = 20.0,        // Transconductance parameter [A/V²] (10-50 typical)
          Real rds = 0.1,        // On-resistance [Ω] (10mΩ-1Ω typical)
          Real vbd = 600.0,      // Breakdown voltage [V] (60-600V typical)
          Real lambda = 0.01,    // Channel length modulation [1/V]
          // Capacitance parameters
          Real cgs = 3e-9,       // Gate-source capacitance [F] (1-10nF typical)
          Real cgdmax = 1e-9,    // Gate-drain max capacitance [F] (at Vds=0)
          Real cgdmin = 1e-11,   // Gate-drain min capacitance [F] (at Vds >> Vth)
          Real cds = 1e-10,      // Drain-source capacitance [F] (100-500pF typical)
          // Noise parameters
          Real kf = 0.0,         // Flicker noise coefficient
          Real af = 1.0,         // Flicker noise current exponent
          Real ef = 1.0,         // Flicker noise frequency exponent
          Real gamma = 0.667,    // Channel thermal noise coefficient
          // Temperature parameters
          Real kt1 = -3e-3,      // VTO temperature coefficient [V/K] (-2 to -5 mV/K)
          Real kt2 = 0.0,        // VTO second-order temperature coefficient [V/K²]
          Real ute = -1.5,       // Mobility temperature exponent (-1.0 to -1.8)
          Real rtc = 2.0,        // RDS temperature coefficient (1.5 to 2.5)
          Real tnom = 300.15)    // Nominal temperature [K] (27°C)
        : Device(id, name, is_n_channel ? DeviceType::VDMOS_N : DeviceType::VDMOS_P)
        , nodes_({drain, gate, source})
        , vto_(vto), kp_(kp), rds_(rds), vbd_(vbd), lambda_(lambda)
        , cgs_(cgs), cgdmax_(cgdmax), cgdmin_(cgdmin), cds_(cds)
        , kf_(kf), af_(af), ef_(ef), gamma_(gamma)
        , kt1_(kt1), kt2_(kt2), ute_(ute), rtc_(rtc), tnom_(tnom)
        , temperature_(tnom) {

        // Parameter validation
        if (kp <= 0.0) {
            throw std::runtime_error("VDMOS transconductance parameter must be positive: " + name);
        }
        if (rds < 0.0) {
            throw std::runtime_error("VDMOS on-resistance must be non-negative: " + name);
        }
        if (vbd <= 0.0) {
            throw std::runtime_error("VDMOS breakdown voltage must be positive: " + name);
        }
        if (lambda < 0.0) {
            throw std::runtime_error("VDMOS channel length modulation must be non-negative: " + name);
        }
        if (cgs < 0.0 || cgdmax < 0.0 || cgdmin < 0.0 || cds < 0.0) {
            throw std::runtime_error("VDMOS capacitance must be non-negative: " + name);
        }
        if (cgdmax < cgdmin) {
            throw std::runtime_error("VDMOS Cgdmax must be >= Cgdmin: " + name);
        }

        const char* type_str = is_n_channel ? "VDMOS_N" : "VDMOS_P";
        spdlog::debug("Created {} {} (D={}, G={}, S={}, VTO={:.3f}V, KP={:.6e}A/V², "
                      "RDS={:.6e}Ω, VBD={:.1f}V, LAMBDA={:.6e}V⁻¹)",
                      type_str, name, drain, gate, source, vto_, kp_, rds_, vbd_, lambda_);
    }

    ~VDMOS() override = default;

    [[nodiscard]] const std::vector<NodeId>& nodes() const override { return nodes_; }
    [[nodiscard]] bool is_linear() const override { return false; }
    [[nodiscard]] bool is_dynamic() const override { return (cgs_ > 0.0 || cgdmax_ > 0.0 || cds_ > 0.0); }

    void stamp_dc_linear(MNASystem& mna, const DenseVector& /*node_voltages*/) const override {
        stamp_gmin(mna);
    }

    void stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const override;
    void stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                        Real timestep, IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;
    void stamp_ac(ComplexMNASystem& sys, Real omega, const DenseVector& dc_solution) const override;

    [[nodiscard]] std::vector<NoiseSource> get_noise_sources(
        const DenseVector& dc_solution, Real frequency) const override;

    [[nodiscard]] Real get_physical_lte(Real /*chgtol*/, const DenseVector& /*node_voltages*/) const override { return 0.0; }

    void set_temperature(Real temp_kelvin) {
        if (temp_kelvin <= 0.0) {
            throw std::runtime_error("Temperature must be positive: " + name_);
        }
        temperature_ = temp_kelvin;
        spdlog::debug("{} set temperature to {:.2f}K", name_, temperature_);
    }
    [[nodiscard]] Real get_temperature() const { return temperature_; }

    // State management
    void reset_state() override {
        vgs_prev_ = 0.0;
        vgd_prev_ = 0.0;
        vds_prev_ = 0.0;
    }

    void accept_solution(const DenseVector& node_voltages) override {
        update_operating_point(node_voltages);
    }

    // Parameter access for sensitivity analysis
    [[nodiscard]] std::optional<Real> get_parameter(const std::string& param_name) const override {
        if (param_name == "vto" || param_name == "VTO") return vto_;
        if (param_name == "kp" || param_name == "KP") return kp_;
        if (param_name == "rds" || param_name == "RDS") return rds_;
        if (param_name == "vbd" || param_name == "VBD") return vbd_;
        if (param_name == "lambda" || param_name == "LAMBDA") return lambda_;
        if (param_name == "cgs" || param_name == "CGS") return cgs_;
        if (param_name == "cgdmax" || param_name == "CGDMAX") return cgdmax_;
        if (param_name == "cgdmin" || param_name == "CGDMIN") return cgdmin_;
        if (param_name == "cds" || param_name == "CDS") return cds_;
        return std::nullopt;
    }

    bool set_parameter(const std::string& param_name, Real value) override {
        if ((param_name == "vto" || param_name == "VTO")) { vto_ = value; return true; }
        if ((param_name == "kp" || param_name == "KP") && value > 0.0) { kp_ = value; return true; }
        if ((param_name == "rds" || param_name == "RDS") && value >= 0.0) { rds_ = value; return true; }
        if ((param_name == "vbd" || param_name == "VBD") && value > 0.0) { vbd_ = value; return true; }
        if ((param_name == "lambda" || param_name == "LAMBDA") && value >= 0.0) { lambda_ = value; return true; }
        if ((param_name == "cgs" || param_name == "CGS") && value >= 0.0) { cgs_ = value; return true; }
        if ((param_name == "cgdmax" || param_name == "CGDMAX") && value >= 0.0) { cgdmax_ = value; return true; }
        if ((param_name == "cgdmin" || param_name == "CGDMIN") && value >= 0.0) { cgdmin_ = value; return true; }
        if ((param_name == "cds" || param_name == "CDS") && value >= 0.0) { cds_ = value; return true; }
        return false;
    }

    [[nodiscard]] std::vector<std::string> get_perturbable_parameters() const override {
        return {"VTO", "KP", "RDS", "VBD", "LAMBDA"};
    }

private:
    std::vector<NodeId> nodes_;  // D, G, S

    // Parameters
    Real vto_, kp_, rds_, vbd_, lambda_;
    Real cgs_, cgdmax_, cgdmin_, cds_;
    Real kf_, af_, ef_, gamma_;
    Real kt1_, kt2_, ute_, rtc_, tnom_;
    Real temperature_;

    // Voltage history
    mutable Real vgs_prev_ = 0.0;
    mutable Real vgd_prev_ = 0.0;
    mutable Real vds_prev_ = 0.0;

    // Constants
    static constexpr Real gmin_default = 1e-12;
    static constexpr Real k_boltzmann = 1.380649e-23;

    void stamp_gmin(MNASystem& mna) const {
        NodeId nd = nodes_[0], ng = nodes_[1], ns = nodes_[2];
        Real g = gmin_default;

        auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
        auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
        auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

        // D-S conductance
        if (nd_idx >= 0) {
            mna.G.add_entry(nd_idx, nd_idx, g);
            if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -g);
        }
        if (ns_idx >= 0) {
            mna.G.add_entry(ns_idx, ns_idx, g);
            if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, -g);
        }

        // G-S conductance
        if (ng_idx >= 0) {
            mna.G.add_entry(ng_idx, ng_idx, g);
            if (ns_idx >= 0) mna.G.add_entry(ng_idx, ns_idx, -g);
        }
        if (ns_idx >= 0) {
            mna.G.add_entry(ns_idx, ns_idx, g);
            if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -g);
        }
    }

    [[nodiscard]] Real compute_temperature_scaled_vto() const {
        Real temp_diff = temperature_ - tnom_;
        return vto_ + kt1_ * temp_diff + kt2_ * temp_diff * temp_diff;
    }

    [[nodiscard]] Real compute_temperature_scaled_kp() const {
        return kp_ * std::pow(temperature_ / tnom_, ute_);
    }

    [[nodiscard]] Real compute_temperature_scaled_rds() const {
        return rds_ * std::pow(temperature_ / tnom_, rtc_);
    }

    [[nodiscard]] Real compute_cgd(Real vds_eff) const {
        if (vds_eff <= 0.0) return cgdmax_;
        Real vto_scaled = std::abs(compute_temperature_scaled_vto());
        Real ratio = vds_eff / vto_scaled;
        return cgdmin_ + (cgdmax_ - cgdmin_) / (1.0 + ratio * ratio);
    }

    void update_operating_point(const DenseVector& node_voltages) {
        NodeId drain = nodes_[0], gate = nodes_[1], source = nodes_[2];
        Real vd = (drain == NODE_GND) ? 0.0 : node_voltages[drain - 1];
        Real vg = (gate == NODE_GND) ? 0.0 : node_voltages[gate - 1];
        Real vs = (source == NODE_GND) ? 0.0 : node_voltages[source - 1];

        vgs_prev_ = vg - vs;
        vgd_prev_ = vg - vd;
        vds_prev_ = vd - vs;
    }

    /// Compute effective voltages based on polarity
    void compute_effective_voltages(Real vd, Real vg, Real vs,
                                    Real& vgs_eff, Real& vds_eff) const {
        if constexpr (is_n_channel) {
            vgs_eff = vg - vs;
            vds_eff = vd - vs;
        } else {
            vgs_eff = vs - vg;  // Vsg for P-channel
            vds_eff = vs - vd;  // Vsd for P-channel
        }
    }
};

// ============================================================================
// Implementation
// ============================================================================

template<VDMOSPolarity Pol>
void VDMOS<Pol>::stamp_dc_nonlinear(MNASystem& mna,
                                     const DenseVector& node_voltages) const {
    NodeId nd = nodes_[0], ng = nodes_[1], ns = nodes_[2];

    Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

    Real vgs_eff, vds_eff;
    compute_effective_voltages(vd, vg, vs, vgs_eff, vds_eff);

    // Temperature-scaled parameters
    Real vth_raw = compute_temperature_scaled_vto();
    Real vth = is_n_channel ? vth_raw : std::abs(vth_raw);
    Real kp_temp = compute_temperature_scaled_kp();
    Real rds_temp = compute_temperature_scaled_rds();

    // Cutoff region
    if (vgs_eff < vth) {
        Real gmin = 1e-12;
        auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
        auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

        if (nd_idx >= 0) {
            mna.G.add_entry(nd_idx, nd_idx, gmin);
            if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -gmin);
        }
        if (ns_idx >= 0) {
            mna.G.add_entry(ns_idx, ns_idx, gmin);
            if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, -gmin);
        }
        return;
    }

    // Overdrive voltage
    Real vov = vgs_eff - vth;

    // Drain current computation with RDS self-alignment
    Real ids = 0.0, gm = 0.0, gds = 0.0;
    Real vds_internal = vds_eff;

    const int max_iterations = 5;
    for (int iter = 0; iter < max_iterations; ++iter) {
        Real ids_old = ids;

        if (vds_internal < vov) {
            // Linear region
            Real clm = 1.0 + lambda_ * vds_internal;
            ids = kp_temp * (vov * vds_internal - 0.5 * vds_internal * vds_internal) * clm;
            gm = kp_temp * vds_internal * clm;
            gds = kp_temp * (vov - vds_internal) * clm +
                  kp_temp * (vov * vds_internal - 0.5 * vds_internal * vds_internal) * lambda_;
        } else {
            // Saturation region
            Real clm = 1.0 + lambda_ * vds_internal;
            ids = 0.5 * kp_temp * vov * vov * clm;
            gm = kp_temp * vov * clm;
            gds = 0.5 * kp_temp * vov * vov * lambda_;
        }

        gds += 1.0 / rds_temp;
        vds_internal = vds_eff - ids * rds_temp;

        Real rel_change = std::abs(ids - ids_old) / (std::abs(ids) + 1e-9);
        if (rel_change < 1e-6) break;

        if (iter > 0) ids = 0.7 * ids + 0.3 * ids_old;
    }

    // Breakdown voltage check
    if (vds_eff > vbd_) {
        Real delta_v = vds_eff - vbd_;
        Real breakdown_current = 1e-3 * std::exp(10.0 * delta_v);
        ids += breakdown_current;
        gds += 10.0 * 1e-3 * std::exp(10.0 * delta_v);

        const char* voltage_name = is_n_channel ? "Vds" : "Vsd";
        spdlog::warn("{} entered breakdown region: {}={:.2f}V > Vbd={:.2f}V",
                     name_, voltage_name, vds_eff, vbd_);
    }

    // Convert for MNA stamping
    Real id_mna, vgs_mna, vds_mna;
    if constexpr (is_n_channel) {
        id_mna = ids;
        vgs_mna = vgs_eff;
        vds_mna = vds_eff;
    } else {
        // P-channel: convert from Vsg/Vsd convention back to Vgs/Vds
        id_mna = -ids;  // Negate current for MNA convention
        vgs_mna = -vgs_eff;
        vds_mna = -vds_eff;
    }

    // Stamp Norton equivalent
    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    Real i_norton = id_mna - gm * vgs_mna - gds * vds_mna;

    // Conductance matrix
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, gds);
        if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -gds);
    }
    if (ns_idx >= 0) {
        mna.G.add_entry(ns_idx, ns_idx, gds);
        if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, -gds);
    }

    // Transconductance terms
    if (nd_idx >= 0) {
        if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, gm);
        if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -gm);
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -gm);
        if (ns_idx >= 0) mna.G.add_entry(ns_idx, ns_idx, gm);
    }

    // Current source
    if (nd_idx >= 0) mna.b[nd_idx] -= i_norton;
    if (ns_idx >= 0) mna.b[ns_idx] += i_norton;
}

template<VDMOSPolarity Pol>
void VDMOS<Pol>::stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                                  Real timestep, IntegrationMethod method,
                                  const BDFCoefficients* bdf_coef,
                                  const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // First stamp the DC nonlinear part
    stamp_dc_nonlinear(mna, node_voltages);

    // Get node indices and voltages
    NodeId nd = nodes_[0], ng = nodes_[1], ns = nodes_[2];

    Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[vs - 1];

    Real vgs = vg - vs;
    Real vgd = vg - vd;
    Real vds = vd - vs;

    // Compute voltage-dependent Cgd
    Real vds_eff = is_n_channel ? vds : -vds;
    Real cgd = compute_cgd(std::abs(vds_eff));

    // Determine integration coefficient based on method
    Real alpha = 1.0;  // Default: Backward Euler
    if (method == IntegrationMethod::TRAPEZOIDAL) {
        alpha = 2.0;
    } else if (method == IntegrationMethod::GEAR2 && bdf_coef) {
        alpha = bdf_coef->alpha0;
    } else if (method == IntegrationMethod::GEAR3 && bdf_coef) {
        alpha = bdf_coef->alpha0;
    } else if (method == IntegrationMethod::TR_BDF2 && tr_bdf2_coef) {
        alpha = tr_bdf2_coef->alpha0;
    }

    Real geq_cgs = alpha * cgs_ / timestep;
    Real geq_cgd = alpha * cgd / timestep;
    Real geq_cds = alpha * cds_ / timestep;

    // Historical current terms
    Real ieq_cgs = cgs_ * (vgs - vgs_prev_) / timestep;
    Real ieq_cgd = cgd * (vgd - vgd_prev_) / timestep;
    Real ieq_cds = cds_ * (vds - vds_prev_) / timestep;

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // Stamp Cgs
    if (ng_idx >= 0) {
        mna.G.add_entry(ng_idx, ng_idx, geq_cgs);
        if (ns_idx >= 0) mna.G.add_entry(ng_idx, ns_idx, -geq_cgs);
    }
    if (ns_idx >= 0) {
        mna.G.add_entry(ns_idx, ns_idx, geq_cgs);
        if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -geq_cgs);
    }
    if (ng_idx >= 0) mna.b[ng_idx] -= ieq_cgs;
    if (ns_idx >= 0) mna.b[ns_idx] += ieq_cgs;

    // Stamp Cgd (Miller capacitance)
    if (ng_idx >= 0) {
        mna.G.add_entry(ng_idx, ng_idx, geq_cgd);
        if (nd_idx >= 0) mna.G.add_entry(ng_idx, nd_idx, -geq_cgd);
    }
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, geq_cgd);
        if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, -geq_cgd);
    }
    if (ng_idx >= 0) mna.b[ng_idx] -= ieq_cgd;
    if (nd_idx >= 0) mna.b[nd_idx] += ieq_cgd;

    // Stamp Cds
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, geq_cds);
        if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -geq_cds);
    }
    if (ns_idx >= 0) {
        mna.G.add_entry(ns_idx, ns_idx, geq_cds);
        if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, -geq_cds);
    }
    if (nd_idx >= 0) mna.b[nd_idx] -= ieq_cds;
    if (ns_idx >= 0) mna.b[ns_idx] += ieq_cds;
}

template<VDMOSPolarity Pol>
void VDMOS<Pol>::stamp_ac(ComplexMNASystem& sys, Real omega,
                           const DenseVector& dc_solution) const {
    NodeId nd = nodes_[0], ng = nodes_[1], ns = nodes_[2];

    Real vd = (nd == NODE_GND) ? 0.0 : dc_solution[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : dc_solution[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : dc_solution[ns - 1];

    Real vgs_eff, vds_eff;
    compute_effective_voltages(vd, vg, vs, vgs_eff, vds_eff);

    // Temperature-scaled parameters
    Real vth_raw = compute_temperature_scaled_vto();
    Real vth = is_n_channel ? vth_raw : std::abs(vth_raw);
    Real kp_temp = compute_temperature_scaled_kp();
    Real rds_temp = compute_temperature_scaled_rds();

    // Small-signal parameters
    Real gm = 0.0, gds = 1.0 / rds_temp;  // Minimum gds from RDS

    if (vgs_eff >= vth) {
        Real vov = vgs_eff - vth;
        Real vds_internal = vds_eff;

        // Simple iteration for internal Vds
        for (int iter = 0; iter < 3; ++iter) {
            Real ids_temp;
            if (vds_internal < vov) {
                ids_temp = kp_temp * (vov * vds_internal - 0.5 * vds_internal * vds_internal);
            } else {
                ids_temp = 0.5 * kp_temp * vov * vov;
            }
            vds_internal = vds_eff - ids_temp * rds_temp;
        }

        if (vds_internal < vov) {
            Real clm = 1.0 + lambda_ * vds_internal;
            gm = kp_temp * vds_internal * clm;
            gds += kp_temp * (vov - vds_internal) * clm +
                   kp_temp * (vov * vds_internal - 0.5 * vds_internal * vds_internal) * lambda_;
        } else {
            Real clm = 1.0 + lambda_ * vds_internal;
            gm = kp_temp * vov * clm;
            gds += 0.5 * kp_temp * vov * vov * lambda_;
        }
    }

    // Capacitances
    Real cgd = compute_cgd(std::abs(vds_eff));
    Complex jw(0.0, omega);

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // Capacitance admittances
    Complex y_cgs(0.0, omega * cgs_);
    Complex y_cgd(0.0, omega * cgd);
    Complex y_cds(0.0, omega * cds_);

    // Stamp gds (output conductance)
    device_utils::stamp_complex_conductance_ds(sys.Y, nd_idx, ns_idx, Complex(gds, 0.0));

    // Stamp gm (transconductance): id = gm * vgs
    device_utils::stamp_complex_vccs_3terminal(sys.Y, nd_idx, ng_idx, ns_idx,
                                               Complex(gm, 0.0), polarity_sign);

    // Stamp capacitances
    device_utils::stamp_complex_capacitor(sys.Y, ng_idx, ns_idx, y_cgs, polarity_sign);
    device_utils::stamp_complex_capacitor(sys.Y, ng_idx, nd_idx, y_cgd, 1.0);
    device_utils::stamp_complex_capacitor(sys.Y, nd_idx, ns_idx, y_cds, polarity_sign);
}

template<VDMOSPolarity Pol>
std::vector<NoiseSource> VDMOS<Pol>::get_noise_sources(
    const DenseVector& dc_solution, Real frequency) const {

    std::vector<NoiseSource> sources;
    if (frequency <= 0.0) return sources;

    NodeId drain = nodes_[0], source = nodes_[2];

    Real vd = (drain == NODE_GND) ? 0.0 : dc_solution[drain - 1];
    Real vs = (source == NODE_GND) ? 0.0 : dc_solution[source - 1];
    Real vg = (nodes_[1] == NODE_GND) ? 0.0 : dc_solution[nodes_[1] - 1];

    Real vgs_eff, vds_eff;
    compute_effective_voltages(vd, vg, vs, vgs_eff, vds_eff);

    Real vth = is_n_channel ? compute_temperature_scaled_vto()
                            : std::abs(compute_temperature_scaled_vto());
    Real kp_temp = compute_temperature_scaled_kp();

    // Compute gm
    Real gm = 0.0;
    if (vgs_eff >= vth) {
        Real vov = vgs_eff - vth;
        Real clm = 1.0 + lambda_ * vds_eff;
        if (vds_eff < vov) {
            gm = kp_temp * vds_eff * clm;
        } else {
            gm = kp_temp * vov * clm;
        }
    }

    // Channel thermal noise
    Real thermal_psd = device_utils::thermal_noise_psd(temperature_, gm, gamma_);
    if (thermal_psd > 0.0) {
        sources.push_back({name_ + "_thermal", drain, source, thermal_psd, true});
    }

    // Flicker noise
    Real ids = 0.0;
    if (vgs_eff >= vth) {
        Real vov = vgs_eff - vth;
        if (vds_eff < vov) {
            ids = kp_temp * (vov * vds_eff - 0.5 * vds_eff * vds_eff);
        } else {
            ids = 0.5 * kp_temp * vov * vov;
        }
    }

    if (kf_ > 0.0 && ids > 0.0) {
        Real flicker_psd = kf_ * std::pow(std::abs(ids), af_) / std::pow(frequency, ef_);
        if (flicker_psd > 0.0) {
            sources.push_back({name_ + "_flicker", drain, source, flicker_psd, true});
        }
    }

    return sources;
}

// Type aliases for backward compatibility
using VDMOS_N = VDMOS<VDMOSPolarity::N>;
using VDMOS_P = VDMOS<VDMOSPolarity::P>;

} // namespace kirimaspice::devices
