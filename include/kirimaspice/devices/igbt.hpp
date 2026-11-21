// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/igbt.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

/// IGBT polarity enumeration for N/P-channel selection
enum class IGBTPolarity { N, P };

/// The default parameters of the IGBT, the one set that the class and
/// CircuitGraph::add_igbt_n and add_igbt_p (and so a netlist's LEVEL=21
/// card) take; for the P-channel device VGE_TH is the magnitude of VEG_TH
struct IGBTDefaults {
    static constexpr Real vge_th = 5.0;     ///< Gate-emitter threshold voltage [V]
    static constexpr Real kp = 2.0;         ///< Transconductance parameter [A/V²]
    static constexpr Real vce_sat = 2.0;    ///< Collector-emitter saturation voltage [V]
    static constexpr Real tau = 1e-6;       ///< Tail current time constant [s]
    static constexpr Real area = 1.0;       ///< Area factor
    static constexpr Real cge = 5e-9;       ///< Gate-emitter capacitance [F]
    static constexpr Real cce = 5e-10;      ///< Collector-emitter capacitance [F]
    static constexpr Real kf = 0.0;         ///< Flicker noise coefficient
    static constexpr Real af = 1.0;         ///< Flicker noise current exponent
    static constexpr Real kt1 = -2e-3;      ///< VGE_TH temperature coefficient [V/K]
    static constexpr Real kt2 = 0.0;        ///< Second-order VGE_TH coefficient [V/K²]
    static constexpr Real ute = -1.5;       ///< Mobility temperature exponent
    static constexpr Real tcv = 1e-3;       ///< VCE_SAT temperature coefficient [1/K]
    static constexpr Real tnom = 300.15;    ///< Nominal temperature [K] (27 °C)
};

/// IGBT: Insulated Gate Bipolar Transistor (Unified Template)
/// Simplified Hefner model for power switching applications
///
/// Device structure:
/// - 3 terminals: Collector (C), Gate (G), Emitter (E)
/// - Combines MOSFET input (voltage-controlled gate) with BJT output (current drive)
/// - N-IGBT: NPN-like structure (electrons as majority carriers)
/// - P-IGBT: PNP-like structure (holes as majority carriers)
///
/// Operating regions:
/// 1. Cutoff (Vge < Vge_th for N-ch, Veg < Veg_th for P-ch):
///    Ic = 0
///
/// 2. Saturation (Vge > Vge_th, Vce < Vce_sat):
///    Ic = KP × (Vge - Vge_th)² × (Vce / Vce_sat)
///
/// 3. Active (Vge > Vge_th, Vce >= Vce_sat):
///    Ic = KP × (Vge - Vge_th)²
///
/// 4. Tail current (transient): the stored-carrier current Is follows the
///    channel current Ich with the lifetime TAU, dIs/dt = (Ich - Is)/TAU
///    (charge control: the stored charge TAU·Is), and the collector current
///    is max(Ich, Is). In steady state Is = Ich; after a turn-off Ich falls
///    with the gate and the collector current decays as Is, exp(-t/TAU).
///    Is is integrated exactly for Ich linear between accepted points, so
///    it does not depend on the step.
///
/// Key features:
///   - High voltage blocking capability: 600V-6.5kV
///   - High current capability: 10A-1000A+
///   - Low conduction loss (Vce_sat): 1.5-3V typical
///   - Tail current during turn-off (switching loss)
///   - MOSFET-like gate drive (voltage control)
///
/// Key parameters:
///   VGE_TH  = gate-emitter threshold voltage [V] (4-6V typical)
///   KP      = transconductance parameter [A/V²] (10-100 typical)
///   VCE_SAT = collector-emitter saturation voltage [V] (1.5-3V typical)
///   TAU     = carrier lifetime / tail current time constant [s] (1-10 μs typical)
///   AREA    = device area scaling factor (default 1.0)
///
/// Capacitance model:
///   CGE     = gate-emitter capacitance [F] (1-10nF typical)
///   CCE     = collector-emitter capacitance [F] (100-1000pF typical)
///
/// Temperature coefficients:
///   - Threshold voltage: VGE_TH(T) = VGE_TH0 + KT1×(T-Tnom) + KT2×(T-Tnom)²
///   - Transconductance: KP(T) = KP0 × (T/Tnom)^UTE
///   - Saturation voltage: VCE_SAT(T) = VCE_SAT0 × (1 + TCV×(T-Tnom))
///
template<IGBTPolarity Pol>
class IGBT : public Device {
public:
    /// Polarity sign: +1 for N-channel, -1 for P-channel
    static constexpr Real polarity_sign = (Pol == IGBTPolarity::N) ? 1.0 : -1.0;
    static constexpr bool is_n_channel = (Pol == IGBTPolarity::N);

    IGBT(DeviceId id, const std::string& name,
         NodeId collector, NodeId gate, NodeId emitter,
         // Basic IGBT parameters (IGBTDefaults)
         Real vge_th = IGBTDefaults::vge_th,
         Real kp = IGBTDefaults::kp,
         Real vce_sat = IGBTDefaults::vce_sat,
         Real tau = IGBTDefaults::tau,
         Real area = IGBTDefaults::area,
         // Capacitance parameters
         Real cge = IGBTDefaults::cge,
         Real cce = IGBTDefaults::cce,
         // Noise parameters
         Real kf = IGBTDefaults::kf,
         Real af = IGBTDefaults::af,
         // Temperature parameters
         Real kt1 = IGBTDefaults::kt1,
         Real kt2 = IGBTDefaults::kt2,
         Real ute = IGBTDefaults::ute,
         Real tcv = IGBTDefaults::tcv,
         Real tnom = IGBTDefaults::tnom)
        : Device(id, name, is_n_channel ? DeviceType::IGBT_N : DeviceType::IGBT_P),
          nodes_{collector, gate, emitter},
          collector_(collector), gate_(gate), emitter_(emitter),
          vge_th_(std::abs(vge_th)), kp_(std::abs(kp)), vce_sat_(std::abs(vce_sat)),
          tau_(std::abs(tau)), area_(std::abs(area)),
          cge_(std::abs(cge)), cce_(std::abs(cce)),
          kf_(std::abs(kf)), af_(af),
          kt1_(kt1), kt2_(kt2), ute_(ute), tcv_(tcv), tnom_(tnom),
          temperature_(tnom),
          // Initialize state variables
          vge_prev_(0.0), vce_prev_(0.0), ic_prev_(0.0),
          current_time_(0.0),
          vge_prev2_(0.0), vge_prev3_(0.0), vce_prev2_(0.0), vce_prev3_(0.0),
          vce_last_eval_(0.0) {
        spdlog::trace("IGBT<{}> '{}' created: C={} G={} E={} VGE_TH={:.2f} KP={:.1f} VCE_SAT={:.2f}",
                      is_n_channel ? "N" : "P", name, collector, gate, emitter, vge_th_, kp_, vce_sat_);
    }

    ~IGBT() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: Nonlinear device requires Newton-Raphson
    void stamp_dc_linear(MNASystem& mna,
                         const DenseVector& node_voltages) const override {
        // IGBT is nonlinear - no linear contribution
        (void)mna; (void)node_voltages;
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                            const DenseVector& node_voltages) const override {
        stamp_collector_current(mna, node_voltages, false);
    }

    // Transient analysis: Nonlinear capacitance with companion model
    void stamp_transient(MNASystem& mna,
                         const DenseVector& node_voltages,
                         Real timestep,
                         IntegrationMethod method,
                         const BDFCoefficients* bdf_coef = nullptr,
                         const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override {
        // The collector current as in DC, or the stored-carrier current
        // where it is the larger (stamp_collector_current)
        const Real ic = stamp_collector_current(mna, node_voltages, true);

        Real vc = (collector_ != NODE_GND) ? node_voltages[collector_ - 1] : 0.0;
        Real vg = (gate_ != NODE_GND) ? node_voltages[gate_ - 1] : 0.0;
        Real ve = (emitter_ != NODE_GND) ? node_voltages[emitter_ - 1] : 0.0;
        Real vge = polarity_sign * (vg - ve);
        Real vce = polarity_sign * (vc - ve);
        auto nc_idx = (collector_ == NODE_GND) ? -1 : static_cast<int>(collector_ - 1);
        auto ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
        auto ne_idx = (emitter_ == NODE_GND) ? -1 : static_cast<int>(emitter_ - 1);

        // Cge and Cce, integrated as capacitors are (device_utils::
        // capacitor_companion) from the accepted points; the voltages are
        // those of the N-like device, so for P the companion's current flows
        // the other way between the physical nodes: its nodes are swapped
        cap_dt_ = timestep;
        cap_method_ = method;
        cap_bdf_ = bdf_coef ? std::optional<BDFCoefficients>(*bdf_coef) : std::nullopt;
        cap_tr_bdf2_ = tr_bdf2_coef ? std::optional<TR_BDF2_Coefficients>(*tr_bdf2_coef) : std::nullopt;
        cap_stamped_ = true;
        const bool n_like = is_n_channel;
        const auto ge = device_utils::capacitor_companion(method, cge_, timestep, vge_prev_, vge_prev2_,
                                                          vge_prev3_, ige_prev_, bdf_coef, tr_bdf2_coef);
        const auto ce = device_utils::capacitor_companion(method, cce_, timestep, vce_prev_, vce_prev2_,
                                                          vce_prev3_, ice_prev_, bdf_coef, tr_bdf2_coef);
        device_utils::stamp_capacitor_companion(mna, n_like ? ng_idx : ne_idx, n_like ? ne_idx : ng_idx,
                                                ge.g, ge.i_eq, 1.0);
        device_utils::stamp_capacitor_companion(mna, n_like ? nc_idx : ne_idx, n_like ? ne_idx : nc_idx,
                                                ce.g, ce.i_eq, 1.0);

        spdlog::trace("IGBT<{}> '{}' TRAN: Vge={:.3f} V, Vce={:.3f} V, Ic={:.6f} A, Is={:.6f} A",
                      is_n_channel ? "N" : "P", name(), vge, vce, ic, is_prev_);
    }

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return (cge_ > 0.0 || cce_ > 0.0); }

    // AC analysis: Small-signal linearization
    void stamp_ac(ComplexMNASystem& mna, Real omega,
                  const DenseVector& dc_solution) const override {
        // The operating point, node k at element k - 1 as the AC solver
        // passes it (DCSolver::solve)
        Real vc = (collector_ != NODE_GND) ? dc_solution[collector_ - 1] : 0.0;
        Real vg = (gate_ != NODE_GND) ? dc_solution[gate_ - 1] : 0.0;
        Real ve = (emitter_ != NODE_GND) ? dc_solution[emitter_ - 1] : 0.0;

        // Compute control voltages with polarity
        Real vge = polarity_sign * (vg - ve);
        Real vce = polarity_sign * (vc - ve);

        // Small-signal parameters at DC operating point, those of the DC
        // stamp, with its gmin across C-E
        const CollectorCurrent cc = collector_current(vge, vce);
        const Real gm = cc.gm;
        const Real gds = cc.gds + kGmin;

        // Get node indices
        auto nc_idx = (collector_ == NODE_GND) ? -1 : static_cast<int>(collector_ - 1);
        auto ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
        auto ne_idx = (emitter_ == NODE_GND) ? -1 : static_cast<int>(emitter_ - 1);

        // Capacitive admittances
        Complex y_cge(0.0, omega * cge_);
        Complex y_cce(0.0, omega * cce_);

        // Stamp transconductance (same pattern for N and P channel)
        if (nc_idx >= 0) {
            if (ng_idx >= 0) {
                mna.Y.add_entry(nc_idx, ng_idx, Complex(gm, 0.0));
            }
            if (ne_idx >= 0) {
                mna.Y.add_entry(nc_idx, ne_idx, Complex(-gm, 0.0));
            }
        }
        if (ne_idx >= 0) {
            if (ng_idx >= 0) {
                mna.Y.add_entry(ne_idx, ng_idx, Complex(-gm, 0.0));
            }
            mna.Y.add_entry(ne_idx, ne_idx, Complex(gm, 0.0));
        }

        // Stamp output conductance gds
        if (gds > 0.0) {
            if (nc_idx >= 0) {
                mna.Y.add_entry(nc_idx, nc_idx, Complex(gds, 0.0));
                if (ne_idx >= 0) {
                    mna.Y.add_entry(nc_idx, ne_idx, Complex(-gds, 0.0));
                }
            }
            if (ne_idx >= 0) {
                mna.Y.add_entry(ne_idx, ne_idx, Complex(gds, 0.0));
                if (nc_idx >= 0) {
                    mna.Y.add_entry(ne_idx, nc_idx, Complex(-gds, 0.0));
                }
            }
        }

        // Stamp capacitances (Gate-Emitter)
        if (ng_idx >= 0) {
            mna.Y.add_entry(ng_idx, ng_idx, y_cge);
            if (ne_idx >= 0) {
                mna.Y.add_entry(ng_idx, ne_idx, -y_cge);
            }
        }
        if (ne_idx >= 0) {
            mna.Y.add_entry(ne_idx, ne_idx, y_cge);
            if (ng_idx >= 0) {
                mna.Y.add_entry(ne_idx, ng_idx, -y_cge);
            }
        }

        // Stamp capacitances (Collector-Emitter)
        if (nc_idx >= 0) {
            mna.Y.add_entry(nc_idx, nc_idx, y_cce);
            if (ne_idx >= 0) {
                mna.Y.add_entry(nc_idx, ne_idx, -y_cce);
            }
        }
        if (ne_idx >= 0) {
            mna.Y.add_entry(ne_idx, ne_idx, y_cce);
            if (nc_idx >= 0) {
                mna.Y.add_entry(ne_idx, nc_idx, -y_cce);
            }
        }

        Real frequency = omega / (2.0 * M_PI);
        spdlog::trace("IGBT<{}> '{}' AC: f={:.3e} Hz, gm={:.6e} S, gds={:.6e} S",
                      is_n_channel ? "N" : "P", name(), frequency, gm, gds);
    }

    // Noise analysis
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                                Real frequency) const override {
        std::vector<NoiseSource> sources;

        // At the operating point (node k at element k - 1), with the
        // collector current and gm of the DC and AC stamps
        Real vc = (collector_ != NODE_GND) ? dc_solution[collector_ - 1] : 0.0;
        Real vg = (gate_ != NODE_GND) ? dc_solution[gate_ - 1] : 0.0;
        Real ve = (emitter_ != NODE_GND) ? dc_solution[emitter_ - 1] : 0.0;
        const Real vge = polarity_sign * (vg - ve);
        const Real vce = polarity_sign * (vc - ve);
        const CollectorCurrent cc = collector_current(vge, vce);
        const Real gm = cc.gm;
        const Real ic = cc.ic;

        // Thermal noise: I_n² = 4kT × gm [A²/Hz]

        constexpr Real gamma = 2.0 / 3.0;  // Channel thermal noise coefficient
        Real thermal_psd = device_utils::thermal_noise_psd(temperature_, gm, gamma);

        // Add thermal noise source (C-E current noise)
        NoiseSource thermal;
        thermal.name = is_n_channel ? "IGBT_N_thermal" : "IGBT_P_thermal";
        thermal.node_p = collector_;
        thermal.node_n = emitter_;
        thermal.spectral_density = thermal_psd;
        thermal.is_current = true;
        sources.push_back(thermal);

        // Flicker noise: I_n² = KF × |Ic|^AF / f [A²/Hz]
        if (kf_ > 0.0 && std::abs(ic) > 1e-15 && frequency > 0.0) {
            Real flicker_noise = kf_ * std::pow(std::abs(ic), af_) / frequency;

            NoiseSource flicker;
            flicker.name = is_n_channel ? "IGBT_N_flicker" : "IGBT_P_flicker";
            flicker.node_p = collector_;
            flicker.node_n = emitter_;
            flicker.spectral_density = flicker_noise;
            flicker.is_current = true;
            sources.push_back(flicker);
        }

        return sources;
    }

    // LTE for adaptive timestepping
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override {
        (void)node_voltages;

        Real q_ge_current = cge_ * vge_prev_;
        Real q_ce_current = cce_ * vce_prev_;

        Real q_ge_prev = cge_ * vge_prev2_;
        Real q_ce_prev = cce_ * vce_prev2_;

        Real delta_q_ge = std::abs(q_ge_current - q_ge_prev);
        Real delta_q_ce = std::abs(q_ce_current - q_ce_prev);

        Real q_scale_ge = std::max(std::abs(q_ge_current), std::abs(q_ge_prev));
        Real q_scale_ce = std::max(std::abs(q_ce_current), std::abs(q_ce_prev));

        Real lte_ge = delta_q_ge / (chgtol + q_scale_ge * 1e-3);
        Real lte_ce = delta_q_ce / (chgtol + q_scale_ce * 1e-3);

        return std::sqrt(lte_ge * lte_ge + lte_ce * lte_ce);
    }

    // Temperature control
    void set_temperature(Real temp_kelvin) {
        if (temp_kelvin <= 0.0) {
            spdlog::warn("IGBT<{}> '{}': Temperature must be positive, got {} K",
                         is_n_channel ? "N" : "P", name(), temp_kelvin);
            return;
        }
        temperature_ = temp_kelvin;
        spdlog::trace("IGBT<{}> '{}': Set temperature to {} K",
                      is_n_channel ? "N" : "P", name(), temp_kelvin);
    }

    Real get_temperature() const { return temperature_; }

    // Parameter access for sensitivity analysis
    std::optional<Real> get_parameter(const std::string& param_name) const override {
        std::string param_lower = param_name;
        std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

        if (param_lower == "vge_th" || param_lower == "vth" || param_lower == "veg_th") {
            return vge_th_;
        } else if (param_lower == "kp") {
            return kp_;
        } else if (param_lower == "vce_sat" || param_lower == "vce" || param_lower == "vcesat") {
            return vce_sat_;
        } else if (param_lower == "tau") {
            return tau_;
        } else if (param_lower == "area") {
            return area_;
        } else if (param_lower == "cge" || param_lower == "ceg") {
            return cge_;
        } else if (param_lower == "cce" || param_lower == "cec") {
            return cce_;
        }
        for (const auto& entry : plain_parameters()) {
            if (param_lower == entry.name) return *entry.target;
        }

        return std::nullopt;
    }

    bool set_parameter(const std::string& param_name, Real value) override {
        std::string param_lower = param_name;
        std::transform(param_lower.begin(), param_lower.end(), param_lower.begin(), ::tolower);

        if (param_lower == "vge_th" || param_lower == "vth" || param_lower == "veg_th") {
            if (value <= 0.0) {
                spdlog::warn("IGBT<{}> '{}': VGE_TH must be positive, got {}",
                             is_n_channel ? "N" : "P", name(), value);
                return false;
            }
            vge_th_ = value;
            return true;
        } else if (param_lower == "kp") {
            if (value <= 0.0) {
                spdlog::warn("IGBT<{}> '{}': KP must be positive, got {}",
                             is_n_channel ? "N" : "P", name(), value);
                return false;
            }
            kp_ = value;
            return true;
        } else if (param_lower == "vce_sat" || param_lower == "vce" || param_lower == "vcesat") {
            if (value <= 0.0) {
                spdlog::warn("IGBT<{}> '{}': VCE_SAT must be positive, got {}",
                             is_n_channel ? "N" : "P", name(), value);
                return false;
            }
            vce_sat_ = value;
            return true;
        } else if (param_lower == "tau") {
            if (value <= 0.0) {
                spdlog::warn("IGBT<{}> '{}': TAU must be positive, got {}",
                             is_n_channel ? "N" : "P", name(), value);
                return false;
            }
            tau_ = value;
            return true;
        } else if (param_lower == "area") {
            if (value <= 0.0) {
                spdlog::warn("IGBT<{}> '{}': AREA must be positive, got {}",
                             is_n_channel ? "N" : "P", name(), value);
                return false;
            }
            area_ = value;
            return true;
        }
        for (const auto& entry : plain_parameters()) {
            if (param_lower == entry.name) {
                if (entry.positive && value <= 0.0) return false;
                *entry.target = value;
                return true;
            }
        }

        spdlog::warn("IGBT<{}> '{}': Unknown parameter '{}'",
                     is_n_channel ? "N" : "P", name(), param_name);
        return false;
    }

    struct PlainParameter {
        const char* name;
        Real* target;
        bool positive;  ///< must be > 0
    };
    // The parameters that set_parameter's own cases do not cover; TNOM in kelvin
    std::vector<PlainParameter> plain_parameters() const {
        auto* self = const_cast<IGBT*>(this);
        return {{"cge", &self->cge_, false}, {"cce", &self->cce_, false},
                {"kf", &self->kf_, false},   {"af", &self->af_, true},
                {"kt1", &self->kt1_, false}, {"kt2", &self->kt2_, false},
                {"ute", &self->ute_, false}, {"tcv", &self->tcv_, false},
                {"tnom", &self->tnom_, true}};
    }

    std::vector<std::string> get_perturbable_parameters() const override {
        return {"VGE_TH", "KP", "VCE_SAT", "TAU", "AREA"};
    }

    // State management for dynamic devices
    /// The time of the point being solved (Device::set_time): the stored-
    /// carrier current integrates from the last accepted point to it
    void set_time(std::optional<Real> time) override { current_time_ = time.value_or(0.0); }

    bool limited_in_last_stamp() const override { return limited_in_last_stamp_; }
    void set_voltage_limiting(bool enable) override { enable_voltage_limiting_ = enable; }

    void reset_state() override {
        vge_prev_ = 0.0;
        vge_prev2_ = 0.0;
        vge_prev3_ = 0.0;
        vce_prev_ = 0.0;
        vce_prev2_ = 0.0;
        vce_prev3_ = 0.0;
        ic_prev_ = 0.0;
        ige_prev_ = 0.0;
        ice_prev_ = 0.0;
        cap_stamped_ = false;
        cap_bdf_.reset();
        cap_tr_bdf2_.reset();
        is_prev_ = 0.0;
        ich_prev_ = 0.0;
        time_prev_ = 0.0;
        last_stamp_transient_ = false;
        current_time_ = 0.0;
        vce_last_eval_ = 0.0;
        limited_in_last_stamp_ = false;

        spdlog::trace("IGBT<{}> '{}' reset state", is_n_channel ? "N" : "P", name());
    }

    void accept_solution(const DenseVector& node_voltages) override {
        Real vc = (collector_ != NODE_GND) ? node_voltages[collector_] : 0.0;
        Real vg = (gate_ != NODE_GND) ? node_voltages[gate_] : 0.0;
        Real ve = (emitter_ != NODE_GND) ? node_voltages[emitter_] : 0.0;

        Real vge = polarity_sign * (vg - ve);
        Real vce = polarity_sign * (vc - ve);

        // The stored-carrier current at the accepted point: its steady state
        // at an operating point, and otherwise integrated from the last
        // accepted point (stored_carrier_current)
        const Real ich = collector_current(vge, vce).ic;
        const Real is_now = last_stamp_transient_ ? stored_carrier_current(ich).value : ich;

        // The capacitor currents at the accepted point, from the companion
        // of the step that ended there; none at a point no step reached
        if (cap_stamped_) {
            const BDFCoefficients* bdf = cap_bdf_ ? &*cap_bdf_ : nullptr;
            const TR_BDF2_Coefficients* tr = cap_tr_bdf2_ ? &*cap_tr_bdf2_ : nullptr;
            const auto ge = device_utils::capacitor_companion(cap_method_, cge_, cap_dt_, vge_prev_,
                                                              vge_prev2_, vge_prev3_, ige_prev_, bdf, tr);
            const auto ce = device_utils::capacitor_companion(cap_method_, cce_, cap_dt_, vce_prev_,
                                                              vce_prev2_, vce_prev3_, ice_prev_, bdf, tr);
            ige_prev_ = ge.g * vge + ge.i_eq;
            ice_prev_ = ce.g * vce + ce.i_eq;
        } else {
            ige_prev_ = 0.0;
            ice_prev_ = 0.0;
        }
        cap_stamped_ = false;

        // Update history
        vge_prev3_ = vge_prev2_;
        vge_prev2_ = vge_prev_;
        vge_prev_ = vge;

        vce_prev3_ = vce_prev2_;
        vce_prev2_ = vce_prev_;
        vce_prev_ = vce;

        // The collector current at the accepted point, and the state the next
        // point integrates from
        ic_prev_ = std::max(ich, is_now);
        is_prev_ = is_now;
        ich_prev_ = ich;
        time_prev_ = current_time_;
        last_stamp_transient_ = false;

        spdlog::trace("IGBT<{}> '{}' accept: Vge={:.3f} V, Vce={:.3f} V, Ic={:.6f} A",
                      is_n_channel ? "N" : "P", name(), vge, vce, ic_prev_);
    }

private:
    // Topology
    std::vector<NodeId> nodes_;
    NodeId collector_, gate_, emitter_;

    // Basic IGBT parameters (stored as positive values)
    Real vge_th_;     // Gate-emitter threshold voltage [V]
    Real kp_;         // Transconductance parameter [A/V²]
    Real vce_sat_;    // Collector-emitter saturation voltage [V]
    Real tau_;        // Carrier lifetime / tail current time constant [s]
    Real area_;       // Device area scaling factor

    // Capacitance parameters
    Real cge_;        // Gate-emitter capacitance [F]
    Real cce_;        // Collector-emitter capacitance [F]

    // Noise parameters
    Real kf_;         // Flicker noise coefficient
    Real af_;         // Flicker noise current exponent

    // Temperature parameters
    Real kt1_;        // VGE_TH temperature coefficient [V/K]
    Real kt2_;        // VGE_TH second-order temperature coefficient [V/K²]
    Real ute_;        // Mobility temperature exponent
    Real tcv_;        // VCE_SAT temperature coefficient [1/K]
    Real tnom_;       // Nominal temperature [K]
    Real temperature_; // Current temperature [K]

    // State variables for transient analysis (mutable for const stamp methods)
    mutable Real vge_prev_;   // Previous Vge [V]
    mutable Real vce_prev_;   // Previous Vce [V]
    mutable Real ic_prev_;    // Previous collector current [A]

    // The stored-carrier current Is (see the class comment) and the channel
    // current at the last accepted point, its time, and the time of the
    // point being solved
    mutable Real is_prev_ = 0.0;
    mutable Real ich_prev_ = 0.0;
    mutable Real time_prev_ = 0.0;
    mutable bool last_stamp_transient_ = false;
    mutable Real current_time_;     // Current simulation time [s]

    // The companion of Cge and Cce (device_utils::capacitor_companion): the
    // capacitor currents at the last accepted point, which the trapezoidal
    // rule needs, and the method, step and coefficients of the last stamp
    mutable Real ige_prev_ = 0.0;
    mutable Real ice_prev_ = 0.0;
    mutable Real cap_dt_ = 0.0;
    mutable IntegrationMethod cap_method_ = IntegrationMethod::BACKWARD_EULER;
    mutable std::optional<BDFCoefficients> cap_bdf_;
    mutable std::optional<TR_BDF2_Coefficients> cap_tr_bdf2_;
    mutable bool cap_stamped_ = false;

    // Voltage history (for BDF2/BDF3)
    mutable Real vge_prev2_;
    mutable Real vge_prev3_;
    mutable Real vce_prev2_;
    mutable Real vce_prev3_;

    // Newton iteration: the VCE of the last evaluation, for the step limit
    // (core::limit_fet_drain_step), and whether the last stamp limited it
    mutable Real vce_last_eval_ = 0.0;
    mutable bool limited_in_last_stamp_ = false;
    bool enable_voltage_limiting_ = true;

    /// The conductance across C-E that the DC, transient and AC stamps add
    static constexpr Real kGmin = 1e-12;

    // Helper functions
    Real compute_temperature_scaled_vge_th() const {
        Real delta_t = temperature_ - tnom_;
        return vge_th_ + kt1_ * delta_t + kt2_ * delta_t * delta_t;
    }

    Real compute_temperature_scaled_kp() const {
        return kp_ * std::pow(temperature_ / tnom_, ute_);
    }

    Real compute_temperature_scaled_vce_sat() const {
        Real delta_t = temperature_ - tnom_;
        return vce_sat_ * (1.0 + tcv_ * delta_t);
    }

    /// The collector current of the effective device and its derivatives
    struct CollectorCurrent {
        Real ic = 0.0;   ///< From collector to emitter for N, emitter to collector for P [A]
        Real gm = 0.0;   ///< d ic / d Vge_eff [S]
        Real gds = 0.0;  ///< d ic / d Vce_eff [S]
    };

    /// Ic = AREA KP(T) (VGE - VGE_TH(T))^2 times VCE / VCE_SAT(T) clamped to
    /// 0..1, and its derivatives: gm = 2 AREA KP Vov times the same factor,
    /// gds = AREA KP Vov^2 / VCE_SAT from 0 to VCE_SAT and 0 elsewhere.
    /// The DC, transient, AC and noise stamps all take it from here.
    CollectorCurrent collector_current(Real vge, Real vce) const {
        CollectorCurrent cc;
        const Real vge_th = compute_temperature_scaled_vge_th();
        if (vge <= vge_th) {
            return cc;  // cutoff
        }
        const Real kp_temp = compute_temperature_scaled_kp();
        const Real vce_sat_temp = compute_temperature_scaled_vce_sat();
        const Real vov = vge - vge_th;
        // At the two corners of the factor, VCE = 0 and VCE = VCE_SAT, the
        // slope is taken from the side between them, so that Newton's
        // iteration from a corner sees the saturation region
        Real sat_factor = 1.0;
        Real dsat_dvce = 0.0;
        if (vce_sat_temp > 0.0 && vce <= vce_sat_temp) {
            if (vce >= 0.0) {
                sat_factor = vce / vce_sat_temp;
                dsat_dvce = 1.0 / vce_sat_temp;
            } else {
                sat_factor = 0.0;
            }
        }
        cc.ic = area_ * kp_temp * vov * vov * sat_factor;
        cc.gm = 2.0 * area_ * kp_temp * vov * sat_factor;
        cc.gds = area_ * kp_temp * vov * vov * dsat_dvce;
        return cc;
    }

    /// Stamp the collector current linearized at the present iterate, with
    /// VCE limited by core::limit_fet_drain_step from its last evaluation,
    /// and the gmin across C-E. In a transient after a turn-off the current
    /// is the tail current (accept_solution) instead. Returns the current.
    Real stamp_collector_current(MNASystem& mna, const DenseVector& node_voltages,
                                 bool transient) const {
        const Real vc = (collector_ != NODE_GND) ? node_voltages[collector_ - 1] : 0.0;
        const Real vg = (gate_ != NODE_GND) ? node_voltages[gate_ - 1] : 0.0;
        const Real ve = (emitter_ != NODE_GND) ? node_voltages[emitter_ - 1] : 0.0;
        const Real vge = polarity_sign * (vg - ve);
        const Real vce_iterate = polarity_sign * (vc - ve);

        const Real vce = enable_voltage_limiting_
                             ? core::limit_fet_drain_step(vce_iterate, vce_last_eval_)
                             : vce_iterate;
        limited_in_last_stamp_ = (vce != vce_iterate);
        vce_last_eval_ = vce;

        CollectorCurrent cc = collector_current(vge, vce);
        last_stamp_transient_ = transient;
        if (transient) {
            // The stored-carrier current at this point depends on the channel
            // current here with the weight w: where it is the larger, it is
            // the collector current, with w times the channel's derivatives
            const StoredCarrier is = stored_carrier_current(cc.ic);
            if (is.value > cc.ic) {
                cc = CollectorCurrent{is.value, is.weight * cc.gm, is.weight * cc.gds};
            }
        }

        // The current from collector to emitter is ic for N and -ic for P;
        // its derivatives with respect to Vg, Vc and Ve are gm, gds and
        // -(gm + gds) for both (device_utils::stamp_vccs_3terminal,
        // stamp_conductance_ds), taken at the limited VCE
        const Real i_ce = polarity_sign * cc.ic;
        const Real i_eq = i_ce - cc.gm * (vg - ve) - cc.gds * polarity_sign * vce;
        const auto nc_idx = (collector_ == NODE_GND) ? -1 : static_cast<int>(collector_ - 1);
        const auto ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
        const auto ne_idx = (emitter_ == NODE_GND) ? -1 : static_cast<int>(emitter_ - 1);
        device_utils::stamp_vccs_3terminal(mna.G, nc_idx, ng_idx, ne_idx, cc.gm, 1.0);
        device_utils::stamp_conductance_ds(mna.G, nc_idx, ne_idx, cc.gds + kGmin);
        if (nc_idx >= 0) mna.b[nc_idx] -= i_eq;
        if (ne_idx >= 0) mna.b[ne_idx] += i_eq;

        spdlog::trace("IGBT<{}> '{}': Vge={:.3f} V, Vce={:.3f} V, Ic={:.6f} A, gm={:.6f} S",
                      is_n_channel ? "N" : "P", name(), vge, vce, cc.ic, cc.gm);
        return cc.ic;
    }

    /// The stored-carrier current at the point being solved, for the
    /// channel current ich there: the exact solution of dIs/dt =
    /// (Ich - Is)/TAU over h = t - t_prev with Ich linear from its accepted
    /// value to ich, Is = E Is_prev + (1 - E - w) Ich_prev + w ich, with
    /// E = exp(-h/TAU) and w = 1 - (TAU/h)(1 - E); weight is w
    struct StoredCarrier {
        Real value;
        Real weight;
    };
    StoredCarrier stored_carrier_current(Real ich) const {
        const Real h = current_time_ - time_prev_;
        if (!(h > 0.0) || !(tau_ > 0.0)) {
            return {ich, 1.0};
        }
        const Real x = h / tau_;
        const Real e = std::exp(-x);
        const Real one_minus_e_over_x = -std::expm1(-x) / x;  // (TAU/h)(1 - E)
        const Real w = 1.0 - one_minus_e_over_x;
        return {e * is_prev_ + (one_minus_e_over_x - e) * ich_prev_ + w * ich, w};
    }


};

// Type aliases for backward compatibility
using IGBT_N = IGBT<IGBTPolarity::N>;
using IGBT_P = IGBT<IGBTPolarity::P>;

} // namespace kirimaspice::devices
