// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/scr.hpp
// Unified SCR (N/P) template class
#pragma once
#include "kirimaspice/devices/device.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <algorithm>

namespace kirimaspice::devices {

/// SCR Polarity type for template parameter
enum class SCRPolarity { N, P };

/// SCR (Silicon Controlled Rectifier) unified template
/// Unidirectional thyristor for AC-DC conversion and phase-control applications
///
/// Device structure:
/// - 3 terminals: T1, T2, Gate
/// - For N-type: T1=Anode, T2=Cathode, conducts A→K when triggered
/// - For P-type: T1=Cathode, T2=Anode, conducts K→A when triggered
/// - Unidirectional pnpn structure
/// - Latching behavior: once triggered, remains ON until current drops below IH
///
/// Operating regions:
/// 1. Blocking (V12 < VBF and Ig < IGT):
///    I12 = 0 (OFF state, blocking voltage)
///
/// 2. Triggering (V12 > 0 and Ig >= IGT):
///    latched_on_ = true (gate trigger initiates conduction)
///
/// 3. Latched ON (latched_on_ = true and I12 >= IH):
///    Conducting state, gate signal no longer needed
///    I12 = (V12 - VTM) / RON  (diode-like characteristic)
///
/// 4. Turn-off (I12 < IH):
///    latched_on_ = false, returns to blocking state
///
/// Key features:
///   - Unidirectional voltage blocking: 100V-1200V typical
///   - Unidirectional current capability: 1A-100A+
///   - Latching behavior (turn-on via gate, turn-off via current)
///   - AC phase control (rectifiers, motor speed control)
///   - Low ON-state voltage drop: 1-2V
///
/// Key parameters:
///   VBF = forward breakover voltage [V] (100-1200V typical)
///   IGT = gate trigger current [A] (5-50mA typical)
///   IH  = holding current [A] (5-50mA typical)
///   IL  = latching current [A] (typically 2-3× IH)
///   VGT = gate trigger voltage [V] (0.7-1.5V typical)
///   RON = on-state resistance [Ω] (0.01-1Ω typical)
///   VTM = on-state voltage drop [V] (1-2V typical)
///
template<SCRPolarity Pol>
class SCR : public Device {
public:
    // Polarity-dependent constants
    static constexpr DeviceType device_type =
        (Pol == SCRPolarity::N) ? DeviceType::SCR_N : DeviceType::SCR_P;

    /// Constructor
    /// @param id Device ID
    /// @param name Device name
    /// @param t1 Terminal 1 (Anode for N-type, Cathode for P-type)
    /// @param t2 Terminal 2 (Cathode for N-type, Anode for P-type)
    /// @param gate Gate terminal
    /// @param vbf Forward breakover voltage [V]
    /// @param igt Gate trigger current [A]
    /// @param ih Holding current [A]
    /// @param il Latching current [A]
    /// @param vgt Gate trigger voltage [V]
    /// @param ron On-state resistance [Ω]
    /// @param vtm On-state voltage drop [V]
    /// @param dv_dt dv/dt rating [V/μs] (optional)
    /// @param tnom Nominal temperature [K]
    SCR(DeviceId id, const std::string& name,
        NodeId t1, NodeId t2, NodeId gate,
        Real vbf = 400.0,      // Forward breakover voltage [V]
        Real igt = 20e-3,      // Gate trigger current [A]
        Real ih = 10e-3,       // Holding current [A]
        Real il = 30e-3,       // Latching current [A]
        Real vgt = 1.0,        // Gate trigger voltage [V]
        Real ron = 0.1,        // On-state resistance [Ω]
        Real vtm = 1.5,        // On-state voltage drop [V]
        Real dv_dt = 10.0,     // dv/dt rating [V/μs]
        Real tnom = 300.15)    // Nominal temperature [K]
        : Device(id, name, device_type),
          nodes_{t1, t2, gate},
          vbf_(vbf), igt_(igt), ih_(ih), il_(il), vgt_(vgt),
          ron_(ron), vtm_(vtm), dv_dt_(dv_dt), tnom_(tnom),
          latched_on_(false), i12_prev_(0.0), first_accept_(true) {

        if constexpr (Pol == SCRPolarity::N) {
            spdlog::trace("SCR_N {} created: VBF={} V, IGT={} A, IH={} A, RON={} Ω",
                          name, vbf_, igt_, ih_, ron_);
        } else {
            spdlog::trace("SCR_P {} created: VBF={} V, IGT={} A, IH={} A, RON={} Ω",
                          name, vbf_, igt_, ih_, ron_);
        }
    }

    ~SCR() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: Nonlinear device requires Newton-Raphson
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        // SCR is nonlinear - no linear contribution
        (void)mna; (void)node_voltages;
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        NodeId t1 = nodes_[0];  // T1 (Anode for N, Cathode for P)
        NodeId t2 = nodes_[1];  // T2 (Cathode for N, Anode for P)
        NodeId g = nodes_[2];   // Gate

        // Get node voltages (NodeId is 1-based, node_voltages is 0-based)
        Real vt1 = (t1 != NODE_GND) ? node_voltages[t1 - 1] : 0.0;
        Real vt2 = (t2 != NODE_GND) ? node_voltages[t2 - 1] : 0.0;
        Real vg = (g != NODE_GND) ? node_voltages[g - 1] : 0.0;

        // Compute voltages
        Real v12 = vt1 - vt2;     // T1-T2 voltage (forward direction)
        Real vg_t2 = vg - vt2;    // Gate-T2 voltage (gate reference is T2)

        // Compute gate current
        Real ig = compute_gate_current(vg_t2);

        // Check trigger condition (update latched_on_ state)
        if (check_trigger_condition(v12, ig)) {
            latched_on_ = true;
            if constexpr (Pol == SCRPolarity::N) {
                spdlog::trace("SCR_N {}: Triggered (Vak={} V, Ig={} A)", name(), v12, ig);
            } else {
                spdlog::trace("SCR_P {}: Triggered (Vka={} V, Ig={} A)", name(), v12, ig);
            }
        }

        // Compute T1-T2 current
        Real i12 = compute_current(v12);

        // Check turn-off condition
        if (check_turnoff_condition(i12)) {
            latched_on_ = false;
            if constexpr (Pol == SCRPolarity::N) {
                spdlog::trace("SCR_N {}: Turned off (Iak={} A < IH={} A)", name(), i12, ih_);
            } else {
                spdlog::trace("SCR_P {}: Turned off (Ika={} A < IH={} A)", name(), i12, ih_);
            }
        }

        // Norton equivalent for T1-T2
        // If conducting: I12 = (V12 - VTM) / RON
        // If blocking: I12 = 0
        Real gon = latched_on_ ? (1.0 / std::max(ron_, 1e-6)) : 0.0;
        Real i_norton = latched_on_ ? (vtm_ / std::max(ron_, 1e-6)) : 0.0;

        // Stamp T1-T2 conductance
        int t1_idx = (t1 != NODE_GND) ? static_cast<int>(t1 - 1) : -1;
        int t2_idx = (t2 != NODE_GND) ? static_cast<int>(t2 - 1) : -1;

        if (t1_idx >= 0 && t2_idx >= 0) {
            mna.G.add_entry(t1_idx, t1_idx, gon);
            mna.G.add_entry(t1_idx, t2_idx, -gon);
            mna.G.add_entry(t2_idx, t1_idx, -gon);
            mna.G.add_entry(t2_idx, t2_idx, gon);
        } else if (t1_idx >= 0) {
            mna.G.add_entry(t1_idx, t1_idx, gon);
        } else if (t2_idx >= 0) {
            mna.G.add_entry(t2_idx, t2_idx, gon);
        }

        // Stamp T1-T2 Norton current (current flows T1 → T2)
        if (t1_idx >= 0) {
            mna.b[t1_idx] += i_norton;
        }
        if (t2_idx >= 0) {
            mna.b[t2_idx] -= i_norton;
        }

        // Gate circuit: simple resistive model (Vg_t2 / Rgate)
        // Gate conductance (simplified: Rgate = VGT / IGT)
        Real rgate = std::max(vgt_ / std::max(igt_, 1e-9), 1.0);
        Real ggate = 1.0 / rgate;

        int g_idx = (g != NODE_GND) ? static_cast<int>(g - 1) : -1;

        if (g_idx >= 0 && t2_idx >= 0) {
            mna.G.add_entry(g_idx, g_idx, ggate);
            mna.G.add_entry(g_idx, t2_idx, -ggate);
            mna.G.add_entry(t2_idx, g_idx, -ggate);
            mna.G.add_entry(t2_idx, t2_idx, ggate);
        } else if (g_idx >= 0) {
            mna.G.add_entry(g_idx, g_idx, ggate);
        } else if (t2_idx >= 0) {
            mna.G.add_entry(t2_idx, t2_idx, ggate);
        }
    }

    // Transient analysis: Simplified static model (no dynamic capacitance)
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override {
        // SCR is a static device (no dynamic capacitance in this simplified model)
        // Use DC nonlinear stamp
        (void)timestep; (void)method; (void)bdf_coef; (void)tr_bdf2_coef;
        stamp_dc_nonlinear(mna, node_voltages);
    }

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return false; }  // No dynamic capacitance

    // AC analysis: Small-signal linearization
    void stamp_ac(ComplexMNASystem& sys, Real omega,
                  const DenseVector& dc_solution) const override {
        (void)omega;  // Not used in this simplified model

        NodeId t1 = nodes_[0];
        NodeId t2 = nodes_[1];
        NodeId g = nodes_[2];

        // Get DC operating point
        Real vt1 = (t1 != NODE_GND) ? dc_solution[t1 - 1] : 0.0;
        Real vt2 = (t2 != NODE_GND) ? dc_solution[t2 - 1] : 0.0;
        Real vg = (g != NODE_GND) ? dc_solution[g - 1] : 0.0;

        Real v12 = vt1 - vt2;
        Real vg_t2 = vg - vt2;
        Real ig = compute_gate_current(vg_t2);

        // Determine if conducting in DC operating point
        bool conducting = latched_on_;
        if (!conducting && check_trigger_condition(v12, ig)) {
            conducting = true;
        }

        // AC conductance
        std::complex<Real> gon_ac = conducting ? std::complex<Real>(1.0 / std::max(ron_, 1e-6), 0.0)
                                               : std::complex<Real>(0.0, 0.0);

        // Stamp T1-T2 AC admittance
        int t1_idx = (t1 != NODE_GND) ? static_cast<int>(t1 - 1) : -1;
        int t2_idx = (t2 != NODE_GND) ? static_cast<int>(t2 - 1) : -1;

        if (t1_idx >= 0 && t2_idx >= 0) {
            sys.Y.add_entry(t1_idx, t1_idx, gon_ac);
            sys.Y.add_entry(t1_idx, t2_idx, -gon_ac);
            sys.Y.add_entry(t2_idx, t1_idx, -gon_ac);
            sys.Y.add_entry(t2_idx, t2_idx, gon_ac);
        } else if (t1_idx >= 0) {
            sys.Y.add_entry(t1_idx, t1_idx, gon_ac);
        } else if (t2_idx >= 0) {
            sys.Y.add_entry(t2_idx, t2_idx, gon_ac);
        }

        // Gate AC admittance (simplified)
        Real rgate = std::max(vgt_ / std::max(igt_, 1e-9), 1.0);
        std::complex<Real> ggate_ac(1.0 / rgate, 0.0);

        int g_idx = (g != NODE_GND) ? static_cast<int>(g - 1) : -1;

        if (g_idx >= 0 && t2_idx >= 0) {
            sys.Y.add_entry(g_idx, g_idx, ggate_ac);
            sys.Y.add_entry(g_idx, t2_idx, -ggate_ac);
            sys.Y.add_entry(t2_idx, g_idx, -ggate_ac);
            sys.Y.add_entry(t2_idx, t2_idx, ggate_ac);
        } else if (g_idx >= 0) {
            sys.Y.add_entry(g_idx, g_idx, ggate_ac);
        } else if (t2_idx >= 0) {
            sys.Y.add_entry(t2_idx, t2_idx, ggate_ac);
        }
    }

    // Noise analysis
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                               Real frequency) const override {
        (void)dc_solution; (void)frequency;

        std::vector<NoiseSource> sources;

        // Thermal noise from on-state resistance (Johnson-Nyquist noise)
        // i_n^2 = 4kT / RON
        if (latched_on_) {
            constexpr Real k_boltzmann = 1.380649e-23;
            Real thermal_noise_psd = 4.0 * k_boltzmann * tnom_ / std::max(ron_, 1e-6);
            sources.push_back({
                name() + "_thermal",
                nodes_[0],  // T1
                nodes_[1],  // T2
                thermal_noise_psd,
                true  // is_current
            });
        }

        return sources;
    }

    // LTE for adaptive timestepping
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override {
        (void)chgtol; (void)node_voltages;
        return 0.0;  // Static device, no LTE
    }

    // State management
    void reset_state() override {
        latched_on_ = false;
        i12_prev_ = 0.0;
        first_accept_ = true;
        if constexpr (Pol == SCRPolarity::N) {
            spdlog::trace("SCR_N {}: State reset", name());
        } else {
            spdlog::trace("SCR_P {}: State reset", name());
        }
    }

    void accept_solution(const DenseVector& node_voltages) override {
        NodeId t1 = nodes_[0];
        NodeId t2 = nodes_[1];

        Real vt1 = (t1 != NODE_GND) ? node_voltages[t1 - 1] : 0.0;
        Real vt2 = (t2 != NODE_GND) ? node_voltages[t2 - 1] : 0.0;
        Real v12 = vt1 - vt2;

        // Update previous current
        i12_prev_ = compute_current(v12);
        first_accept_ = false;

        if constexpr (Pol == SCRPolarity::N) {
            spdlog::trace("SCR_N {}: Accepted solution (Vak={} V, Iak={} A, latched={})",
                          name(), v12, i12_prev_, latched_on_);
        } else {
            spdlog::trace("SCR_P {}: Accepted solution (Vka={} V, Ika={} A, latched={})",
                          name(), v12, i12_prev_, latched_on_);
        }
    }

    // Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override {
        std::string lower_name = param_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        if (lower_name == "vbf") return vbf_;
        if (lower_name == "igt") return igt_;
        if (lower_name == "ih") return ih_;
        if (lower_name == "il") return il_;
        if (lower_name == "vgt") return vgt_;
        if (lower_name == "ron") return ron_;
        if (lower_name == "vtm") return vtm_;
        if (lower_name == "dv_dt") return dv_dt_;
        if (lower_name == "tnom") return tnom_;

        return std::nullopt;
    }

    bool set_parameter(const std::string& param_name, Real value) override {
        std::string lower_name = param_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        auto log_name = [this]() {
            if constexpr (Pol == SCRPolarity::N) return "SCR_N";
            else return "SCR_P";
        };

        if (lower_name == "vbf") {
            if (value <= 0.0) {
                spdlog::warn("{} {}: VBF must be positive (attempted: {})", log_name(), name(), value);
                return false;
            }
            vbf_ = value;
            spdlog::trace("{} {}: VBF set to {}", log_name(), name(), value);
            return true;
        }

        if (lower_name == "igt") {
            if (value <= 0.0) {
                spdlog::warn("{} {}: IGT must be positive (attempted: {})", log_name(), name(), value);
                return false;
            }
            igt_ = value;
            spdlog::trace("{} {}: IGT set to {}", log_name(), name(), value);
            return true;
        }

        if (lower_name == "ih") {
            if (value <= 0.0) {
                spdlog::warn("{} {}: IH must be positive (attempted: {})", log_name(), name(), value);
                return false;
            }
            ih_ = value;
            spdlog::trace("{} {}: IH set to {}", log_name(), name(), value);
            return true;
        }

        if (lower_name == "il") {
            if (value <= 0.0) {
                spdlog::warn("{} {}: IL must be positive (attempted: {})", log_name(), name(), value);
                return false;
            }
            il_ = value;
            spdlog::trace("{} {}: IL set to {}", log_name(), name(), value);
            return true;
        }

        if (lower_name == "ron") {
            if (value <= 0.0) {
                spdlog::warn("{} {}: RON must be positive (attempted: {})", log_name(), name(), value);
                return false;
            }
            ron_ = value;
            spdlog::trace("{} {}: RON set to {}", log_name(), name(), value);
            return true;
        }

        if (lower_name == "vtm") {
            if (value <= 0.0) {
                spdlog::warn("{} {}: VTM must be positive (attempted: {})", log_name(), name(), value);
                return false;
            }
            vtm_ = value;
            spdlog::trace("{} {}: VTM set to {}", log_name(), name(), value);
            return true;
        }

        spdlog::warn("{} {}: Unknown parameter '{}'", log_name(), name(), param_name);
        return false;
    }

    std::vector<std::string> get_perturbable_parameters() const override {
        return {"VBF", "IGT", "IH", "RON"};
    }

    // Accessors
    NodeId t1() const { return nodes_[0]; }
    NodeId t2() const { return nodes_[1]; }
    NodeId gate() const { return nodes_[2]; }
    Real vbf() const { return vbf_; }
    Real igt() const { return igt_; }
    Real ih() const { return ih_; }
    Real il() const { return il_; }
    Real ron() const { return ron_; }
    Real vtm() const { return vtm_; }
    bool is_latched() const { return latched_on_; }

private:
    std::vector<NodeId> nodes_;  // [0]=T1, [1]=T2, [2]=Gate

    // SCR parameters
    Real vbf_;     // Forward breakover voltage [V]
    Real igt_;     // Gate trigger current [A]
    Real ih_;      // Holding current [A]
    Real il_;      // Latching current [A]
    Real vgt_;     // Gate trigger voltage [V]
    Real ron_;     // On-state resistance [Ω]
    Real vtm_;     // On-state voltage drop [V]
    Real dv_dt_;   // dv/dt rating [V/μs]
    Real tnom_;    // Nominal temperature [K]

    // State variables
    mutable bool latched_on_;    // Latching state
    mutable Real i12_prev_;      // Previous T1-T2 current [A]
    mutable bool first_accept_;  // First accept_solution() call flag

    // Helper methods
    Real compute_gate_current(Real vg_t2) const {
        // Simplified gate junction model: Ig = Vg_t2 / Rgate
        Real rgate = std::max(vgt_ / std::max(igt_, 1e-9), 1.0);
        return vg_t2 / rgate;
    }

    Real compute_current(Real v12) const {
        if (!latched_on_) {
            return 0.0;  // Blocking state
        }

        // Conducting state: I12 = (V12 - VTM) / RON
        Real i12 = (v12 - vtm_) / std::max(ron_, 1e-6);
        return std::max(i12, 0.0);  // Clamp negative current to zero
    }

    bool check_trigger_condition(Real v12, Real ig) const {
        // Trigger if:
        // 1. Forward voltage exceeds breakover: V12 >= VBF
        // 2. Gate current exceeds trigger: Ig >= IGT (and V12 > 0)

        if (v12 >= vbf_) {
            return true;  // Breakover trigger
        }

        if (v12 > 0.0 && ig >= igt_) {
            return true;  // Gate trigger
        }

        return false;
    }

    bool check_turnoff_condition(Real i12) const {
        // Turn off if current drops below holding current
        return (latched_on_ && i12 < ih_);
    }
};

// Type aliases for backward compatibility
using SCR_N = SCR<SCRPolarity::N>;
using SCR_P = SCR<SCRPolarity::P>;

} // namespace kirimaspice::devices
