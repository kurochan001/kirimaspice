// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/jfet.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/constants.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace kirimaspice::devices {

/// Polarity for JFET devices
enum class JFETPolarity { N, P };

/// JFET: the junction field-effect transistor of SPICE (ngspice's level 1,
/// Shichman and Hodges), for both polarities.
///
/// With the polarity t (+1 for N, -1 for P), the device works on
/// vgs = t (Vg - Vs), vgd = t (Vg - Vd) and vds = vgs - vgd. For vds >= 0 the
/// channel current from drain to source is, with vgst = vgs - VTO,
///   0                                  for vgst <= 0,
///   BETA vds (2 vgst - vds) (1 + LAMBDA vds)  for 0 < vds < vgst,
///   BETA vgst^2 (1 + LAMBDA vds)        for vds >= vgst,
/// and for vds < 0 drain and source change places (vgd for vgs, -vds for
/// vds, the current reversed). The terminal current is t times it. Each of
/// the gate-source and gate-drain junctions carries IS (exp(v/(N Vt)) - 1)
/// with GMIN in parallel, and a depletion capacitance C0/sqrt(1 - v/PB),
/// continued linearly above FC PB (CGS and CGD for C0).
///
/// With the temperature T and dT = T - TNOM (set_temperature): IS is
/// IS exp((T/TNOM - 1) EG/(N Vt)); VTO moves by its coefficient times dT
/// (VTOTC, or -TCV); BETA is BETA 1.01^(BETATCE dT), or BETA (T/TNOM)^BEX
/// without BETATCE; PB, CGS and CGD follow SPICE's junction law
/// (core::junction_potential_at, core::depletion_capacitance_at with
/// M = 0.5). ngspice-45.2 gives these values (run 2026-11-28;
/// tests/unit/test_jfet.cpp). RD and RS are resistors that the parser puts
/// in series with the drain and the source. Not modelled: the doping-tail
/// parameter B (other than 1) and noise.
template<JFETPolarity Pol>
class JFET : public Device {
public:
    /// Polarity sign: +1 for N-channel, -1 for P-channel
    static constexpr Real polarity_sign = (Pol == JFETPolarity::N) ? 1.0 : -1.0;

    /// Device type based on polarity
    static constexpr DeviceType device_type =
        (Pol == JFETPolarity::N) ? DeviceType::JFET_N : DeviceType::JFET_P;

    /// Type name string
    static constexpr const char* type_name =
        (Pol == JFETPolarity::N) ? "JFET_N" : "JFET_P";

    /// The channel current from drain to source, in the device's polarity,
    /// and its derivatives with respect to vgs and vds
    struct Channel {
        Real id = 0.0, gm = 0.0, gds = 0.0;
    };

    enum class Region { CUTOFF, LINEAR, SATURATION };

    JFET(DeviceId id, const std::string& name, NodeId drain, NodeId gate, NodeId source,
         Real beta = 1e-4,   // BETA [A/V²]
         Real vto = -2.0,    // VTO [V]
         Real lambda = 0.0,  // LAMBDA [1/V]
         Real is = 1e-14,    // IS of each gate junction [A]
         Real n = 1.0,       // N of the gate junctions
         Real cgs = 0.0,     // CGS, zero-bias G-S capacitance [F]
         Real cgd = 0.0,     // CGD, zero-bias G-D capacitance [F]
         Real pb = 1.0,      // PB, gate junction potential [V]
         Real fc = 0.5)      // FC, forward-bias capacitance coefficient
        : Device(id, name, device_type),
          nodes_{drain, gate, source},
          beta_(beta),
          vto_(vto),
          lambda_(lambda),
          is_(is),
          n_(n),
          cgs_(cgs),
          cgd_(cgd),
          pb_(pb),
          fc_(fc),
          is_tnom_(is),
          beta_tnom_(beta),
          vto_tnom_(vto),
          pb_tnom_(pb),
          cgs_tnom_(cgs),
          cgd_tnom_(cgd) {
        if (beta <= 0.0) {
            throw std::runtime_error("JFET transconductance parameter must be positive: " + name);
        }
        if (vto >= 0.0) {
            spdlog::warn("{} {} has positive Vto={:.3f}V (should be negative)", type_name, name, vto);
        }
        if (lambda < 0.0) {
            throw std::runtime_error("JFET channel modulation parameter must be non-negative: " + name);
        }
        if (is <= 0.0) {
            throw std::runtime_error("JFET gate saturation current must be positive: " + name);
        }
        if (n <= 0.0) {
            throw std::runtime_error("JFET emission coefficient must be positive: " + name);
        }
        if (cgs < 0.0 || cgd < 0.0) {
            throw std::runtime_error("JFET capacitance must be non-negative: " + name);
        }
        if (pb <= 0.0) {
            throw std::runtime_error("JFET junction potential PB must be positive: " + name);
        }
        if (fc < 0.0 || fc >= 1.0) {
            throw std::runtime_error("JFET FC must be from 0 to below 1: " + name);
        }
        spdlog::debug("Created {} {} (D={}, G={}, S={}, BETA={:.6e}A/V², Vto={:.3f}V, LAMBDA={:.6e}1/V)",
                      type_name, name, drain, gate, source, beta_, vto_, lambda_);
    }

    ~JFET() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }
    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return cgs_ > 0.0 || cgd_ > 0.0; }

    void stamp_dc_linear(MNASystem&, const DenseVector&) const override {}

    void stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const override {
        stamp_currents(mna, node_voltages, true);
    }

    void stamp_transient(MNASystem& mna, const DenseVector& node_voltages, Real timestep,
                         IntegrationMethod method, const BDFCoefficients* bdf_coef = nullptr,
                         const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override {
        stamp_currents(mna, node_voltages, true);
        if (!is_dynamic()) {
            return;
        }
        // Each depletion capacitance at the voltage of the last accepted
        // point, integrated as a capacitor (device_utils::CapacitorHistory)
        const int d = index(nodes_[0]), g = index(nodes_[1]), s = index(nodes_[2]);
        gs_history_.stamp(mna, g, s, capacitance(cgs_, polarity_sign * gs_history_.last_voltage()), method,
                          timestep, bdf_coef, tr_bdf2_coef);
        gd_history_.stamp(mna, g, d, capacitance(cgd_, polarity_sign * gd_history_.last_voltage()), method,
                          timestep, bdf_coef, tr_bdf2_coef);
    }

    void stamp_ac(ComplexMNASystem& sys, Real omega, const DenseVector& dc_solution) const override {
        const Real vg = voltage(dc_solution, nodes_[1]);
        const Real vgs = polarity_sign * (vg - voltage(dc_solution, nodes_[2]));
        const Real vgd = polarity_sign * (vg - voltage(dc_solution, nodes_[0]));
        const Channel ch = channel(vgs, vgs - vgd);
        const int d = index(nodes_[0]), g = index(nodes_[1]), s = index(nodes_[2]);
        auto add = [&](int r, int c, Complex v) {
            if (r >= 0 && c >= 0) sys.Y.add_entry(r, c, v);
        };
        // Drain-to-source current gm (Vg - Vs) + gds (Vd - Vs)
        add(d, g, Complex(ch.gm, 0.0));
        add(d, s, Complex(-ch.gm - ch.gds, 0.0));
        add(d, d, Complex(ch.gds, 0.0));
        add(s, g, Complex(-ch.gm, 0.0));
        add(s, s, Complex(ch.gm + ch.gds, 0.0));
        add(s, d, Complex(-ch.gds, 0.0));
        // Each gate junction: its conductance with GMIN, and its capacitance
        auto branch = [&](int a, int b, Complex y) {
            add(a, a, y);
            add(b, b, y);
            add(a, b, -y);
            add(b, a, -y);
        };
        branch(g, s, Complex(junction_conductance(vgs) + kGmin, omega * capacitance(cgs_, vgs)));
        branch(g, d, Complex(junction_conductance(vgd) + kGmin, omega * capacitance(cgd_, vgd)));
    }

    bool limited_in_last_stamp() const override { return limited_; }

    void save_state() override {
        saved_gs_ = gs_history_;
        saved_gd_ = gd_history_;
    }

    void restore_state() override {
        gs_history_ = saved_gs_;
        gd_history_ = saved_gd_;
    }

    void reset_state() override {
        gs_history_.reset();
        gd_history_.reset();
        saved_gs_.reset();
        saved_gd_.reset();
        vgs_last_.reset();
        vgd_last_.reset();
        limited_ = false;
    }

    void accept_solution(const DenseVector& node_voltages) override {
        const Real vg = core::accepted_node_voltage(node_voltages, nodes_[1]);
        gs_history_.accept(vg - core::accepted_node_voltage(node_voltages, nodes_[2]));
        gd_history_.accept(vg - core::accepted_node_voltage(node_voltages, nodes_[0]));
    }

    // Accessors
    Real transconductance_parameter() const { return beta_; }
    Real pinchoff_voltage() const { return vto_; }
    Real channel_modulation() const { return lambda_; }
    Real gate_saturation_current() const { return is_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }

    /// VTO at TNOM, the card's value; the device takes it at its temperature
    void set_pinchoff_voltage(Real vto) {
        vto_tnom_ = vto;
        set_temperature(temperature_);
    }

    /// TNOM [K], at which the card's values hold, and EG [eV] of IS(T)
    void set_nominal_temperature(Real tnom_kelvin) { tnom_ = tnom_kelvin; }
    void set_energy_gap(Real eg) { eg_ = eg; }

    /// How VTO and BETA follow the temperature: dVTO/dT [V/K] (the card's
    /// VTOTC, or -TCV), the exponent BEX of BETA (T/TNOM)^BEX, and BETATCE
    /// [%/K] of BETA 1.01^(BETATCE (T - TNOM)), which is taken in place of
    /// BEX when the card gives it, also as 0, as in ngspice-45.2
    void set_temperature_coefficients(Real vto_per_kelvin, Real bex, std::optional<Real> betatce) {
        vto_tc_ = vto_per_kelvin;
        bex_ = bex;
        betatce_ = betatce;
    }

    /// The operating temperature [K]: every value that follows it is taken
    /// anew from the card's value at TNOM, as ngspice-45.2 has it (the class
    /// comment gives the laws)
    void set_temperature(Real temp_kelvin) {
        if (temp_kelvin <= 0.0) {
            throw std::runtime_error("Temperature must be positive: " + name_);
        }
        temperature_ = temp_kelvin;
        const Real vt = thermal_voltage();
        const Real dt = temp_kelvin - tnom_;
        is_ = is_tnom_ * std::exp((temp_kelvin / tnom_ - 1.0) * eg_ / (n_ * vt));
        vto_ = vto_tnom_ + vto_tc_ * dt;
        beta_ = beta_tnom_ *
                (betatce_ ? std::pow(1.01, *betatce_ * dt) : std::pow(temp_kelvin / tnom_, bex_));
        pb_ = core::junction_potential_at(pb_tnom_, temp_kelvin, tnom_);
        cgs_ = core::depletion_capacitance_at(cgs_tnom_, pb_tnom_, pb_, 0.5, temp_kelvin, tnom_);
        cgd_ = core::depletion_capacitance_at(cgd_tnom_, pb_tnom_, pb_, 0.5, temp_kelvin, tnom_);
    }

    /// The region at vgs and vds of the device's polarity (vds >= 0)
    Region get_region(Real vgs, Real vds) const {
        const Real vgst = vgs - vto_;
        if (vgst <= 0.0) return Region::CUTOFF;
        return (vds < vgst) ? Region::LINEAR : Region::SATURATION;
    }

    Real get_drain_current(Real vgs, Real vds) const { return channel(vgs, vds).id; }
    Real get_transconductance(Real vgs, Real vds) const { return channel(vgs, vds).gm; }
    Real get_output_conductance(Real vgs, Real vds) const { return channel(vgs, vds).gds; }
    /// A gate junction's own current and conductance at v, without GMIN
    Real get_gate_current(Real v) const { return junction_current(v); }
    Real get_gate_conductance(Real v) const { return junction_conductance(v); }

    /// The channel at vgs and vds of the device's polarity, in either mode
    Channel channel(Real vgs, Real vds) const {
        if (vds >= 0.0) {
            return forward(vgs - vto_, vds);
        }
        // Inverse mode: the drain is the source, Id = -f(vgd - VTO, -vds)
        const Channel f = forward(vgs - vds - vto_, -vds);
        return {-f.id, -f.gm, f.gm + f.gds};
    }

private:
    static constexpr Real kGmin = 1e-12;

    static int index(NodeId node) { return node == NODE_GND ? -1 : static_cast<int>(node - 1); }
    static Real voltage(const DenseVector& x, NodeId node) {
        return node == NODE_GND ? 0.0 : x[node - 1];
    }

    Real thermal_voltage() const { return core::compute_thermal_voltage(temperature_); }

    /// The channel for vds >= 0: f(vgst, vds) and its derivatives
    Channel forward(Real vgst, Real vds) const {
        if (vgst <= 0.0) {
            return {};
        }
        const Real clm = 1.0 + lambda_ * vds;
        if (vds >= vgst) {
            return {beta_ * vgst * vgst * clm, 2.0 * beta_ * vgst * clm, lambda_ * beta_ * vgst * vgst};
        }
        const Real core = beta_ * vds * (2.0 * vgst - vds);
        return {core * clm, 2.0 * beta_ * vds * clm,
                2.0 * beta_ * (vgst - vds) * clm + core * lambda_};
    }

    Real junction_current(Real v) const {
        const Real nvt = n_ * thermal_voltage();
        return is_ * (std::exp(std::min(v / nvt, core::EXP_OVERFLOW_LIMIT)) - 1.0);
    }

    Real junction_conductance(Real v) const {
        const Real nvt = n_ * thermal_voltage();
        return is_ / nvt * std::exp(std::min(v / nvt, core::EXP_OVERFLOW_LIMIT));
    }

    /// The depletion capacitance of zero-bias value c0 at v
    Real capacitance(Real c0, Real v) const {
        if (c0 <= 0.0) {
            return 0.0;
        }
        if (v < fc_ * pb_) {
            return c0 / std::sqrt(1.0 - v / pb_);
        }
        return c0 / std::pow(1.0 - fc_, 1.5) * (1.0 - 1.5 * fc_ + 0.5 * v / pb_);
    }

    /// The DC currents linearized at the present iterate: the channel and
    /// the two gate junctions with their GMIN. With `limit`, each junction
    /// voltage moves from its last value by SPICE's junction rule
    /// (core::limit_junction_step).
    void stamp_currents(MNASystem& mna, const DenseVector& x, bool limit) const {
        const Real t = polarity_sign;
        const Real vg = voltage(x, nodes_[1]);
        Real vgs = t * (vg - voltage(x, nodes_[2]));
        Real vgd = t * (vg - voltage(x, nodes_[0]));
        limited_ = false;
        if (limit) {
            const Real nvt = n_ * thermal_voltage();
            const Real vcrit = core::compute_vcrit(nvt, is_);
            auto step = [&](Real v, std::optional<Real>& last) {
                const Real limited = last ? core::limit_junction_step(v, *last, nvt, vcrit) : v;
                limited_ = limited_ || limited != v;
                last = limited;
                return limited;
            };
            vgs = step(vgs, vgs_last_);
            vgd = step(vgd, vgd_last_);
        }
        const Real vds = vgs - vgd;
        const Channel ch = channel(vgs, vds);
        const int d = index(nodes_[0]), g = index(nodes_[1]), s = index(nodes_[2]);
        auto add = [&](int r, int c, Real v) {
            if (r >= 0 && c >= 0) mna.G.add_entry(r, c, v);
        };
        auto inject = [&](int r, Real i) {
            if (r >= 0) mna.b[static_cast<std::size_t>(r)] += i;
        };
        // The terminal current from drain to source, t id, and its
        // derivatives with respect to the terminal voltages, gm and gds (t^2
        // is 1), at the terminal voltages t vgs and t vds
        const Real ieq = t * ch.id - ch.gm * (t * vgs) - ch.gds * (t * vds);
        add(d, g, ch.gm);
        add(d, s, -ch.gm - ch.gds);
        add(d, d, ch.gds);
        add(s, g, -ch.gm);
        add(s, s, ch.gm + ch.gds);
        add(s, d, -ch.gds);
        inject(d, -ieq);
        inject(s, ieq);
        // Each gate junction from the gate to a terminal, with GMIN
        auto junction = [&](int other, Real v) {
            const Real gj = junction_conductance(v) + kGmin;
            const Real ij = t * (junction_current(v) + kGmin * v);
            const Real ieq_j = ij - gj * (t * v);
            add(g, g, gj);
            add(other, other, gj);
            add(g, other, -gj);
            add(other, g, -gj);
            inject(g, -ieq_j);
            inject(other, ieq_j);
        };
        junction(s, vgs);
        junction(d, vgd);
    }

    std::vector<NodeId> nodes_;
    Real beta_, vto_, lambda_, is_, n_, cgs_, cgd_, pb_, fc_;              // at the temperature
    Real is_tnom_, beta_tnom_, vto_tnom_, pb_tnom_, cgs_tnom_, cgd_tnom_;  // the card's, at TNOM
    Real vto_tc_ = 0.0;                                                    // dVTO/dT [V/K]
    Real bex_ = 0.0;                                                       // BEX
    std::optional<Real> betatce_;  // BETATCE [%/K], when the card gives it
    Real tnom_ = 300.15;         // TNOM [K]
    Real eg_ = 1.11;             // EG [eV]
    Real temperature_ = 300.15;  // operating temperature [K]

    mutable device_utils::CapacitorHistory gs_history_, gd_history_;
    device_utils::CapacitorHistory saved_gs_, saved_gd_;
    mutable std::optional<Real> vgs_last_, vgd_last_;  // the junction voltages last evaluated
    mutable bool limited_ = false;
};

// Type aliases for backward compatibility
using JFET_N = JFET<JFETPolarity::N>;
using JFET_P = JFET<JFETPolarity::P>;

} // namespace kirimaspice::devices
