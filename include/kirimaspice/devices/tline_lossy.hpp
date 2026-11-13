// include/kirimaspice/devices/tline_lossy.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include <cmath>
#include <deque>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

/// Lossy transmission line (O-element): distributed R, L, C, G with time delay TD
/// AC-only minimal implementation using closed-form Y-parameters.
class TLineLossy : public Device {
public:
    TLineLossy(DeviceId id, const std::string& name,
               NodeId ap, NodeId an, NodeId bp, NodeId bn,
               Real r_per_m, Real l_per_m, Real c_per_m, Real g_per_m,
               Real td);

    ~TLineLossy() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    void stamp_dc_linear(MNASystem& mna,
                         const DenseVector& node_voltages) const override {
        (void)mna; (void)node_voltages;
        // DC: model as open (same as lossless line) — no coupling at DC.
    }

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

private:
    std::vector<NodeId> nodes_; // [ap, an, bp, bn]
    Real r_per_m_;
    Real l_per_m_;
    Real c_per_m_;
    Real g_per_m_;
    Real td_;

    Real z0_;   // approx characteristic impedance (sqrt(L/C))
    Real g0_;   // 1/Z0
    Real alpha_; // approximate attenuation constant (Np) using low-frequency approx

    struct HistoryPoint {
        Real time;
        Real v_a;
        Real v_b;
        Real i_a;
        Real i_b;
    };

    mutable std::deque<HistoryPoint> history_;
    mutable std::deque<HistoryPoint> history_saved_;
    mutable Real current_time_ = 0.0;
    mutable Real current_time_saved_ = 0.0;
    mutable Real last_timestep_ = 0.0;
    mutable Real filt_a_ = 0.0;   // 1st-pole filtered delayed V_a
    mutable Real filt_b_ = 0.0;   // 1st-pole filtered delayed V_b
    mutable Real filt_a2_ = 0.0;  // 2nd-pole state (optional)
    mutable Real filt_b2_ = 0.0;  // 2nd-pole state (optional)
    mutable Real filt_a_saved_ = 0.0;
    mutable Real filt_b_saved_ = 0.0;
    mutable Real filt_a2_saved_ = 0.0;
    mutable Real filt_b2_saved_ = 0.0;

    // Characteristic RC/LR time constants for simple dispersion/attenuation filter
    Real tau_filter_ = 0.0;
    bool use_iir_filter_ = false; // Env: KIRIMASPICE_TLINE_LOSSY_IIR=1 to enable 2-pole filter

    void get_delayed_signals(Real current_time,
                             Real& v_a_delayed, Real& v_b_delayed,
                             Real& i_a_delayed, Real& i_b_delayed) const;

    void save_state() override;
    void restore_state() override;
    void reset_state() override;
    void accept_solution(const DenseVector& node_voltages) override;
};

} // namespace kirimaspice::devices
