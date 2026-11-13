// src/devices/scr.cpp
#include "kirimaspice/devices/scr.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

namespace kirimaspice::devices {

SCR::SCR(DeviceId id, const std::string& name,
         NodeId anode, NodeId cathode, NodeId gate,
         Real vbf, Real igt, Real ih, Real vgt, Real ron, Real vf, Real tnom)
    : Device(id, name, DeviceType::SCR),
      nodes_{anode, cathode, gate},
      vbf_(vbf), igt_(igt), ih_(ih), vgt_(vgt), ron_(ron), vf_(vf), tnom_(tnom),
      latched_(false), iak_prev_(0.0), first_accept_(true) {

    // Parameter validation
    if (vbf_ <= 0.0) {
        spdlog::warn("SCR {}: VBF must be positive, using default 600V", name);
        vbf_ = 600.0;
    }
    if (igt_ <= 0.0) {
        spdlog::warn("SCR {}: IGT must be positive, using default 10mA", name);
        igt_ = 10e-3;
    }
    if (ih_ <= 0.0) {
        spdlog::warn("SCR {}: IH must be positive, using default 5mA", name);
        ih_ = 5e-3;
    }
    if (ron_ <= 0.0) {
        spdlog::warn("SCR {}: RON must be positive, using default 0.1Ω", name);
        ron_ = 0.1;
    }

    spdlog::trace("SCR {} created: A={}, K={}, G={}, VBF={}V, IGT={}mA, IH={}mA, RON={}Ω",
                  name, anode, cathode, gate, vbf_, igt_*1000, ih_*1000, ron_);
}

void SCR::reset_state() {
    latched_ = false;
    iak_prev_ = 0.0;
    first_accept_ = true;
    spdlog::trace("SCR {}: State reset to blocking mode", name());
}

void SCR::accept_solution(const DenseVector& node_voltages) {
    // Update state for next iteration
    (void)node_voltages;  // Suppress unused parameter warning
    first_accept_ = false;
    // iak_prev_ is updated in stamp methods
}

// Helper: Compute gate current from gate-cathode voltage
Real SCR::compute_gate_current(Real vgk) const {
    // Simple diode model for gate-cathode junction
    if (vgk < vgt_) {
        return 0.0;
    }
    // Linear approximation above threshold
    return (vgk - vgt_) / 100.0;  // Assume 100Ω gate resistance
}

// Helper: Check if trigger condition is met
bool SCR::check_trigger_condition(Real vak, Real ig) const {
    // Trigger if gate current exceeds IGT or anode voltage exceeds VBF
    return (ig >= igt_) || (vak >= vbf_);
}

// Helper: Check if turn-off condition is met
bool SCR::check_turnoff_condition(Real iak) const {
    // Turn off if anode current drops below holding current
    return (iak < ih_);
}

// Helper: Compute anode current based on current state
Real SCR::compute_anode_current(Real vak, const std::vector<Real>& node_voltages) const {
    if (!latched_) {
        // Blocking state: no current
        return 0.0;
    }

    // Conducting state: diode-like forward characteristic
    if (vak < vf_) {
        // Reverse bias or below forward voltage: minimal leakage
        return 0.0;
    }

    // Forward conducting: Ohmic behavior
    return (vak - vf_) / ron_;
}

void SCR::stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const {
    const NodeId a = nodes_[0];  // Anode
    const NodeId k = nodes_[1];  // Cathode
    const NodeId g = nodes_[2];  // Gate

    // Get node voltages
    const Real va = (a > 0) ? node_voltages[a - 1] : 0.0;
    const Real vk = (k > 0) ? node_voltages[k - 1] : 0.0;
    const Real vg = (g > 0) ? node_voltages[g - 1] : 0.0;

    const Real vak = va - vk;  // Anode-cathode voltage
    const Real vgk = vg - vk;  // Gate-cathode voltage

    // Compute gate current
    const Real ig = compute_gate_current(vgk);

    // Check trigger condition (mutable state, need const_cast for state update)
    if (!latched_ && check_trigger_condition(vak, ig)) {
        const_cast<SCR*>(this)->latched_ = true;
        spdlog::debug("SCR {}: Triggered ON (Vak={:.3f}V, Ig={:.3f}mA)", name(), vak, ig*1000);
    }

    // Compute anode current
    const Real iak = compute_anode_current(vak, node_voltages);

    // Check turn-off condition
    if (latched_ && check_turnoff_condition(iak)) {
        const_cast<SCR*>(this)->latched_ = false;
        spdlog::debug("SCR {}: Turned OFF (Iak={:.3f}mA < IH={:.3f}mA)", name(), iak*1000, ih_*1000);
    }

    // Norton equivalent circuit for anode-cathode
    // G = 1/RON (when conducting), I_norton = iak - g*vak
    Real g_ak = 0.0;
    Real i_norton_ak = 0.0;

    if (latched_) {
        g_ak = 1.0 / ron_;
        i_norton_ak = iak - g_ak * vak;
    }

    // Stamp anode-cathode conductance
    if (a > 0 && k > 0) {
        mna.G.add_entry(a - 1, a - 1, g_ak);
        mna.G.add_entry(a - 1, k - 1, -g_ak);
        mna.G.add_entry(k - 1, a - 1, -g_ak);
        mna.G.add_entry(k - 1, k - 1, g_ak);
    } else if (a > 0) {
        mna.G.add_entry(a - 1, a - 1, g_ak);
    } else if (k > 0) {
        mna.G.add_entry(k - 1, k - 1, g_ak);
    }

    // Stamp anode-cathode Norton current
    if (a > 0) mna.b[a - 1] -= i_norton_ak;
    if (k > 0) mna.b[k - 1] += i_norton_ak;

    // Gate-cathode diode (simplified as linear resistance above VGT)
    const Real g_gk = (vgk >= vgt_) ? 0.01 : 1e-12;  // 100Ω when conducting, high-Z when blocking
    const Real i_norton_gk = ig - g_gk * vgk;

    // Stamp gate-cathode conductance
    if (g > 0 && k > 0) {
        mna.G.add_entry(g - 1, g - 1, g_gk);
        mna.G.add_entry(g - 1, k - 1, -g_gk);
        mna.G.add_entry(k - 1, g - 1, -g_gk);
        mna.G.add_entry(k - 1, k - 1, g_gk);
    } else if (g > 0) {
        mna.G.add_entry(g - 1, g - 1, g_gk);
    } else if (k > 0) {
        mna.G.add_entry(k - 1, k - 1, g_gk);
    }

    // Stamp gate-cathode Norton current
    if (g > 0) mna.b[g - 1] -= i_norton_gk;
    if (k > 0) mna.b[k - 1] += i_norton_gk;

    // Store current for turn-off detection in next iteration
    const_cast<SCR*>(this)->iak_prev_ = iak;

    spdlog::trace("SCR {}: DC stamp: Vak={:.3f}V, Iak={:.3f}mA, latched={}, g_ak={:.6f}S",
                  name(), vak, iak*1000, latched_, g_ak);
}

void SCR::stamp_ac(ComplexMNASystem& sys, Real omega,
                   const DenseVector& dc_solution) const {
    // AC small-signal analysis: SCR is linearized around DC operating point
    // If conducting: acts as resistor RON
    // If blocking: acts as open circuit
    (void)omega;  // Not used for resistive behavior
    (void)dc_solution;  // State already captured in latched_

    const NodeId a = nodes_[0];
    const NodeId k = nodes_[1];

    Real g_ak = 0.0;
    if (latched_) {
        g_ak = 1.0 / ron_;
    }

    // Stamp conductance (real part, no imaginary part for resistive behavior)
    Complex g_complex(g_ak, 0.0);

    if (a > 0 && k > 0) {
        sys.Y.add_entry(a - 1, a - 1, g_complex);
        sys.Y.add_entry(a - 1, k - 1, -g_complex);
        sys.Y.add_entry(k - 1, a - 1, -g_complex);
        sys.Y.add_entry(k - 1, k - 1, g_complex);
    } else if (a > 0) {
        sys.Y.add_entry(a - 1, a - 1, g_complex);
    } else if (k > 0) {
        sys.Y.add_entry(k - 1, k - 1, g_complex);
    }

    spdlog::trace("SCR {}: AC stamp: g_ak={:.6f}S, latched={}", name(), g_ak, latched_);
}

void SCR::stamp_transient(MNASystem& mna,
                         const DenseVector& node_voltages,
                         Real timestep,
                         IntegrationMethod method,
                         const BDFCoefficients* bdf_coef,
                         const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // For transient analysis, use DC stamp (SCR has no dynamic capacitance in this model)
    (void)timestep; (void)method; (void)bdf_coef; (void)tr_bdf2_coef;  // Suppress unused parameter warnings
    stamp_dc_nonlinear(mna, node_voltages);
}

std::vector<NoiseSource> SCR::get_noise_sources(const DenseVector& dc_solution,
                                                Real frequency) const {
    // Thermal noise from ON-state resistance (when conducting)
    (void)dc_solution;  // State already captured in latched_
    (void)frequency;  // White noise is frequency-independent

    if (!latched_) {
        return {};  // No noise when blocking
    }

    const NodeId a = nodes_[0];
    const NodeId k = nodes_[1];

    // Thermal noise: I_n² = 4kT × G [A²/Hz]
    const Real k_boltzmann = 1.380649e-23;  // Boltzmann constant [J/K]
    const Real T = tnom_;                    // Temperature [K]
    const Real g_ak = 1.0 / ron_;

    NoiseSource thermal_noise;
    thermal_noise.name = name() + "_thermal";
    thermal_noise.node_p = a;
    thermal_noise.node_n = k;
    thermal_noise.spectral_density = 4.0 * k_boltzmann * T * g_ak;  // [A²/Hz]
    thermal_noise.is_current = true;  // Current noise source

    return {thermal_noise};
}

std::optional<Real> SCR::get_parameter(const std::string& param_name) const {
    std::string name_upper = param_name;
    std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);

    if (name_upper == "VBF") return vbf_;
    if (name_upper == "IGT") return igt_;
    if (name_upper == "IH") return ih_;
    if (name_upper == "VGT") return vgt_;
    if (name_upper == "RON") return ron_;
    if (name_upper == "VF") return vf_;
    if (name_upper == "TNOM") return tnom_;

    return std::nullopt;
}

bool SCR::set_parameter(const std::string& param_name, Real value) {
    std::string name_upper = param_name;
    std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);

    if (name_upper == "VBF") {
        if (value <= 0.0) {
            spdlog::warn("SCR {}: VBF must be positive", name());
            return false;
        }
        vbf_ = value;
        return true;
    }
    if (name_upper == "IGT") {
        if (value <= 0.0) {
            spdlog::warn("SCR {}: IGT must be positive", name());
            return false;
        }
        igt_ = value;
        return true;
    }
    if (name_upper == "IH") {
        if (value <= 0.0) {
            spdlog::warn("SCR {}: IH must be positive", name());
            return false;
        }
        ih_ = value;
        return true;
    }
    if (name_upper == "VGT") {
        if (value <= 0.0) {
            spdlog::warn("SCR {}: VGT must be positive", name());
            return false;
        }
        vgt_ = value;
        return true;
    }
    if (name_upper == "RON") {
        if (value <= 0.0) {
            spdlog::warn("SCR {}: RON must be positive", name());
            return false;
        }
        ron_ = value;
        return true;
    }
    if (name_upper == "VF") {
        if (value < 0.0) {
            spdlog::warn("SCR {}: VF must be non-negative", name());
            return false;
        }
        vf_ = value;
        return true;
    }

    return false;
}

std::vector<std::string> SCR::get_perturbable_parameters() const {
    return {"VBF", "IGT", "IH", "VGT", "RON", "VF"};
}

} // namespace kirimaspice::devices
