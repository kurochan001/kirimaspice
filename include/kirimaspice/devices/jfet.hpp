// include/kirimaspice/devices/jfet.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include "kirimaspice/core/constants.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

/// Polarity for JFET devices
enum class JFETPolarity { N, P };

/// JFET: Nonlinear 3-terminal Junction Field Effect Transistor
/// Shichman-Hodges Level 1 model with unified N/P implementation
///
/// Operating regions:
/// 1. Cutoff: Vov < 0 (Vov = Vgs - Vto for N, Vsg - |Vto| for P)
/// 2. Linear: Vov > 0, Vds/Vsd < Vov
/// 3. Saturation: Vov > 0, Vds/Vsd >= Vov
///
/// Where:
///   BETA   = transconductance parameter [A/V²]
///   Vto    = pinch-off voltage [V] (negative for both N and P)
///   LAMBDA = channel length modulation coefficient [1/V]
///
/// Gate junction current (PN diode):
///   Ig = IS × (exp(Vgs/(n×Vt)) - 1)
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

    JFET(DeviceId id, const std::string& name,
         NodeId drain, NodeId gate, NodeId source,
         Real beta = 1e-4,     // Transconductance parameter [A/V²]
         Real vto = -2.0,      // Pinch-off voltage [V] (negative)
         Real lambda = 0.01,   // Channel length modulation [1/V]
         Real is = 1e-14,      // Gate junction saturation current [A]
         Real n = 1.0,         // Gate junction emission coefficient
         Real cgs = 0.0,       // G-S capacitance [F]
         Real cgd = 0.0)       // G-D capacitance [F]
        : Device(id, name, device_type)
        , nodes_{drain, gate, source}
        , beta_(beta)
        , vto_(vto)
        , lambda_(lambda)
        , is_(is)
        , n_(n)
        , cgs_(cgs)
        , cgd_(cgd)
        // Temperature model parameters
        , is_tnom_(is)
        , tnom_(T_nominal)
        , xti_(3.0)
        , eg_(1.11)
        // State variables
        , vgs_prev_(0.0), vgs_prev2_(0.0), vgs_prev3_(0.0)
        , vgs_saved_(0.0), vgs_saved2_(0.0), vgs_saved3_(0.0)
        , charge_gs_(0.0), charge_gs_prev_(0.0)
        , vgd_prev_(0.0), vgd_prev2_(0.0), vgd_prev3_(0.0)
        , vgd_saved_(0.0), vgd_saved2_(0.0), vgd_saved3_(0.0)
        , charge_gd_(0.0), charge_gd_prev_(0.0)
        , timestep_(0.0)
        , last_method_(IntegrationMethod::BACKWARD_EULER)
        , timestep_saved_(0.0)
        , method_saved_(IntegrationMethod::BACKWARD_EULER)
        , first_accept_(true)
        , bdf_alpha0_(0.0), bdf_alpha1_(0.0), bdf_alpha2_(0.0), bdf_alpha3_(0.0)
        , bdf_beta0_(0.0)
        , use_bdf_var_(false) {

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

        spdlog::debug("Created {} {} (D={}, G={}, S={}, BETA={:.6e}A/V², Vto={:.3f}V, LAMBDA={:.6e}1/V)",
                      type_name, name, drain, gate, source, beta_, vto_, lambda_);
    }

    ~JFET() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        stamp_gmin(mna);
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        NodeId nd = nodes_[0];
        NodeId ng = nodes_[1];
        NodeId ns = nodes_[2];

        Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
        Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
        Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

        // Internal voltage calculations
        Real vov, vds_eff;
        compute_effective_voltages(vd, vg, vs, vov, vds_eff);

        // Calculate drain current and conductances
        Real id = compute_drain_current(vov, vds_eff);
        Real gm = compute_transconductance(vov, vds_eff);
        Real gds = compute_output_conductance(vov, vds_eff);

        // Gate junction current
        Real vgs_for_gate = (Pol == JFETPolarity::N) ? (vg - vs) : -(vs - vg);
        Real ig = compute_gate_current(vgs_for_gate);
        Real gg = compute_gate_conductance(vgs_for_gate);

        // Compute Norton equivalent currents
        Real ieq_d, ieq_g;
        if constexpr (Pol == JFETPolarity::N) {
            Real vgs = vg - vs;
            Real vds = vd - vs;
            ieq_d = id - gm * vgs - gds * vds;
            ieq_g = ig - gg * vgs;
        } else {
            Real vsg = vs - vg;
            Real vsd = vs - vd;
            ieq_d = id - gm * vsg - gds * vsd;
            ieq_g = ig - gg * vsg;
        }

        auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
        auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
        auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

        // Stamp drain current
        if constexpr (Pol == JFETPolarity::N) {
            // N-channel: Id = gm×(Vg-Vs) + gds×(Vd-Vs) + Ieq_d
            if (nd_idx >= 0) {
                mna.G.add_entry(nd_idx, nd_idx, gds);
                if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, gm);
                if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -gm - gds);
                mna.b[nd_idx] -= ieq_d;
            }
            if (ns_idx >= 0) {
                mna.G.add_entry(ns_idx, ns_idx, gm + gds);
                if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -gm);
                if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, -gds);
                mna.b[ns_idx] += ieq_d;
            }
        } else {
            // P-channel: Id = gm×(Vs-Vg) + gds×(Vs-Vd) + Ieq_d
            if (nd_idx >= 0) {
                mna.G.add_entry(nd_idx, nd_idx, -gds);
                if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, -gm);
                if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, gm + gds);
                mna.b[nd_idx] -= ieq_d;
            }
            if (ns_idx >= 0) {
                mna.G.add_entry(ns_idx, ns_idx, -gm - gds);
                if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, gm);
                if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, gds);
                mna.b[ns_idx] += ieq_d;
            }
        }

        // Stamp gate junction current
        if constexpr (Pol == JFETPolarity::N) {
            if (ng_idx >= 0) {
                mna.G.add_entry(ng_idx, ng_idx, gg);
                if (ns_idx >= 0) mna.G.add_entry(ng_idx, ns_idx, -gg);
                mna.b[ng_idx] -= ieq_g;
            }
            if (ns_idx >= 0) {
                mna.G.add_entry(ns_idx, ns_idx, gg);
                if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -gg);
                mna.b[ns_idx] += ieq_g;
            }
        } else {
            if (ng_idx >= 0) {
                mna.G.add_entry(ng_idx, ng_idx, -gg);
                if (ns_idx >= 0) mna.G.add_entry(ng_idx, ns_idx, gg);
                mna.b[ng_idx] -= ieq_g;
            }
            if (ns_idx >= 0) {
                mna.G.add_entry(ns_idx, ns_idx, -gg);
                if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, gg);
                mna.b[ns_idx] += ieq_g;
            }
        }
    }

    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override {
        timestep_ = timestep;
        last_method_ = method;

        stamp_dc_nonlinear(mna, node_voltages);

        if (!is_dynamic()) return;

        NodeId nd = nodes_[0];
        NodeId ng = nodes_[1];
        NodeId ns = nodes_[2];

        Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
        Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
        Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

        Real vgs = vg - vs;
        Real vgd = vg - vd;

        charge_gs_ = cgs_ * vgs;
        charge_gd_ = cgd_ * vgd;

        Real i_cgs = 0.0, i_cgd = 0.0;
        Real geq_cgs = 0.0, geq_cgd = 0.0;

        switch (method) {
            case IntegrationMethod::BACKWARD_EULER: {
                geq_cgs = cgs_ / timestep;
                geq_cgd = cgd_ / timestep;
                i_cgs = geq_cgs * (vgs - vgs_prev_);
                i_cgd = geq_cgd * (vgd - vgd_prev_);
                break;
            }
            case IntegrationMethod::TRAPEZOIDAL:
            case IntegrationMethod::GEAR2: {
                geq_cgs = 2.0 * cgs_ / timestep;
                geq_cgd = 2.0 * cgd_ / timestep;
                Real i_cgs_prev = cgs_ * (vgs_prev_ - vgs_prev2_) / timestep;
                Real i_cgd_prev = cgd_ * (vgd_prev_ - vgd_prev2_) / timestep;
                i_cgs = geq_cgs * (vgs - vgs_prev_) - i_cgs_prev;
                i_cgd = geq_cgd * (vgd - vgd_prev_) - i_cgd_prev;
                break;
            }
            case IntegrationMethod::GEAR3:
            case IntegrationMethod::TR_BDF2: {
                geq_cgs = cgs_ / timestep;
                geq_cgd = cgd_ / timestep;
                i_cgs = geq_cgs * (vgs - vgs_prev_);
                i_cgd = geq_cgd * (vgd - vgd_prev_);
                break;
            }
        }

        auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
        auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
        auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

        // G-S capacitance
        if (ng_idx >= 0) {
            mna.G.add_entry(ng_idx, ng_idx, geq_cgs);
            if (ns_idx >= 0) mna.G.add_entry(ng_idx, ns_idx, -geq_cgs);
            mna.b[ng_idx] -= i_cgs;
        }
        if (ns_idx >= 0) {
            mna.G.add_entry(ns_idx, ns_idx, geq_cgs);
            if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -geq_cgs);
            mna.b[ns_idx] += i_cgs;
        }

        // G-D capacitance
        if (ng_idx >= 0) {
            mna.G.add_entry(ng_idx, ng_idx, geq_cgd);
            if (nd_idx >= 0) mna.G.add_entry(ng_idx, nd_idx, -geq_cgd);
            mna.b[ng_idx] -= i_cgd;
        }
        if (nd_idx >= 0) {
            mna.G.add_entry(nd_idx, nd_idx, geq_cgd);
            if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, -geq_cgd);
            mna.b[nd_idx] += i_cgd;
        }
    }

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return (cgs_ > 0.0 || cgd_ > 0.0); }

    void stamp_ac(ComplexMNASystem& sys, Real omega,
                  const DenseVector& dc_solution) const override {
        NodeId nd = nodes_[0];
        NodeId ng = nodes_[1];
        NodeId ns = nodes_[2];

        Real vd = (nd == NODE_GND) ? 0.0 : dc_solution[nd - 1];
        Real vg = (ng == NODE_GND) ? 0.0 : dc_solution[ng - 1];
        Real vs = (ns == NODE_GND) ? 0.0 : dc_solution[ns - 1];

        Real vov, vds_eff;
        compute_effective_voltages(vd, vg, vs, vov, vds_eff);

        Real gm = compute_transconductance(vov, vds_eff);
        Real gds = compute_output_conductance(vov, vds_eff);
        Real vgs_for_gate = (Pol == JFETPolarity::N) ? (vg - vs) : -(vs - vg);
        Real gg = compute_gate_conductance(vgs_for_gate);

        Complex j_omega_cgs = Complex(0.0, omega * cgs_);
        Complex j_omega_cgd = Complex(0.0, omega * cgd_);

        auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
        auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
        auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

        // Stamp transconductance (gm)
        if constexpr (Pol == JFETPolarity::N) {
            if (nd_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(nd_idx, ng_idx, Complex(gm, 0.0));
            if (nd_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, Complex(-gm, 0.0));
            if (ns_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, Complex(-gm, 0.0));
            if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, Complex(gm, 0.0));
        } else {
            if (nd_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(nd_idx, ng_idx, Complex(-gm, 0.0));
            if (nd_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, Complex(gm, 0.0));
            if (ns_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, Complex(gm, 0.0));
            if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, Complex(-gm, 0.0));
        }

        // Stamp output conductance (gds)
        if constexpr (Pol == JFETPolarity::N) {
            if (nd_idx >= 0) sys.Y.add_entry(nd_idx, nd_idx, Complex(gds, 0.0));
            if (nd_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, Complex(-gds, 0.0));
            if (ns_idx >= 0 && nd_idx >= 0) sys.Y.add_entry(ns_idx, nd_idx, Complex(-gds, 0.0));
            if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, Complex(gds, 0.0));
        } else {
            if (nd_idx >= 0) sys.Y.add_entry(nd_idx, nd_idx, Complex(-gds, 0.0));
            if (nd_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, Complex(gds, 0.0));
            if (ns_idx >= 0 && nd_idx >= 0) sys.Y.add_entry(ns_idx, nd_idx, Complex(gds, 0.0));
            if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, Complex(-gds, 0.0));
        }

        // Stamp gate conductance (gg)
        if constexpr (Pol == JFETPolarity::N) {
            if (ng_idx >= 0) sys.Y.add_entry(ng_idx, ng_idx, Complex(gg, 0.0));
            if (ng_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(ng_idx, ns_idx, Complex(-gg, 0.0));
            if (ns_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, Complex(-gg, 0.0));
            if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, Complex(gg, 0.0));
        } else {
            if (ng_idx >= 0) sys.Y.add_entry(ng_idx, ng_idx, Complex(-gg, 0.0));
            if (ng_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(ng_idx, ns_idx, Complex(gg, 0.0));
            if (ns_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, Complex(gg, 0.0));
            if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, Complex(-gg, 0.0));
        }

        // G-S capacitance (same for both polarities)
        if (ng_idx >= 0) sys.Y.add_entry(ng_idx, ng_idx, j_omega_cgs);
        if (ng_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(ng_idx, ns_idx, -j_omega_cgs);
        if (ns_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, -j_omega_cgs);
        if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, j_omega_cgs);

        // G-D capacitance (same for both polarities)
        if (ng_idx >= 0) sys.Y.add_entry(ng_idx, ng_idx, j_omega_cgd);
        if (ng_idx >= 0 && nd_idx >= 0) sys.Y.add_entry(ng_idx, nd_idx, -j_omega_cgd);
        if (nd_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(nd_idx, ng_idx, -j_omega_cgd);
        if (nd_idx >= 0) sys.Y.add_entry(nd_idx, nd_idx, j_omega_cgd);
    }

    // Accessors
    Real transconductance_parameter() const { return beta_; }
    Real pinchoff_voltage() const { return vto_; }
    Real channel_modulation() const { return lambda_; }
    Real gate_saturation_current() const { return is_; }
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }

    void set_pinchoff_voltage(Real vto) {
        vto_ = vto;
        spdlog::debug("{} set pinch-off voltage to {:.3f}V", name_, vto_);
    }

    void set_temperature(Real temp_kelvin) {
        if (temp_kelvin <= 0.0) {
            throw std::runtime_error("Temperature must be positive: " + name_);
        }
        Real vt_nom = k_boltzmann * tnom_ / q_electron;
        Real vt_op = k_boltzmann * temp_kelvin / q_electron;
        Real temp_ratio = temp_kelvin / tnom_;
        Real vt_factor = 1.0 / vt_op - 1.0 / vt_nom;
        is_ = is_tnom_ * std::pow(temp_ratio, xti_ / n_) * std::exp(-eg_ / n_ * vt_factor);
        spdlog::debug("{} set temperature to {:.2f}K (IS={:.6e}A)", name_, temp_kelvin, is_);
    }

    enum class Region { CUTOFF, LINEAR, SATURATION };

    Region get_region(Real v1, Real v2) const {
        // v1 = vgs (N) or vsg (P), v2 = vds (N) or vsd (P)
        Real vov = (Pol == JFETPolarity::N) ? (v1 - vto_) : (v1 - std::abs(vto_));
        if (vov < 0.0) return Region::CUTOFF;
        if (v2 < vov) return Region::LINEAR;
        return Region::SATURATION;
    }

    Real get_drain_current(Real v1, Real v2) const {
        Real vov, vds_eff;
        if constexpr (Pol == JFETPolarity::N) {
            vov = v1 - vto_;
            vds_eff = v2;
        } else {
            vov = v1 - std::abs(vto_);
            vds_eff = v2;
        }
        return compute_drain_current(vov, vds_eff);
    }

    Real get_transconductance(Real v1, Real v2) const {
        Real vov, vds_eff;
        if constexpr (Pol == JFETPolarity::N) {
            vov = v1 - vto_;
            vds_eff = v2;
        } else {
            vov = v1 - std::abs(vto_);
            vds_eff = v2;
        }
        return compute_transconductance(vov, vds_eff);
    }

    Real get_output_conductance(Real v1, Real v2) const {
        Real vov, vds_eff;
        if constexpr (Pol == JFETPolarity::N) {
            vov = v1 - vto_;
            vds_eff = v2;
        } else {
            vov = v1 - std::abs(vto_);
            vds_eff = v2;
        }
        return compute_output_conductance(vov, vds_eff);
    }

    Real get_gate_current(Real vgs) const {
        return compute_gate_current(vgs);
    }

    Real get_gate_conductance(Real vgs) const {
        return compute_gate_conductance(vgs);
    }

    void save_state() override {
        vgs_saved_ = vgs_prev_;
        vgs_saved2_ = vgs_prev2_;
        vgs_saved3_ = vgs_prev3_;
        vgd_saved_ = vgd_prev_;
        vgd_saved2_ = vgd_prev2_;
        vgd_saved3_ = vgd_prev3_;
        timestep_saved_ = timestep_;
        method_saved_ = last_method_;
    }

    void restore_state() override {
        vgs_prev_ = vgs_saved_;
        vgs_prev2_ = vgs_saved2_;
        vgs_prev3_ = vgs_saved3_;
        vgd_prev_ = vgd_saved_;
        vgd_prev2_ = vgd_saved2_;
        vgd_prev3_ = vgd_saved3_;
        timestep_ = timestep_saved_;
        last_method_ = method_saved_;
    }

    void accept_solution(const DenseVector& node_voltages) override {
        NodeId nd = nodes_[0];
        NodeId ng = nodes_[1];
        NodeId ns = nodes_[2];

        Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
        Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
        Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

        Real vgs = vg - vs;
        Real vgd = vg - vd;

        vgs_prev3_ = vgs_prev2_;
        vgs_prev2_ = vgs_prev_;
        vgs_prev_ = vgs;

        vgd_prev3_ = vgd_prev2_;
        vgd_prev2_ = vgd_prev_;
        vgd_prev_ = vgd;

        charge_gs_prev_ = charge_gs_;
        charge_gd_prev_ = charge_gd_;

        first_accept_ = false;
    }

    Real get_charge() const override { return charge_gs_ + charge_gd_; }

    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override {
        if (!is_dynamic() || first_accept_) return 0.0;
        Real q_error_gs = std::abs(charge_gs_ - charge_gs_prev_);
        Real q_error_gd = std::abs(charge_gd_ - charge_gd_prev_);
        return (q_error_gs + q_error_gd) / std::max(chgtol, 1e-20);
    }

private:
    void stamp_gmin(MNASystem& mna) const {
        NodeId nd = nodes_[0];
        NodeId ng = nodes_[1];
        NodeId ns = nodes_[2];
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

        // G-D conductance
        if (ng_idx >= 0) {
            mna.G.add_entry(ng_idx, ng_idx, g);
            if (nd_idx >= 0) mna.G.add_entry(ng_idx, nd_idx, -g);
        }
        if (nd_idx >= 0) {
            mna.G.add_entry(nd_idx, nd_idx, g);
            if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, -g);
        }
    }

    void compute_effective_voltages(Real vd, Real vg, Real vs, Real& vov, Real& vds_eff) const {
        if constexpr (Pol == JFETPolarity::N) {
            vov = (vg - vs) - vto_;
            vds_eff = vd - vs;
        } else {
            vov = (vs - vg) - std::abs(vto_);
            vds_eff = vs - vd;
        }
    }

    Real compute_drain_current(Real vov, Real vds_eff) const {
        if (vov < 0.0) return 0.0;
        if (vds_eff < 0.0) vds_eff = 0.0;
        Real lambda_factor = 1.0 + lambda_ * vds_eff;
        if (vds_eff < vov) {
            return beta_ * (2.0 * vov * vds_eff - vds_eff * vds_eff) * lambda_factor;
        }
        return beta_ * vov * vov * lambda_factor;
    }

    Real compute_transconductance(Real vov, Real vds_eff) const {
        if (vov < 0.0) return gmin_default;
        if (vds_eff < 0.0) vds_eff = 0.0;
        Real lambda_factor = 1.0 + lambda_ * vds_eff;
        if (vds_eff < vov) {
            return std::max(2.0 * beta_ * vds_eff * lambda_factor, gmin_default);
        }
        return std::max(2.0 * beta_ * vov * lambda_factor, gmin_default);
    }

    Real compute_output_conductance(Real vov, Real vds_eff) const {
        if (vov < 0.0) return gmin_default;
        if (vds_eff < 0.0) vds_eff = 0.0;
        if (vds_eff < vov) {
            Real id_basic = beta_ * (2.0 * vov * vds_eff - vds_eff * vds_eff);
            Real did_dvds = beta_ * (2.0 * vov - 2.0 * vds_eff);
            return std::max(did_dvds * (1.0 + lambda_ * vds_eff) + id_basic * lambda_, gmin_default);
        }
        Real id_basic = beta_ * vov * vov;
        return std::max(id_basic * lambda_, gmin_default);
    }

    Real compute_gate_current(Real vgs) const {
        Real vt = k_boltzmann * T_nominal / q_electron;
        Real vd_norm = vgs / (n_ * vt);
        if (vd_norm > core::EXP_OVERFLOW_LIMIT) vd_norm = core::EXP_OVERFLOW_LIMIT;
        Real ig = is_ * (std::exp(vd_norm) - 1.0);
        if constexpr (Pol == JFETPolarity::P) {
            return -ig;
        }
        return ig;
    }

    Real compute_gate_conductance(Real vgs) const {
        Real vt = k_boltzmann * T_nominal / q_electron;
        Real vd_norm = vgs / (n_ * vt);
        if (vd_norm > core::EXP_OVERFLOW_LIMIT) vd_norm = core::EXP_OVERFLOW_LIMIT;
        return std::max((is_ / (n_ * vt)) * std::exp(vd_norm), gmin_default);
    }

    std::vector<NodeId> nodes_;
    Real beta_, vto_, lambda_, is_, n_, cgs_, cgd_;
    Real is_tnom_, tnom_, xti_, eg_;

    static constexpr Real k_boltzmann = 1.380649e-23;
    static constexpr Real q_electron = 1.602176634e-19;
    static constexpr Real T_nominal = 300.15;
    static constexpr Real gmin_default = 1e-12;

    // State variables for transient analysis
    mutable Real vgs_prev_, vgs_prev2_, vgs_prev3_;
    mutable Real vgs_saved_, vgs_saved2_, vgs_saved3_;
    mutable Real charge_gs_, charge_gs_prev_;
    mutable Real vgd_prev_, vgd_prev2_, vgd_prev3_;
    mutable Real vgd_saved_, vgd_saved2_, vgd_saved3_;
    mutable Real charge_gd_, charge_gd_prev_;
    mutable Real timestep_;
    mutable IntegrationMethod last_method_;
    mutable Real timestep_saved_;
    mutable IntegrationMethod method_saved_;
    mutable bool first_accept_;
    mutable Real bdf_alpha0_, bdf_alpha1_, bdf_alpha2_, bdf_alpha3_, bdf_beta0_;
    mutable bool use_bdf_var_;
};

// Type aliases for backward compatibility
using JFET_N = JFET<JFETPolarity::N>;
using JFET_P = JFET<JFETPolarity::P>;

} // namespace kirimaspice::devices
