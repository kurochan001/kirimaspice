// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/finfet.cpp
// Unified FinFET template implementation
#include "kirimaspice/devices/finfet.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace kirimaspice::devices {

template<Polarity P>
FinFET<P>::FinFET(DeviceId id, const std::string& name,
                   NodeId drain, NodeId gate, NodeId source,
                   int nfin, Real hfin, Real tfin,
                   Real vth, Real u0, Real vsat, Real ss, Real dibl, Real tox,
                   Real cgs, Real cgd,
                   Real kf, Real af, Real kt1, Real tnom, Real l)
    : Device(id, name, device_type),
      nodes_{drain, gate, source},
      nfin_(nfin), hfin_(hfin), tfin_(tfin), l_(l),
      vth_(vth), u0_(u0), vsat_(vsat), ss_(ss), dibl_(dibl), tox_(tox),
      cgs_(cgs), cgd_(cgd),
      kf_(kf), af_(af), kt1_(kt1), tnom_(tnom) {

    // Calculate effective width: Weff = NFin × Hfin
    weff_ = static_cast<Real>(nfin_) * hfin_;

    // Calculate oxide capacitance per unit area: Cox = εox × ε0 / tox
    cox_ = epsilon_ox * epsilon_0 / tox_;

    // The gain factor of the square law, u0 Cox Weff / L
    beta_ = cox_ * weff_ * u0_ / l_;

    const char* type_str = (P == Polarity::N) ? "FinFET_N" : "FinFET_P";
    spdlog::debug("Created {}: {} D={} G={} S={} NFin={} Hfin={:.2e}m Tfin={:.2e}m "
                  "Weff={:.2e}m Vth={:.3f}V β={:.3e}A/V² SS={:.1f}mV/dec DIBL={:.3f}V/V",
                  type_str, name, drain, gate, source, nfin_, hfin_, tfin_, weff_, vth_, beta_, ss_*1e3, dibl_);
}

template<Polarity P>
void FinFET<P>::stamp_gmin(MNASystem& mna) const {
    NodeId nd = nodes_[0];  // Drain
    NodeId ns = nodes_[2];  // Source

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // Add minimum conductance between drain and source
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, gmin_default);
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -gmin_default);
        }
    }
    if (ns_idx >= 0) {
        if (nd_idx >= 0) {
            mna.G.add_entry(ns_idx, nd_idx, -gmin_default);
        }
        mna.G.add_entry(ns_idx, ns_idx, gmin_default);
    }
}

template<Polarity P>
void FinFET<P>::stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const {
    NodeId nd_node = nodes_[0];  // Drain
    NodeId ng_node = nodes_[1];  // Gate
    NodeId ns_node = nodes_[2];  // Source

    // Extract node voltages
    Real v_d = (nd_node == NODE_GND) ? 0.0 : node_voltages[nd_node - 1];
    Real v_g = (ng_node == NODE_GND) ? 0.0 : node_voltages[ng_node - 1];
    Real v_s = (ns_node == NODE_GND) ? 0.0 : node_voltages[ns_node - 1];

    // Compute effective voltages using polarity-specific convention
    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(v_d, v_g, v_s, v_gs_eff, v_ds_eff);

    // Temperature-adjusted threshold voltage: Vth(T) = Vth0 + KT1 × (T - Tnom)
    Real delta_t = temperature_ - tnom_;
    Real vth_temp = get_effective_threshold() + kt1_ * delta_t;

    // DIBL effect: Vth_eff = Vth(T) - DIBL × Vds_eff
    Real vth_eff = vth_temp - dibl_ * std::max(0.0, v_ds_eff);

    // Temperature-scaled mobility: u(T) = u0 × (T/Tnom)^(-1.5)
    Real u_temp = u0_ * std::pow(tnom_ / temperature_, 1.5);
    Real beta_temp = cox_ * weff_ * u_temp / l_;

    auto nd_idx = (nd_node == NODE_GND) ? -1 : static_cast<int>(nd_node - 1);
    auto ng_idx = (ng_node == NODE_GND) ? -1 : static_cast<int>(ng_node - 1);
    auto ns_idx = (ns_node == NODE_GND) ? -1 : static_cast<int>(ns_node - 1);

    // Cutoff region: Vgs_eff < Vth_eff (for N) or Vsg < |Vth| (for P)
    if (v_gs_eff < vth_eff) {
        // Subthreshold conduction (simplified exponential model)
        Real vt = k_boltzmann * temperature_ / q_electron;  // Thermal voltage
        Real n = 1.0 + (ss_ * std::log(10.0)) / vt;

        // Subthreshold current: Ids = I0 × exp((Vgs_eff - Vth_eff) / (n × Vt))
        Real i0 = beta_temp * vt * vt;
        Real id_sub = i0 * std::exp((v_gs_eff - vth_eff) / (n * vt)) * (1.0 - std::exp(-v_ds_eff / vt));

        stamp_gmin(mna);

        // Stamp subthreshold current (polarity-specific)
        if constexpr (P == Polarity::N) {
            if (nd_idx >= 0) mna.b[nd_idx] -= id_sub;
            if (ns_idx >= 0) mna.b[ns_idx] += id_sub;
        } else {
            if (ns_idx >= 0) mna.b[ns_idx] -= id_sub;
            if (nd_idx >= 0) mna.b[nd_idx] += id_sub;
        }

        spdlog::trace("FinFET {} cutoff: Vgs_eff={:.6f}V < Vth={:.6f}V, Ids_sub={:.3e}A",
                     name_, v_gs_eff, vth_eff, id_sub);
        return;
    }

    // Active region: Vgs_eff >= Vth_eff
    Real vov = v_gs_eff - vth_eff;  // Overdrive voltage
    Real vdsat = vov;  // Simplified velocity saturation

    Real id_intrinsic = 0.0;
    Real gm_intrinsic = 0.0;
    Real gds_intrinsic = 0.0;

    // Linear region: Vds_eff < Vdsat
    if (v_ds_eff < vdsat) {
        id_intrinsic = beta_temp * (vov * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
        gm_intrinsic = beta_temp * v_ds_eff;
        gds_intrinsic = beta_temp * (vov - v_ds_eff);
    }
    // Saturation region: Vds_eff >= Vdsat
    else {
        id_intrinsic = 0.5 * beta_temp * vov * vov;
        gm_intrinsic = beta_temp * vov;
        gds_intrinsic = 0.0;
    }

    Real id = id_intrinsic;
    Real gm = gm_intrinsic;
    Real gds = gds_intrinsic + gmin_default;
    Real id_eq = id - gm * v_gs_eff - gds * v_ds_eff;

    // MNA stamps differ for N-channel and P-channel
    if constexpr (P == Polarity::N) {
        // N-channel: Transconductance (Gm) from gate/source
        if (nd_idx >= 0) {
            if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, gm);
            if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -gm);
        }
        if (ns_idx >= 0) {
            if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -gm);
            mna.G.add_entry(ns_idx, ns_idx, gm);
        }

        // Output conductance (Gds)
        if (nd_idx >= 0) {
            mna.G.add_entry(nd_idx, nd_idx, gds);
            if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, -gds);
        }
        if (ns_idx >= 0) {
            if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, -gds);
            mna.G.add_entry(ns_idx, ns_idx, gds);
        }

        // Equivalent current source
        if (nd_idx >= 0) mna.b[nd_idx] -= id_eq;
        if (ns_idx >= 0) mna.b[ns_idx] += id_eq;
    } else {
        // P-channel: Source-referenced (Vsg, Vsd)
        if (ns_idx >= 0) {
            if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -gm);
            mna.G.add_entry(ns_idx, ns_idx, gm);
        }
        if (nd_idx >= 0) {
            if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, gm);
            mna.G.add_entry(nd_idx, nd_idx, -gm);
        }

        // Output conductance
        if (ns_idx >= 0) {
            if (nd_idx >= 0) mna.G.add_entry(ns_idx, nd_idx, -gds);
            mna.G.add_entry(ns_idx, ns_idx, gds);
        }
        if (nd_idx >= 0) {
            if (ns_idx >= 0) mna.G.add_entry(nd_idx, ns_idx, gds);
            mna.G.add_entry(nd_idx, nd_idx, -gds);
        }

        // Current source
        if (ns_idx >= 0) mna.b[ns_idx] -= id_eq;
        if (nd_idx >= 0) mna.b[nd_idx] += id_eq;
    }

    spdlog::trace("FinFET {} DC: Vgs_eff={:.6f}V Vds_eff={:.6f}V Id={:.3e}A Gm={:.3e}S Gds={:.3e}S",
                 name_, v_gs_eff, v_ds_eff, id, gm, gds);
}

template<Polarity P>
void FinFET<P>::stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                                 Real timestep, IntegrationMethod /*method*/,
                                 const BDFCoefficients* /*bdf_coef*/,
                                 const TR_BDF2_Coefficients* /*tr_bdf2_coef*/) const {
    // First stamp DC nonlinear part
    stamp_dc_nonlinear(mna, node_voltages);

    NodeId nd_node = nodes_[0];
    NodeId ng_node = nodes_[1];
    NodeId ns_node = nodes_[2];

    Real v_d = (nd_node == NODE_GND) ? 0.0 : node_voltages[nd_node - 1];
    Real v_g = (ng_node == NODE_GND) ? 0.0 : node_voltages[ng_node - 1];
    Real v_s = (ns_node == NODE_GND) ? 0.0 : node_voltages[ns_node - 1];

    auto nd_idx = (nd_node == NODE_GND) ? -1 : static_cast<int>(nd_node - 1);
    auto ng_idx = (ng_node == NODE_GND) ? -1 : static_cast<int>(ng_node - 1);
    auto ns_idx = (ns_node == NODE_GND) ? -1 : static_cast<int>(ns_node - 1);

    Real dt = timestep;

    if constexpr (P == Polarity::N) {
        Real vgs = v_g - v_s;
        Real vgd = v_g - v_d;

        // Gate-Source capacitance (Cgs)
        if (cgs_ > 0.0) {
            Real g_cgs = cgs_ / dt;
            Real ieq_gs = g_cgs * (vgs - v_gs_prev_);

            if (ng_idx >= 0) {
                mna.G.add_entry(ng_idx, ng_idx, g_cgs);
                if (ns_idx >= 0) mna.G.add_entry(ng_idx, ns_idx, -g_cgs);
                mna.b[ng_idx] -= ieq_gs;
            }
            if (ns_idx >= 0) {
                if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -g_cgs);
                mna.G.add_entry(ns_idx, ns_idx, g_cgs);
                mna.b[ns_idx] += ieq_gs;
            }
        }

        // Gate-Drain capacitance (Cgd)
        if (cgd_ > 0.0) {
            Real g_cgd = cgd_ / dt;
            Real ieq_gd = g_cgd * (vgd - v_gd_prev_);

            if (ng_idx >= 0) {
                mna.G.add_entry(ng_idx, ng_idx, g_cgd);
                if (nd_idx >= 0) mna.G.add_entry(ng_idx, nd_idx, -g_cgd);
                mna.b[ng_idx] -= ieq_gd;
            }
            if (nd_idx >= 0) {
                if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, -g_cgd);
                mna.G.add_entry(nd_idx, nd_idx, g_cgd);
                mna.b[nd_idx] += ieq_gd;
            }
        }
    } else {
        // P-channel: Vsg = Vs - Vg, Vsd = Vs - Vd
        Real vsg = v_s - v_g;
        Real vgd = v_g - v_d;

        // Gate-Source capacitance (using Vsg)
        if (cgs_ > 0.0) {
            Real g_cgs = cgs_ / dt;
            Real ieq_sg = g_cgs * (vsg - v_gs_prev_);

            if (ns_idx >= 0) {
                mna.G.add_entry(ns_idx, ns_idx, g_cgs);
                if (ng_idx >= 0) mna.G.add_entry(ns_idx, ng_idx, -g_cgs);
                mna.b[ns_idx] -= ieq_sg;
            }
            if (ng_idx >= 0) {
                if (ns_idx >= 0) mna.G.add_entry(ng_idx, ns_idx, -g_cgs);
                mna.G.add_entry(ng_idx, ng_idx, g_cgs);
                mna.b[ng_idx] += ieq_sg;
            }
        }

        // Gate-Drain capacitance
        if (cgd_ > 0.0) {
            Real g_cgd = cgd_ / dt;
            Real ieq_gd = g_cgd * (vgd - v_gd_prev_);

            if (ng_idx >= 0) {
                mna.G.add_entry(ng_idx, ng_idx, g_cgd);
                if (nd_idx >= 0) mna.G.add_entry(ng_idx, nd_idx, -g_cgd);
                mna.b[ng_idx] -= ieq_gd;
            }
            if (nd_idx >= 0) {
                if (ng_idx >= 0) mna.G.add_entry(nd_idx, ng_idx, -g_cgd);
                mna.G.add_entry(nd_idx, nd_idx, g_cgd);
                mna.b[nd_idx] += ieq_gd;
            }
        }
    }
}

template<Polarity P>
void FinFET<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                          const DenseVector& dc_solution) const {
    NodeId nd_node = nodes_[0];
    NodeId ng_node = nodes_[1];
    NodeId ns_node = nodes_[2];

    Real v_d = (nd_node == NODE_GND) ? 0.0 : dc_solution[nd_node - 1];
    Real v_g = (ng_node == NODE_GND) ? 0.0 : dc_solution[ng_node - 1];
    Real v_s = (ns_node == NODE_GND) ? 0.0 : dc_solution[ns_node - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(v_d, v_g, v_s, v_gs_eff, v_ds_eff);

    Real delta_t = temperature_ - tnom_;
    Real vth_temp = get_effective_threshold() + kt1_ * delta_t;
    Real vth_eff = vth_temp - dibl_ * std::max(0.0, v_ds_eff);

    auto nd_idx = (nd_node == NODE_GND) ? -1 : static_cast<int>(nd_node - 1);
    auto ng_idx = (ng_node == NODE_GND) ? -1 : static_cast<int>(ng_node - 1);
    auto ns_idx = (ns_node == NODE_GND) ? -1 : static_cast<int>(ns_node - 1);

    // Numerical differentiation for gm
    Real delta = 1e-6;
    Real u_temp = u0_ * std::pow(tnom_ / temperature_, 1.5);
    Real beta_temp = cox_ * weff_ * u_temp / l_;

    Real vgs_plus = v_gs_eff + delta;
    Real vgs_eff_plus = vgs_plus - vth_eff;
    Real id_plus = (vgs_eff_plus > 0.0) ? 0.5 * beta_temp * vgs_eff_plus * vgs_eff_plus : 0.0;

    Real vgs_minus = v_gs_eff - delta;
    Real vgs_eff_minus = vgs_minus - vth_eff;
    Real id_minus = (vgs_eff_minus > 0.0) ? 0.5 * beta_temp * vgs_eff_minus * vgs_eff_minus : 0.0;

    Real gm = (id_plus - id_minus) / (2.0 * delta);
    Real gds = gmin_default;

    Complex gm_complex(gm, 0.0);
    Complex gds_complex(gds, 0.0);

    if constexpr (P == Polarity::N) {
        // N-channel AC stamps
        if (nd_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(nd_idx, ng_idx, gm_complex);
        if (nd_idx >= 0 && ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, -gm_complex);
        if (ns_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, -gm_complex);
        if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, gm_complex);

        if (nd_idx >= 0) {
            sys.Y.add_entry(nd_idx, nd_idx, gds_complex);
            if (ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, -gds_complex);
        }
        if (ns_idx >= 0) {
            if (nd_idx >= 0) sys.Y.add_entry(ns_idx, nd_idx, -gds_complex);
            sys.Y.add_entry(ns_idx, ns_idx, gds_complex);
        }

        // Capacitances: G-S, G-D
        if (cgs_ > 0.0) {
            Complex y_cgs(0.0, omega * cgs_);
            if (ng_idx >= 0) {
                sys.Y.add_entry(ng_idx, ng_idx, y_cgs);
                if (ns_idx >= 0) sys.Y.add_entry(ng_idx, ns_idx, -y_cgs);
            }
            if (ns_idx >= 0) {
                if (ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, -y_cgs);
                sys.Y.add_entry(ns_idx, ns_idx, y_cgs);
            }
        }
        if (cgd_ > 0.0) {
            Complex y_cgd(0.0, omega * cgd_);
            if (ng_idx >= 0) {
                sys.Y.add_entry(ng_idx, ng_idx, y_cgd);
                if (nd_idx >= 0) sys.Y.add_entry(ng_idx, nd_idx, -y_cgd);
            }
            if (nd_idx >= 0) {
                if (ng_idx >= 0) sys.Y.add_entry(nd_idx, ng_idx, -y_cgd);
                sys.Y.add_entry(nd_idx, nd_idx, y_cgd);
            }
        }
    } else {
        // P-channel AC stamps
        if (ns_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, -gm_complex);
        if (ns_idx >= 0) sys.Y.add_entry(ns_idx, ns_idx, gm_complex);
        if (nd_idx >= 0 && ng_idx >= 0) sys.Y.add_entry(nd_idx, ng_idx, gm_complex);
        if (nd_idx >= 0) sys.Y.add_entry(nd_idx, nd_idx, -gm_complex);

        if (ns_idx >= 0) {
            if (nd_idx >= 0) sys.Y.add_entry(ns_idx, nd_idx, -gds_complex);
            sys.Y.add_entry(ns_idx, ns_idx, gds_complex);
        }
        if (nd_idx >= 0) {
            if (ns_idx >= 0) sys.Y.add_entry(nd_idx, ns_idx, gds_complex);
            sys.Y.add_entry(nd_idx, nd_idx, -gds_complex);
        }

        // Capacitances: S-G, G-D
        if (cgs_ > 0.0) {
            Complex y_cgs(0.0, omega * cgs_);
            if (ns_idx >= 0) {
                sys.Y.add_entry(ns_idx, ns_idx, y_cgs);
                if (ng_idx >= 0) sys.Y.add_entry(ns_idx, ng_idx, -y_cgs);
            }
            if (ng_idx >= 0) {
                if (ns_idx >= 0) sys.Y.add_entry(ng_idx, ns_idx, -y_cgs);
                sys.Y.add_entry(ng_idx, ng_idx, y_cgs);
            }
        }
        if (cgd_ > 0.0) {
            Complex y_cgd(0.0, omega * cgd_);
            if (ng_idx >= 0) {
                sys.Y.add_entry(ng_idx, ng_idx, y_cgd);
                if (nd_idx >= 0) sys.Y.add_entry(ng_idx, nd_idx, -y_cgd);
            }
            if (nd_idx >= 0) {
                if (ng_idx >= 0) sys.Y.add_entry(nd_idx, ng_idx, -y_cgd);
                sys.Y.add_entry(nd_idx, nd_idx, y_cgd);
            }
        }
    }

    spdlog::trace("FinFET {} AC: gm={:.6e}S gds={:.6e}S @{:.3e}Hz",
                 name_, gm, gds, omega / (2.0 * M_PI));
}

template<Polarity P>
std::vector<NoiseSource> FinFET<P>::get_noise_sources(const DenseVector& dc_solution,
                                                        Real frequency) const {
    std::vector<NoiseSource> sources;

    NodeId nd_node = nodes_[0];
    NodeId ng_node = nodes_[1];
    NodeId ns_node = nodes_[2];

    Real v_d = (nd_node == NODE_GND) ? 0.0 : dc_solution[nd_node - 1];
    Real v_g = (ng_node == NODE_GND) ? 0.0 : dc_solution[ng_node - 1];
    Real v_s = (ns_node == NODE_GND) ? 0.0 : dc_solution[ns_node - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(v_d, v_g, v_s, v_gs_eff, v_ds_eff);

    Real vth_eff = get_effective_threshold();
    Real u_temp = u0_ * std::pow(tnom_ / temperature_, 1.5);
    Real beta_temp = cox_ * weff_ * u_temp / l_;

    // Calculate gm for thermal noise using numerical differentiation
    Real delta = 1e-6;
    Real vgs_plus = v_gs_eff + delta;
    Real vgs_eff_plus = vgs_plus - vth_eff;
    Real id_plus = (vgs_eff_plus > 0.0) ? 0.5 * beta_temp * vgs_eff_plus * vgs_eff_plus : 0.0;

    Real vgs_minus = v_gs_eff - delta;
    Real vgs_eff_minus = vgs_minus - vth_eff;
    Real id_minus = (vgs_eff_minus > 0.0) ? 0.5 * beta_temp * vgs_eff_minus * vgs_eff_minus : 0.0;

    Real gm = (id_plus - id_minus) / (2.0 * delta);

    // Channel thermal noise: I_n² = 4kT × γ × gm [A²/Hz]
    Real gamma = 2.0 / 3.0;
    Real thermal_psd = 4.0 * k_boltzmann * temperature_ * gamma * gm;

    if (thermal_psd > 0.0) {
        NoiseSource thermal;
        thermal.name = name_ + "_thermal";
        if constexpr (P == Polarity::N) {
            thermal.node_p = nd_node;
            thermal.node_n = ns_node;
        } else {
            thermal.node_p = ns_node;
            thermal.node_n = nd_node;
        }
        thermal.spectral_density = thermal_psd;
        thermal.is_current = true;
        sources.push_back(thermal);
    }

    // Flicker noise: I_n² = KF × |ID|^AF / f [A²/Hz]
    if (kf_ > 0.0 && frequency > 0.0) {
        Real vov = v_gs_eff - vth_eff;
        Real id = (vov > 0.0) ? 0.5 * beta_temp * vov * vov : 0.0;
        Real flicker_psd = kf_ * std::pow(std::abs(id), af_) / frequency;

        NoiseSource flicker;
        flicker.name = name_ + "_flicker";
        if constexpr (P == Polarity::N) {
            flicker.node_p = nd_node;
            flicker.node_n = ns_node;
        } else {
            flicker.node_p = ns_node;
            flicker.node_n = nd_node;
        }
        flicker.spectral_density = flicker_psd;
        flicker.is_current = true;
        sources.push_back(flicker);
    }

    return sources;
}

template<Polarity P>
void FinFET<P>::accept_solution(const DenseVector& node_voltages) {
    NodeId nd_node = nodes_[0];
    NodeId ng_node = nodes_[1];
    NodeId ns_node = nodes_[2];

    Real v_d = (nd_node == NODE_GND) ? 0.0 : node_voltages[nd_node];
    Real v_g = (ng_node == NODE_GND) ? 0.0 : node_voltages[ng_node];
    Real v_s = (ns_node == NODE_GND) ? 0.0 : node_voltages[ns_node];

    if constexpr (P == Polarity::N) {
        v_gs_prev_ = v_g - v_s;
        v_gd_prev_ = v_g - v_d;
    } else {
        v_gs_prev_ = v_s - v_g;  // Store vsg for P-channel
        v_gd_prev_ = v_g - v_d;  // Store vgd
    }
}

template<Polarity P>
std::optional<Real> FinFET<P>::get_parameter(const std::string& param_name) const {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "nfin") return static_cast<Real>(nfin_);
    if (param_lower == "hfin") return hfin_;
    if (param_lower == "tfin") return tfin_;
    if (param_lower == "l") return l_;
    if (param_lower == "weff") return weff_;
    if (param_lower == "vth" || param_lower == "vth0") return vth_;
    if (param_lower == "u0") return u0_;
    if (param_lower == "vsat") return vsat_;
    if (param_lower == "ss") return ss_;
    if (param_lower == "dibl") return dibl_;
    if (param_lower == "tox") return tox_;
    if (param_lower == "cgs") return cgs_;
    if (param_lower == "cgd") return cgd_;
    if (param_lower == "kf") return kf_;
    if (param_lower == "af") return af_;
    if (param_lower == "kt1") return kt1_;
    if (param_lower == "tnom") return tnom_;

    return std::nullopt;
}

template<Polarity P>
bool FinFET<P>::set_parameter(const std::string& param_name, Real value) {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "nfin") {
        if (value >= 1) {
            nfin_ = static_cast<int>(value);
            weff_ = static_cast<Real>(nfin_) * hfin_;
            beta_ = cox_ * weff_ * u0_ / l_;
            spdlog::trace("FinFET {} set NFin={}", name_, nfin_);
            return true;
        }
        return false;
    }
    if (param_lower == "hfin") {
        if (value > 0.0) {
            hfin_ = value;
            weff_ = static_cast<Real>(nfin_) * hfin_;
            beta_ = cox_ * weff_ * u0_ / l_;
            return true;
        }
        return false;
    }
    if (param_lower == "l") {
        if (value > 0.0) {
            l_ = value;
            beta_ = cox_ * weff_ * u0_ / l_;
            return true;
        }
        return false;
    }
    if (param_lower == "vth" || param_lower == "vth0") {
        vth_ = value;
        return true;
    }
    if (param_lower == "u0") {
        if (value > 0.0) {
            u0_ = value;
            beta_ = cox_ * weff_ * u0_ / l_;
            return true;
        }
        return false;
    }

    return false;
}

template<Polarity P>
std::vector<std::string> FinFET<P>::get_perturbable_parameters() const {
    return {"NFIN", "HFIN", "VTH", "U0"};
}

// Explicit template instantiations
template class FinFET<Polarity::N>;
template class FinFET<Polarity::P>;

} // namespace kirimaspice::devices
