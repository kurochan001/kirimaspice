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
#include <algorithm>
#include <array>
#include <cctype>
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

    /// The current of the effective device and its derivatives
    struct DrainCurrent {
        Real id = 0.0;   ///< From drain to source for N, from source to drain for P [A]
        Real gm = 0.0;   ///< d id / d Vgs_eff [S]
        Real gds = 0.0;  ///< d id / d Vds_eff [S]
    };

    /// The drain current at the effective voltages (VGS and VDS for N, VSG
    /// and VSD for P) at the device's temperature: the square law with
    /// LAMBDA, divided by 1 + gds_i (RS + RD) for the series resistances
    /// (gds_i that of the square law), times the trapping factor; gm and gds
    /// are forward differences of it. Above VBD the drain adds the breakdown
    /// current kBreakdownConductance (VDS - VBD), zero at VBD, with its
    /// conductance, whether the channel is on or off. The DC, AC and noise
    /// stamps all take it from here.
    [[nodiscard]] DrainCurrent drain_current(Real vgs_eff, Real vds_eff) const;

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
        cgs_hist_.accept(vgs_prev_);
        cgd_hist_.accept(vgd_prev_);
        cds_hist_.accept(vds_prev_);
    }

    // Temperature methods
    void set_temperature(Real temp_K) { temperature_ = temp_K; }
    [[nodiscard]] Real get_temperature() const { return temperature_; }

    // Parameter access, by name in any case; TNOM in kelvin
    [[nodiscard]] std::optional<Real> get_parameter(const std::string& param_name) const override {
        const std::string name = lower(param_name);
        for (const auto& entry : parameter_table()) {
            if (name == entry.name) return *entry.target;
        }
        if (name == "vto") return vth_;
        return std::nullopt;
    }

    bool set_parameter(const std::string& param_name, Real value) override {
        const std::string name = lower(param_name);
        if (name == "vto") return set_parameter("vth", value);
        for (const auto& entry : parameter_table()) {
            if (name == entry.name) {
                if (entry.positive && value <= 0.0) return false;
                if (!entry.positive && entry.non_negative && value < 0.0) return false;
                *entry.target = value;
                return true;
            }
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
    struct ParameterEntry {
        const char* name;
        Real* target;
        bool positive;      ///< must be > 0
        bool non_negative;  ///< must be >= 0
    };
    // Every parameter of the device, for get_parameter and set_parameter
    std::array<ParameterEntry, 17> parameter_table() const {
        auto* self = const_cast<GaN_HEMT*>(this);
        return {{{"beta", &self->beta_, true, false},  {"vth", &self->vth_, false, false},
                 {"lambda", &self->lambda_, false, true}, {"rs", &self->rs_, false, true},
                 {"rd", &self->rd_, false, true},     {"vbd", &self->vbd_, true, false},
                 {"trap", &self->trap_, false, true}, {"cgs", &self->cgs_, false, true},
                 {"cgd", &self->cgd_, false, true},   {"cds", &self->cds_, false, true},
                 {"kf", &self->kf_, false, true},     {"af", &self->af_, true, false},
                 {"ef", &self->ef_, true, false},     {"kt1", &self->kt1_, false, false},
                 {"kt2", &self->kt2_, false, false},  {"ute", &self->ute_, false, false},
                 {"tnom", &self->tnom_, true, false}}};
    }
    static std::string lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    Real beta_, vth_, lambda_;
    Real rs_, rd_, vbd_, trap_;
    Real cgs_, cgd_, cds_;
    Real kf_, af_, ef_;
    Real kt1_, kt2_, ute_, tnom_, temperature_;

    // State variables for transient analysis
    mutable Real vgs_prev_ = 0.0;
    mutable Real vgd_prev_ = 0.0;
    mutable Real vds_prev_ = 0.0;
    device_utils::CapacitorHistory cgs_hist_, cgd_hist_, cds_hist_;

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

    // The threshold that the N-like gate voltage vgs_eff = polarity_sign
    // (vg - vs) is compared with. VTO carries the device's sign, as for the
    // MOSFETs, the MESFET, the SiC MOSFET, the VDMOS and the FinFET (a P
    // device's -1 V is a threshold of +1 V for its VSG), and KT1 and KT2 move
    // the N-like threshold, as for the SiC MOSFET, so that the defaults
    // (-2 V and +2 V, KT1 the same for both) mirror each other
    [[nodiscard]] Real compute_temperature_scaled_vth() const {
        Real temp_diff = temperature_ - tnom_;
        return polarity_sign * vth_ + kt1_ * temp_diff + kt2_ * temp_diff * temp_diff;
    }

    // Whether the channel conducts: the gate against the source, or against
    // the drain when VDS < 0 and the source acts as the drain
    /// Whether the drain carries current: the channel is on, or VDS is
    /// above VBD (drain_current)
    [[nodiscard]] bool conducting(Real vgs_eff, Real vds_eff) const {
        const Real control = (vds_eff < 0.0) ? vgs_eff - vds_eff : vgs_eff;
        return control >= compute_temperature_scaled_vth() || vds_eff > vbd_;
    }
    static constexpr Real kBreakdownConductance = 1e-3;  // [S] above VBD
    // The breakdown is named once per device (stamp_dc_nonlinear)
    mutable bool breakdown_warned_ = false;

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
typename GaN_HEMT<Pol>::DrainCurrent GaN_HEMT<Pol>::drain_current(Real vgs_eff,
                                                                  Real vds_eff) const {
    const Real vth = compute_temperature_scaled_vth();
    const Real beta_temp = compute_temperature_scaled_beta();
    const Real r_total = rs_ + rd_;
    const Real trap_factor = (trap_ > 0.0) ? 1.0 / (1.0 + trap_ * 1e6) : 1.0;

    auto current = [&](Real vgs, Real vds) {
        const Real vov = vgs - vth;
        if (vov <= 0.0) {
            return 0.0;
        }
        Real id_i = 0.0, gds_i = 0.0;
        if (vds < vov) {
            id_i = beta_temp * (vov * vds - 0.5 * vds * vds) * (1.0 + lambda_ * vds);
            gds_i = beta_temp * (vov - vds) * (1.0 + lambda_ * vds) +
                    beta_temp * (vov * vds - 0.5 * vds * vds) * lambda_;
        } else {
            id_i = 0.5 * beta_temp * vov * vov * (1.0 + lambda_ * vds);
            gds_i = 0.5 * beta_temp * vov * vov * lambda_;
        }
        return trap_factor * id_i / (1.0 + gds_i * r_total);
    };

    // For VDS < 0 the source acts as the drain, as in a MOSFET: the same
    // current of VGD and VSD flows from source to drain
    DrainCurrent dc;
    const bool reverse = vds_eff < 0.0;
    const Real vgs = reverse ? vgs_eff - vds_eff : vgs_eff;
    const Real vds = reverse ? -vds_eff : vds_eff;
    // The breakdown current above VBD, with the channel on or off
    auto add_breakdown = [&](DrainCurrent& out) {
        if (vds_eff > vbd_) {
            out.id += kBreakdownConductance * (vds_eff - vbd_);
            out.gds += kBreakdownConductance;
        }
    };
    if (vgs < vth) {
        add_breakdown(dc);  // channel cut off
        return dc;
    }
    constexpr Real delta = 1e-6;
    const Real f = current(vgs, vds);
    const Real gm = (current(vgs + delta, vds) - f) / delta;
    const Real gds = (current(vgs, vds + delta) - f) / delta;
    if (!reverse) {
        dc.id = f;
        dc.gm = gm;
        dc.gds = gds;
    } else {
        // Id = -f(Vgs - Vds, -Vds): its derivatives by Vgs and Vds
        dc.id = -f;
        dc.gm = -gm;
        dc.gds = gm + gds;
    }
    add_breakdown(dc);
    return dc;
}

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

    // Cutoff region
    if (!conducting(vgs_eff, vds_eff)) {
        stamp_gmin(mna);
        return;
    }
    if (vds_eff > vbd_ && !breakdown_warned_) {
        breakdown_warned_ = true;
        spdlog::warn("{} {} exceeds breakdown: Vds={:.2f}V > Vbd={:.2f}V",
                     device_name, name_, vds_eff, vbd_);
    }
    const DrainCurrent dc = drain_current(vgs_eff, vds_eff);
    const Real id = dc.id, gm = dc.gm, gds = dc.gds;

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
                                     IntegrationMethod method,
                                     const BDFCoefficients* bdf_coef,
                                     const TR_BDF2_Coefficients* tr_bdf2_coef) const {
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

    // The capacitances, integrated by the transient's method as a capacitor
    // is (device_utils::CapacitorHistory), from the first node to the
    // second, with the voltages accepted at the time points
    // (accept_solution)
    (void)vgs;
    (void)vgd;
    (void)vds;
    cgs_hist_.stamp(mna, ng_idx, ns_idx, cgs_, method, timestep, bdf_coef, tr_bdf2_coef);
    cgd_hist_.stamp(mna, ng_idx, nd_idx, cgd_, method, timestep, bdf_coef, tr_bdf2_coef);
    cds_hist_.stamp(mna, nd_idx, ns_idx, cds_, method, timestep, bdf_coef, tr_bdf2_coef);
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


    auto nd_idx = (drain == NODE_GND) ? -1 : static_cast<int>(drain - 1);
    auto ng_idx = (gate == NODE_GND) ? -1 : static_cast<int>(gate - 1);
    auto ns_idx = (source == NODE_GND) ? -1 : static_cast<int>(source - 1);

    // Capacitance admittances
    Complex y_cgs(0.0, omega * cgs_);
    Complex y_cgd(0.0, omega * cgd_);
    Complex y_cds(0.0, omega * cds_);

    // Cutoff region - only capacitances
    if (!conducting(vgs_eff, vds_eff)) {
        device_utils::stamp_complex_capacitor(sys.Y, ng_idx, ns_idx, y_cgs, polarity_sign);
        device_utils::stamp_complex_capacitor(sys.Y, ng_idx, nd_idx, y_cgd, 1.0);
        device_utils::stamp_complex_capacitor(sys.Y, nd_idx, ns_idx, y_cds, polarity_sign);
        return;
    }

    // Active region: the small-signal current from drain to source, as in
    // the DC stamp
    const DrainCurrent dc = drain_current(vgs_eff, vds_eff);
    const Real gm = dc.gm, gds = dc.gds;

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

    if (!conducting(vgs_eff, vds_eff)) return sources;
    const DrainCurrent dc = drain_current(vgs_eff, vds_eff);
    const Real gm = std::abs(dc.gm);

    // Thermal noise: 4kT × (8/3) × gm
    Real thermal_psd = device_utils::thermal_noise_psd(temperature_, gm, 8.0 / 3.0);
    sources.push_back({name_ + "_thermal", drain, source, thermal_psd, true});

    // Flicker noise
    if (kf_ > 0.0 && frequency > 0.0) {
        Real flicker_psd =
            device_utils::flicker_noise_psd(kf_, af_, ef_, std::abs(dc.id), frequency);
        sources.push_back({name_ + "_flicker", drain, source, flicker_psd, true});
    }

    return sources;
}

}  // namespace kirimaspice::devices
