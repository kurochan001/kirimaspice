// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/gan_hemt.hpp
/**
 * @file gan_hemt.hpp
 * @brief Unified GaN HEMT (High Electron Mobility Transistor) template class
 *
 * N- and P-channel GaN HEMTs share one template.
 */
#pragma once

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <vector>
#include <string>
#include <cmath>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

/**
 * @brief Polarity enumeration for GaN HEMT devices
 */
enum class GaNPolarity { N, P };

/**
 * @brief Unified GaN HEMT template class
 *
 * @tparam Pol GaNPolarity::N for N-channel, GaNPolarity::P for P-channel
 *
 * This template unifies GaN_HEMT_N and GaN_HEMT_P implementations.
 * The polarity sign handles all voltage convention and current direction differences.
 */
template<GaNPolarity Pol>
class GaN_HEMT : public Device {
public:
    /// Polarity sign: +1 for N-channel, -1 for P-channel
    static constexpr Real polarity_sign = (Pol == GaNPolarity::N) ? 1.0 : -1.0;

    /// Device type based on polarity
    static constexpr DeviceType device_type =
        (Pol == GaNPolarity::N) ? DeviceType::GAN_HEMT_N : DeviceType::GAN_HEMT_P;

    /// Device name string for logging
    static constexpr const char* device_name =
        (Pol == GaNPolarity::N) ? "GaN_HEMT_N" : "GaN_HEMT_P";

    /**
     * @brief Constructor for GaN HEMT device
     */
    GaN_HEMT(DeviceId id, const std::string& name,
             NodeId drain, NodeId gate, NodeId source,
             Real beta = 0.1, Real vth = (Pol == GaNPolarity::N) ? -2.0 : 2.0,
             Real lambda = 0.04, Real rs = 0.1, Real rd = 0.1,
             Real vbd = 1200.0, Real trap = 0.0,
             Real cgs = 1e-12, Real cgd = 0.5e-12, Real cds = 0.1e-12,
             Real kf = 0.0, Real af = 1.0, Real ef = 1.0,
             Real kt1 = -2e-3, Real kt2 = 0.0, Real ute = -1.5, Real tnom = 300.15)
        : Device(id, name, device_type),
          id_(id), name_(name), nodes_({drain, gate, source}),
          beta_(beta), vth_(vth), lambda_(lambda),
          rs_(rs), rd_(rd), vbd_(vbd), trap_(trap),
          cgs_(cgs), cgd_(cgd), cds_(cds),
          kf_(kf), af_(af), ef_(ef),
          kt1_(kt1), kt2_(kt2), ute_(ute), tnom_(tnom), temperature_(tnom) {

        if (beta <= 0.0) {
            throw std::invalid_argument(std::string(device_name) + ": beta must be positive");
        }
        if (lambda < 0.0) {
            throw std::invalid_argument(std::string(device_name) + ": lambda must be non-negative");
        }
        if (rs < 0.0 || rd < 0.0) {
            throw std::invalid_argument(std::string(device_name) + ": series resistances must be non-negative");
        }
        if (vbd <= 0.0) {
            throw std::invalid_argument(std::string(device_name) + ": vbd must be positive");
        }
        if (trap < 0.0) {
            throw std::invalid_argument(std::string(device_name) + ": trap must be non-negative");
        }
        if (cgs < 0.0 || cgd < 0.0 || cds < 0.0) {
            throw std::invalid_argument(std::string(device_name) + ": capacitances must be non-negative");
        }

        spdlog::debug("Created {}: {} D={} G={} S={} beta={:.3e}A/V² vth={:.2f}V lambda={:.3e}V⁻¹",
                      device_name, name_, drain, gate, source, beta_, vth_, lambda_);
    }

    [[nodiscard]] const std::vector<NodeId>& nodes() const override { return nodes_; }
    [[nodiscard]] bool is_linear() const override { return false; }
    [[nodiscard]] bool is_dynamic() const override { return true; }  // Has capacitors

    void stamp_dc_linear(MNASystem& /*mna*/, const DenseVector& /*node_voltages*/) const override {}
    void stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const override;
    void stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                         Real timestep, IntegrationMethod method,
                         const BDFCoefficients* bdf_coef = nullptr,
                         const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;
    void stamp_ac(ComplexMNASystem& sys, Real omega, const DenseVector& dc_solution) const override;

    void update_operating_point(const DenseVector& node_voltages) {
        NodeId drain = nodes_[0], gate = nodes_[1], source = nodes_[2];
        Real vd = (drain == NODE_GND) ? 0.0 : node_voltages[drain - 1];
        Real vg = (gate == NODE_GND) ? 0.0 : node_voltages[gate - 1];
        Real vs = (source == NODE_GND) ? 0.0 : node_voltages[source - 1];

        // Store with unified convention (always vg-vs, vd-vs for internal state)
        vgs_prev_ = vg - vs;
        vgd_prev_ = vg - vd;
        vds_prev_ = vd - vs;
    }

    [[nodiscard]] std::vector<NoiseSource> get_noise_sources(
        const DenseVector& dc_solution, Real frequency) const override;

    [[nodiscard]] Real get_physical_lte(Real /*chgtol*/, const DenseVector& /*node_voltages*/) const override { return 0.0; }
    // The voltages of the capacitances at the accepted time point (node k at
    // element k, ground at element 0)
    void accept_solution(const DenseVector& solution) override {
        const NodeId drain = nodes_[0], gate = nodes_[1], source = nodes_[2];
        const Real vd = (drain == NODE_GND) ? 0.0 : solution[drain];
        const Real vg = (gate == NODE_GND) ? 0.0 : solution[gate];
        const Real vs = (source == NODE_GND) ? 0.0 : solution[source];
        vgs_prev_ = vg - vs;
        vgd_prev_ = vg - vd;
        vds_prev_ = vd - vs;
    }

    // Temperature methods
    void set_temperature(Real temp_K) { temperature_ = temp_K; }
    [[nodiscard]] Real get_temperature() const { return temperature_; }

    // Parameter access
    [[nodiscard]] std::optional<Real> get_parameter(const std::string& param_name) const override {
        if (param_name == "beta" || param_name == "BETA") return beta_;
        if (param_name == "vth" || param_name == "VTH") return vth_;
        if (param_name == "lambda" || param_name == "LAMBDA") return lambda_;
        if (param_name == "rs" || param_name == "RS") return rs_;
        if (param_name == "rd" || param_name == "RD") return rd_;
        if (param_name == "vbd" || param_name == "VBD") return vbd_;
        if (param_name == "trap" || param_name == "TRAP") return trap_;
        if (param_name == "cgs" || param_name == "CGS") return cgs_;
        if (param_name == "cgd" || param_name == "CGD") return cgd_;
        if (param_name == "cds" || param_name == "CDS") return cds_;
        return std::nullopt;
    }

    bool set_parameter(const std::string& param_name, Real value) override {
        if ((param_name == "beta" || param_name == "BETA") && value > 0.0) {
            beta_ = value; return true;
        }
        if (param_name == "vth" || param_name == "VTH") { vth_ = value; return true; }
        if ((param_name == "lambda" || param_name == "LAMBDA") && value >= 0.0) {
            lambda_ = value; return true;
        }
        return false;
    }

    [[nodiscard]] std::vector<std::string> get_perturbable_parameters() const override {
        return {"beta", "vth", "lambda"};
    }

private:
    DeviceId id_;
    std::string name_;
    std::vector<NodeId> nodes_;  // D, G, S

    // Device parameters
    Real beta_, vth_, lambda_;
    Real rs_, rd_, vbd_, trap_;
    Real cgs_, cgd_, cds_;
    Real kf_, af_, ef_;
    Real kt1_, kt2_, ute_, tnom_, temperature_;

    // State variables for transient analysis
    mutable Real vgs_prev_ = 0.0;
    mutable Real vgd_prev_ = 0.0;
    mutable Real vds_prev_ = 0.0;

    // Helper methods
    void stamp_gmin(MNASystem& mna) const {
        NodeId drain = nodes_[0], source = nodes_[2];
        constexpr Real gmin = 1e-12;
        auto nd = (drain == NODE_GND) ? -1 : static_cast<int>(drain - 1);
        auto ns = (source == NODE_GND) ? -1 : static_cast<int>(source - 1);

        if (nd >= 0) {
            mna.G.add_entry(nd, nd, gmin);
            if (ns >= 0) mna.G.add_entry(nd, ns, -gmin);
        }
        if (ns >= 0) {
            if (nd >= 0) mna.G.add_entry(ns, nd, -gmin);
            mna.G.add_entry(ns, ns, gmin);
        }
    }

    [[nodiscard]] Real compute_temperature_scaled_vth() const {
        Real temp_diff = temperature_ - tnom_;
        return vth_ + kt1_ * temp_diff + kt2_ * temp_diff * temp_diff;
    }

    [[nodiscard]] Real compute_temperature_scaled_beta() const {
        Real temp_ratio = temperature_ / tnom_;
        return beta_ * std::pow(temp_ratio, ute_);
    }
};

// Type aliases for backward compatibility
using GaN_HEMT_N = GaN_HEMT<GaNPolarity::N>;
using GaN_HEMT_P = GaN_HEMT<GaNPolarity::P>;

// ============================================================================
// Template Implementation
// ============================================================================

template<GaNPolarity Pol>
void GaN_HEMT<Pol>::stamp_dc_nonlinear(MNASystem& mna,
                                        const DenseVector& node_voltages) const {
    NodeId drain = nodes_[0], gate = nodes_[1], source = nodes_[2];
    Real vd = (drain == NODE_GND) ? 0.0 : node_voltages[drain - 1];
    Real vg = (gate == NODE_GND) ? 0.0 : node_voltages[gate - 1];
    Real vs = (source == NODE_GND) ? 0.0 : node_voltages[source - 1];

    // Unified voltage calculation with polarity sign
    // N-channel: vgs = vg - vs, vds = vd - vs (polarity_sign = +1)
    // P-channel: vsg = vs - vg, vsd = vs - vd (polarity_sign = -1)
    Real vgs_eff = polarity_sign * (vg - vs);
    Real vds_eff = polarity_sign * (vd - vs);

    Real vth = compute_temperature_scaled_vth();
    Real beta_temp = compute_temperature_scaled_beta();

    // Cutoff region
    if (vgs_eff < vth) {
        stamp_gmin(mna);
        return;
    }

    Real vov = vgs_eff - vth;  // Overdrive voltage
    Real vds_sat = vov;

    Real id_intrinsic = 0.0, gm_intrinsic = 0.0, gds_intrinsic = 0.0;

    if (vds_eff < vds_sat) {
        // Linear region
        id_intrinsic = beta_temp * (vov * vds_eff - 0.5 * vds_eff * vds_eff) * (1.0 + lambda_ * vds_eff);
        gm_intrinsic = beta_temp * vds_eff * (1.0 + lambda_ * vds_eff);
        gds_intrinsic = beta_temp * (vov - vds_eff) * (1.0 + lambda_ * vds_eff)
                      + beta_temp * (vov * vds_eff - 0.5 * vds_eff * vds_eff) * lambda_;
    } else {
        // Saturation region
        id_intrinsic = 0.5 * beta_temp * vov * vov * (1.0 + lambda_ * vds_eff);
        gm_intrinsic = beta_temp * vov * (1.0 + lambda_ * vds_eff);
        gds_intrinsic = 0.5 * beta_temp * vov * vov * lambda_;
    }

    // Series resistance correction
    Real r_total = rs_ + rd_;
    Real id = id_intrinsic / (1.0 + gds_intrinsic * r_total);
    Real gm = gm_intrinsic / (1.0 + gds_intrinsic * r_total);
    Real gds = gds_intrinsic / (1.0 + gds_intrinsic * r_total);

    // Breakdown handling
    if (vds_eff > vbd_) {
        spdlog::warn("{} {} exceeds breakdown: Vds={:.2f}V > Vbd={:.2f}V",
                     device_name, name_, vds_eff, vbd_);
        gds += 1e-3;
    }

    // Trapping effect
    if (trap_ > 0.0) {
        Real trap_factor = 1.0 / (1.0 + trap_ * 1e6);
        id *= trap_factor;
        gm *= trap_factor;
        gds *= trap_factor;
    }

    // Equivalent current source
    Real id_eq = id - gm * vgs_eff - gds * vds_eff;

    auto nd_idx = (drain == NODE_GND) ? -1 : static_cast<int>(drain - 1);
    auto ng_idx = (gate == NODE_GND) ? -1 : static_cast<int>(gate - 1);
    auto ns_idx = (source == NODE_GND) ? -1 : static_cast<int>(source - 1);

    // MNA stamping with polarity-dependent current direction
    // N-channel: current flows D→S (positive into drain)
    // P-channel: current flows S→D (positive into source)
    device_utils::stamp_vccs_3terminal(mna.G, nd_idx, ng_idx, ns_idx, gm, polarity_sign);
    device_utils::stamp_conductance_ds(mna.G, nd_idx, ns_idx, gds);

    // RHS stamping
    if constexpr (Pol == GaNPolarity::N) {
        if (nd_idx >= 0) mna.b[nd_idx] -= id_eq;
        if (ns_idx >= 0) mna.b[ns_idx] += id_eq;
    } else {
        if (ns_idx >= 0) mna.b[ns_idx] -= id_eq;
        if (nd_idx >= 0) mna.b[nd_idx] += id_eq;
    }

    spdlog::trace("{} {}: Vgs={:.3f}V Vds={:.3f}V Id={:.6e}A gm={:.6e}S gds={:.6e}S",
                  device_name, name_, vgs_eff, vds_eff, id, gm, gds);
}

template<GaNPolarity Pol>
void GaN_HEMT<Pol>::stamp_transient(MNASystem& mna,
                                     const DenseVector& node_voltages,
                                     Real timestep,
                                     IntegrationMethod /*method*/,
                                     const BDFCoefficients* /*bdf_coef*/,
                                     const TR_BDF2_Coefficients* /*tr_bdf2_coef*/) const {
    // First stamp DC nonlinear contribution
    stamp_dc_nonlinear(mna, node_voltages);

    NodeId drain = nodes_[0], gate = nodes_[1], source = nodes_[2];
    Real vd = (drain == NODE_GND) ? 0.0 : node_voltages[drain - 1];
    Real vg = (gate == NODE_GND) ? 0.0 : node_voltages[gate - 1];
    Real vs = (source == NODE_GND) ? 0.0 : node_voltages[source - 1];

    // Current voltages (stored with N-channel convention internally)
    Real vgs = vg - vs;
    Real vgd = vg - vd;
    Real vds = vd - vs;

    auto nd_idx = (drain == NODE_GND) ? -1 : static_cast<int>(drain - 1);
    auto ng_idx = (gate == NODE_GND) ? -1 : static_cast<int>(gate - 1);
    auto ns_idx = (source == NODE_GND) ? -1 : static_cast<int>(source - 1);

    Real dt = timestep;

    // The capacitances by backward Euler: the current C / dt (V - V_prev)
    // from the first node to the second, with V_prev accepted at the last
    // time point (accept_solution)
    (void)vgs;
    (void)vgd;
    (void)vds;
    auto companion = [&](Real c, int n1, int n2, Real v_prev) {
        if (c <= 0.0) {
            return;
        }
        const Real g = c / dt;
        device_utils::stamp_capacitor_companion(mna, n1, n2, g, -g * v_prev, 1.0);
    };
    companion(cgs_, ng_idx, ns_idx, vgs_prev_);
    companion(cgd_, ng_idx, nd_idx, vgd_prev_);
    companion(cds_, nd_idx, ns_idx, vds_prev_);
}

template<GaNPolarity Pol>
void GaN_HEMT<Pol>::stamp_ac(ComplexMNASystem& sys, Real omega,
                              const DenseVector& dc_solution) const {
    NodeId drain = nodes_[0], gate = nodes_[1], source = nodes_[2];
    Real vd = (drain == NODE_GND) ? 0.0 : dc_solution[drain - 1];
    Real vg = (gate == NODE_GND) ? 0.0 : dc_solution[gate - 1];
    Real vs = (source == NODE_GND) ? 0.0 : dc_solution[source - 1];

    Real vgs_eff = polarity_sign * (vg - vs);
    Real vds_eff = polarity_sign * (vd - vs);

    Real vth = compute_temperature_scaled_vth();
    Real beta_temp = compute_temperature_scaled_beta();

    auto nd_idx = (drain == NODE_GND) ? -1 : static_cast<int>(drain - 1);
    auto ng_idx = (gate == NODE_GND) ? -1 : static_cast<int>(gate - 1);
    auto ns_idx = (source == NODE_GND) ? -1 : static_cast<int>(source - 1);

    // Capacitance admittances
    Complex y_cgs(0.0, omega * cgs_);
    Complex y_cgd(0.0, omega * cgd_);
    Complex y_cds(0.0, omega * cds_);

    // Cutoff region - only capacitances
    if (vgs_eff < vth) {
        device_utils::stamp_complex_capacitor(sys.Y, ng_idx, ns_idx, y_cgs, polarity_sign);
        device_utils::stamp_complex_capacitor(sys.Y, ng_idx, nd_idx, y_cgd, 1.0);
        device_utils::stamp_complex_capacitor(sys.Y, nd_idx, ns_idx, y_cds, polarity_sign);
        return;
    }

    // Active region - compute small-signal parameters
    Real vov = vgs_eff - vth;
    Real vds_sat = vov;

    Real gm = 0.0, gds = 0.0;
    if (vds_eff < vds_sat) {
        gm = beta_temp * vds_eff * (1.0 + lambda_ * vds_eff);
        gds = beta_temp * (vov - vds_eff) * (1.0 + lambda_ * vds_eff)
            + beta_temp * (vov * vds_eff - 0.5 * vds_eff * vds_eff) * lambda_;
    } else {
        gm = beta_temp * vov * (1.0 + lambda_ * vds_eff);
        gds = 0.5 * beta_temp * vov * vov * lambda_;
    }

    // Series resistance correction
    Real r_total = rs_ + rd_;
    gm = gm / (1.0 + gds * r_total);
    gds = gds / (1.0 + gds * r_total);

    // Trapping effect
    if (trap_ > 0.0) {
        Real trap_factor = 1.0 / (1.0 + trap_ * 1e6);
        gm *= trap_factor;
        gds *= trap_factor;
    }

    // Stamp transconductance and output conductance
    device_utils::stamp_complex_vccs_3terminal(sys.Y, nd_idx, ng_idx, ns_idx,
                                               Complex(gm, 0.0), polarity_sign);
    device_utils::stamp_complex_conductance_ds(sys.Y, nd_idx, ns_idx, Complex(gds, 0.0));

    // Stamp capacitances
    device_utils::stamp_complex_capacitor(sys.Y, ng_idx, ns_idx, y_cgs, polarity_sign);
    device_utils::stamp_complex_capacitor(sys.Y, ng_idx, nd_idx, y_cgd, 1.0);
    device_utils::stamp_complex_capacitor(sys.Y, nd_idx, ns_idx, y_cds, polarity_sign);
}

template<GaNPolarity Pol>
std::vector<NoiseSource> GaN_HEMT<Pol>::get_noise_sources(
    const DenseVector& dc_solution, Real frequency) const {

    std::vector<NoiseSource> sources;
    NodeId drain = nodes_[0], gate = nodes_[1], source = nodes_[2];

    Real vd = (drain == NODE_GND) ? 0.0 : dc_solution[drain - 1];
    Real vg = (gate == NODE_GND) ? 0.0 : dc_solution[gate - 1];
    Real vs = (source == NODE_GND) ? 0.0 : dc_solution[source - 1];

    Real vgs_eff = polarity_sign * (vg - vs);
    Real vds_eff = polarity_sign * (vd - vs);

    Real vth = compute_temperature_scaled_vth();
    Real beta_temp = compute_temperature_scaled_beta();

    if (vgs_eff < vth) return sources;

    Real vov = vgs_eff - vth;
    Real vds_sat = vov;

    Real gm = 0.0;
    if (vds_eff < vds_sat) {
        gm = beta_temp * vds_eff * (1.0 + lambda_ * vds_eff);
    } else {
        gm = beta_temp * vov * (1.0 + lambda_ * vds_eff);
    }

    Real r_total = rs_ + rd_;
    gm = std::abs(gm / (1.0 + gm * r_total * 0.1));

    // Thermal noise: 4kT × (8/3) × gm
    Real thermal_psd = device_utils::thermal_noise_psd(temperature_, gm, 8.0 / 3.0);
    sources.push_back({name_ + "_thermal", drain, source, thermal_psd, true});

    // Flicker noise
    if (kf_ > 0.0 && frequency > 0.0) {
        Real id = 0.5 * beta_temp * vov * vov * (1.0 + lambda_ * vds_eff);
        Real flicker_psd = device_utils::flicker_noise_psd(kf_, af_, ef_, std::abs(id), frequency);
        sources.push_back({name_ + "_flicker", drain, source, flicker_psd, true});
    }

    return sources;
}

}  // namespace kirimaspice::devices
