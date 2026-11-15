// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mesfet.hpp
// Unified MESFET N/P-channel template
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace kirimaspice::devices {

/// MESFET polarity for template parameter
enum class MESFETPolarity { N, P };

/// MESFET model type
enum class MESFETModel {
    CURTICE = 1,  ///< Level 1: Curtice model (basic)
    STATZ = 2     ///< Level 2: Statz model (higher accuracy)
};

/// Unified MESFET template (GaAs Metal-Semiconductor FET)
/// Used in GaAs high-frequency amplifiers and microwave circuits
///
/// Two models supported:
/// 1. Curtice model (Level 1): Id = beta(Vgs-Vto)^2(1+lambda*Vds)tanh(alpha*Vds)
/// 2. Statz model (Level 2): Id = beta(Vgs-Vto)^3(1+lambda*Vds)(1-(1-Vds/Vp)^3)/3
///
/// @tparam Pol MESFETPolarity::N for N-channel, MESFETPolarity::P for P-channel
template<MESFETPolarity Pol>
class MESFET : public Device {
public:
    static constexpr Real polarity_sign = (Pol == MESFETPolarity::N) ? 1.0 : -1.0;
    static constexpr DeviceType device_type =
        (Pol == MESFETPolarity::N) ? DeviceType::MESFET_N : DeviceType::MESFET_P;
    static constexpr Real default_vto = (Pol == MESFETPolarity::N) ? -2.0 : 2.0;

    MESFET(DeviceId id, const std::string& name,
           NodeId drain, NodeId gate, NodeId source,
           MESFETModel model = MESFETModel::CURTICE,
           Real beta = 0.1,
           Real vto = default_vto,
           Real lambda = 0.05,
           Real alpha = 2.0,
           Real b = 0.3,
           Real vp = 3.0,
           Real cgs = 1e-12,
           Real cgd = 0.5e-12,
           Real cds = 0.1e-12,
           Real kf = 0.0,
           Real af = 1.0,
           Real ef = 1.0,
           Real tnom = 300.15)
        : Device(id, name, device_type),
          nodes_{drain, gate, source},
          model_(model),
          beta_(beta), vto_(vto), lambda_(lambda), alpha_(alpha), b_(b), vp_(vp),
          cgs_(cgs), cgd_(cgd), cds_(cds),
          kf_(kf), af_(af), ef_(ef),
          tnom_(tnom) {

        // Parameter validation
        if (beta_ <= 0.0) {
            spdlog::warn("MESFET_{} {}: BETA must be positive, using default 0.1 A/V^2",
                         polarity_char(), name);
            beta_ = 0.1;
        }
        if (lambda_ < 0.0) {
            spdlog::warn("MESFET_{} {}: LAMBDA must be non-negative, using default 0.05",
                         polarity_char(), name);
            lambda_ = 0.05;
        }
        if (alpha_ <= 0.0 && model_ == MESFETModel::CURTICE) {
            spdlog::warn("MESFET_{} {}: ALPHA must be positive for Curtice model, using default 2.0",
                         polarity_char(), name);
            alpha_ = 2.0;
        }
        if (vp_ <= 0.0 && model_ == MESFETModel::STATZ) {
            spdlog::warn("MESFET_{} {}: VP must be positive for Statz model, using default 3.0 V",
                         polarity_char(), name);
            vp_ = 3.0;
        }

        std::string model_str = (model_ == MESFETModel::CURTICE) ? "Curtice" : "Statz";
        spdlog::trace("MESFET_{} {} created: D={}, G={}, S={}, model={}, BETA={}A/V^2, VTO={}V, LAMBDA={}",
                      polarity_char(), name, drain, gate, source, model_str, beta_, vto_, lambda_);
    }

    ~MESFET() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis
    void stamp_dc_linear(MNASystem& mna, const DenseVector& node_voltages) const override {
        (void)mna; (void)node_voltages;
    }

    void stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const override {
        const NodeId d = nodes_[0];
        const NodeId g = nodes_[1];
        const NodeId s = nodes_[2];

        const Real vd = (d > 0) ? node_voltages[d - 1] : 0.0;
        const Real vg = (g > 0) ? node_voltages[g - 1] : 0.0;
        const Real vs = (s > 0) ? node_voltages[s - 1] : 0.0;

        Real v_ctrl, v_ds;
        compute_effective_voltages(vd, vg, vs, v_ctrl, v_ds);

        const Real id = compute_drain_current(v_ctrl, v_ds);
        const Real gm = compute_transconductance(v_ctrl, v_ds);
        const Real gds = compute_output_conductance(v_ctrl, v_ds);

        const Real i_norton = id - gm * v_ctrl - gds * v_ds;

        if constexpr (Pol == MESFETPolarity::N) {
            // N-channel: D-S conductance, gm from G-S, current D→S
            if (d > 0 && s > 0) {
                mna.G.add_entry(d - 1, d - 1, gds);
                mna.G.add_entry(d - 1, s - 1, -gds);
                mna.G.add_entry(s - 1, d - 1, -gds);
                mna.G.add_entry(s - 1, s - 1, gds);
            } else if (d > 0) {
                mna.G.add_entry(d - 1, d - 1, gds);
            } else if (s > 0) {
                mna.G.add_entry(s - 1, s - 1, gds);
            }

            if (d > 0 && g > 0 && s > 0) {
                mna.G.add_entry(d - 1, g - 1, gm);
                mna.G.add_entry(d - 1, s - 1, -gm);
                mna.G.add_entry(s - 1, g - 1, -gm);
                mna.G.add_entry(s - 1, s - 1, gm);
            }

            if (d > 0) mna.b[d - 1] -= i_norton;
            if (s > 0) mna.b[s - 1] += i_norton;
        } else {
            // P-channel: S-D conductance, gm from G-S (inverted), current S→D
            if (s > 0 && d > 0) {
                mna.G.add_entry(s - 1, s - 1, gds);
                mna.G.add_entry(s - 1, d - 1, -gds);
                mna.G.add_entry(d - 1, s - 1, -gds);
                mna.G.add_entry(d - 1, d - 1, gds);
            } else if (s > 0) {
                mna.G.add_entry(s - 1, s - 1, gds);
            } else if (d > 0) {
                mna.G.add_entry(d - 1, d - 1, gds);
            }

            if (s > 0 && g > 0 && d > 0) {
                mna.G.add_entry(s - 1, g - 1, -gm);
                mna.G.add_entry(s - 1, s - 1, gm);
                mna.G.add_entry(d - 1, g - 1, gm);
                mna.G.add_entry(d - 1, s - 1, -gm);
            }

            if (s > 0) mna.b[s - 1] -= -i_norton;
            if (d > 0) mna.b[d - 1] -= i_norton;
        }

        spdlog::trace("MESFET_{} {}: DC stamp: V_ctrl={:.3f}V, V_ds={:.3f}V, Id={:.3f}mA, gm={:.6f}S, gds={:.6f}S",
                      polarity_char(), this->name(), v_ctrl, v_ds, id * 1000, gm, gds);
    }

    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override {
        stamp_dc_nonlinear(mna, node_voltages);

        const NodeId d = nodes_[0];
        const NodeId g = nodes_[1];
        const NodeId s = nodes_[2];

        // CGS, CGD and CDS by backward Euler, whatever the method: the current
        // C / dt (V - V_prev) from the first node to the second, with V_prev
        // accepted at the last time point (accept_solution). For the
        // P-channel device the voltages are S-G, D-G and S-D.
        (void)tr_bdf2_coef;
        const int di = (d > 0) ? static_cast<int>(d - 1) : -1;
        const int gi = (g > 0) ? static_cast<int>(g - 1) : -1;
        const int si = (s > 0) ? static_cast<int>(s - 1) : -1;
        auto companion = [&](Real c, int n1, int n2, Real v_prev) {
            if (c <= 0.0) {
                return;
            }
            const Real geq = c / timestep;
            device_utils::stamp_capacitor_companion(mna, n1, n2, geq, -geq * v_prev, 1.0);
        };
        if constexpr (Pol == MESFETPolarity::N) {
            companion(cgs_, gi, si, v_gs_prev_);
            companion(cgd_, gi, di, v_gd_prev_);
            companion(cds_, di, si, v_ds_prev_);
        } else {
            companion(cgs_, si, gi, v_gs_prev_);
            companion(cgd_, di, gi, v_gd_prev_);
            companion(cds_, si, di, v_ds_prev_);
        }

        (void)method;
        (void)bdf_coef;
    }

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return true; }

    void stamp_ac(ComplexMNASystem& sys, Real omega,
                  const DenseVector& dc_solution) const override {
        const NodeId d = nodes_[0];
        const NodeId g = nodes_[1];
        const NodeId s = nodes_[2];

        const Real vd = (d > 0) ? dc_solution[d - 1] : 0.0;
        const Real vg = (g > 0) ? dc_solution[g - 1] : 0.0;
        const Real vs = (s > 0) ? dc_solution[s - 1] : 0.0;

        Real v_ctrl, v_ds;
        compute_effective_voltages(vd, vg, vs, v_ctrl, v_ds);

        const Real gm = compute_transconductance(v_ctrl, v_ds);
        const Real gds = compute_output_conductance(v_ctrl, v_ds);

        const Complex y_gs(0.0, omega * cgs_);
        const Complex y_gd(0.0, omega * cgd_);
        const Complex y_ds(0.0, omega * cds_);

        if constexpr (Pol == MESFETPolarity::N) {
            // Gate-source capacitance
            if (g > 0 && s > 0) {
                sys.Y.add_entry(g - 1, g - 1, y_gs);
                sys.Y.add_entry(g - 1, s - 1, -y_gs);
                sys.Y.add_entry(s - 1, g - 1, -y_gs);
                sys.Y.add_entry(s - 1, s - 1, y_gs);
            }
            // Gate-drain capacitance
            if (g > 0 && d > 0) {
                sys.Y.add_entry(g - 1, g - 1, y_gd);
                sys.Y.add_entry(g - 1, d - 1, -y_gd);
                sys.Y.add_entry(d - 1, g - 1, -y_gd);
                sys.Y.add_entry(d - 1, d - 1, y_gd);
            }
            // Drain-source capacitance
            if (d > 0 && s > 0) {
                sys.Y.add_entry(d - 1, d - 1, y_ds);
                sys.Y.add_entry(d - 1, s - 1, -y_ds);
                sys.Y.add_entry(s - 1, d - 1, -y_ds);
                sys.Y.add_entry(s - 1, s - 1, y_ds);
            }
            // Transconductance
            const Complex gm_c(gm, 0.0);
            if (d > 0 && g > 0 && s > 0) {
                sys.Y.add_entry(d - 1, g - 1, gm_c);
                sys.Y.add_entry(d - 1, s - 1, -gm_c);
                sys.Y.add_entry(s - 1, g - 1, -gm_c);
                sys.Y.add_entry(s - 1, s - 1, gm_c);
            }
            // Output conductance
            const Complex gds_c(gds, 0.0);
            if (d > 0 && s > 0) {
                sys.Y.add_entry(d - 1, d - 1, gds_c);
                sys.Y.add_entry(d - 1, s - 1, -gds_c);
                sys.Y.add_entry(s - 1, d - 1, -gds_c);
                sys.Y.add_entry(s - 1, s - 1, gds_c);
            }
        } else {
            // P-channel: S-G capacitance
            if (s > 0 && g > 0) {
                sys.Y.add_entry(s - 1, s - 1, y_gs);
                sys.Y.add_entry(s - 1, g - 1, -y_gs);
                sys.Y.add_entry(g - 1, s - 1, -y_gs);
                sys.Y.add_entry(g - 1, g - 1, y_gs);
            }
            // D-G capacitance
            if (d > 0 && g > 0) {
                sys.Y.add_entry(d - 1, d - 1, y_gd);
                sys.Y.add_entry(d - 1, g - 1, -y_gd);
                sys.Y.add_entry(g - 1, d - 1, -y_gd);
                sys.Y.add_entry(g - 1, g - 1, y_gd);
            }
            // S-D capacitance
            if (s > 0 && d > 0) {
                sys.Y.add_entry(s - 1, s - 1, y_ds);
                sys.Y.add_entry(s - 1, d - 1, -y_ds);
                sys.Y.add_entry(d - 1, s - 1, -y_ds);
                sys.Y.add_entry(d - 1, d - 1, y_ds);
            }
            // Transconductance (P-channel)
            const Complex gm_c(gm, 0.0);
            if (s > 0 && g > 0 && d > 0) {
                sys.Y.add_entry(s - 1, g - 1, -gm_c);
                sys.Y.add_entry(s - 1, s - 1, gm_c);
                sys.Y.add_entry(d - 1, g - 1, gm_c);
                sys.Y.add_entry(d - 1, s - 1, -gm_c);
            }
            // Output conductance (S-D)
            const Complex gds_c(gds, 0.0);
            if (s > 0 && d > 0) {
                sys.Y.add_entry(s - 1, s - 1, gds_c);
                sys.Y.add_entry(s - 1, d - 1, -gds_c);
                sys.Y.add_entry(d - 1, s - 1, -gds_c);
                sys.Y.add_entry(d - 1, d - 1, gds_c);
            }
        }

        spdlog::trace("MESFET_{} {}: AC stamp: gm={:.6f}S, gds={:.6f}S, Cgs={:.3e}F, Cgd={:.3e}F",
                      polarity_char(), this->name(), gm, gds, cgs_, cgd_);
    }

    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                               Real frequency) const override {
        std::vector<NoiseSource> noise_sources;

        const NodeId d = nodes_[0];
        const NodeId g = nodes_[1];
        const NodeId s = nodes_[2];

        const Real vd = (d > 0) ? dc_solution[d - 1] : 0.0;
        const Real vg = (g > 0) ? dc_solution[g - 1] : 0.0;
        const Real vs = (s > 0) ? dc_solution[s - 1] : 0.0;

        Real v_ctrl, v_ds;
        compute_effective_voltages(vd, vg, vs, v_ctrl, v_ds);

        const Real id = compute_drain_current(v_ctrl, v_ds);

        if (id < 1e-15) {
            return noise_sources;
        }

        const Real k_boltzmann = 1.380649e-23;
        const Real T = tnom_;
        const Real gm = compute_transconductance(v_ctrl, v_ds);

        NoiseSource thermal_noise;
        thermal_noise.name = this->name() + "_thermal";
        if constexpr (Pol == MESFETPolarity::N) {
            thermal_noise.node_p = d;
            thermal_noise.node_n = s;
        } else {
            thermal_noise.node_p = s;
            thermal_noise.node_n = d;
        }
        thermal_noise.spectral_density = 4.0 * k_boltzmann * T * (2.0 / 3.0) * gm;
        thermal_noise.is_current = true;
        noise_sources.push_back(thermal_noise);

        if (kf_ > 0.0 && frequency > 0.0) {
            NoiseSource flicker_noise;
            flicker_noise.name = this->name() + "_flicker";
            if constexpr (Pol == MESFETPolarity::N) {
                flicker_noise.node_p = d;
                flicker_noise.node_n = s;
            } else {
                flicker_noise.node_p = s;
                flicker_noise.node_n = d;
            }
            flicker_noise.spectral_density = kf_ * std::pow(std::abs(id), af_) / std::pow(frequency, ef_);
            flicker_noise.is_current = true;
            noise_sources.push_back(flicker_noise);
        }

        return noise_sources;
    }

    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override {
        if (first_accept_) {
            return 0.0;
        }

        const NodeId d = nodes_[0];
        const NodeId g = nodes_[1];
        const NodeId s = nodes_[2];

        const Real vd = (d > 0) ? node_voltages[d - 1] : 0.0;
        const Real vg = (g > 0) ? node_voltages[g - 1] : 0.0;
        const Real vs = (s > 0) ? node_voltages[s - 1] : 0.0;

        Real v_gs, v_gd, v_ds;
        if constexpr (Pol == MESFETPolarity::N) {
            v_gs = vg - vs;
            v_gd = vg - vd;
            v_ds = vd - vs;
        } else {
            v_gs = vs - vg;
            v_gd = vd - vg;
            v_ds = vs - vd;
        }

        const Real q_gs = cgs_ * v_gs;
        const Real q_gd = cgd_ * v_gd;
        const Real q_ds = cds_ * v_ds;

        const Real dq_gs = std::abs(q_gs - charge_gs_prev_);
        const Real dq_gd = std::abs(q_gd - charge_gd_prev_);
        const Real dq_ds = std::abs(q_ds - charge_ds_prev_);

        const Real q_scale_gs = std::max(std::abs(q_gs), std::abs(charge_gs_prev_));
        const Real q_scale_gd = std::max(std::abs(q_gd), std::abs(charge_gd_prev_));
        const Real q_scale_ds = std::max(std::abs(q_ds), std::abs(charge_ds_prev_));

        const Real lte_gs = dq_gs / (chgtol + q_scale_gs);
        const Real lte_gd = dq_gd / (chgtol + q_scale_gd);
        const Real lte_ds = dq_ds / (chgtol + q_scale_ds);

        return std::sqrt(lte_gs * lte_gs + lte_gd * lte_gd + lte_ds * lte_ds);
    }

    void reset_state() override {
        v_gs_prev_ = 0.0;
        v_gd_prev_ = 0.0;
        v_ds_prev_ = 0.0;
        v_gs_prev2_ = 0.0;
        v_gd_prev2_ = 0.0;
        v_ds_prev2_ = 0.0;
        charge_gs_prev_ = 0.0;
        charge_gd_prev_ = 0.0;
        charge_ds_prev_ = 0.0;
        first_accept_ = true;
        spdlog::trace("MESFET_{} {}: State reset", polarity_char(), this->name());
    }

    void accept_solution(const DenseVector& node_voltages) override {
        const NodeId d = nodes_[0];
        const NodeId g = nodes_[1];
        const NodeId s = nodes_[2];

        const Real vd = (d > 0) ? node_voltages[d] : 0.0;
        const Real vg = (g > 0) ? node_voltages[g] : 0.0;
        const Real vs = (s > 0) ? node_voltages[s] : 0.0;

        v_gs_prev2_ = v_gs_prev_;
        v_gd_prev2_ = v_gd_prev_;
        v_ds_prev2_ = v_ds_prev_;

        if constexpr (Pol == MESFETPolarity::N) {
            v_gs_prev_ = vg - vs;
            v_gd_prev_ = vg - vd;
            v_ds_prev_ = vd - vs;
        } else {
            v_gs_prev_ = vs - vg;
            v_gd_prev_ = vd - vg;
            v_ds_prev_ = vs - vd;
        }

        charge_gs_prev_ = cgs_ * v_gs_prev_;
        charge_gd_prev_ = cgd_ * v_gd_prev_;
        charge_ds_prev_ = cds_ * v_ds_prev_;

        first_accept_ = false;
    }

    std::optional<Real> get_parameter(const std::string& param_name) const override {
        std::string name_upper = param_name;
        std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);

        if (name_upper == "BETA") return beta_;
        if (name_upper == "VTO") return vto_;
        if (name_upper == "LAMBDA") return lambda_;
        if (name_upper == "ALPHA") return alpha_;
        if (name_upper == "B") return b_;
        if (name_upper == "VP") return vp_;
        if (name_upper == "CGS") return cgs_;
        if (name_upper == "CGD") return cgd_;
        if (name_upper == "CDS") return cds_;
        if (name_upper == "KF") return kf_;
        if (name_upper == "AF") return af_;
        if (name_upper == "EF") return ef_;
        if (name_upper == "TNOM") return tnom_;

        return std::nullopt;
    }

    bool set_parameter(const std::string& param_name, Real value) override {
        std::string name_upper = param_name;
        std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);

        if (name_upper == "BETA") {
            if (value <= 0.0) {
                spdlog::warn("MESFET_{} {}: BETA must be positive", polarity_char(), this->name());
                return false;
            }
            beta_ = value;
            return true;
        }
        if (name_upper == "VTO") { vto_ = value; return true; }
        if (name_upper == "LAMBDA") {
            if (value < 0.0) {
                spdlog::warn("MESFET_{} {}: LAMBDA must be non-negative", polarity_char(), this->name());
                return false;
            }
            lambda_ = value;
            return true;
        }
        if (name_upper == "ALPHA") {
            if (value <= 0.0) {
                spdlog::warn("MESFET_{} {}: ALPHA must be positive", polarity_char(), this->name());
                return false;
            }
            alpha_ = value;
            return true;
        }
        if (name_upper == "VP") {
            if (value <= 0.0) {
                spdlog::warn("MESFET_{} {}: VP must be positive", polarity_char(), this->name());
                return false;
            }
            vp_ = value;
            return true;
        }
        if (name_upper == "CGS" || name_upper == "CGD" || name_upper == "CDS") {
            if (value < 0.0) {
                spdlog::warn("MESFET_{} {}: {} must be non-negative", polarity_char(), this->name(), name_upper);
                return false;
            }
            if (name_upper == "CGS") cgs_ = value;
            else if (name_upper == "CGD") cgd_ = value;
            else cds_ = value;
            return true;
        }

        return false;
    }

    std::vector<std::string> get_perturbable_parameters() const override {
        return {"BETA", "VTO", "LAMBDA", "ALPHA", "VP", "CGS", "CGD", "CDS"};
    }

    // Accessors
    NodeId drain() const { return nodes_[0]; }
    NodeId gate() const { return nodes_[1]; }
    NodeId source() const { return nodes_[2]; }
    MESFETModel model() const { return model_; }
    Real beta() const { return beta_; }
    Real vto() const { return vto_; }
    Real lambda() const { return lambda_; }

private:
    static constexpr char polarity_char() { return (Pol == MESFETPolarity::N) ? 'N' : 'P'; }

    void compute_effective_voltages(Real vd, Real vg, Real vs, Real& v_ctrl, Real& v_ds) const {
        if constexpr (Pol == MESFETPolarity::N) {
            v_ctrl = vg - vs;  // Vgs
            v_ds = vd - vs;    // Vds
        } else {
            v_ctrl = vs - vg;  // Vsg
            v_ds = vs - vd;    // Vsd
        }
    }

    Real compute_drain_current(Real v_ctrl, Real v_ds) const {
        return (model_ == MESFETModel::CURTICE) ?
               compute_drain_current_curtice(v_ctrl, v_ds) :
               compute_drain_current_statz(v_ctrl, v_ds);
    }

    Real compute_drain_current_curtice(Real v_ctrl, Real v_ds) const {
        if (v_ctrl < vto_) {
            return 0.0;
        }

        const Real vov = v_ctrl - vto_;
        const Real clm_factor = 1.0 + lambda_ * std::max(0.0, v_ds);
        const Real sat_factor = std::tanh(alpha_ * std::max(0.0, v_ds));
        const Real id = beta_ * vov * vov * clm_factor * sat_factor;

        return std::max(0.0, id);
    }

    Real compute_drain_current_statz(Real v_ctrl, Real v_ds) const {
        if (v_ctrl < vto_) {
            return 0.0;
        }

        const Real vov = v_ctrl - vto_;
        const Real v_ds_pos = std::max(0.0, v_ds);
        const Real clm_factor = 1.0 + lambda_ * v_ds_pos;

        Real sat_factor = 0.0;
        if (v_ds_pos < vp_) {
            const Real ratio = 1.0 - v_ds_pos / vp_;
            sat_factor = (1.0 - ratio * ratio * ratio) / 3.0;
        } else {
            sat_factor = 1.0 / 3.0;
        }

        const Real id = beta_ * vov * vov * vov * clm_factor * sat_factor;

        return std::max(0.0, id);
    }

    Real compute_transconductance(Real v_ctrl, Real v_ds, Real delta = 1e-6) const {
        const Real id_plus = compute_drain_current(v_ctrl + delta, v_ds);
        const Real id_minus = compute_drain_current(v_ctrl - delta, v_ds);
        return (id_plus - id_minus) / (2.0 * delta);
    }

    Real compute_output_conductance(Real v_ctrl, Real v_ds, Real delta = 1e-6) const {
        const Real id_plus = compute_drain_current(v_ctrl, v_ds + delta);
        const Real id_minus = compute_drain_current(v_ctrl, v_ds - delta);
        return (id_plus - id_minus) / (2.0 * delta);
    }

    std::vector<NodeId> nodes_;

    MESFETModel model_;
    Real beta_, vto_, lambda_, alpha_, b_, vp_;
    Real cgs_, cgd_, cds_;
    Real kf_, af_, ef_;
    Real tnom_;

    // State variables (unified naming)
    mutable Real v_gs_prev_ = 0.0;
    mutable Real v_gd_prev_ = 0.0;
    mutable Real v_ds_prev_ = 0.0;
    mutable Real v_gs_prev2_ = 0.0;
    mutable Real v_gd_prev2_ = 0.0;
    mutable Real v_ds_prev2_ = 0.0;
    mutable Real charge_gs_prev_ = 0.0;
    mutable Real charge_gd_prev_ = 0.0;
    mutable Real charge_ds_prev_ = 0.0;
    mutable bool first_accept_ = true;
};

// Type aliases for backward compatibility
using MESFET_N = MESFET<MESFETPolarity::N>;
using MESFET_P = MESFET<MESFETPolarity::P>;

} // namespace kirimaspice::devices
