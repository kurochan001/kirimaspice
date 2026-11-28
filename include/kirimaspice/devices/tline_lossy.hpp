// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/tline_lossy.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include <cmath>
#include <deque>
#include <vector>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

/// Lossy transmission line (O-element): R, L, C and G per unit length, and the
/// delay TD of the line without loss, which fixes its length TD / sqrt(L C).
/// DC and AC take the line's exact two-port (two_port). The transient takes
/// the line as M sections of the constant-parameter (Bergeron) line: each
/// a lossless line of Z0 = sqrt(L/C) and delay TD/M, by the method of
/// characteristics as the T line, with its loss lumped at its ends,
/// R length / (2M) in series and G length / (2M) in parallel at each end.
/// A junction of two sections meets nothing else, so its voltage follows
/// from the two waves arriving at it, which left TD/M earlier: no node is
/// added, and the step is no longer than TD/M. M grows with the loss, so
/// that each section's R length / M and G length Z0 / M are at most
/// kLossPerSection, up to kMaxSections (KNOWN_ISSUES). Without loss it is
/// the T line.
class TLineLossy : public Device {
public:
    /// The delay of a section, TD/M: the step is no longer than it
    /// (Device::max_time_step), so that every delayed value is accepted
    Real max_time_step() const override { return tau_; }

    static constexpr Real kLossPerSection = 0.01;
    static constexpr int kMaxSections = 16;

    TLineLossy(DeviceId id, const std::string& name,
               NodeId ap, NodeId an, NodeId bp, NodeId bn,
               Real r_per_m, Real l_per_m, Real c_per_m, Real g_per_m,
               Real td);

    ~TLineLossy() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    void stamp_dc_linear(MNASystem& mna,
                         const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                            const DenseVector& node_voltages) const override {
        (void)mna; (void)node_voltages; // linear device
    }

    void stamp_transient(MNASystem& mna,
                         const DenseVector& node_voltages,
                         Real timestep,
                         IntegrationMethod method,
                         const BDFCoefficients* bdf_coef = nullptr,
                         const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return true; }

    // Accessors
    Real r() const { return r_per_m_; }
    Real l() const { return l_per_m_; }
    Real c() const { return c_per_m_; }
    Real g() const { return g_per_m_; }
    Real td() const { return td_; }
    /// TD / sqrt(L C), in the unit of length of R, L, C and G
    Real length() const { return length_; }
    /// The number M of sections of the transient model
    int sections() const { return sections_; }

    /// The line's admittance parameters at angular frequency omega:
    /// I_A = Y11 V_A + Y12 V_B, I_B = Y12 V_A + Y11 V_B. With the series
    /// impedance Z = (R + j omega L) length, the shunt admittance
    /// Y = (G + j omega C) length and x = sqrt(Z Y), Y11 = x coth(x) / Z and
    /// Y12 = -x csch(x) / Z (the telegrapher's equations). Z is 0 only at DC
    /// without R, where the line is a wire: there the result is the wire as
    /// the lossless line's DC coupling (TLineLossless::kDcCoupling), with
    /// half of G length on each port. DC and AC at omega = 0 stamp the same
    /// two-port.
    struct TwoPort {
        Complex y11, y12;
    };
    TwoPort two_port(Real omega) const;

private:
    std::vector<NodeId> nodes_; // [ap, an, bp, bn]
    Real r_per_m_;
    Real l_per_m_;
    Real c_per_m_;
    Real g_per_m_;
    Real td_;
    Real length_;

    int sections_;   // M
    Real tau_;       // TD/M, the delay of a section
    Real z0_;        // sqrt(L/C), of the lossless lines between the lumped losses
    Real g0_;        // 1/Z0
    Real rs_;        // R length / (2M), in series at each end of a section
    Real gsh_;       // G length / (2M), in parallel at each end of a section
    Real g_port_;    // 1/(Z0 + rs): a section's lossless line seen through rs
    Real k_hist_;    // Z0/(Z0 + rs): the share of an arriving wave at an end

    /// The values at a section's lossless line's own ends, inside the series
    /// resistances: the voltages across them and the currents into them
    struct HistoryPoint {
        Real time;
        Real v_a;
        Real v_b;
        Real i_a;
        Real i_b;
    };
    /// The current sources of the waves arriving at the A and B ends of
    /// section k at time t, from its values at t - TD/M
    Real arriving_at_a(int k, Real t) const;
    Real arriving_at_b(int k, Real t) const;
    /// The section's values at time t, interpolated in its history
    HistoryPoint delayed(int k, Real t) const;
    /// The voltages of the junctions 0..M (0 and M the ports) at time t of
    /// an accepted point with port voltages v_a and v_b
    std::vector<Real> junction_voltages(Real t, Real v_a, Real v_b) const;

    mutable std::vector<std::deque<HistoryPoint>> history_;  // one per section
    mutable std::vector<std::deque<HistoryPoint>> history_saved_;
    mutable Real current_time_ = 0.0;
    mutable Real current_time_saved_ = 0.0;
    mutable Real last_timestep_ = 0.0;
    // The last stamp was TR-BDF2's first stage, whose point t_n + gamma h is
    // not a time point of the line (accept_solution)
    mutable bool last_stamp_was_first_stage_ = false;
    // The last stamp was the operating point's: the history is its steady
    // state (accept_solution)
    mutable bool last_stamp_was_dc_ = false;
    void save_state() override;
    void restore_state() override;
    void reset_state() override;
    void accept_solution(const DenseVector& node_voltages) override;
};

} // namespace kirimaspice::devices
