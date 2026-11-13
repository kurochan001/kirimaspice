// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/triac.cpp
#include "kirimaspice/devices/triac.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace kirimaspice::devices {

TRIAC::TRIAC(DeviceId id, const std::string& name,
             NodeId mt2, NodeId mt1, NodeId gate,
             Real vbo, Real igt, Real ih, Real vgt, Real ron, Real vtm, Real dv_dt, Real tnom)
    : Device(id, name, DeviceType::TRIAC),
      nodes_{mt2, mt1, gate},
      vbo_(vbo), igt_(igt), ih_(ih), vgt_(vgt), ron_(ron), vtm_(vtm), dv_dt_(dv_dt), tnom_(tnom),
      latched_pos_(false), latched_neg_(false), imt_prev_(0.0), first_accept_(true) {

    // Parameter validation
    if (vbo_ <= 0.0) {
        spdlog::warn("TRIAC {}: VBO must be positive, using default 400V", name);
        vbo_ = 400.0;
    }
    if (igt_ <= 0.0) {
        spdlog::warn("TRIAC {}: IGT must be positive, using default 20mA", name);
        igt_ = 20e-3;
    }
    if (ih_ <= 0.0) {
        spdlog::warn("TRIAC {}: IH must be positive, using default 10mA", name);
        ih_ = 10e-3;
    }
    if (ron_ <= 0.0) {
        spdlog::warn("TRIAC {}: RON must be positive, using default 0.1Ω", name);
        ron_ = 0.1;
    }

    spdlog::trace("TRIAC {} created: MT2={}, MT1={}, G={}, VBO={}V, IGT={}mA, IH={}mA, RON={}Ω",
                  name, mt2, mt1, gate, vbo_, igt_*1000, ih_*1000, ron_);
}

void TRIAC::reset_state() {
    latched_pos_ = false;
    latched_neg_ = false;
    imt_prev_ = 0.0;
    first_accept_ = true;
    spdlog::trace("TRIAC {}: State reset to blocking mode (both directions)", name());
}

void TRIAC::accept_solution(const DenseVector& node_voltages) {
    // Update state for next iteration
    (void)node_voltages;  // Suppress unused parameter warning
    first_accept_ = false;
    // imt_prev_ is updated in stamp methods
}

// Helper: Compute gate current from gate-MT1 voltage
Real TRIAC::compute_gate_current(Real vg_mt1) const {
    // Simple diode model for gate-MT1 junction (bidirectional)
    const Real abs_vg = std::abs(vg_mt1);
    if (abs_vg < vgt_) {
        return 0.0;
    }
    // Linear approximation above threshold (both directions)
    const Real sign = (vg_mt1 >= 0.0) ? 1.0 : -1.0;
    return sign * (abs_vg - vgt_) / 100.0;  // Assume 100Ω gate resistance
}

// Helper: Check if positive direction trigger condition is met (Q1+/Q1-)
bool TRIAC::check_trigger_condition_pos(Real vmt, Real ig) const {
    // Q1+ (MT2+, G+): positive Vmt, positive Ig
    // Q1- (MT2+, G-): positive Vmt, negative Ig
    // Trigger if |Ig| >= IGT or Vmt >= VBO
    return (vmt >= vbo_) || (std::abs(ig) >= igt_);
}

// Helper: Check if negative direction trigger condition is met (Q3+/Q3-)
bool TRIAC::check_trigger_condition_neg(Real vmt, Real ig) const {
    // Q3+ (MT2-, G+): negative Vmt, positive Ig
    // Q3- (MT2-, G-): negative Vmt, negative Ig
    // Trigger if |Ig| >= IGT or |Vmt| >= VBO
    return (std::abs(vmt) >= vbo_) || (std::abs(ig) >= igt_);
}

// Helper: Check if turn-off condition is met
bool TRIAC::check_turnoff_condition(Real imt) const {
    // Turn off if |Imt| drops below holding current
    return (std::abs(imt) < ih_);
}

// Helper: Compute MT2-MT1 current based on current state
Real TRIAC::compute_mt_current(Real vmt, const DenseVector& node_voltages) const {
    (void)node_voltages;  // Unused in this simplified model

    // Blocking state: no current in either direction
    if (!latched_pos_ && !latched_neg_) {
        return 0.0;
    }

    // Determine direction
    const bool is_positive_latched = latched_pos_;
    const bool is_negative_latched = latched_neg_;

    // Positive direction conducting (MT2 > MT1)
    if (is_positive_latched && vmt >= vtm_) {
        return (vmt - vtm_) / ron_;
    }

    // Negative direction conducting (MT2 < MT1)
    if (is_negative_latched && vmt <= -vtm_) {
        return (vmt + vtm_) / ron_;
    }

    // Below threshold voltage: minimal leakage
    return 0.0;
}

void TRIAC::stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const {
    const NodeId mt2 = nodes_[0];  // MT2
    const NodeId mt1 = nodes_[1];  // MT1
    const NodeId g = nodes_[2];    // Gate

    // Get node voltages
    const Real v_mt2 = (mt2 > 0) ? node_voltages[mt2 - 1] : 0.0;
    const Real v_mt1 = (mt1 > 0) ? node_voltages[mt1 - 1] : 0.0;
    const Real v_g = (g > 0) ? node_voltages[g - 1] : 0.0;

    const Real vmt = v_mt2 - v_mt1;      // MT2-MT1 voltage
    const Real vg_mt1 = v_g - v_mt1;     // Gate-MT1 voltage

    // Compute gate current
    const Real ig = compute_gate_current(vg_mt1);

    // Check trigger conditions (mutable state, need const_cast for state update)
    if (!latched_pos_ && !latched_neg_) {
        // Positive direction trigger (Q1+/Q1-)
        if (vmt > 0.0 && check_trigger_condition_pos(vmt, ig)) {
            const_cast<TRIAC*>(this)->latched_pos_ = true;
            spdlog::debug("TRIAC {}: Triggered ON in positive direction (Vmt={:.3f}V, Ig={:.3f}mA)",
                          name(), vmt, ig*1000);
        }
        // Negative direction trigger (Q3+/Q3-)
        else if (vmt < 0.0 && check_trigger_condition_neg(vmt, ig)) {
            const_cast<TRIAC*>(this)->latched_neg_ = true;
            spdlog::debug("TRIAC {}: Triggered ON in negative direction (Vmt={:.3f}V, Ig={:.3f}mA)",
                          name(), vmt, ig*1000);
        }
    }

    // Compute MT2-MT1 current
    const Real imt = compute_mt_current(vmt, node_voltages);

    // Check turn-off condition
    if ((latched_pos_ || latched_neg_) && check_turnoff_condition(imt)) {
        const_cast<TRIAC*>(this)->latched_pos_ = false;
        const_cast<TRIAC*>(this)->latched_neg_ = false;
        spdlog::debug("TRIAC {}: Turned OFF (|Imt|={:.3f}mA < IH={:.3f}mA)",
                      name(), std::abs(imt)*1000, ih_*1000);
    }

    // Norton equivalent circuit for MT2-MT1
    // G = 1/RON (when conducting), I_norton = imt - g*vmt
    Real g_mt = 0.0;
    Real i_norton_mt = 0.0;

    if (latched_pos_ || latched_neg_) {
        g_mt = 1.0 / ron_;
        i_norton_mt = imt - g_mt * vmt;
    }

    // Stamp MT2-MT1 conductance
    if (mt2 > 0 && mt1 > 0) {
        mna.G.add_entry(mt2 - 1, mt2 - 1, g_mt);
        mna.G.add_entry(mt2 - 1, mt1 - 1, -g_mt);
        mna.G.add_entry(mt1 - 1, mt2 - 1, -g_mt);
        mna.G.add_entry(mt1 - 1, mt1 - 1, g_mt);
    } else if (mt2 > 0) {
        mna.G.add_entry(mt2 - 1, mt2 - 1, g_mt);
    } else if (mt1 > 0) {
        mna.G.add_entry(mt1 - 1, mt1 - 1, g_mt);
    }

    // Stamp MT2-MT1 Norton current
    if (mt2 > 0) mna.b[mt2 - 1] -= i_norton_mt;
    if (mt1 > 0) mna.b[mt1 - 1] += i_norton_mt;

    // Gate-MT1 junction (simplified as linear resistance above VGT, bidirectional)
    const Real abs_vg = std::abs(vg_mt1);
    const Real g_g_mt1 = (abs_vg >= vgt_) ? 0.01 : 1e-12;  // 100Ω when conducting, high-Z when blocking
    const Real i_norton_g = ig - g_g_mt1 * vg_mt1;

    // Stamp gate-MT1 conductance
    if (g > 0 && mt1 > 0) {
        mna.G.add_entry(g - 1, g - 1, g_g_mt1);
        mna.G.add_entry(g - 1, mt1 - 1, -g_g_mt1);
        mna.G.add_entry(mt1 - 1, g - 1, -g_g_mt1);
        mna.G.add_entry(mt1 - 1, mt1 - 1, g_g_mt1);
    } else if (g > 0) {
        mna.G.add_entry(g - 1, g - 1, g_g_mt1);
    } else if (mt1 > 0) {
        mna.G.add_entry(mt1 - 1, mt1 - 1, g_g_mt1);
    }

    // Stamp gate Norton current
    if (g > 0) mna.b[g - 1] -= i_norton_g;
    if (mt1 > 0) mna.b[mt1 - 1] += i_norton_g;

    // Store current for next iteration
    const_cast<TRIAC*>(this)->imt_prev_ = imt;
}

void TRIAC::stamp_transient(MNASystem& mna, const DenseVector& node_voltages,
                            Real timestep, IntegrationMethod method,
                            const BDFCoefficients* bdf_coef, const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // TRIAC is a simplified static model - no dynamic capacitance
    // Use same stamp as DC nonlinear
    (void)timestep; (void)method; (void)bdf_coef; (void)tr_bdf2_coef;
    stamp_dc_nonlinear(mna, node_voltages);
}

void TRIAC::stamp_ac(ComplexMNASystem& sys, Real omega, const DenseVector& dc_solution) const {
    (void)omega;  // TRIAC AC model is frequency-independent (simplified)

    const NodeId mt2 = nodes_[0];
    const NodeId mt1 = nodes_[1];
    const NodeId g = nodes_[2];

    // Get DC operating point
    const Real v_mt2 = (mt2 > 0) ? dc_solution[mt2 - 1] : 0.0;
    const Real v_mt1 = (mt1 > 0) ? dc_solution[mt1 - 1] : 0.0;
    const Real v_g = (g > 0) ? dc_solution[g - 1] : 0.0;

    const Real vmt = v_mt2 - v_mt1;
    const Real vg_mt1 = v_g - v_mt1;

    // Small-signal linearization
    // If latched: G_mt = 1/RON (constant)
    // If blocking: G_mt = 0 (high impedance)
    Complex g_mt = 0.0;
    if (latched_pos_ || latched_neg_) {
        g_mt = 1.0 / ron_;
    }

    // Stamp MT2-MT1 admittance
    if (mt2 > 0 && mt1 > 0) {
        sys.Y.add_entry(mt2 - 1, mt2 - 1, g_mt);
        sys.Y.add_entry(mt2 - 1, mt1 - 1, -g_mt);
        sys.Y.add_entry(mt1 - 1, mt2 - 1, -g_mt);
        sys.Y.add_entry(mt1 - 1, mt1 - 1, g_mt);
    } else if (mt2 > 0) {
        sys.Y.add_entry(mt2 - 1, mt2 - 1, g_mt);
    } else if (mt1 > 0) {
        sys.Y.add_entry(mt1 - 1, mt1 - 1, g_mt);
    }

    // Gate admittance (simplified)
    const Real abs_vg = std::abs(vg_mt1);
    Complex g_g_mt1 = (abs_vg >= vgt_) ? Complex(0.01, 0.0) : Complex(1e-12, 0.0);

    if (g > 0 && mt1 > 0) {
        sys.Y.add_entry(g - 1, g - 1, g_g_mt1);
        sys.Y.add_entry(g - 1, mt1 - 1, -g_g_mt1);
        sys.Y.add_entry(mt1 - 1, g - 1, -g_g_mt1);
        sys.Y.add_entry(mt1 - 1, mt1 - 1, g_g_mt1);
    } else if (g > 0) {
        sys.Y.add_entry(g - 1, g - 1, g_g_mt1);
    } else if (mt1 > 0) {
        sys.Y.add_entry(mt1 - 1, mt1 - 1, g_g_mt1);
    }
}

std::vector<NoiseSource> TRIAC::get_noise_sources(const DenseVector& dc_solution, Real frequency) const {
    (void)frequency;  // Noise is frequency-independent in this model

    std::vector<NoiseSource> sources;

    const NodeId mt2 = nodes_[0];
    const NodeId mt1 = nodes_[1];

    // Only generate noise when conducting
    if (!latched_pos_ && !latched_neg_) {
        return sources;
    }

    // Thermal noise from on-state resistance
    // Noise spectral density: 4kT/R [A²/Hz]
    const Real k = 1.380649e-23;  // Boltzmann constant [J/K]
    const Real T = tnom_;          // Temperature [K]
    const Real spectral_density = 4.0 * k * T / ron_;  // [A²/Hz]

    sources.push_back({
        name() + "_thermal",
        mt2, mt1,
        spectral_density,
        true  // is_current (current noise source)
    });

    return sources;
}

std::optional<Real> TRIAC::get_parameter(const std::string& param_name) const {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "vbo") return vbo_;
    if (param_lower == "igt") return igt_;
    if (param_lower == "ih") return ih_;
    if (param_lower == "vgt") return vgt_;
    if (param_lower == "ron") return ron_;
    if (param_lower == "vtm") return vtm_;
    if (param_lower == "dv_dt" || param_lower == "dvdt") return dv_dt_;
    if (param_lower == "tnom") return tnom_;

    return std::nullopt;
}

bool TRIAC::set_parameter(const std::string& param_name, Real value) {
    std::string param_lower = param_name;
    std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

    if (param_lower == "vbo") {
        if (value <= 0.0) {
            spdlog::warn("TRIAC {}: VBO must be positive", name());
            return false;
        }
        vbo_ = value;
        return true;
    }
    if (param_lower == "igt") {
        if (value <= 0.0) {
            spdlog::warn("TRIAC {}: IGT must be positive", name());
            return false;
        }
        igt_ = value;
        return true;
    }
    if (param_lower == "ih") {
        if (value <= 0.0) {
            spdlog::warn("TRIAC {}: IH must be positive", name());
            return false;
        }
        ih_ = value;
        return true;
    }
    if (param_lower == "vgt") {
        vgt_ = value;
        return true;
    }
    if (param_lower == "ron") {
        if (value <= 0.0) {
            spdlog::warn("TRIAC {}: RON must be positive", name());
            return false;
        }
        ron_ = value;
        return true;
    }
    if (param_lower == "vtm") {
        vtm_ = value;
        return true;
    }
    if (param_lower == "dv_dt" || param_lower == "dvdt") {
        dv_dt_ = value;
        return true;
    }
    if (param_lower == "tnom") {
        tnom_ = value;
        return true;
    }

    spdlog::warn("TRIAC {}: Unknown parameter '{}'", name(), param_name);
    return false;
}

std::vector<std::string> TRIAC::get_perturbable_parameters() const {
    return {"VBO", "IGT", "IH", "RON"};
}

} // namespace kirimaspice::devices
