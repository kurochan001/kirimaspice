// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception AND ECL-2.0

// Parts of this file are based on the BSIM4 4.8.3 model code (b4ld.c, b4temp.c),
// as distributed by UC Berkeley and in ngspice. That code carries this notice:
//
//   Copyright (c) 2025 University of California
//   Project Directors: Prof. Sayeef Salahuddin and Prof. Chenming Hu
//   Developers list: https://www.bsim.berkeley.edu/models/bsim4/auth_bsim4/
//
//   Licensed under Educational Community License, Version 2.0 (the "License"); you may
//   not use this file except in compliance with the License. You may obtain a copy of the
//   license at http://opensource.org/licenses/ECL-2.0
//   Unless required by applicable law or agreed to in writing, software distributed under
//   the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the License for the specific language governing
//   permissions and limitations under the License.
//
// Modified for this project: rewritten in C++ with a different structure.

// src/devices/mosfet_bsim4.cpp
// Unified MOSFET BSIM4.8.3 template implementation
// Reference: BSIM4.8.3 User's Manual (UC Berkeley, 2025)

#include "kirimaspice/devices/mosfet_bsim4.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <limits>

namespace kirimaspice::devices {
namespace {

inline int node_index(NodeId node) {
    return (node == NODE_GND) ? -1 : static_cast<int>(node - 1);
}

inline void add_entry_if_valid(MNASystem& mna, int row, int col, Real value) {
    if (row >= 0 && col >= 0 && value != 0.0) {
        mna.G.add_entry(row, col, value);
    }
}

inline bool is_finite(Real value) {
    return std::isfinite(value);
}

}  // namespace

//==============================================================================
// Constructor and Destructor
//==============================================================================

template<Polarity P>
MOSFET_BSIM4<P>::MOSFET_BSIM4(DeviceId id, const std::string& name,
                               NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                               const BSIM4Params& params)
    : Device(id, name, device_type),
      nodes_({drain, gate, source, bulk}),
      drain_(drain), gate_(gate), source_(source), bulk_(bulk),
      params_(params),
      core_(params_, P == Polarity::P) {
    // The derived parameters (Xdep0, factor1, ...) are computed here, as the
    // core computes its own, so that the device does not depend on whether
    // its caller computed them (subcircuit expansion did not). TYPE is the
    // polarity, as in the core.
    params_.type = (P == Polarity::P) ? -1.0 : 1.0;
    params_.compute_derived_params(params_.tnom);

    // Calculate instance-specific theta0vb0
    // This MUST be done per-instance because it depends on leff.
    // Reference: ngspice b4temp.c, BSIM4temp() function (around line 800+)
    const Real leff = params_.get_leff();
    if (params_.Xdep0 > 0.0 && params_.factor1 > 0.0 && leff > 0.0) {
        const Real tmp = params_.factor1 * std::sqrt(params_.Xdep0);
        const Real EXP_THRESHOLD = 60.0;
        const Real MIN_EXP = 1.0e-20;
        const Real T0 = params_.dsub * leff / tmp;

        if (T0 < EXP_THRESHOLD) {
            const Real T1 = std::exp(T0);
            const Real T2 = T1 - 1.0;
            const Real T3 = T2 * T2;
            const Real T4 = T3 + 2.0 * T1 * MIN_EXP;
            theta0vb0_ = T1 / T4;
            spdlog::debug("{} {}: theta0vb0 = {:.6e} (leff={:.3e} m, T0={:.6f})",
                          polarity_name(), name, theta0vb0_, leff, T0);
        } else {
            theta0vb0_ = 1.0 / (EXP_THRESHOLD - 2.0);
            spdlog::debug("{} {}: theta0vb0 = {:.6e} (T0={:.6f} >= EXP_THRESHOLD, using max)",
                          polarity_name(), name, theta0vb0_, T0);
        }
    } else {
        theta0vb0_ = 0.0;
        spdlog::warn("{} {}: theta0vb0 = 0 (invalid params: Xdep0={:.3e}, factor1={:.3e}, leff={:.3e})",
                     polarity_name(), name, params_.Xdep0, params_.factor1, leff);
    }

    spdlog::trace("MOSFET_{}::MOSFET_{}: {} D={} G={} S={} B={} W={:.3e} L={:.3e}",
                  polarity_name(), polarity_name(), name, drain, gate, source, bulk, params.w, params.l);
}

template<Polarity P>
MOSFET_BSIM4<P>::~MOSFET_BSIM4() = default;

//==============================================================================
// Temperature Support
//==============================================================================

template<Polarity P>
void MOSFET_BSIM4<P>::set_temperature(Real temp_kelvin) {
    // The core keeps the card's parameters at TNOM; evaluate() takes the
    // operating temperature and scales them from TNOM to it.
    temperature_ = temp_kelvin;

    // Recompute instance-specific theta0vb0 at new temperature
    const Real leff = params_.get_leff();
    if (params_.Xdep0 > 0.0 && params_.factor1 > 0.0 && leff > 0.0) {
        const Real tmp = params_.factor1 * std::sqrt(params_.Xdep0);
        const Real EXP_THRESHOLD = 60.0;
        const Real MIN_EXP = 1.0e-20;
        const Real T0 = params_.dsub * leff / tmp;

        if (T0 < EXP_THRESHOLD) {
            const Real T1 = std::exp(T0);
            const Real T2 = T1 - 1.0;
            const Real T3 = T2 * T2;
            const Real T4 = T3 + 2.0 * T1 * MIN_EXP;
            theta0vb0_ = T1 / T4;
        } else {
            theta0vb0_ = 1.0 / (EXP_THRESHOLD - 2.0);
        }
    }
    spdlog::debug("{} {}: temperature set to {:.2f} K", polarity_name(), name(), temp_kelvin);
}

//==============================================================================
// DC Analysis
//==============================================================================

template<Polarity P>
void MOSFET_BSIM4<P>::stamp_dc_linear(MNASystem& mna, const DenseVector& node_voltages) const {
    stamp_dc_nonlinear(mna, node_voltages);
}

template<Polarity P>
void MOSFET_BSIM4<P>::stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const {

    const Real vd = (drain_ != NODE_GND) ? node_voltages[drain_ - 1] : 0.0;
    const Real vg = (gate_ != NODE_GND) ? node_voltages[gate_ - 1] : 0.0;
    const Real vs = (source_ != NODE_GND) ? node_voltages[source_ - 1] : 0.0;
    const Real vb = (bulk_ != NODE_GND) ? node_voltages[bulk_ - 1] : 0.0;

    const BSIM4OperatingPoint op = core_.evaluate(vg, vd, vs, vb, temperature_, theta0vb0_);

    const Real gm = std::isfinite(op.gm) ? op.gm : 0.0;
    const Real gds = std::isfinite(op.gds) ? op.gds : 0.0;
    const Real gmb = std::isfinite(op.gmb) ? op.gmb : 0.0;
    Real ids = std::isfinite(op.ids) ? op.ids : 0.0;

    // Clamp terminal voltages for id_eq calculation
    const Real vmax = params_.vmax_clamp;
    const Real vgs = std::clamp(vg - vs, -vmax, vmax);
    const Real vds = std::clamp(vd - vs, -vmax, vmax);
    const Real vbs = std::clamp(vb - vs, -vmax, vmax);
    const Real id_eq = ids - gm * vgs - gds * vds - gmb * vbs;

    // DEBUG: Log BSIM4 operating point values
    thread_local static int log_count = 0;
    if (log_count < 20) {
        spdlog::debug("{} eval [call {}]:", polarity_name(), log_count);
        spdlog::debug("  Terminal voltages: vgs={:.6f}, vds={:.6f}, vbs={:.6f}", vgs, vds, vbs);
        spdlog::debug("  Node voltages: vd={:.6f}, vg={:.6f}, vs={:.6f}, vb={:.6f}", vd, vg, vs, vb);
        spdlog::debug("  DC current: ids={:.6e} A", ids);
        spdlog::debug("  Transconductances: gm={:.6e} S, gds={:.6e} S, gmb={:.6e} S", gm, gds, gmb);
        spdlog::debug("  Equivalent current: id_eq={:.6e} A", id_eq);
        log_count++;
    }

    const int nd_idx = node_index(drain_);
    const int ng_idx = node_index(gate_);
    const int ns_idx = node_index(source_);
    const int nb_idx = node_index(bulk_);

    add_entry_if_valid(mna, nd_idx, ng_idx, gm);
    add_entry_if_valid(mna, nd_idx, nd_idx, gds);
    add_entry_if_valid(mna, nd_idx, ns_idx, -(gm + gds + gmb));
    add_entry_if_valid(mna, nd_idx, nb_idx, gmb);

    add_entry_if_valid(mna, ns_idx, ng_idx, -gm);
    add_entry_if_valid(mna, ns_idx, nd_idx, -gds);
    add_entry_if_valid(mna, ns_idx, ns_idx, gm + gds + gmb);
    add_entry_if_valid(mna, ns_idx, nb_idx, -gmb);

    if (nd_idx >= 0) {
        mna.b[nd_idx] -= id_eq;
    }
    if (ns_idx >= 0) {
        mna.b[ns_idx] += id_eq;
    }
}


//==============================================================================
// Transient Analysis
//==============================================================================

template<Polarity P>
void MOSFET_BSIM4<P>::stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                                       Real timestep, IntegrationMethod method,
                                       const BDFCoefficients* bdf_coef,
                                       const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Step 1: Stamp DC nonlinear model (drain current)
    stamp_dc_nonlinear(mna, node_voltages);

    // Step 2: Stamp nonlinear capacitances using companion model
    timestep_ = timestep;
    last_method_ = method;

    Real vg = (gate_ != NODE_GND) ? node_voltages[gate_ - 1] : 0.0;
    Real vd = (drain_ != NODE_GND) ? node_voltages[drain_ - 1] : 0.0;
    Real vs = (source_ != NODE_GND) ? node_voltages[source_ - 1] : 0.0;
    Real vb = (bulk_ != NODE_GND) ? node_voltages[bulk_ - 1] : 0.0;

    // BSIM4 core capacitances at this bias
    const BSIM4OperatingPoint op = core_.evaluate(vg, vd, vs, vb, temperature_, theta0vb0_);
    const BSIM4Capacitances caps = core_.compute_capacitances(vg, vd, vs, vb, op.vth, temperature_);
    Real cgs = caps.cgs;
    Real cgd = caps.cgd;
    Real cgb = caps.cgb;
    Real cbd = caps.cbd;
    Real cbs = caps.cbs;

    const int nd_idx = (drain_ == NODE_GND) ? -1 : static_cast<int>(drain_ - 1);
    const int ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
    const int ns_idx = (source_ == NODE_GND) ? -1 : static_cast<int>(source_ - 1);
    const int nb_idx = (bulk_ == NODE_GND) ? -1 : static_cast<int>(bulk_ - 1);

    // Helper lambda: Stamp companion model for a junction capacitance
    auto stamp_capacitance = [&](Real c_now, Real v_prev, Real v_prev2, Real v_prev3,
                                  int node_p_idx, int node_n_idx) {
        Real g_eq = 0.0;
        Real i_eq = 0.0;

        switch (method) {
            case IntegrationMethod::BACKWARD_EULER:
                g_eq = c_now / timestep;
                i_eq = -g_eq * v_prev;
                break;

            case IntegrationMethod::TRAPEZOIDAL:
                g_eq = 2.0 * c_now / timestep;
                i_eq = -g_eq * v_prev;
                break;

            case IntegrationMethod::GEAR2:
                g_eq = 1.5 * c_now / timestep;
                i_eq = -(2.0 * c_now / timestep) * v_prev + (0.5 * c_now / timestep) * v_prev2;
                break;

            case IntegrationMethod::GEAR3:
                g_eq = (11.0 / 6.0) * c_now / timestep;
                i_eq = -(3.0 * c_now / timestep) * v_prev
                      + (1.5 * c_now / timestep) * v_prev2
                      - (c_now / (3.0 * timestep)) * v_prev3;
                break;

            case IntegrationMethod::TR_BDF2:
                if (tr_bdf2_coef) {
                    g_eq = tr_bdf2_coef->alpha0 * c_now;
                    i_eq = -tr_bdf2_coef->beta0 * c_now * v_prev;
                } else {
                    g_eq = 2.0 * c_now / timestep;
                    i_eq = -g_eq * v_prev;
                }
                break;

            default:
                g_eq = c_now / timestep;
                i_eq = -g_eq * v_prev;
                break;
        }

        if (node_p_idx >= 0) {
            if (node_n_idx >= 0) {
                mna.G.add_entry(node_p_idx, node_p_idx, g_eq);
                mna.G.add_entry(node_p_idx, node_n_idx, -g_eq);
            } else {
                mna.G.add_entry(node_p_idx, node_p_idx, g_eq);
            }
            mna.b[node_p_idx] -= i_eq;
        }
        if (node_n_idx >= 0) {
            if (node_p_idx >= 0) {
                mna.G.add_entry(node_n_idx, node_n_idx, g_eq);
                mna.G.add_entry(node_n_idx, node_p_idx, -g_eq);
            } else {
                mna.G.add_entry(node_n_idx, node_n_idx, g_eq);
            }
            mna.b[node_n_idx] += i_eq;
        }
    };

    // Stamp each capacitance
    stamp_capacitance(cgs, vgs_prev_, vgs_prev2_, vgs_prev3_, ng_idx, ns_idx);
    stamp_capacitance(cgd, vgd_prev_, vgd_prev2_, vgd_prev3_, ng_idx, nd_idx);
    stamp_capacitance(cgb, vgb_prev_, vgb_prev2_, vgb_prev3_, ng_idx, nb_idx);
    stamp_capacitance(cbd, vbd_prev_, vbd_prev2_, vbd_prev3_, nb_idx, nd_idx);
    stamp_capacitance(cbs, vbs_prev_, vbs_prev2_, vbs_prev3_, nb_idx, ns_idx);

    (void)bdf_coef;
}

//==============================================================================
// AC Analysis
//==============================================================================

template<Polarity P>
void MOSFET_BSIM4<P>::stamp_ac(ComplexMNASystem& mna, Real frequency,
                                const DenseVector& dc_solution) const {
    Real vg = (gate_ != NODE_GND) ? dc_solution[gate_ - 1] : 0.0;
    Real vd = (drain_ != NODE_GND) ? dc_solution[drain_ - 1] : 0.0;
    Real vs = (source_ != NODE_GND) ? dc_solution[source_ - 1] : 0.0;
    Real vb = (bulk_ != NODE_GND) ? dc_solution[bulk_ - 1] : 0.0;

    Real vgs = vg - vs;
    Real vds = vd - vs;
    Real vbs = vb - vs;

    const BSIM4OperatingPoint op = core_.evaluate(vg, vd, vs, vb, temperature_, theta0vb0_);
    Real gm = op.gm;
    Real gds = op.gds;
    Real gmb = op.gmb;

    const BSIM4Capacitances caps = core_.compute_capacitances(vg, vd, vs, vb, op.vth, temperature_);
    Real cgs = caps.cgs;
    Real cgd = caps.cgd;
    Real cgb = caps.cgb;
    Real cbd = caps.cbd;
    Real cbs = caps.cbs;

    spdlog::trace("{} {} stamp_ac: Vgs={:.6f}V, Vds={:.6f}V, Vbs={:.6f}V, "
                  "Gm={:.6e}S, Gds={:.6e}S, Gmb={:.6e}S, "
                  "Cgs={:.6e}F, Cgd={:.6e}F, Cgb={:.6e}F, Cbd={:.6e}F, Cbs={:.6e}F, ω={:.3e}rad/s",
                  polarity_name(), name_, vgs, vds, vbs, gm, gds, gmb, cgs, cgd, cgb, cbd, cbs, frequency);

    const int nd_idx = (drain_ == NODE_GND) ? -1 : static_cast<int>(drain_ - 1);
    const int ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
    const int ns_idx = (source_ == NODE_GND) ? -1 : static_cast<int>(source_ - 1);
    const int nb_idx = (bulk_ == NODE_GND) ? -1 : static_cast<int>(bulk_ - 1);

    Real omega = 2.0 * M_PI * frequency;

    Complex ygs(0.0, omega * cgs);
    Complex ygd(0.0, omega * cgd);
    Complex ygb(0.0, omega * cgb);
    Complex ybd(0.0, omega * cbd);
    Complex ybs(0.0, omega * cbs);
    Complex yds(gds, 0.0);
    Complex gmvgs(gm, 0.0);
    Complex gmbvbs(gmb, 0.0);

    // Stamp G-S capacitance
    if (ng_idx >= 0) {
        mna.Y.add_entry(ng_idx, ng_idx, ygs);
        if (ns_idx >= 0) mna.Y.add_entry(ng_idx, ns_idx, -ygs);
    }
    if (ns_idx >= 0) {
        mna.Y.add_entry(ns_idx, ns_idx, ygs);
        if (ng_idx >= 0) mna.Y.add_entry(ns_idx, ng_idx, -ygs);
    }

    // Stamp G-D capacitance (Miller)
    if (ng_idx >= 0) {
        mna.Y.add_entry(ng_idx, ng_idx, ygd);
        if (nd_idx >= 0) mna.Y.add_entry(ng_idx, nd_idx, -ygd);
    }
    if (nd_idx >= 0) {
        mna.Y.add_entry(nd_idx, nd_idx, ygd);
        if (ng_idx >= 0) mna.Y.add_entry(nd_idx, ng_idx, -ygd);
    }

    // Stamp G-B capacitance
    if (ng_idx >= 0) {
        mna.Y.add_entry(ng_idx, ng_idx, ygb);
        if (nb_idx >= 0) mna.Y.add_entry(ng_idx, nb_idx, -ygb);
    }
    if (nb_idx >= 0) {
        mna.Y.add_entry(nb_idx, nb_idx, ygb);
        if (ng_idx >= 0) mna.Y.add_entry(nb_idx, ng_idx, -ygb);
    }

    // Stamp B-D junction capacitance
    if (nb_idx >= 0) {
        mna.Y.add_entry(nb_idx, nb_idx, ybd);
        if (nd_idx >= 0) mna.Y.add_entry(nb_idx, nd_idx, -ybd);
    }
    if (nd_idx >= 0) {
        mna.Y.add_entry(nd_idx, nd_idx, ybd);
        if (nb_idx >= 0) mna.Y.add_entry(nd_idx, nb_idx, -ybd);
    }

    // Stamp B-S junction capacitance
    if (nb_idx >= 0) {
        mna.Y.add_entry(nb_idx, nb_idx, ybs);
        if (ns_idx >= 0) mna.Y.add_entry(nb_idx, ns_idx, -ybs);
    }
    if (ns_idx >= 0) {
        mna.Y.add_entry(ns_idx, ns_idx, ybs);
        if (nb_idx >= 0) mna.Y.add_entry(ns_idx, nb_idx, -ybs);
    }

    // Stamp D-S output conductance
    if (nd_idx >= 0) {
        mna.Y.add_entry(nd_idx, nd_idx, yds);
        if (ns_idx >= 0) mna.Y.add_entry(nd_idx, ns_idx, -yds);
    }
    if (ns_idx >= 0) {
        mna.Y.add_entry(ns_idx, ns_idx, yds);
        if (nd_idx >= 0) mna.Y.add_entry(ns_idx, nd_idx, -yds);
    }

    // Stamp Gm controlled current source
    if (nd_idx >= 0) {
        if (ng_idx >= 0) mna.Y.add_entry(nd_idx, ng_idx, +gmvgs);
        if (ns_idx >= 0) mna.Y.add_entry(nd_idx, ns_idx, -gmvgs);
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) mna.Y.add_entry(ns_idx, ng_idx, -gmvgs);
        if (ns_idx >= 0) mna.Y.add_entry(ns_idx, ns_idx, +gmvgs);
    }

    // Stamp Gmb controlled current source
    if (nd_idx >= 0) {
        if (nb_idx >= 0) mna.Y.add_entry(nd_idx, nb_idx, +gmbvbs);
        if (ns_idx >= 0) mna.Y.add_entry(nd_idx, ns_idx, -gmbvbs);
    }
    if (ns_idx >= 0) {
        if (nb_idx >= 0) mna.Y.add_entry(ns_idx, nb_idx, -gmbvbs);
        if (ns_idx >= 0) mna.Y.add_entry(ns_idx, ns_idx, +gmbvbs);
    }
}

//==============================================================================
// State Management
//==============================================================================

template<Polarity P>
void MOSFET_BSIM4<P>::accept_solution(const DenseVector& node_voltages) {
    Real vg = (gate_ != NODE_GND) ? node_voltages[gate_] : 0.0;
    Real vd = (drain_ != NODE_GND) ? node_voltages[drain_] : 0.0;
    Real vs = (source_ != NODE_GND) ? node_voltages[source_] : 0.0;
    Real vb = (bulk_ != NODE_GND) ? node_voltages[bulk_] : 0.0;

    Real vgs_new = vg - vs;
    Real vgd_new = vg - vd;
    Real vgb_new = vg - vb;
    Real vbd_new = vb - vd;
    Real vbs_new = vb - vs;

    // Shift voltage history for each junction (BDF3 requires 3 past timesteps)
    vgs_prev3_ = vgs_prev2_;
    vgs_prev2_ = vgs_prev_;
    vgs_prev_ = vgs_new;

    vgd_prev3_ = vgd_prev2_;
    vgd_prev2_ = vgd_prev_;
    vgd_prev_ = vgd_new;

    vgb_prev3_ = vgb_prev2_;
    vgb_prev2_ = vgb_prev_;
    vgb_prev_ = vgb_new;

    vbd_prev3_ = vbd_prev2_;
    vbd_prev2_ = vbd_prev_;
    vbd_prev_ = vbd_new;

    vbs_prev3_ = vbs_prev2_;
    vbs_prev2_ = vbs_prev_;
    vbs_prev_ = vbs_new;

    // Update charge history for Physical LTE estimation
    const BSIM4OperatingPoint op = core_.evaluate(vg, vd, vs, vb, temperature_, theta0vb0_);
    const BSIM4Capacitances caps = core_.compute_capacitances(vg, vd, vs, vb, op.vth, temperature_);

    charge_gs_prev_ = caps.cgs * vgs_new;
    charge_gd_prev_ = caps.cgd * vgd_new;
    charge_gb_prev_ = caps.cgb * vgb_new;
    charge_bd_prev_ = caps.cbd * vbd_new;
    charge_bs_prev_ = caps.cbs * vbs_new;

    // Legacy DC analysis state
    vds_prev_ = vd - vs;
}

//==============================================================================
// Physical LTE Estimation
//==============================================================================

template<Polarity P>
Real MOSFET_BSIM4<P>::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    if (!is_dynamic()) {
        return 0.0;
    }

    const int nd_idx = (drain_ == NODE_GND) ? -1 : static_cast<int>(drain_ - 1);
    const int ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
    const int ns_idx = (source_ == NODE_GND) ? -1 : static_cast<int>(source_ - 1);
    const int nb_idx = (bulk_ == NODE_GND) ? -1 : static_cast<int>(bulk_ - 1);

    Real vg = (ng_idx >= 0) ? node_voltages[ng_idx] : 0.0;
    Real vd = (nd_idx >= 0) ? node_voltages[nd_idx] : 0.0;
    Real vs = (ns_idx >= 0) ? node_voltages[ns_idx] : 0.0;
    Real vb = (nb_idx >= 0) ? node_voltages[nb_idx] : 0.0;

    Real vgs = vg - vs;
    Real vgd = vg - vd;
    Real vgb = vg - vb;
    Real vbs = vb - vs;
    Real vbd = vb - vd;

    const BSIM4OperatingPoint op = core_.evaluate(vg, vd, vs, vb, temperature_, theta0vb0_);
    const BSIM4Capacitances caps = core_.compute_capacitances(vg, vd, vs, vb, op.vth, temperature_);

    Real total_lte_sq = 0.0;

    auto compute_lte = [&](Real cap, Real v_now, Real q_prev) -> Real {
        if (cap <= 0.0) return 0.0;
        Real q_now = cap * v_now;
        Real delta_q = std::abs(q_now - q_prev);
        Real q_scale = std::max(std::abs(q_prev), std::abs(q_now));
        return delta_q / (chgtol + q_scale);
    };

    Real lte_gs = compute_lte(caps.cgs, vgs, charge_gs_prev_);
    total_lte_sq += lte_gs * lte_gs;

    Real lte_gd = compute_lte(caps.cgd, vgd, charge_gd_prev_);
    total_lte_sq += lte_gd * lte_gd;

    Real lte_gb = compute_lte(caps.cgb, vgb, charge_gb_prev_);
    total_lte_sq += lte_gb * lte_gb;

    Real lte_bd = compute_lte(caps.cbd, vbd, charge_bd_prev_);
    total_lte_sq += lte_bd * lte_bd;

    Real lte_bs = compute_lte(caps.cbs, vbs, charge_bs_prev_);
    total_lte_sq += lte_bs * lte_bs;

    return std::sqrt(total_lte_sq);
}

//==============================================================================
// BSIM4.8.3 DC Model - Threshold Voltage
//==============================================================================

template<Polarity P>
void MOSFET_BSIM4<P>::compute_effective_dimensions(Real& weff, Real& leff) const {
    weff = params_.w - params_.wint;
    leff = params_.l - params_.lint;

    if (weff <= 0.0) {
        spdlog::warn("{}: Effective width Weff={:.3e} <= 0, using W={:.3e}",
                     name_, weff, params_.w);
        weff = params_.w;
    }
    if (leff <= 0.0) {
        spdlog::warn("{}: Effective length Leff={:.3e} <= 0, using L={:.3e}",
                     name_, leff, params_.l);
        leff = params_.l;
    }
}

template<Polarity P>
Real MOSFET_BSIM4<P>::compute_threshold_voltage(Real vds, Real vbs) const {
    const Real kb = 1.380649e-23;
    const Real q = 1.602176634e-19;
    const Real T = temperature_;
    const Real Vt = kb * T / q;

    const Real phi_s = 2.0 * Vt * 17.0;

    Real sqrt_term = 0.0;
    if (phi_s - vbs > 0.0) {
        sqrt_term = std::sqrt(phi_s - vbs) - std::sqrt(phi_s);
    } else {
        sqrt_term = -std::sqrt(phi_s);
    }
    Real vth_body = params_.k1 * sqrt_term + params_.k2 * vbs;

    Real weff, leff;
    compute_effective_dimensions(weff, leff);

    Real dvth_sce = 0.0;
    if (params_.dvt0 > 0.0 && params_.dvt1 > 0.0) {
        const Real epsilon_si = 11.7 * 8.854e-12;
        const Real epsilon_ox = 3.9 * 8.854e-12;
        Real lt = std::sqrt(epsilon_si * params_.toxe / epsilon_ox);

        Real theta_sce = params_.dvt0 / (1.0 + params_.dvt2 * vbs);
        dvth_sce = -theta_sce * phi_s * std::exp(-params_.dvt1 * leff / lt);
    }

    Real dvth_dibl = 0.0;
    if (vds > 0.0) {
        Real eta_eff = params_.eta0 * (1.0 - params_.dsub * leff);
        dvth_dibl = -eta_eff * vds;
    }

    Real dvth_wpe = 0.0;
    if (params_.k3 > 0.0 && params_.w0 > 0.0 && weff < params_.w0) {
        dvth_wpe = params_.k3 * (params_.w0 / weff - 1.0);
    }

    Real delta_temp = T - params_.tnom;
    Real dvth_temp = params_.kt1 * delta_temp + params_.kt2 * vbs * delta_temp;

    Real vth = params_.vth0 + vth_body + dvth_sce + dvth_dibl + dvth_wpe + dvth_temp;

    spdlog::trace("{}: Vth calculation: VTH0={:.4f}V, Body={:.4f}V, SCE={:.4f}V, "
                  "DIBL={:.4f}V, WPE={:.4f}V, Temp={:.4f}V → Vth={:.4f}V",
                  name_, params_.vth0, vth_body, dvth_sce, dvth_dibl, dvth_wpe, dvth_temp, vth);

    return vth;
}

//==============================================================================
// BSIM4.8.3 DC Model - Drain Current
//==============================================================================

template<Polarity P>
Real MOSFET_BSIM4<P>::compute_mobility(Real vgs, Real vth, Real weff, Real leff) const {
    Real vgs_eff = vgs - vth;
    if (vgs_eff <= 0.0) {
        return params_.u0;
    }

    Real delta_temp = temperature_ - params_.tnom;
    Real temp_ratio = temperature_ / params_.tnom;
    Real mu0_temp = params_.u0 * std::pow(temp_ratio, params_.ute);

    Real ua_temp = params_.ua + params_.ua1 * delta_temp;
    Real ub_temp = params_.ub + params_.ub1 * delta_temp;
    Real uc_temp = params_.uc + params_.uc1 * delta_temp;

    Real e_eff = vgs_eff / params_.toxe;

    Real denom = 1.0 + ua_temp * e_eff + ub_temp * e_eff * e_eff;
    Real mu_eff = mu0_temp / denom;

    if (uc_temp > 0.0) {
        mu_eff *= (1.0 - uc_temp * vgs_eff / params_.vth0);
        if (mu_eff < mu0_temp * 0.1) {
            mu_eff = mu0_temp * 0.1;
        }
    }

    spdlog::trace("{}: Mobility: μ0={:.3e}, μ0(T)={:.3e}, Eeff={:.3e}V/m, μeff={:.3e}m²/V·s",
                  name_, params_.u0, mu0_temp, e_eff, mu_eff);

    return mu_eff;
}

template<Polarity P>
Real MOSFET_BSIM4<P>::compute_drain_current(Real vgs, Real vds, Real vbs, Real vth) const {
    const Real kb = 1.380649e-23;
    const Real q = 1.602176634e-19;
    const Real T = temperature_;
    const Real Vt = kb * T / q;

    Real weff, leff;
    compute_effective_dimensions(weff, leff);

    Real vgs_eff = vgs - vth;

    // Subthreshold region
    if (vgs_eff < 0.0) {
        Real n = params_.nfactor;
        if (n < 1.0) n = 1.0;

        Real voff = params_.voff;
        Real arg = (vgs_eff - voff) / (n * Vt);

        if (arg > 20.0) arg = 20.0;
        if (arg < -20.0) arg = -20.0;

        const Real epsilon_ox = 3.9 * 8.854e-12;
        Real cox = epsilon_ox / params_.toxe;
        Real i0 = 2.0 * n * Vt * Vt * cox * weff / leff;

        Real ids_sub = i0 * std::exp(arg);

        Real vds_eff = vds;
        if (vds_eff > 2.0 * n * Vt) {
            vds_eff = 2.0 * n * Vt;
        }
        ids_sub *= (1.0 - std::exp(-vds_eff / (n * Vt)));

        spdlog::trace("{}: Subthreshold: Vgs_eff={:.4f}V, Ids={:.3e}A", name_, vgs_eff, ids_sub);

        return ids_sub;
    }

    // Strong inversion
    Real mu_eff = compute_mobility(vgs, vth, weff, leff);

    const Real epsilon_ox = 3.9 * 8.854e-12;
    Real cox = epsilon_ox / params_.toxe;
    Real beta = mu_eff * cox * weff / leff;

    Real delta_temp = T - params_.tnom;
    Real vsat_temp = params_.vsat - params_.at * delta_temp;
    if (vsat_temp < 0.0) vsat_temp = 0.0;

    Real vdsat = vgs_eff;
    if (vsat_temp > 0.0) {
        Real vsat_l = leff * vsat_temp / mu_eff;
        if (vsat_l > 0.0) {
            vdsat = vgs_eff / (1.0 + vgs_eff / vsat_l);
        }
    }

    if (vdsat < 0.0) vdsat = 0.0;

    Real ids = 0.0;

    if (vds < vdsat) {
        ids = beta * (vgs_eff * vds - 0.5 * vds * vds);
        spdlog::trace("{}: Linear: Vgs_eff={:.4f}V, Vds={:.4f}V, Vdsat={:.4f}V, Ids={:.3e}A",
                      name_, vgs_eff, vds, vdsat, ids);
    } else {
        ids = 0.5 * beta * vdsat * vdsat;

        if (params_.pclm > 0.0 && leff > 0.0) {
            Real lambda = params_.pclm / leff;
            Real clm_factor = 1.0 + lambda * (vds - vdsat);
            if (clm_factor < 0.0) clm_factor = 0.0;
            ids *= clm_factor;
        }

        spdlog::trace("{}: Saturation: Vgs_eff={:.4f}V, Vds={:.4f}V, Vdsat={:.4f}V, Ids={:.3e}A",
                      name_, vgs_eff, vds, vdsat, ids);
    }

    if (ids < 0.0) ids = 0.0;

    return ids;
}

//==============================================================================
// Capacitance Models
//==============================================================================

template<Polarity P>
void MOSFET_BSIM4<P>::compute_gate_capacitances(Real vgs, Real vds, Real vbs, Real vth,
                                                  Real& cgs, Real& cgd, Real& cgb) const {
    // Use BSIM4Core for gate capacitances
    Real vg = vgs;  // Simplified: assume vs = 0
    Real vd = vds;
    Real vs = 0.0;
    Real vb = vbs;

    const BSIM4Capacitances caps = core_.compute_capacitances(vg, vd, vs, vb, vth, temperature_);
    cgs = caps.cgs;
    cgd = caps.cgd;
    cgb = caps.cgb;
}

template<Polarity P>
void MOSFET_BSIM4<P>::compute_junction_capacitances(Real vbs, Real vbd,
                                                      Real& cbd, Real& cbs) const {
    // Use BSIM4Core for junction capacitances
    Real vg = 0.0;
    Real vd = -vbd;  // vbd = vb - vd → vd = vb - vbd
    Real vs = -vbs;  // vbs = vb - vs → vs = vb - vbs
    Real vb = 0.0;

    const BSIM4Capacitances caps = core_.compute_capacitances(vg, vd, vs, vb, 0.0, temperature_);
    cbd = caps.cbd;
    cbs = caps.cbs;
}

//==============================================================================
// Noise Analysis
//==============================================================================

template<Polarity P>
std::vector<NoiseSource> MOSFET_BSIM4<P>::get_noise_sources(const DenseVector& dc_solution,
                                                              Real frequency) const {
    constexpr Real k_boltzmann = 1.380649e-23;

    std::vector<NoiseSource> sources;

    const int nd_idx = (drain_ == NODE_GND) ? -1 : static_cast<int>(drain_ - 1);
    const int ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
    const int ns_idx = (source_ == NODE_GND) ? -1 : static_cast<int>(source_ - 1);
    const int nb_idx = (bulk_ == NODE_GND) ? -1 : static_cast<int>(bulk_ - 1);

    Real vd = (nd_idx >= 0) ? dc_solution[nd_idx] : 0.0;
    Real vg = (ng_idx >= 0) ? dc_solution[ng_idx] : 0.0;
    Real vs = (ns_idx >= 0) ? dc_solution[ns_idx] : 0.0;
    Real vb = (nb_idx >= 0) ? dc_solution[nb_idx] : 0.0;

    Real vgs = vg - vs;
    Real vds = vd - vs;
    Real vbs = vb - vs;

    Real vth = compute_threshold_voltage(vds, vbs);
    Real ids = compute_drain_current(vgs, vds, vbs, vth);

    // Compute gm via numerical differentiation
    const Real delta = 1e-6;
    Real vth_plus = compute_threshold_voltage(vds, vbs);
    Real ids_plus = compute_drain_current(vgs + delta, vds, vbs, vth_plus);
    Real gm = (ids_plus - ids) / delta;

    constexpr Real gm_threshold = 1e-15;

    // Channel thermal noise
    if (gm > gm_threshold) {
        constexpr Real thermal_coeff = 8.0 / 3.0;
        Real I_n_th_squared = 4.0 * k_boltzmann * temperature_ * thermal_coeff * gm;

        NoiseSource thermal;
        thermal.name = name_ + "_thermal";
        thermal.node_p = drain_;
        thermal.node_n = source_;
        thermal.spectral_density = I_n_th_squared;
        thermal.is_current = true;

        sources.push_back(thermal);

        spdlog::trace("{} {} channel thermal noise: I_n² = {:.3e} A²/Hz (g_m = {:.3e} S, T = {:.2f}K)",
                     polarity_name(), name_, I_n_th_squared, gm, temperature_);
    }

    // Flicker noise
    constexpr Real ids_threshold = 1e-15;

    if (params_.kf > 0.0 && std::abs(ids) > ids_threshold && frequency > 0.0) {
        const Real epsilon_ox = 3.9 * 8.854e-12;
        Real cox = epsilon_ox / params_.toxe;

        Real weff, leff;
        compute_effective_dimensions(weff, leff);

        Real cox_total = cox * leff * weff;

        Real I_n_f_squared = params_.kf * std::pow(std::abs(ids), params_.af) /
                            (cox_total * std::pow(frequency, params_.ef));

        NoiseSource flicker;
        flicker.name = name_ + "_flicker";
        flicker.node_p = drain_;
        flicker.node_n = source_;
        flicker.spectral_density = I_n_f_squared;
        flicker.is_current = true;

        sources.push_back(flicker);

        spdlog::trace("{} {} flicker noise: I_n² = {:.3e} A²/Hz (I_D = {:.3e} A, f = {:.3e} Hz, "
                     "K_F = {:.3e}, A_F = {:.2f}, E_F = {:.2f})",
                     polarity_name(), name_, I_n_f_squared, ids, frequency, params_.kf, params_.af, params_.ef);
    }

    return sources;
}

//==============================================================================
// Explicit Template Instantiations
//==============================================================================

template class MOSFET_BSIM4<Polarity::N>;
template class MOSFET_BSIM4<Polarity::P>;

} // namespace kirimaspice::devices
