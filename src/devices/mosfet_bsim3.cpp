// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/mosfet_bsim3.cpp
// Unified MOSFET BSIM3v3.3 template implementation
// Reference: BSIM3v3.3 User's Manual (UC Berkeley, 2005)

#include "kirimaspice/devices/mosfet_bsim3.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include "kirimaspice/core/constants.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace kirimaspice::devices {

//==============================================================================
// Constructor
//==============================================================================

template<Polarity P>
MOSFET_BSIM3<P>::MOSFET_BSIM3(DeviceId id, const std::string& name,
                               NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                               const BSIM3Params& params)
    : Device(id, name, device_type),
      nodes_{drain, gate, source, bulk},
      params_(params),
      core_(params_) {

    // Validate parameters
    if (!params_.validate()) {
        throw std::invalid_argument(
            fmt::format("Invalid BSIM3 parameters for MOSFET {}: check TOX, W, L, U0, VSAT, TNOM",
                       name));
    }

    // Log BSIM3 device creation
    if constexpr (P == Polarity::N) {
        spdlog::info("Created BSIM3 N-channel MOSFET: {} (D={}, G={}, S={}, B={}), "
                     "W={:.3e}m, L={:.3e}m, TOX={:.3e}m, VTH0={:.3f}V",
                     name, drain, gate, source, bulk,
                     params_.w, params_.l, params_.tox, params_.vth0);
    } else {
        spdlog::info("Created BSIM3 P-channel MOSFET: {} (D={}, G={}, S={}, B={}), "
                     "W={:.3e}m, L={:.3e}m, TOX={:.3e}m, VTH0={:.3f}V",
                     name, drain, gate, source, bulk,
                     params_.w, params_.l, params_.tox, params_.vth0);

        // For P-channel, vth0 should be negative
        if (params_.vth0 > 0.0) {
            spdlog::warn("BSIM3 P-channel {} has positive vth0={:.3f}V, should be negative",
                         name, params_.vth0);
        }
    }
}

//==============================================================================
// DC Analysis
//==============================================================================

template<Polarity P>
void MOSFET_BSIM3<P>::stamp_dc_linear(MNASystem& mna,
                                       const DenseVector& node_voltages) const {
    // BSIM3 is nonlinear - only add gmin for numerical stability
    stamp_gmin(mna);
}

template<Polarity P>
void MOSFET_BSIM3<P>::stamp_dc_nonlinear(MNASystem& mna,
                                          const DenseVector& node_voltages) const {
    // Nodes: [drain, gate, source, bulk]
    NodeId d = nodes_[0];
    NodeId g = nodes_[1];
    NodeId s = nodes_[2];
    NodeId b = nodes_[3];

    // Extract DC voltages
    Real vd = (d == 0) ? 0.0 : node_voltages[d - 1];
    Real vg = (g == 0) ? 0.0 : node_voltages[g - 1];
    Real vs = (s == 0) ? 0.0 : node_voltages[s - 1];
    Real vb = (b == 0) ? 0.0 : node_voltages[b - 1];

    // Convert to effective voltages (NMOS-equivalent for core evaluation)
    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    // Calculate drain current using BSIM3 model (always positive magnitude)
    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);

    // Calculate small-signal parameters
    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gds = get_output_conductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gmb = get_bulk_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);

    if constexpr (P == Polarity::N) {
        // N-channel: Current flows from drain to source
        // Newton-Raphson linearization: I = Ieq + g*(V - Vold)
        Real vgs = vg - vs;
        Real vds = vd - vs;
        Real vbs = vb - vs;
        Real ieq_d = ids - gm * vgs - gds * vds - gmb * vbs;

        // Drain node (positive current flows out of drain)
        if (d != 0) {
            mna.G.add_entry(d - 1, d - 1, gds);
            if (g != 0) mna.G.add_entry(d - 1, g - 1, gm);
            if (s != 0) mna.G.add_entry(d - 1, s - 1, -gm - gds - gmb);
            if (b != 0) mna.G.add_entry(d - 1, b - 1, gmb);
            mna.b[d - 1] -= ieq_d;
        }

        // Source node (current flows into source)
        if (s != 0) {
            mna.G.add_entry(s - 1, s - 1, gm + gds + gmb);
            if (d != 0) mna.G.add_entry(s - 1, d - 1, -gds);
            if (g != 0) mna.G.add_entry(s - 1, g - 1, -gm);
            if (b != 0) mna.G.add_entry(s - 1, b - 1, -gmb);
            mna.b[s - 1] += ieq_d;
        }
    } else {
        // P-channel: Current flows from source to drain (vsg, vsd, vsb convention)
        // The effective voltages already represent vsg, vsd, vsb
        Real vsg = v_gs_eff;  // vs - vg
        Real vsd = v_ds_eff;  // vs - vd

        // Linearized current: I_lin = ids - gm×Vg + gds×(Vs-Vd) - gmb×Vs
        Real i_linear = ids - gm * vg + gds * (vs - vd) - gmb * vs;

        // Stamp currents (source → drain flow)
        if (s != 0) mna.b[s - 1] -= i_linear;
        if (d != 0) mna.b[d - 1] += i_linear;

        // Stamp conductances
        if (s != 0) mna.G.add_entry(s - 1, s - 1, gds + gmb);
        if (d != 0) mna.G.add_entry(d - 1, d - 1, gds);
        if (s != 0 && d != 0) {
            mna.G.add_entry(s - 1, d - 1, -gds);
            mna.G.add_entry(d - 1, s - 1, -gds);
        }
        if (s != 0 && g != 0) mna.G.add_entry(s - 1, g - 1, gm);
        if (d != 0 && g != 0) mna.G.add_entry(d - 1, g - 1, -gm);
        if (s != 0 && b != 0) mna.G.add_entry(s - 1, b - 1, -gmb);
    }
}

//==============================================================================
// Transient Analysis
//==============================================================================

template<Polarity P>
void MOSFET_BSIM3<P>::stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                                       Real timestep, IntegrationMethod method,
                                       const BDFCoefficients* bdf_coef,
                                       const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // First stamp DC nonlinear contribution
    stamp_dc_nonlinear(mna, node_voltages);

    NodeId d = nodes_[0];
    NodeId g = nodes_[1];
    NodeId s = nodes_[2];
    NodeId b = nodes_[3];

    // Extract voltages
    Real vd = (d == 0) ? 0.0 : node_voltages[d - 1];
    Real vg = (g == 0) ? 0.0 : node_voltages[g - 1];
    Real vs = (s == 0) ? 0.0 : node_voltages[s - 1];
    Real vb = (b == 0) ? 0.0 : node_voltages[b - 1];

    // The voltages across the capacitances, in the order they are stamped
    // (first node minus second), for both polarities; the capacitance values
    // follow from the effective voltages
    const Real vgs = vg - vs;
    const Real vgd = vg - vd;
    const Real vgb = vg - vb;
    const Real vbd = vb - vd;
    const Real vbs = vb - vs;

    // Get effective voltages for capacitance calculation
    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    // Get capacitances
    Real cgs, cgd, cgb;
    get_gate_capacitances(v_gs_eff, v_ds_eff, v_bs_eff, cgs, cgd, cgb);

    Real cbd, cbs;
    Real v_bd_eff = (P == Polarity::N) ? vbd : -vbd;  // Transform for junction cap
    Real v_bs_eff_junc = (P == Polarity::N) ? vbs : -vbs;
    get_junction_capacitances(v_bd_eff, v_bs_eff_junc, cbd, cbs);

    // The companion of each capacitance (device_utils::capacitor_companion)
    cap_dt_ = timestep;
    cap_method_ = method;
    cap_bdf_ = bdf_coef ? std::optional<BDFCoefficients>(*bdf_coef) : std::nullopt;
    (void)tr_bdf2_coef;
    auto calc_companion = [&](Real cap, Real v_prev, Real v_prev2, Real v_prev3,
                              Real i_prev) -> std::pair<Real, Real> {
        const auto cc = device_utils::capacitor_companion(method, cap, timestep, v_prev, v_prev2,
                                                          v_prev3, i_prev, bdf_coef);
        return {cc.g, cc.i_eq};
    };

    // Stamp capacitor helper
    auto stamp_capacitor = [&](NodeId n1, NodeId n2, Real g_eq, Real i_eq) {
        if (g_eq <= 0.0) return;

        if (n1 != 0) {
            mna.G.add_entry(n1 - 1, n1 - 1, g_eq);
            if (n2 != 0) mna.G.add_entry(n1 - 1, n2 - 1, -g_eq);
            mna.b[n1 - 1] -= i_eq;
        }
        if (n2 != 0) {
            mna.G.add_entry(n2 - 1, n2 - 1, g_eq);
            if (n1 != 0) mna.G.add_entry(n2 - 1, n1 - 1, -g_eq);
            mna.b[n2 - 1] += i_eq;
        }
    };

    // Calculate and stamp gate capacitances
    (void)vgs;
    (void)vgd;
    (void)vgb;
    auto [g_gs, i_gs] = calc_companion(cgs, vgs_prev_, vgs_prev2_, vgs_prev3_, icap_prev_[0]);
    auto [g_gd, i_gd] = calc_companion(cgd, vgd_prev_, vgd_prev2_, vgd_prev3_, icap_prev_[1]);
    auto [g_gb, i_gb] = calc_companion(cgb, vgb_prev_, vgb_prev2_, vgb_prev3_, icap_prev_[2]);

    stamp_capacitor(g, s, g_gs, i_gs);
    stamp_capacitor(g, d, g_gd, i_gd);
    stamp_capacitor(g, b, g_gb, i_gb);

    // Calculate and stamp junction capacitances
    auto [g_bd, i_bd] = calc_companion(cbd, vbd_prev_, vbd_prev2_, vbd_prev3_, icap_prev_[3]);
    auto [g_bs, i_bs] = calc_companion(cbs, vbs_prev_, vbs_prev2_, vbs_prev3_, icap_prev_[4]);

    stamp_capacitor(b, d, g_bd, i_bd);
    stamp_capacitor(b, s, g_bs, i_bs);

    // Store integration state
    timestep_ = timestep;
    last_method_ = method;
}

//==============================================================================
// AC Analysis
//==============================================================================

template<Polarity P>
void MOSFET_BSIM3<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                                const DenseVector& dc_solution) const {
    NodeId d = nodes_[0];
    NodeId g = nodes_[1];
    NodeId s = nodes_[2];
    NodeId b = nodes_[3];

    // Extract DC voltages
    Real vd = (d == 0) ? 0.0 : dc_solution[d - 1];
    Real vg = (g == 0) ? 0.0 : dc_solution[g - 1];
    Real vs = (s == 0) ? 0.0 : dc_solution[s - 1];
    Real vb = (b == 0) ? 0.0 : dc_solution[b - 1];

    // Get effective voltages
    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    // Calculate small-signal parameters at DC operating point
    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gds = get_output_conductance(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gmb = get_bulk_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);

    // Get capacitances
    Real cgs, cgd, cgb;
    get_gate_capacitances(v_gs_eff, v_ds_eff, v_bs_eff, cgs, cgd, cgb);

    Real cbd, cbs;
    Real v_bd_eff = (P == Polarity::N) ? (vb - vd) : (vd - vb);
    Real v_bs_eff_junc = (P == Polarity::N) ? (vb - vs) : (vs - vb);
    get_junction_capacitances(v_bd_eff, v_bs_eff_junc, cbd, cbs);

    // Complex admittances
    std::complex<Real> jw(0.0, omega);
    std::complex<Real> y_gs = jw * cgs;
    std::complex<Real> y_gd = jw * cgd;
    std::complex<Real> y_gb = jw * cgb;
    std::complex<Real> y_bd = jw * cbd;
    std::complex<Real> y_bs = jw * cbs;

    // Stamp transconductance and output conductance
    if constexpr (P == Polarity::N) {
        // N-channel AC stamp
        if (d != 0) {
            sys.Y.add_entry(d - 1, d - 1, std::complex<Real>(gds, 0));
            if (g != 0) sys.Y.add_entry(d - 1, g - 1, std::complex<Real>(gm, 0));
            if (s != 0) sys.Y.add_entry(d - 1, s - 1, std::complex<Real>(-gm - gds - gmb, 0));
            if (b != 0) sys.Y.add_entry(d - 1, b - 1, std::complex<Real>(gmb, 0));
        }
        if (s != 0) {
            sys.Y.add_entry(s - 1, s - 1, std::complex<Real>(gm + gds + gmb, 0));
            if (d != 0) sys.Y.add_entry(s - 1, d - 1, std::complex<Real>(-gds, 0));
            if (g != 0) sys.Y.add_entry(s - 1, g - 1, std::complex<Real>(-gm, 0));
            if (b != 0) sys.Y.add_entry(s - 1, b - 1, std::complex<Real>(-gmb, 0));
        }
    } else {
        // P-channel AC stamp (source-drain current flow)
        if (s != 0) {
            sys.Y.add_entry(s - 1, s - 1, std::complex<Real>(gds + gmb, 0));
            if (g != 0) sys.Y.add_entry(s - 1, g - 1, std::complex<Real>(gm, 0));
        }
        if (d != 0) {
            sys.Y.add_entry(d - 1, d - 1, std::complex<Real>(gds, 0));
            if (g != 0) sys.Y.add_entry(d - 1, g - 1, std::complex<Real>(-gm, 0));
        }
        if (s != 0 && d != 0) {
            sys.Y.add_entry(s - 1, d - 1, std::complex<Real>(-gds, 0));
            sys.Y.add_entry(d - 1, s - 1, std::complex<Real>(-gds, 0));
        }
        if (s != 0 && b != 0) sys.Y.add_entry(s - 1, b - 1, std::complex<Real>(-gmb, 0));
    }

    // Stamp capacitive admittances (same for both polarities)
    auto stamp_y = [&](NodeId n1, NodeId n2, std::complex<Real> y) {
        if (n1 != 0) {
            sys.Y.add_entry(n1 - 1, n1 - 1, y);
            if (n2 != 0) sys.Y.add_entry(n1 - 1, n2 - 1, -y);
        }
        if (n2 != 0) {
            sys.Y.add_entry(n2 - 1, n2 - 1, y);
            if (n1 != 0) sys.Y.add_entry(n2 - 1, n1 - 1, -y);
        }
    };

    stamp_y(g, s, y_gs);
    stamp_y(g, d, y_gd);
    stamp_y(g, b, y_gb);
    stamp_y(b, d, y_bd);
    stamp_y(b, s, y_bs);
}

//==============================================================================
// State Management
//==============================================================================

template<Polarity P>
void MOSFET_BSIM3<P>::save_state() const {
    vgs_saved_ = vgs_prev_;
    vgs_saved2_ = vgs_prev2_;
    vgs_saved3_ = vgs_prev3_;
    vgd_saved_ = vgd_prev_;
    vgd_saved2_ = vgd_prev2_;
    vgd_saved3_ = vgd_prev3_;
    vgb_saved_ = vgb_prev_;
    vgb_saved2_ = vgb_prev2_;
    vgb_saved3_ = vgb_prev3_;
    vbd_saved_ = vbd_prev_;
    vbd_saved2_ = vbd_prev2_;
    vbd_saved3_ = vbd_prev3_;
    vbs_saved_ = vbs_prev_;
    vbs_saved2_ = vbs_prev2_;
    vbs_saved3_ = vbs_prev3_;
    charge_gs_saved_ = charge_gs_prev_;
    charge_gd_saved_ = charge_gd_prev_;
    charge_gb_saved_ = charge_gb_prev_;
    charge_bd_saved_ = charge_bd_prev_;
    charge_bs_saved_ = charge_bs_prev_;
}

template<Polarity P>
void MOSFET_BSIM3<P>::restore_state() const {
    vgs_prev_ = vgs_saved_;
    vgs_prev2_ = vgs_saved2_;
    vgs_prev3_ = vgs_saved3_;
    vgd_prev_ = vgd_saved_;
    vgd_prev2_ = vgd_saved2_;
    vgd_prev3_ = vgd_saved3_;
    vgb_prev_ = vgb_saved_;
    vgb_prev2_ = vgb_saved2_;
    vgb_prev3_ = vgb_saved3_;
    vbd_prev_ = vbd_saved_;
    vbd_prev2_ = vbd_saved2_;
    vbd_prev3_ = vbd_saved3_;
    vbs_prev_ = vbs_saved_;
    vbs_prev2_ = vbs_saved2_;
    vbs_prev3_ = vbs_saved3_;
    charge_gs_prev_ = charge_gs_saved_;
    charge_gd_prev_ = charge_gd_saved_;
    charge_gb_prev_ = charge_gb_saved_;
    charge_bd_prev_ = charge_bd_saved_;
    charge_bs_prev_ = charge_bs_saved_;
}

template<Polarity P>
void MOSFET_BSIM3<P>::accept_solution(const DenseVector& node_voltages) {
    NodeId d = nodes_[0];
    NodeId g = nodes_[1];
    NodeId s = nodes_[2];
    NodeId b = nodes_[3];

    Real vd = core::accepted_node_voltage(node_voltages, d);
    Real vg = core::accepted_node_voltage(node_voltages, g);
    Real vs = core::accepted_node_voltage(node_voltages, s);
    Real vb = core::accepted_node_voltage(node_voltages, b);

    // The voltages across the capacitances, as stamp_transient takes them
    const Real vgs_new = vg - vs;
    const Real vgd_new = vg - vd;
    const Real vgb_new = vg - vb;
    const Real vbd_new = vb - vd;
    const Real vbs_new = vb - vs;

    // The capacitances' currents at this point, with the companion of the
    // step just accepted (none at the operating point)
    {
        Real ve_gs, ve_ds, ve_bs;
        compute_effective_voltages(vd, vg, vs, vb, ve_gs, ve_ds, ve_bs);
        Real c_gs, c_gd, c_gb, c_bd, c_bs;
        get_gate_capacitances(ve_gs, ve_ds, ve_bs, c_gs, c_gd, c_gb);
        get_junction_capacitances((P == Polarity::N) ? vbd_new : -vbd_new,
                                  (P == Polarity::N) ? vbs_new : -vbs_new, c_bd, c_bs);
        const Real c[5] = {c_gs, c_gd, c_gb, c_bd, c_bs};
        const Real v_new[5] = {vgs_new, vgd_new, vgb_new, vbd_new, vbs_new};
        const Real v1[5] = {vgs_prev_, vgd_prev_, vgb_prev_, vbd_prev_, vbs_prev_};
        const Real v2[5] = {vgs_prev2_, vgd_prev2_, vgb_prev2_, vbd_prev2_, vbs_prev2_};
        const Real v3[5] = {vgs_prev3_, vgd_prev3_, vgb_prev3_, vbd_prev3_, vbs_prev3_};
        const BDFCoefficients* bdf = cap_bdf_ ? &*cap_bdf_ : nullptr;
        for (int k = 0; k < 5; ++k) {
            const auto cc = device_utils::capacitor_companion(cap_method_, c[k], cap_dt_, v1[k], v2[k],
                                                              v3[k], icap_prev_[k], bdf);
            icap_prev_[k] = (cap_dt_ > 0.0) ? cc.g * v_new[k] + cc.i_eq : 0.0;
        }
    }

    // Shift history
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

    // Calculate charges for Physical LTE
    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    Real cgs, cgd, cgb;
    get_gate_capacitances(v_gs_eff, v_ds_eff, v_bs_eff, cgs, cgd, cgb);

    Real cbd, cbs;
    Real v_bd_eff = (P == Polarity::N) ? vbd_new : -vbd_new;
    Real v_bs_eff_junc = (P == Polarity::N) ? vbs_new : -vbs_new;
    get_junction_capacitances(v_bd_eff, v_bs_eff_junc, cbd, cbs);

    charge_gs_prev_ = charge_gs_;
    charge_gd_prev_ = charge_gd_;
    charge_gb_prev_ = charge_gb_;
    charge_bd_prev_ = charge_bd_;
    charge_bs_prev_ = charge_bs_;

    charge_gs_ = cgs * vgs_new;
    charge_gd_ = cgd * vgd_new;
    charge_gb_ = cgb * vgb_new;
    charge_bd_ = cbd * vbd_new;
    charge_bs_ = cbs * vbs_new;

    first_accept_ = false;
}

//==============================================================================
// Physical LTE
//==============================================================================

template<Polarity P>
Real MOSFET_BSIM3<P>::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    if (first_accept_) {
        return 0.0;
    }

    // Calculate charge changes
    auto calc_lte = [&](Real q_now, Real q_prev, Real cap) -> Real {
        Real dq = std::abs(q_now - q_prev);
        Real q_scale = std::max(std::abs(q_now), std::abs(cap * 0.1)) + 1e-18;
        return dq / (chgtol + q_scale);
    };

    NodeId d = nodes_[0];
    NodeId g = nodes_[1];
    NodeId s = nodes_[2];
    NodeId b = nodes_[3];

    Real vd = (d == 0) ? 0.0 : node_voltages[d - 1];
    Real vg = (g == 0) ? 0.0 : node_voltages[g - 1];
    Real vs = (s == 0) ? 0.0 : node_voltages[s - 1];
    Real vb = (b == 0) ? 0.0 : node_voltages[b - 1];

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    Real cgs, cgd, cgb;
    get_gate_capacitances(v_gs_eff, v_ds_eff, v_bs_eff, cgs, cgd, cgb);

    Real cbd, cbs;
    Real vbd = (P == Polarity::N) ? (vb - vd) : (vd - vb);
    Real vbs = (P == Polarity::N) ? (vb - vs) : (vs - vb);
    get_junction_capacitances(vbd, vbs, cbd, cbs);

    Real lte_gs = calc_lte(charge_gs_, charge_gs_prev_, cgs);
    Real lte_gd = calc_lte(charge_gd_, charge_gd_prev_, cgd);
    Real lte_gb = calc_lte(charge_gb_, charge_gb_prev_, cgb);
    Real lte_bd = calc_lte(charge_bd_, charge_bd_prev_, cbd);
    Real lte_bs = calc_lte(charge_bs_, charge_bs_prev_, cbs);

    return std::sqrt(lte_gs * lte_gs + lte_gd * lte_gd + lte_gb * lte_gb +
                     lte_bd * lte_bd + lte_bs * lte_bs);
}

//==============================================================================
// Parameter Access
//==============================================================================

template<Polarity P>
std::optional<Real> MOSFET_BSIM3<P>::get_parameter(const std::string& param_name) const {
    std::string lower_name = param_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    if (lower_name == "w" || lower_name == "width") return params_.w;
    if (lower_name == "l" || lower_name == "length") return params_.l;
    if (lower_name == "vth0") return params_.vth0;
    if (lower_name == "u0") return params_.u0;
    if (lower_name == "tox") return params_.tox;
    if (lower_name == "vsat") return params_.vsat;

    return std::nullopt;
}

template<Polarity P>
bool MOSFET_BSIM3<P>::set_parameter(const std::string& param_name, Real value) {
    std::string lower_name = param_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    if (lower_name == "w" || lower_name == "width") {
        if (value <= 0) return false;
        params_.w = value;
        return true;
    }
    if (lower_name == "l" || lower_name == "length") {
        if (value <= 0) return false;
        params_.l = value;
        return true;
    }
    if (lower_name == "vth0") {
        params_.vth0 = value;
        return true;
    }

    return false;
}

template<Polarity P>
std::vector<std::string> MOSFET_BSIM3<P>::get_perturbable_parameters() const {
    return {"W", "L", "VTH0", "U0", "TOX", "VSAT"};
}

//==============================================================================
// Helper Functions
//==============================================================================

template<Polarity P>
void MOSFET_BSIM3<P>::stamp_gmin(MNASystem& mna) const {
    NodeId d = nodes_[0];
    NodeId s = nodes_[2];

    if (d != 0) mna.G.add_entry(d - 1, d - 1, gmin_default);
    if (s != 0) mna.G.add_entry(s - 1, s - 1, gmin_default);
    if (d != 0 && s != 0) {
        mna.G.add_entry(d - 1, s - 1, -gmin_default);
        mna.G.add_entry(s - 1, d - 1, -gmin_default);
    }
}

template<Polarity P>
Real MOSFET_BSIM3<P>::calculate_vth(Real v_bs_eff, Real leff, Real weff) const {
    // BSIM3 threshold voltage calculation
    Real vth0 = params_.vth0;
    Real k1 = params_.k1;
    Real k2 = params_.k2;
    Real k3 = params_.k3;

    // Body effect
    Real phi_s = 2.0 * vt_ * std::log(params_.nch / 1.45e10);
    Real sqrt_phi = std::sqrt(std::max(phi_s, 0.01));
    Real sqrt_phi_vbs = std::sqrt(std::max(phi_s - v_bs_eff, 0.01));
    Real delta_vth_body = k1 * (sqrt_phi_vbs - sqrt_phi) + k2 * v_bs_eff;

    // Short channel effect (simplified)
    Real dvt0 = params_.dvt0;
    Real lt0 = std::sqrt(eps_si * params_.tox / eps_ox) * weff;
    Real theta_sce = dvt0 * std::exp(-leff / (2.0 * lt0));
    Real delta_vth_sce = -theta_sce * (vth0 + k1 * sqrt_phi);

    return vth0 + delta_vth_body + delta_vth_sce;
}

template<Polarity P>
Real MOSFET_BSIM3<P>::calculate_mobility(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff,
                                          Real vth, Real leff, Real weff) const {
    Real u0 = apply_temp_mobility(params_.u0);
    Real ua = params_.ua;
    Real ub = params_.ub;
    Real uc = params_.uc;

    Real vgst = v_gs_eff - vth;
    if (vgst < 0.1) vgst = 0.1;

    Real eeff = vgst / params_.tox;
    Real mobility_factor = 1.0 + ua * eeff + ub * eeff * eeff + uc * v_bs_eff;
    if (mobility_factor < 0.1) mobility_factor = 0.1;

    return u0 / mobility_factor;
}

template<Polarity P>
Real MOSFET_BSIM3<P>::calculate_vdsat(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff, Real vth) const {
    Real vgst = v_gs_eff - vth;
    if (vgst < 0.0) return 0.0;

    Real vsat = apply_temp_vsat(params_.vsat);
    Real leff = calculate_leff();

    // Velocity saturation
    Real mu = calculate_mobility(v_gs_eff, v_ds_eff, v_bs_eff, vth, leff, calculate_weff());
    Real esat = 2.0 * vsat / (mu * 1.0e-4); // Convert mu to m^2/Vs
    Real vdsat_vsat = vgst / (1.0 + vgst / (esat * leff));

    return vdsat_vsat;
}

template<Polarity P>
Real MOSFET_BSIM3<P>::calculate_dibl(Real v_ds_eff, Real leff) const {
    Real eta0 = params_.eta0;
    Real dsub = params_.dsub;

    Real dibl = eta0 * v_ds_eff + dsub * v_ds_eff * v_ds_eff / leff;
    return std::max(dibl, 0.0);
}

template<Polarity P>
Real MOSFET_BSIM3<P>::calculate_clm_factor(Real v_ds_eff, Real vdsat) const {
    Real pclm = params_.pclm;
    Real delta_vds = v_ds_eff - vdsat;

    if (delta_vds <= 0.0) return 1.0;

    Real clm = 1.0 + pclm * delta_vds / std::max(vdsat, 0.1);
    return std::max(clm, 1.0);
}

template<Polarity P>
Real MOSFET_BSIM3<P>::apply_temp_mobility(Real u0_base) const {
    Real ute = params_.ute;
    Real tnom = params_.tnom;
    Real temp_ratio = temp_ / tnom;

    return u0_base * std::pow(temp_ratio, ute);
}

template<Polarity P>
Real MOSFET_BSIM3<P>::apply_temp_vth(Real vth_base, Real v_bs_eff) const {
    Real kt1 = params_.kt1;
    Real kt2 = params_.kt2;
    Real tnom = params_.tnom;
    Real delta_t = temp_ - tnom;

    return vth_base + kt1 * delta_t + kt2 * v_bs_eff * delta_t;
}

template<Polarity P>
Real MOSFET_BSIM3<P>::apply_temp_vsat(Real vsat_base) const {
    Real at = params_.at;
    Real tnom = params_.tnom;
    Real delta_t = temp_ - tnom;

    return vsat_base - at * delta_t;
}

//==============================================================================
// Drain Current and Conductances
//==============================================================================

template<Polarity P>
Real MOSFET_BSIM3<P>::get_drain_current(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    // Use BSIM3Core for evaluation (works with NMOS-equivalent voltages)
    BSIM3OperatingPoint op = core_.evaluate(v_gs_eff, v_ds_eff, v_bs_eff, temp_);
    return op.ids;  // Always returns positive magnitude
}

template<Polarity P>
Real MOSFET_BSIM3<P>::get_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real dv = 1e-6;
    Real ids_plus = get_drain_current(v_gs_eff + dv, v_ds_eff, v_bs_eff);
    Real ids_minus = get_drain_current(v_gs_eff - dv, v_ds_eff, v_bs_eff);
    return std::max((ids_plus - ids_minus) / (2.0 * dv), 1e-15);
}

template<Polarity P>
Real MOSFET_BSIM3<P>::get_output_conductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real dv = 1e-6;
    Real ids_plus = get_drain_current(v_gs_eff, v_ds_eff + dv, v_bs_eff);
    Real ids_minus = get_drain_current(v_gs_eff, v_ds_eff - dv, v_bs_eff);
    return std::max((ids_plus - ids_minus) / (2.0 * dv), 1e-15);
}

template<Polarity P>
Real MOSFET_BSIM3<P>::get_bulk_transconductance(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    constexpr Real dv = 1e-6;
    Real ids_plus = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff + dv);
    Real ids_minus = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff - dv);
    return std::max((ids_plus - ids_minus) / (2.0 * dv), 1e-15);
}

template<Polarity P>
typename MOSFET_BSIM3<P>::Region MOSFET_BSIM3<P>::get_region(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff) const {
    Real leff = calculate_leff();
    Real weff = calculate_weff();
    Real vth = calculate_vth(v_bs_eff, leff, weff);
    vth = apply_temp_vth(vth, v_bs_eff);

    Real vgst = v_gs_eff - vth;
    if (vgst <= 0.0) {
        return Region::CUTOFF;
    }

    Real vdsat = calculate_vdsat(v_gs_eff, v_ds_eff, v_bs_eff, vth);
    if (v_ds_eff < vdsat) {
        return Region::LINEAR;
    }

    return Region::SATURATION;
}

//==============================================================================
// Capacitances
//==============================================================================

template<Polarity P>
void MOSFET_BSIM3<P>::get_gate_capacitances(Real v_gs_eff, Real v_ds_eff, Real v_bs_eff,
                                             Real& cgs, Real& cgd, Real& cgb) const {
    Real leff = calculate_leff();
    Real weff = calculate_weff();

    // Oxide capacitance
    Real cox = eps_ox / params_.tox;
    Real cox_total = cox * leff * weff;

    // Overlap capacitances
    Real cgso = params_.cgso * weff;
    Real cgdo = params_.cgdo * weff;
    Real cgbo = params_.cgbo * leff;

    Real vth = calculate_vth(v_bs_eff, leff, weff);
    vth = apply_temp_vth(vth, v_bs_eff);
    Real vgst = v_gs_eff - vth;

    Region region = get_region(v_gs_eff, v_ds_eff, v_bs_eff);

    switch (region) {
        case Region::CUTOFF:
        case Region::SUBTHRESHOLD:
            // Cutoff: all capacitance to bulk
            cgs = cgso;
            cgd = cgdo;
            cgb = cox_total + cgbo;
            break;

        case Region::LINEAR:
            // Linear: 1/2 Cox to each of source and drain (Meyer model)
            cgs = 0.5 * cox_total + cgso;
            cgd = 0.5 * cox_total + cgdo;
            cgb = cgbo;
            break;

        case Region::SATURATION:
            // Saturation: 2/3 Cox to source, 0 to drain
            cgs = (2.0 / 3.0) * cox_total + cgso;
            cgd = cgdo;
            cgb = cgbo;
            break;
    }
}

template<Polarity P>
void MOSFET_BSIM3<P>::get_junction_capacitances(Real v_bd_eff, Real v_bs_eff,
                                                 Real& cbd, Real& cbs) const {
    Real weff = calculate_weff();
    Real leff = calculate_leff();

    // Junction parameters
    Real cj = params_.cj;
    Real mj = params_.mj;
    Real pb = params_.pb;
    Real cjsw = params_.cjsw;
    Real mjsw = params_.mjsw;
    Real pbsw = params_.pbsw;

    // Use instance parameters for Area and Perimeter, with fallback
    Real area_d = (params_.ad > 0.0) ? params_.ad : weff * leff * 0.5;
    Real area_s = (params_.as > 0.0) ? params_.as : weff * leff * 0.5;
    Real perim_d = (params_.pd > 0.0) ? params_.pd : 2.0 * (weff + leff * 0.5);
    Real perim_s = (params_.ps > 0.0) ? params_.ps : 2.0 * (weff + leff * 0.5);

    // Depletion capacitance model helper
    auto calc_term = [&](Real vj, Real c0, Real m, Real phi) -> Real {
        Real v_ratio = 1.0 - vj / phi;
        if (v_ratio < 0.1) v_ratio = 0.1;
        return c0 / std::pow(v_ratio, m);
    };

    Real cbd_area = calc_term(v_bd_eff, cj * area_d, mj, pb);
    Real cbd_perim = calc_term(v_bd_eff, cjsw * perim_d, mjsw, pbsw);
    cbd = std::max(cbd_area + cbd_perim, 1e-18);

    Real cbs_area = calc_term(v_bs_eff, cj * area_s, mj, pb);
    Real cbs_perim = calc_term(v_bs_eff, cjsw * perim_s, mjsw, pbsw);
    cbs = std::max(cbs_area + cbs_perim, 1e-18);
}

//==============================================================================
// Noise Sources
//==============================================================================

template<Polarity P>
std::vector<NoiseSource> MOSFET_BSIM3<P>::get_noise_sources(const DenseVector& dc_solution,
                                                             Real frequency) const {
    std::vector<NoiseSource> sources;

    NodeId d = nodes_[0];
    NodeId g = nodes_[1];
    NodeId s = nodes_[2];
    NodeId b = nodes_[3];

    Real vd = (d == 0) ? 0.0 : dc_solution[d - 1];
    Real vg = (g == 0) ? 0.0 : dc_solution[g - 1];
    Real vs = (s == 0) ? 0.0 : dc_solution[s - 1];
    Real vb = (b == 0) ? 0.0 : dc_solution[b - 1];

    Real v_gs_eff, v_ds_eff, v_bs_eff;
    compute_effective_voltages(vd, vg, vs, vb, v_gs_eff, v_ds_eff, v_bs_eff);

    Real ids = get_drain_current(v_gs_eff, v_ds_eff, v_bs_eff);
    Real gm = get_transconductance(v_gs_eff, v_ds_eff, v_bs_eff);

    // Channel thermal noise: I_n^2 = 4kT × (8/3) × gm
    Real thermal_coefficient = 8.0 / 3.0;
    Real thermal_psd = 4.0 * k_boltzmann * temp_ * thermal_coefficient * gm;

    NoiseSource thermal_noise;
    thermal_noise.name = name_ + "_thermal";
    thermal_noise.node_p = d;
    thermal_noise.node_n = s;
    thermal_noise.spectral_density = thermal_psd;
    thermal_noise.is_current = true;
    sources.push_back(thermal_noise);

    // Flicker noise: I_n^2 = KF × |ID|^AF / (Cox × Leff × Weff × f^EF)
    Real kf = params_.kf;
    Real af = params_.af;
    Real ef = params_.ef;

    if (kf > 0.0 && frequency > 0.0 && ids > 0.0) {
        Real leff = calculate_leff();
        Real weff = calculate_weff();
        Real cox = eps_ox / params_.tox;

        Real flicker_psd = kf * std::pow(std::abs(ids), af) /
                           (cox * leff * weff * std::pow(frequency, ef));

        NoiseSource flicker_noise;
        flicker_noise.name = name_ + "_flicker";
        flicker_noise.node_p = d;
        flicker_noise.node_n = s;
        flicker_noise.spectral_density = flicker_psd;
        flicker_noise.is_current = true;
        sources.push_back(flicker_noise);
    }

    return sources;
}

//==============================================================================
// Explicit Template Instantiation
//==============================================================================

template class MOSFET_BSIM3<Polarity::N>;
template class MOSFET_BSIM3<Polarity::P>;

} // namespace kirimaspice::devices
