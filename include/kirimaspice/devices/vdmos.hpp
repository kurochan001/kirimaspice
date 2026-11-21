// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/vdmos.hpp
// Unified VDMOS (Vertical Double-diffused MOSFET) template class
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

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
        for (const auto& entry : plain_parameters()) {
            if (lower_case(param_name) == entry.name) return *entry.target;
        }
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
        if (param_name == "rdson" || param_name == "RDSON") return set_parameter("rds", value);
        for (const auto& entry : plain_parameters()) {
            if (lower_case(param_name) == entry.name) {
                if (entry.positive && value <= 0.0) return false;
                *entry.target = value;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::vector<std::string> get_perturbable_parameters() const override {
        return {"VTO", "KP", "RDS", "VBD", "LAMBDA"};
    }

private:
    struct PlainParameter {
        const char* name;
        Real* target;
        bool positive;  ///< must be > 0
    };
    // The parameters that set_parameter's own cases do not cover; TNOM in kelvin
    std::vector<PlainParameter> plain_parameters() const {
        auto* self = const_cast<VDMOS*>(this);
        return {{"kf", &self->kf_, false},   {"af", &self->af_, true},
                {"ef", &self->ef_, true},    {"gamma", &self->gamma_, false},
                {"kt1", &self->kt1_, false}, {"kt2", &self->kt2_, false},
                {"ute", &self->ute_, false}, {"rtc", &self->rtc_, false},
                {"tnom", &self->tnom_, true}};
    }
    static std::string lower_case(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    std::vector<NodeId> nodes_;  // D, G, S

    // Parameters
    Real vto_, kp_, rds_, vbd_, lambda_;
    mutable bool breakdown_warned_ = false;  // stamp_dc_nonlinear
    Real cgs_, cgdmax_, cgdmin_, cds_;
    Real kf_, af_, ef_, gamma_;
    Real kt1_, kt2_, ute_, rtc_, tnom_;
    Real temperature_;

    // Voltage history
    mutable Real vgs_prev_ = 0.0;
    mutable Real vgd_prev_ = 0.0;
    mutable Real vds_prev_ = 0.0;

    // The drain current of the model and its derivatives
    struct DrainCurrent {
        Real id = 0.0;   ///< From drain to source for N, from source to drain for P [A]
        Real gm = 0.0;   ///< d id / d Vgs_eff [S]
        Real gds = 0.0;  ///< d id / d Vds_eff [S]
    };

    /// The drain current at the effective voltages (VGS and VDS for N, VSG
    /// and VSD for P) and the operating temperature. The DC and AC stamps
    /// and the noise sources take it from here.
    DrainCurrent drain_current(Real vgs_eff, Real vds_eff) const;

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
        Real vd = (drain == NODE_GND) ? 0.0 : node_voltages[drain];
        Real vg = (gate == NODE_GND) ? 0.0 : node_voltages[gate];
        Real vs = (source == NODE_GND) ? 0.0 : node_voltages[source];

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
typename VDMOS<Pol>::DrainCurrent VDMOS<Pol>::drain_current(Real vgs_eff, Real vds_eff) const {
    // The channel is a square law with channel-length modulation, in series
    // with RDS at the drain: the current I solves I = f(VGS, VDS - I RDS).
    const Real vth_raw = compute_temperature_scaled_vto();
    const Real vth = is_n_channel ? vth_raw : std::abs(vth_raw);
    const Real kp = compute_temperature_scaled_kp();
    const Real rds = compute_temperature_scaled_rds();

    DrainCurrent out;
    if (vgs_eff >= vth) {
        const Real vov = vgs_eff - vth;
        // f and its derivatives with respect to VGS and to the channel's own
        // drain voltage v
        auto channel = [&](Real v, Real& f, Real& f_g, Real& f_v) {
            const Real clm = 1.0 + lambda_ * v;
            if (v < vov) {
                const Real q = vov * v - 0.5 * v * v;
                f = kp * q * clm;
                f_g = kp * v * clm;
                f_v = kp * (vov - v) * clm + kp * q * lambda_;
            } else {
                f = 0.5 * kp * vov * vov * clm;
                f_g = kp * vov * clm;
                f_v = 0.5 * kp * vov * vov * lambda_;
            }
        };
        // I - f(VDS - I RDS) grows with I and changes sign between 0 and
        // f(VDS), the current without RDS: Newton's method, kept inside
        // that bracket by bisection
        Real f = 0.0, f_g = 0.0, f_v = 0.0;
        channel(vds_eff, f, f_g, f_v);
        Real lo = std::min(0.0, f);
        Real hi = std::max(0.0, f);
        Real i = f;
        for (int iter = 0; iter < 200 && rds > 0.0; ++iter) {
            channel(vds_eff - i * rds, f, f_g, f_v);
            const Real residual = i - f;
            if (residual > 0.0) {
                hi = i;
            } else {
                lo = i;
            }
            Real next = i - residual / (1.0 + rds * f_v);
            if (!(next > lo && next < hi)) {
                next = 0.5 * (lo + hi);
            }
            const bool done = std::abs(next - i) <= 1e-14 * std::max(std::abs(i), 1e-12);
            i = next;
            if (done) {
                break;
            }
        }
        channel(vds_eff - i * rds, f, f_g, f_v);
        out.id = i;
        // dI = f_g dVGS + f_v (dVDS - RDS dI)
        out.gm = f_g / (1.0 + rds * f_v);
        out.gds = f_v / (1.0 + rds * f_v);
    }

    // Avalanche above VBD: 1 mA (exp(10 (VDS - VBD)) - 1), which is zero at
    // VBD and so continuous there, and extended linearly where the
    // exponential would overflow, as a junction's is (core::safe_exp_junction)
    if (vds_eff > vbd_) {
        constexpr Real kAvalancheVt = 0.1;  // 1 / (10 per volt)
        constexpr Real kAvalancheScale = 1e-3;
        const Real x = vds_eff - vbd_;
        out.id += core::safe_exp_junction(x, kAvalancheVt, kAvalancheScale);
        out.gds += core::safe_exp_conductance(x, kAvalancheVt, kAvalancheScale / kAvalancheVt, 0.0);
    }
    return out;
}

template<VDMOSPolarity Pol>
void VDMOS<Pol>::stamp_dc_nonlinear(MNASystem& mna,
                                     const DenseVector& node_voltages) const {
    NodeId nd = nodes_[0], ng = nodes_[1], ns = nodes_[2];

    Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

    Real vgs_eff, vds_eff;
    compute_effective_voltages(vd, vg, vs, vgs_eff, vds_eff);
    const DrainCurrent dc = drain_current(vgs_eff, vds_eff);
    // Named once per device, not at every Newton iteration
    if (vds_eff > vbd_ && !breakdown_warned_) {
        breakdown_warned_ = true;
        const char* voltage_name = is_n_channel ? "Vds" : "Vsd";
        spdlog::warn("{} entered breakdown region: {}={:.2f}V > Vbd={:.2f}V",
                     name_, voltage_name, vds_eff, vbd_);
    }

    // The current from drain to source is id for N and -id for P; its
    // derivatives with respect to Vg, Vd and Vs are gm, gds and -(gm + gds)
    // for both (device_utils::stamp_vccs_3terminal, stamp_conductance_ds)
    const Real i_ds = is_n_channel ? dc.id : -dc.id;
    const Real i_eq = i_ds - dc.gm * (vg - vs) - dc.gds * (vd - vs);

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);
    device_utils::stamp_vccs_3terminal(mna.G, nd_idx, ng_idx, ns_idx, dc.gm, polarity_sign);
    device_utils::stamp_conductance_ds(mna.G, nd_idx, ns_idx, dc.gds + gmin_default);
    if (nd_idx >= 0) mna.b[nd_idx] -= i_eq;
    if (ns_idx >= 0) mna.b[ns_idx] += i_eq;
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
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

    Real vgs = vg - vs;
    Real vgd = vg - vd;
    Real vds = vd - vs;

    // Compute voltage-dependent Cgd
    Real vds_eff = is_n_channel ? vds : -vds;
    Real cgd = compute_cgd(std::abs(vds_eff));

    // The capacitances by backward Euler, whatever the method: the current
    // C / dt (V - V_prev), with V_prev accepted at the last time point
    // (update_operating_point)
    (void)method;
    (void)bdf_coef;
    (void)tr_bdf2_coef;
    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);
    auto companion = [&](Real c, int n1, int n2, Real v_prev) {
        const Real g = c / timestep;
        device_utils::stamp_capacitor_companion(mna, n1, n2, g, -g * v_prev, 1.0);
    };
    companion(cgs_, ng_idx, ns_idx, vgs_prev_);
    companion(cgd, ng_idx, nd_idx, vgd_prev_);
    companion(cds_, nd_idx, ns_idx, vds_prev_);
    (void)vgs;
    (void)vgd;
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
    const DrainCurrent dc = drain_current(vgs_eff, vds_eff);

    // Capacitances
    Real cgd = compute_cgd(std::abs(vds_eff));

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    Complex y_cgs(0.0, omega * cgs_);
    Complex y_cgd(0.0, omega * cgd);
    Complex y_cds(0.0, omega * cds_);

    // The small-signal current from drain to source, as in the DC stamp
    device_utils::stamp_complex_conductance_ds(sys.Y, nd_idx, ns_idx,
                                               Complex(dc.gds + gmin_default, 0.0));
    device_utils::stamp_complex_vccs_3terminal(sys.Y, nd_idx, ng_idx, ns_idx,
                                               Complex(dc.gm, 0.0), polarity_sign);

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
    const DrainCurrent dc = drain_current(vgs_eff, vds_eff);

    // Channel thermal noise
    Real thermal_psd = device_utils::thermal_noise_psd(temperature_, dc.gm, gamma_);
    if (thermal_psd > 0.0) {
        sources.push_back({name_ + "_thermal", drain, source, thermal_psd, true});
    }

    // Flicker noise
    if (kf_ > 0.0 && dc.id > 0.0) {
        Real flicker_psd = kf_ * std::pow(std::abs(dc.id), af_) / std::pow(frequency, ef_);
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
