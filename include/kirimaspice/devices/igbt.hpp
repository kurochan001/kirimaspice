// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/igbt.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

/// IGBT polarity enumeration for N/P-channel selection
enum class IGBTPolarity { N, P };

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
/// 4. Tail current (transient turn-off):
///    I_tail = I_tail_0 × exp(-Δt / TAU)
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
         // Basic IGBT parameters
         Real vge_th = 5.0,       // Gate-emitter threshold voltage [V] (4-6V typical)
         Real kp = 50.0,          // Transconductance parameter [A/V²] (10-100 typical)
         Real vce_sat = 2.0,      // Collector-emitter saturation voltage [V] (1.5-3V)
         Real tau = 1e-6,         // Carrier lifetime / tail current time constant [s] (1-10 μs)
         Real area = 1.0,         // Device area scaling factor
         // Capacitance parameters
         Real cge = 5e-9,         // Gate-emitter capacitance [F] (1-10nF typical)
         Real cce = 5e-10,        // Collector-emitter capacitance [F] (100-1000pF typical)
         // Noise parameters
         Real kf = 0.0,           // Flicker noise coefficient
         Real af = 1.0,           // Flicker noise current exponent
         // Temperature parameters
         Real kt1 = -2e-3,        // VGE_TH temperature coefficient [V/K] (-2 to -5 mV/K)
         Real kt2 = 0.0,          // VGE_TH second-order temperature coefficient [V/K²]
         Real ute = -1.5,         // Mobility temperature exponent (-1.0 to -1.8)
         Real tcv = 1e-3,         // VCE_SAT temperature coefficient [1/K] (+1 mV/K typical)
         Real tnom = 300.15)      // Nominal temperature [K] (27°C)
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
          in_tail_mode_(false), tail_current_0_(0.0), tail_start_time_(0.0), current_time_(0.0),
          vge_prev2_(0.0), vge_prev3_(0.0), vce_prev2_(0.0), vce_prev3_(0.0),
          vge_saved_(0.0), vce_saved_(0.0), ic_saved_(0.0),
          tail_mode_saved_(false), tail_current_0_saved_(0.0) {
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
        // Get terminal voltages (NodeId is 1-based, node_voltages is 0-based)
        Real vc = (collector_ != NODE_GND) ? node_voltages[collector_ - 1] : 0.0;
        Real vg = (gate_ != NODE_GND) ? node_voltages[gate_ - 1] : 0.0;
        Real ve = (emitter_ != NODE_GND) ? node_voltages[emitter_ - 1] : 0.0;

        // Compute control voltages with polarity
        // N-channel: vge = vg - ve, vce = vc - ve
        // P-channel: vge = ve - vg, vce = ve - vc (internally stored as positive)
        Real vge = polarity_sign * (vg - ve);
        Real vce = polarity_sign * (vc - ve);

        // Compute collector current and small-signal parameters
        Real ic = compute_collector_current(vge, vce);
        Real gm = compute_transconductance(vge, vce);
        Real gds = compute_output_conductance(vge, vce);

        // Norton equivalent: I = ic - gm×vge - gds×vce
        Real i_norton = ic - gm * vge - gds * vce;

        // Apply polarity for MNA stamping
        // N-channel: current flows C→E (positive ic)
        // P-channel: current flows E→C (negative ic_mna)
        Real ic_mna = polarity_sign * i_norton;

        // Get node indices for MNA matrix (0-based)
        auto nc_idx = (collector_ == NODE_GND) ? -1 : static_cast<int>(collector_ - 1);
        auto ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
        auto ne_idx = (emitter_ == NODE_GND) ? -1 : static_cast<int>(emitter_ - 1);

        // Stamp transconductance gm (Ic = gm × Vge = gm × (Vg - Ve))
        // Note: gm stamping pattern is the same for both N and P channel
        // The polarity is already handled in voltage computation and Norton current
        if (nc_idx >= 0) {
            if (ng_idx >= 0) {
                mna.G.add_entry(nc_idx, ng_idx, gm);
            }
            if (ne_idx >= 0) {
                mna.G.add_entry(nc_idx, ne_idx, -gm);
            }
        }
        if (ne_idx >= 0) {
            if (ng_idx >= 0) {
                mna.G.add_entry(ne_idx, ng_idx, -gm);
            }
            mna.G.add_entry(ne_idx, ne_idx, gm);
        }

        // Stamp output conductance gds (C-E)
        if (gds > 0.0) {
            if (nc_idx >= 0) {
                mna.G.add_entry(nc_idx, nc_idx, gds);
                if (ne_idx >= 0) {
                    mna.G.add_entry(nc_idx, ne_idx, -gds);
                }
            }
            if (ne_idx >= 0) {
                mna.G.add_entry(ne_idx, ne_idx, gds);
                if (nc_idx >= 0) {
                    mna.G.add_entry(ne_idx, nc_idx, -gds);
                }
            }
        }

        // Norton current source
        if (nc_idx >= 0) {
            mna.b[nc_idx] -= ic_mna;
        }
        if (ne_idx >= 0) {
            mna.b[ne_idx] += ic_mna;
        }

        spdlog::trace("IGBT<{}> '{}' DC: Vge={:.3f} V, Vce={:.3f} V, Ic={:.6f} A, gm={:.6f} S",
                      is_n_channel ? "N" : "P", name(), vge, vce, ic, gm);
    }

    // Transient analysis: Nonlinear capacitance with companion model
    void stamp_transient(MNASystem& mna,
                         const DenseVector& node_voltages,
                         Real timestep,
                         IntegrationMethod method,
                         const BDFCoefficients* bdf_coef = nullptr,
                         const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override {
        (void)bdf_coef;
        (void)tr_bdf2_coef;

        Real dt = timestep;

        // Get terminal voltages
        Real vc = (collector_ != NODE_GND) ? node_voltages[collector_ - 1] : 0.0;
        Real vg = (gate_ != NODE_GND) ? node_voltages[gate_ - 1] : 0.0;
        Real ve = (emitter_ != NODE_GND) ? node_voltages[emitter_ - 1] : 0.0;

        // Compute control voltages with polarity
        Real vge = polarity_sign * (vg - ve);
        Real vce = polarity_sign * (vc - ve);

        // Update tail current mode check
        Real vge_th = compute_temperature_scaled_vge_th();
        bool was_active = (vge_prev_ >= vge_th);
        bool is_active = (vge >= vge_th);

        // Detect turn-off transition (active → cutoff)
        Real ic = 0.0;
        if (was_active && !is_active && !in_tail_mode_) {
            // Enter tail current mode
            in_tail_mode_ = true;
            tail_current_0_ = ic_prev_;
            tail_start_time_ = current_time_;
        }

        // Compute collector current
        if (in_tail_mode_) {
            Real delta_t = current_time_ - tail_start_time_;
            ic = compute_tail_current(delta_t);

            // Exit tail mode when current becomes negligible
            if (ic < 1e-12) {
                in_tail_mode_ = false;
                ic = 0.0;
            }
        } else {
            ic = compute_collector_current(vge, vce);
        }

        // Compute small-signal parameters
        Real gm = compute_transconductance(vge, vce);
        Real gds = compute_output_conductance(vge, vce);

        // Save state for possible rejection
        vge_saved_ = vge_prev_;
        vce_saved_ = vce_prev_;
        ic_saved_ = ic_prev_;
        tail_mode_saved_ = in_tail_mode_;
        tail_current_0_saved_ = tail_current_0_;

        // Norton equivalent
        Real i_norton = polarity_sign * (ic - gm * vge - gds * vce);

        // Get node indices
        auto nc_idx = (collector_ == NODE_GND) ? -1 : static_cast<int>(collector_ - 1);
        auto ng_idx = (gate_ == NODE_GND) ? -1 : static_cast<int>(gate_ - 1);
        auto ne_idx = (emitter_ == NODE_GND) ? -1 : static_cast<int>(emitter_ - 1);

        // Stamp conductances based on operating mode
        if (vge < vge_th) {
            // Cutoff: add small conductance for numerical stability
            constexpr Real gmin = 1e-12;
            if (nc_idx >= 0) {
                mna.G.add_entry(nc_idx, nc_idx, gmin);
                if (ne_idx >= 0) {
                    mna.G.add_entry(nc_idx, ne_idx, -gmin);
                }
            }
            if (ne_idx >= 0) {
                mna.G.add_entry(ne_idx, ne_idx, gmin);
                if (nc_idx >= 0) {
                    mna.G.add_entry(ne_idx, nc_idx, -gmin);
                }
            }
        } else {
            // Active/tail mode - stamp transconductance (same for N and P channel)
            if (nc_idx >= 0) {
                if (ng_idx >= 0) {
                    mna.G.add_entry(nc_idx, ng_idx, gm);
                }
                if (ne_idx >= 0) {
                    mna.G.add_entry(nc_idx, ne_idx, -gm);
                }
            }
            if (ne_idx >= 0) {
                if (ng_idx >= 0) {
                    mna.G.add_entry(ne_idx, ng_idx, -gm);
                }
                mna.G.add_entry(ne_idx, ne_idx, gm);
            }

            // Stamp output conductance gds
            if (gds > 0.0) {
                if (nc_idx >= 0) {
                    mna.G.add_entry(nc_idx, nc_idx, gds);
                    if (ne_idx >= 0) {
                        mna.G.add_entry(nc_idx, ne_idx, -gds);
                    }
                }
                if (ne_idx >= 0) {
                    mna.G.add_entry(ne_idx, ne_idx, gds);
                    if (nc_idx >= 0) {
                        mna.G.add_entry(ne_idx, nc_idx, -gds);
                    }
                }
            }
        }

        // Norton current source
        if (nc_idx >= 0) {
            mna.b[nc_idx] -= i_norton;
        }
        if (ne_idx >= 0) {
            mna.b[ne_idx] += i_norton;
        }

        // Gate-Emitter capacitance companion model
        Real dvge_dt = 0.0;
        if (method == IntegrationMethod::BACKWARD_EULER) {
            dvge_dt = (vge - vge_prev_) / dt;
        } else if (method == IntegrationMethod::TRAPEZOIDAL) {
            dvge_dt = (vge - vge_prev_) / dt;
        } else if (method == IntegrationMethod::GEAR2) {
            dvge_dt = (1.5 * vge - 2.0 * vge_prev_ + 0.5 * vge_prev2_) / dt;
        } else if (method == IntegrationMethod::GEAR3) {
            dvge_dt = (11.0/6.0 * vge - 3.0 * vge_prev_ + 1.5 * vge_prev2_ - 1.0/3.0 * vge_prev3_) / dt;
        }

        Real i_cge = cge_ * dvge_dt;
        Real g_cge = cge_ / dt;
        Real ieq_cge = i_cge - g_cge * vge;

        // Stamp Cge (Gate-Emitter capacitance)
        // Note: For P-channel, the capacitance stamps are the same (between physical nodes)
        if (ng_idx >= 0) {
            mna.G.add_entry(ng_idx, ng_idx, g_cge);
            if (ne_idx >= 0) {
                mna.G.add_entry(ng_idx, ne_idx, -g_cge);
            }
            mna.b[ng_idx] -= polarity_sign * ieq_cge;
        }
        if (ne_idx >= 0) {
            mna.G.add_entry(ne_idx, ne_idx, g_cge);
            if (ng_idx >= 0) {
                mna.G.add_entry(ne_idx, ng_idx, -g_cge);
            }
            mna.b[ne_idx] += polarity_sign * ieq_cge;
        }

        // Collector-Emitter capacitance companion model
        Real dvce_dt = 0.0;
        if (method == IntegrationMethod::BACKWARD_EULER) {
            dvce_dt = (vce - vce_prev_) / dt;
        } else if (method == IntegrationMethod::TRAPEZOIDAL) {
            dvce_dt = (vce - vce_prev_) / dt;
        } else if (method == IntegrationMethod::GEAR2) {
            dvce_dt = (1.5 * vce - 2.0 * vce_prev_ + 0.5 * vce_prev2_) / dt;
        } else if (method == IntegrationMethod::GEAR3) {
            dvce_dt = (11.0/6.0 * vce - 3.0 * vce_prev_ + 1.5 * vce_prev2_ - 1.0/3.0 * vce_prev3_) / dt;
        }

        Real i_cce = cce_ * dvce_dt;
        Real g_cce = cce_ / dt;
        Real ieq_cce = i_cce - g_cce * vce;

        // Stamp Cce (Collector-Emitter capacitance)
        if (nc_idx >= 0) {
            mna.G.add_entry(nc_idx, nc_idx, g_cce);
            if (ne_idx >= 0) {
                mna.G.add_entry(nc_idx, ne_idx, -g_cce);
            }
            mna.b[nc_idx] -= polarity_sign * ieq_cce;
        }
        if (ne_idx >= 0) {
            mna.G.add_entry(ne_idx, ne_idx, g_cce);
            if (nc_idx >= 0) {
                mna.G.add_entry(ne_idx, nc_idx, -g_cce);
            }
            mna.b[ne_idx] += polarity_sign * ieq_cce;
        }

        spdlog::trace("IGBT<{}> '{}' TRAN: Vge={:.3f} V, Vce={:.3f} V, Ic={:.6f} A, tail={}",
                      is_n_channel ? "N" : "P", name(), vge, vce, ic, in_tail_mode_);
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

        // Small-signal parameters at DC operating point
        Real gm = compute_transconductance(vge, vce);
        Real gds = compute_output_conductance(vge, vce);

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
        const Real gm = compute_transconductance(vge, vce);
        const Real ic = compute_collector_current(vge, vce);

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
    /// The time since turn-off of the tail current (Device::set_time)
    void set_time(std::optional<Real> time) override { current_time_ = time.value_or(0.0); }

    void reset_state() override {
        vge_prev_ = 0.0;
        vge_prev2_ = 0.0;
        vge_prev3_ = 0.0;
        vce_prev_ = 0.0;
        vce_prev2_ = 0.0;
        vce_prev3_ = 0.0;
        ic_prev_ = 0.0;
        in_tail_mode_ = false;
        tail_current_0_ = 0.0;
        tail_start_time_ = 0.0;
        current_time_ = 0.0;

        spdlog::trace("IGBT<{}> '{}' reset state", is_n_channel ? "N" : "P", name());
    }

    void accept_solution(const DenseVector& node_voltages) override {
        Real vc = (collector_ != NODE_GND) ? node_voltages[collector_] : 0.0;
        Real vg = (gate_ != NODE_GND) ? node_voltages[gate_] : 0.0;
        Real ve = (emitter_ != NODE_GND) ? node_voltages[emitter_] : 0.0;

        Real vge = polarity_sign * (vg - ve);
        Real vce = polarity_sign * (vc - ve);

        // Update history
        vge_prev3_ = vge_prev2_;
        vge_prev2_ = vge_prev_;
        vge_prev_ = vge;

        vce_prev3_ = vce_prev2_;
        vce_prev2_ = vce_prev_;
        vce_prev_ = vce;

        // Update collector current
        if (in_tail_mode_) {
            Real delta_t = current_time_ - tail_start_time_;
            ic_prev_ = compute_tail_current(delta_t);
        } else {
            ic_prev_ = compute_collector_current(vge, vce);
        }

        spdlog::trace("IGBT<{}> '{}' accept: Vge={:.3f} V, Vce={:.3f} V, Ic={:.6f} A",
                      is_n_channel ? "N" : "P", name(), vge, vce, ic_prev_);
    }

    void reject_solution() {
        vge_prev_ = vge_saved_;
        vce_prev_ = vce_saved_;
        ic_prev_ = ic_saved_;
        in_tail_mode_ = tail_mode_saved_;
        tail_current_0_ = tail_current_0_saved_;

        spdlog::trace("IGBT<{}> '{}' reject solution", is_n_channel ? "N" : "P", name());
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

    // Tail current state
    mutable bool in_tail_mode_;     // True if in tail current mode
    mutable Real tail_current_0_;   // Initial tail current [A]
    mutable Real tail_start_time_;  // Time when tail current started [s]
    mutable Real current_time_;     // Current simulation time [s]

    // Voltage history (for BDF2/BDF3)
    mutable Real vge_prev2_;
    mutable Real vge_prev3_;
    mutable Real vce_prev2_;
    mutable Real vce_prev3_;

    // Saved state for solution rejection
    mutable Real vge_saved_;
    mutable Real vce_saved_;
    mutable Real ic_saved_;
    mutable bool tail_mode_saved_;
    mutable Real tail_current_0_saved_;

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

    Real compute_collector_current(Real vge, Real vce) const {
        Real vge_th = compute_temperature_scaled_vge_th();
        Real kp_temp = compute_temperature_scaled_kp();
        Real vce_sat_temp = compute_temperature_scaled_vce_sat();

        if (vge <= vge_th) {
            // Cutoff region
            return 0.0;
        }

        Real vov = vge - vge_th;  // Overdrive voltage

        // Saturation factor
        Real sat_factor = 1.0;
        if (vce < vce_sat_temp && vce_sat_temp > 0.0) {
            sat_factor = vce / vce_sat_temp;
            sat_factor = std::max(0.0, std::min(1.0, sat_factor));
        }

        // Collector current: Ic = KP × (Vge - Vth)² × sat_factor
        Real ic = area_ * kp_temp * vov * vov * sat_factor;

        return ic;
    }

    Real compute_tail_current(Real delta_t) const {
        if (delta_t < 0.0) return tail_current_0_;
        return tail_current_0_ * std::exp(-delta_t / tau_);
    }

    Real compute_transconductance(Real vge, Real vce) const {
        Real vge_th = compute_temperature_scaled_vge_th();
        Real kp_temp = compute_temperature_scaled_kp();
        Real vce_sat_temp = compute_temperature_scaled_vce_sat();

        if (vge <= vge_th) {
            return 0.0;  // Cutoff
        }

        Real vov = vge - vge_th;

        // Saturation factor
        Real sat_factor = 1.0;
        if (vce < vce_sat_temp && vce_sat_temp > 0.0) {
            sat_factor = vce / vce_sat_temp;
            sat_factor = std::max(0.0, std::min(1.0, sat_factor));
        }

        // gm = dIc/dVge = 2 × KP × (Vge - Vth) × sat_factor
        return 2.0 * area_ * kp_temp * vov * sat_factor;
    }

    Real compute_output_conductance(Real vge, Real vce) const {
        Real vge_th = compute_temperature_scaled_vge_th();
        Real kp_temp = compute_temperature_scaled_kp();
        Real vce_sat_temp = compute_temperature_scaled_vce_sat();

        if (vge <= vge_th) {
            return 0.0;  // Cutoff
        }

        Real vov = vge - vge_th;

        // In saturation region (Vce < Vce_sat), gds = dIc/dVce
        if (vce < vce_sat_temp && vce_sat_temp > 0.0) {
            // gds = KP × (Vge - Vth)² / Vce_sat
            return area_ * kp_temp * vov * vov / vce_sat_temp;
        }

        // In active region (Vce >= Vce_sat), small output conductance
        return 1e-12;  // Small value for numerical stability
    }
};

// Type aliases for backward compatibility
using IGBT_N = IGBT<IGBTPolarity::N>;
using IGBT_P = IGBT<IGBTPolarity::P>;

} // namespace kirimaspice::devices
