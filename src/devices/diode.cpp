// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/diode.cpp
#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/core/device_utils.hpp"
#include "kirimaspice/core/constants.hpp"  // For safe_get_node_voltage
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

Diode::Diode(DeviceId id, const std::string& name,
             NodeId anode, NodeId cathode,
             Real is, Real n, Real cj0, Real vj, Real m,
             Real xti, Real eg, Real tnom)
    : Device(id, name, DeviceType::DIODE)
    , nodes_{anode, cathode}
    , is_(is)
    , n_(n)
    , vt_(k_boltzmann * tnom / q_electron)  // Vt at nominal temperature
    , cj0_(cj0)
    , vj_(vj)
    , m_(m)
    , xti_(xti)
    , eg_(eg)
    , tnom_(tnom)
    , is_tnom_(is)  // Store nominal IS
    , voltage_prev_(0.0)
    , voltage_prev2_(0.0)
    , voltage_prev3_(0.0)
    , voltage_saved_(0.0)
    , voltage_saved2_(0.0)
    , voltage_saved3_(0.0)
    , charge_(0.0)
    , charge_prev_(0.0)
    , timestep_(0.0)
    , last_method_(IntegrationMethod::BACKWARD_EULER)
    , timestep_saved_(0.0)
    , method_saved_(IntegrationMethod::BACKWARD_EULER)
    , first_accept_(true) {

    if (is <= 0.0) {
        throw std::runtime_error("Diode saturation current must be positive: " + name);
    }
    if (n <= 0.0) {
        throw std::runtime_error("Diode emission coefficient must be positive: " + name);
    }
    if (cj0 < 0.0) {
        throw std::runtime_error("Diode junction capacitance must be non-negative: " + name);
    }
    if (vj <= 0.0 && cj0 > 0.0) {
        throw std::runtime_error("Diode junction potential must be positive when Cj0 > 0: " + name);
    }
    if (m <= 0.0 && cj0 > 0.0) {
        throw std::runtime_error("Diode grading coefficient must be positive when Cj0 > 0: " + name);
    }

    spdlog::debug("Created diode {} (anode={}, cathode={}, Is={:.6e}A, N={:.3f}, Vt={:.6f}V, Cj0={:.6e}F)",
                  name, anode, cathode, is_, n_, vt_, cj0_);
}

void Diode::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }

    // Update thermal voltage using utility function
    vt_ = core::compute_thermal_voltage(temp_kelvin);

    // Update saturation current IS(T) using utility function
    // SPICE equation: IS(T) = IS(Tnom) * (T/Tnom)^(XTI/N) * exp(-EG/N * (1/Vt(T) - 1/Vt(Tnom)))
    is_ = core::scale_saturation_current(is_tnom_, xti_, eg_, n_, temp_kelvin, tnom_);

    spdlog::debug("{} set temperature to {:.2f}K (Vt={:.6f}V, IS={:.6e}A)",
                  name_, temp_kelvin, vt_, is_);
}

Real Diode::get_current(Real voltage) const {
    // Shockley equation: I = Is * (exp(V/(N*Vt)) - 1)
    // Use safe_exp_junction for numerical stability
    Real n_vt = n_ * vt_;
    return core::safe_exp_junction(voltage, n_vt, is_);
}

Real Diode::get_conductance(Real voltage) const {
    // The junction's conductance with the GMIN that SPICE puts in parallel
    // with it: the derivative of the current the stamps take,
    // Is (exp(V/(N Vt)) - 1) + GMIN V
    return get_small_signal_conductance(voltage) + gmin_default;
}

void Diode::stamp_dc_nonlinear(MNASystem& mna,
                               const DenseVector& node_voltages) const {
    // Newton-Raphson linearization:
    // I(V) = Is * (exp(V/(N*Vt)) - 1)
    // g_eq = dI/dV = (Is/(N*Vt)) * exp(V/(N*Vt))
    // I_eq = I(V) - g_eq * V
    //
    // With voltage limiting, the junction is evaluated at V_lim, the voltage
    // of this iterate with its step from the last evaluation limited
    // (core::limit_junction_step), and linearized there:
    // I_eq = I(V_lim) - g(V_lim) * V_lim
    // At convergence V_lim = V, so the solution follows I(V).
    //
    // MNA stamp (same pattern as resistor + current source):
    // G matrix: stamp g_eq
    // b vector: stamp I_eq (current flows from anode to cathode)

    NodeId anode_node = nodes_[0];
    NodeId cathode_node = nodes_[1];

    // Get anode and cathode voltages with bounds checking
    // Use safe_get_node_voltage for bounds checking
    Real v_anode = core::safe_get_node_voltage(node_voltages, anode_node);
    Real v_cathode = core::safe_get_node_voltage(node_voltages, cathode_node);
    Real voltage = v_anode - v_cathode;

    // Limit the step of the junction voltage from its last evaluation
    Real n_vt = n_ * vt_;
    Real voltage_limited = voltage;
    if (enable_voltage_limiting_) {
        voltage_limited = core::limit_junction_step(voltage, v_last_eval_, n_vt,
                                                    core::compute_vcrit(n_vt, is_));
    }
    v_last_eval_ = voltage_limited;
    limited_in_last_stamp_ = (voltage_limited != voltage);

    // Linearize the junction, with its GMIN in parallel (get_conductance),
    // at the voltage where it is evaluated
    Real current = get_current(voltage_limited) + gmin_default * voltage_limited;
    Real g_eq = get_conductance(voltage_limited);
    Real i_eq = current - g_eq * voltage_limited;

    spdlog::trace("{} stamp_nonlinear: V={:.6f}V, V_lim={:.6f}V, I={:.6e}A, g_eq={:.6e}S, I_eq={:.6e}A",
                 name_, voltage, voltage_limited, current, g_eq, i_eq);

    // Convert to MNA indices
    auto anode_idx = (anode_node == NODE_GND) ? -1 : static_cast<int>(anode_node - 1);
    auto cathode_idx = (cathode_node == NODE_GND) ? -1 : static_cast<int>(cathode_node - 1);

    // Stamp G matrix (conductance)
    if (anode_idx >= 0) {
        mna.G.add_entry(anode_idx, anode_idx, g_eq);
        if (cathode_idx >= 0) {
            mna.G.add_entry(anode_idx, cathode_idx, -g_eq);
        }
    }

    if (cathode_idx >= 0) {
        mna.G.add_entry(cathode_idx, cathode_idx, g_eq);
        if (anode_idx >= 0) {
            mna.G.add_entry(cathode_idx, anode_idx, -g_eq);
        }
    }

    // Stamp RHS (equivalent current source)
    // Current flows from anode (+) to cathode (-)
    if (anode_idx >= 0) {
        mna.b[anode_idx] -= i_eq;  // Current flows OUT of anode
    }
    if (cathode_idx >= 0) {
        mna.b[cathode_idx] += i_eq;  // Current flows INTO cathode
    }
}

Real Diode::get_small_signal_conductance(Real voltage) const {
    // Small-signal conductance: gd = dI/dV at DC operating point
    // This is the same as get_conductance() but without gmin
    Real n_vt = n_ * vt_;

    // Use named constants
    if (voltage < core::JUNCTION_CUTOFF_FACTOR * n_vt) {
        // Reverse bias: gd ≈ 0
        return 0.0;
    }

    Real v_normalized = voltage / n_vt;
    if (v_normalized > core::EXP_OVERFLOW_LIMIT) {
        // For very large forward voltage, use constant conductance
        Real exp_limit = std::exp(core::EXP_OVERFLOW_LIMIT);
        return is_ * exp_limit / n_vt;
    }

    // Normal range: gd = (Is/(N*Vt)) * exp(V/(N*Vt))
    return is_ * std::exp(v_normalized) / n_vt;
}

Real Diode::get_junction_capacitance(Real voltage) const {
    // Junction capacitance (SPICE model):
    // For reverse bias (V < Vj): Cj = Cj0 / (1 - V/Vj)^M
    // For forward bias (V >= Vj): use linear approximation to avoid singularity

    if (cj0_ == 0.0) {
        return 0.0;  // No junction capacitance
    }

    // Forward bias limit (FC parameter in SPICE, typically 0.5)
    constexpr Real FC = 0.5;
    Real fc_vj = FC * vj_;

    if (voltage < fc_vj) {
        // Normal reverse bias region
        Real factor = 1.0 - voltage / vj_;
        if (factor <= 0.0) {
            // Avoid singularity
            spdlog::warn("{} junction capacitance singularity at V={:.3f}V (Vj={:.3f}V)",
                        name_, voltage, vj_);
            return cj0_ * 100.0;  // Arbitrary large value
        }
        return cj0_ / std::pow(factor, m_);
    } else {
        // Forward bias: linear extrapolation from FC*Vj
        // Cj(V) = Cj(FC*Vj) * (1 + M*(V - FC*Vj)/Vj)
        Real cj_fc = cj0_ / std::pow(1.0 - FC, m_);
        Real slope = m_ * cj_fc / vj_;
        return cj_fc + slope * (voltage - fc_vj);
    }
}

void Diode::stamp_transient(MNASystem& mna,
                            const DenseVector& node_voltages,
                            Real timestep,
                            IntegrationMethod method,
                            const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Transient analysis with nonlinear junction capacitance
    //
    // Diode transient model = DC nonlinear current + junction capacitance current
    // I_total = I_DC(V) + I_C(dV/dt)
    //
    // Strategy:
    // 1. Stamp DC nonlinear model (Newton-Raphson linearization of Shockley equation)
    // 2. If junction capacitance present (Cj0 > 0), add companion model for dV/dt

    // Step 1: Stamp DC nonlinear model
    stamp_dc_nonlinear(mna, node_voltages);

    // Step 2: Check if junction capacitance is present
    if (cj0_ == 0.0) {
        // No junction capacitance, transient = DC
        return;
    }

    // Save current timestep and method for accept_solution()
    timestep_ = timestep;
    last_method_ = method;

    // Get node voltages with bounds checking
    // Use safe_get_node_voltage for bounds checking
    NodeId anode_node = nodes_[0];
    NodeId cathode_node = nodes_[1];
    Real v_anode = core::safe_get_node_voltage(node_voltages, anode_node);
    Real v_cathode = core::safe_get_node_voltage(node_voltages, cathode_node);
    Real voltage = v_anode - v_cathode;

    // The junction capacitance at the voltage of the last accepted point,
    // integrated as a capacitor is (device_utils::capacitor_companion) from
    // the accepted voltages and, for the trapezoidal rule, the capacitor's
    // current at the last accepted point
    Real cj = get_junction_capacitance(voltage_prev_);
    cap_bdf_ = bdf_coef ? std::optional<BDFCoefficients>(*bdf_coef) : std::nullopt;
    cap_tr_bdf2_ = tr_bdf2_coef ? std::optional<TR_BDF2_Coefficients>(*tr_bdf2_coef) : std::nullopt;
    cap_stamped_ = true;
    const auto cc = device_utils::capacitor_companion(method, cj, timestep, voltage_prev_,
                                                      voltage_prev2_, voltage_prev3_,
                                                      current_prev_, bdf_coef, tr_bdf2_coef);
    const int anode_idx = (anode_node == NODE_GND) ? -1 : static_cast<int>(anode_node - 1);
    const int cathode_idx = (cathode_node == NODE_GND) ? -1 : static_cast<int>(cathode_node - 1);
    device_utils::stamp_capacitor_companion(mna, anode_idx, cathode_idx, cc.g, cc.i_eq, 1.0);

    // Update charge for LTE calculation
    charge_ = cj * voltage;
}

void Diode::stamp_ac(ComplexMNASystem& sys, Real omega,
                     const DenseVector& dc_solution) const {
    // Small-signal AC analysis:
    // Y(jω) = gd + jωCj
    //
    // where:
    //   gd = small-signal conductance at DC operating point
    //   Cj = junction capacitance at DC operating point

    NodeId anode_node = nodes_[0];
    NodeId cathode_node = nodes_[1];

    // Get DC operating point voltage
    Real v_anode = (anode_node == NODE_GND) ? 0.0 : dc_solution[anode_node - 1];
    Real v_cathode = (cathode_node == NODE_GND) ? 0.0 : dc_solution[cathode_node - 1];
    Real voltage = v_anode - v_cathode;

    // Calculate small-signal parameters
    Real gd = get_conductance(voltage);
    Real cj = get_junction_capacitance(voltage);

    // Complex admittance: Y = gd + jωCj
    Complex admittance(gd, omega * cj);

    spdlog::trace("{} stamp_ac: V_dc={:.6f}V, gd={:.6e}S, Cj={:.6e}F, ω={:.3e}rad/s, Y={:.6e}+j{:.6e}",
                 name_, voltage, gd, cj, omega, admittance.real(), admittance.imag());

    // Convert to MNA indices
    auto anode_idx = (anode_node == NODE_GND) ? -1 : static_cast<int>(anode_node - 1);
    auto cathode_idx = (cathode_node == NODE_GND) ? -1 : static_cast<int>(cathode_node - 1);

    // Stamp Y matrix (same pattern as resistor/capacitor)
    if (anode_idx >= 0) {
        sys.Y.add_entry(anode_idx, anode_idx, admittance);
        if (cathode_idx >= 0) {
            sys.Y.add_entry(anode_idx, cathode_idx, -admittance);
        }
    }

    if (cathode_idx >= 0) {
        sys.Y.add_entry(cathode_idx, cathode_idx, admittance);
        if (anode_idx >= 0) {
            sys.Y.add_entry(cathode_idx, anode_idx, -admittance);
        }
    }

    // No current source contribution for AC analysis (linearized at DC operating point)
}

std::vector<NoiseSource> Diode::get_noise_sources(const DenseVector& dc_solution,
                                                    Real frequency) const {
    // Shot noise: I_n² = 2qI_d (A²/Hz)
    // This is the Schottky shot noise arising from discrete charge carrier flow
    //
    // Physical constants:
    // - q = 1.602176634e-19 C (elementary charge)
    // - I_d = DC diode current [A]
    //
    // Derivation:
    // Shot noise arises from quantization of charge carriers
    // Spectral density is frequency-independent (white noise)
    // Proportional to DC current flowing through the device
    //
    // The noise is represented as a current source in parallel with the diode

    (void)frequency;  // White noise - frequency-independent

    // Get DC operating point voltage
    NodeId anode_node = nodes_[0];
    NodeId cathode_node = nodes_[1];

    Real V_anode = (anode_node == NODE_GND) ? 0.0 : dc_solution[anode_node - 1];
    Real V_cathode = (cathode_node == NODE_GND) ? 0.0 : dc_solution[cathode_node - 1];
    Real V_d = V_anode - V_cathode;  // Diode voltage (anode - cathode)

    // Calculate DC current through diode
    Real I_d = get_current(V_d);

    // Shot noise is only significant for forward-biased diodes (I_d > 0)
    // For reverse bias or very small currents, noise is negligible
    if (I_d < 1e-15) {  // Threshold: 1fA
        return {};  // No significant shot noise
    }

    // Calculate spectral density: I_n² = 2qI_d (A²/Hz)
    Real I_n_squared = 2.0 * q_electron * I_d;

    NoiseSource shot;
    shot.name = name_ + "_shot";
    shot.node_p = nodes_[0];  // Anode
    shot.node_n = nodes_[1];  // Cathode
    shot.spectral_density = I_n_squared;
    shot.is_current = true;  // Current noise source

    spdlog::trace("Diode {} shot noise: I_n² = {:.3e} A²/Hz (I_d = {:.3e} A, V_d = {:.3e} V)",
                  name_, I_n_squared, I_d, V_d);

    return {shot};
}

void Diode::save_state() {
    voltage_saved_ = voltage_prev_;
    voltage_saved2_ = voltage_prev2_;
    voltage_saved3_ = voltage_prev3_;
    timestep_saved_ = timestep_;
    method_saved_ = last_method_;
    current_saved_ = current_prev_;
}

void Diode::restore_state() {
    voltage_prev_ = voltage_saved_;
    voltage_prev2_ = voltage_saved2_;
    voltage_prev3_ = voltage_saved3_;
    timestep_ = timestep_saved_;
    last_method_ = method_saved_;
    current_prev_ = current_saved_;
}

void Diode::reset_state() {
    voltage_prev_ = voltage_prev2_ = voltage_prev3_ = 0.0;
    voltage_saved_ = voltage_saved2_ = voltage_saved3_ = 0.0;
    charge_ = charge_prev_ = 0.0;
    timestep_ = timestep_saved_ = 0.0;
    last_method_ = method_saved_ = IntegrationMethod::BACKWARD_EULER;
    current_prev_ = current_saved_ = 0.0;
    cap_bdf_.reset();
    cap_tr_bdf2_.reset();
    cap_stamped_ = false;
    first_accept_ = true;
    v_last_eval_ = 0.0;
    limited_in_last_stamp_ = false;
}

void Diode::accept_solution(const DenseVector& node_voltages) {
    // Update voltage history for next timestep
    // Use safe_get_node_voltage for bounds checking
    // NOTE: Original code had bug - used [anode_node] without -1 offset
    NodeId anode_node = nodes_[0];
    NodeId cathode_node = nodes_[1];

    Real v_anode = core::accepted_node_voltage(node_voltages, anode_node);
    Real v_cathode = core::accepted_node_voltage(node_voltages, cathode_node);

    Real voltage_new = v_anode - v_cathode;

    spdlog::trace("{} accept (before): V_prev={:.6e}, V_new={:.6e}",
                 name_, voltage_prev_, voltage_new);

    // The capacitor's current at the accepted point, from the companion of
    // the step that ended there; 0 at a point no transient step reached (the
    // operating point, or the start under UIC)
    if (cap_stamped_ && cj0_ > 0.0) {
        const Real cj = get_junction_capacitance(voltage_prev_);
        const BDFCoefficients* bdf = cap_bdf_ ? &*cap_bdf_ : nullptr;
        const TR_BDF2_Coefficients* tr = cap_tr_bdf2_ ? &*cap_tr_bdf2_ : nullptr;
        const auto cc = device_utils::capacitor_companion(last_method_, cj, timestep_, voltage_prev_,
                                                          voltage_prev2_, voltage_prev3_,
                                                          current_prev_, bdf, tr);
        current_prev_ = cc.g * voltage_new + cc.i_eq;
    } else {
        current_prev_ = 0.0;
    }
    cap_stamped_ = false;

    // Shift history for BDF methods (must be done AFTER using current values)
    voltage_prev3_ = voltage_prev2_;
    voltage_prev2_ = voltage_prev_;
    voltage_prev_ = voltage_new;

    // Update charge and save previous charge
    // On first call, initialize charge_prev from current state
    if (first_accept_) {
        // First accept_solution(): Initialize history from DC operating point
        Real cj = get_junction_capacitance(voltage_prev_);
        charge_ = cj * voltage_prev_;
        charge_prev_ = charge_;  // No prediction on first step
        first_accept_ = false;
        spdlog::trace("{} accept_first: charge initialized to {:.6e}", name_, charge_);
    } else {
        // Subsequent calls: Normal history update
        charge_prev_ = charge_;
        Real cj = get_junction_capacitance(voltage_prev_);
        charge_ = cj * voltage_prev_;
    }

    spdlog::trace("{} accept (after): V_prev={:.6e}, V_prev2={:.6e}, V_prev3={:.6e}",
                 name_, voltage_prev_, voltage_prev2_, voltage_prev3_);
}

Real Diode::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    // Physical quantity-based error estimation
    // For diode junction capacitance: Q = Cj(V) * V
    //
    // Predictor-corrector approach:
    //   Predictor: Q_pred = Q_prev (no prediction, use last value)
    //   Corrector: Q_corr = Cj(V_new) * V_new (actual computed result)
    //   Error: ΔQ = |Q_corr - Q_pred|

    // If no junction capacitance, return zero error
    if (cj0_ == 0.0) {
        return 0.0;
    }

    // Get voltage from new solution with bounds checking
    // Use safe_get_node_voltage for bounds checking
    // NOTE: Original code had bug - used [anode_node] without -1 offset
    NodeId anode_node = nodes_[0];
    NodeId cathode_node = nodes_[1];
    Real v_anode = core::safe_get_node_voltage(node_voltages, anode_node);
    Real v_cathode = core::safe_get_node_voltage(node_voltages, cathode_node);
    Real voltage_new = v_anode - v_cathode;

    // Predictor: Use previous charge (simple 0th order hold)
    Real Q_pred = charge_prev_;

    // Corrector: Q_corr = Cj(V_new) * V_new
    Real cj_new = get_junction_capacitance(voltage_new);
    Real Q_corr = cj_new * voltage_new;

    // Error: ΔQ = |Q_corr - Q_pred|
    Real Q_error = std::abs(Q_corr - Q_pred);

    // ngspice-compatible scaling: max(|Q|) + chgtol
    Real Q_scale = std::max(std::abs(Q_corr), std::abs(Q_pred)) + chgtol;

    // Normalized error: LTE = ΔQ / Q_scale
    Real lte = Q_error / Q_scale;

    spdlog::trace("{} physical_lte: V_new={:.6e}, Q_prev={:.6e}, Q_pred={:.6e}, Q_corr={:.6e}, ΔQ={:.6e}, Q_scale={:.6e}, LTE={:.6e}",
                 name_, voltage_new, charge_prev_, Q_pred, Q_corr, Q_error, Q_scale, lte);

    return lte;
}

// ========== Sensitivity Analysis: Parameter Access ==========

std::optional<Real> Diode::get_parameter(const std::string& param_name) const {
    if (param_name == "IS" || param_name == "is") {
        return is_;
    } else if (param_name == "N" || param_name == "n") {
        return n_;
    } else if (param_name == "CJ0" || param_name == "cj0") {
        return cj0_;
    } else if (param_name == "VJ" || param_name == "vj") {
        return vj_;
    } else if (param_name == "M" || param_name == "m") {
        return m_;
    }
    return std::nullopt;  // Parameter not found
}

bool Diode::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "IS" || param_name == "is") {
        if (value <= 0.0) {
            spdlog::warn("Diode {}: IS must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        is_ = value;
        spdlog::trace("Diode {}: set IS = {} A", name_, is_);
        return true;
    } else if (param_name == "N" || param_name == "n") {
        if (value <= 0.0) {
            spdlog::warn("Diode {}: N must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        n_ = value;
        spdlog::trace("Diode {}: set N = {}", name_, n_);
        return true;
    } else if (param_name == "CJ0" || param_name == "cj0") {
        if (value < 0.0) {
            spdlog::warn("Diode {}: CJ0 must be non-negative (got {}), not updated",
                        name_, value);
            return false;
        }
        cj0_ = value;
        spdlog::trace("Diode {}: set CJ0 = {} F", name_, cj0_);
        return true;
    } else if (param_name == "VJ" || param_name == "vj") {
        if (value <= 0.0) {
            spdlog::warn("Diode {}: VJ must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        vj_ = value;
        spdlog::trace("Diode {}: set VJ = {} V", name_, vj_);
        return true;
    } else if (param_name == "M" || param_name == "m") {
        if (value <= 0.0 || value >= 1.0) {
            spdlog::warn("Diode {}: M must be in (0, 1) (got {}), not updated",
                        name_, value);
            return false;
        }
        m_ = value;
        spdlog::trace("Diode {}: set M = {}", name_, m_);
        return true;
    }
    return false;  // Parameter not found
}

std::vector<std::string> Diode::get_perturbable_parameters() const {
    return {"IS", "N", "CJ0"};  // Main parameters for sensitivity analysis
}

} // namespace kirimaspice::devices
