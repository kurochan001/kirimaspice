// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/core/device_utils.hpp
#pragma once

#include "types.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kirimaspice::core {

/// General-purpose safe exponential function with overflow/underflow protection
/// Clamps the argument to prevent std::exp() overflow (returns inf) or underflow (returns 0)
///
/// @param x Argument to exponential
/// @param max_arg Maximum allowed argument (default: 80.0, exp(80) ≈ 5.5e34)
/// @param min_arg Minimum allowed argument (default: -80.0, exp(-80) ≈ 1.8e-35)
/// @return exp(clamp(x, min_arg, max_arg))
inline Real safe_exp(Real x, Real max_arg = 80.0, Real min_arg = -80.0) {
    return std::exp(std::clamp(x, min_arg, max_arg));
}

/// Safe exponential function for PN junction equations
/// Prevents overflow and provides linear extrapolation for extreme voltages
///
/// @param v Junction voltage (V)
/// @param vt Thermal voltage (V_T = kT/q ≈ 26mV at 300K)
/// @param scale Scale factor (e.g., IS for current scaling)
/// @param cutoff_factor Deep cutoff threshold factor (default: -5.0)
/// @param overflow_limit Normalized voltage overflow limit (default: 80.0)
/// @return Current: scale * (exp(v/vt) - 1) with overflow protection
///
/// Algorithm:
/// - If v < cutoff_factor * vt: Return -scale (deep cutoff region)
/// - If v/vt > overflow_limit: Use linear extrapolation to prevent exp() overflow
/// - Otherwise: Return scale * (exp(v/vt) - 1)
inline Real safe_exp_junction(Real v, Real vt, Real scale = 1.0,
                              Real cutoff_factor = -5.0, Real overflow_limit = 80.0) {
    if (v < cutoff_factor * vt) {
        // Deep cutoff region: I ≈ -IS
        return -scale;
    }

    Real v_norm = v / vt;
    if (v_norm > overflow_limit) {
        // Linear extrapolation to prevent overflow
        // At v_norm = 80: exp(80) ≈ 5.5e34
        // I(v) = I_80 + g_80 * (v - v_80)
        // where I_80 = IS * (exp(80) - 1), g_80 = IS/VT * exp(80)
        Real exp_limit = std::exp(overflow_limit);
        Real i_limit = scale * (exp_limit - 1.0);
        Real g_limit = (scale / vt) * exp_limit;
        return i_limit + g_limit * (v - overflow_limit * vt);
    }

    // Normal exponential region
    return scale * (std::exp(v_norm) - 1.0);
}

/// Safe exponential conductance for PN junction
/// Returns the derivative of safe_exp_junction with respect to v
///
/// @param v Junction voltage (V)
/// @param vt Thermal voltage (V_T)
/// @param g_scale Conductance scale factor (e.g., IS/VT)
/// @param gmin Minimum conductance to prevent singular matrix (default: 1e-12)
/// @param cutoff_factor Deep cutoff threshold factor (default: -5.0)
/// @param overflow_limit Normalized voltage overflow limit (default: 80.0)
/// @return Conductance: dI/dV with gmin limiter
///
/// Algorithm:
/// - If v < cutoff_factor * vt: Return gmin (deep cutoff, minimal conductance)
/// - If v/vt > overflow_limit: Return g_scale * exp(overflow_limit) (clamped)
/// - Otherwise: Return g_scale * exp(v/vt)
inline Real safe_exp_conductance(Real v, Real vt, Real g_scale,
                                 Real gmin = 1e-12, Real cutoff_factor = -5.0,
                                 Real overflow_limit = 80.0) {
    if (v < cutoff_factor * vt) {
        // Deep cutoff region: minimal conductance
        return gmin;
    }

    Real v_norm = v / vt;
    if (v_norm > overflow_limit) {
        // Clamped conductance at overflow limit
        Real g_limit = g_scale * std::exp(overflow_limit);
        return std::max(g_limit, gmin);
    }

    // Normal exponential conductance
    Real g = g_scale * std::exp(v_norm);
    return std::max(g, gmin);
}

/// Safe natural logarithm
/// Prevents log(0) and log(negative)
///
/// @param x Input value
/// @param min_val Minimum allowed value (default: 1e-30)
/// @return log(max(x, min_val))
inline Real safe_log(Real x, Real min_val = 1e-30) {
    return std::log(std::max(x, min_val));
}

/// Compute thermal voltage V_T = kT/q
/// @param temp_kelvin Temperature in Kelvin (default: 300K)
/// @return Thermal voltage in Volts
inline Real compute_thermal_voltage(Real temp_kelvin = 300.0) {
    constexpr Real k_boltzmann = 1.380649e-23;  // J/K
    constexpr Real q_electron = 1.602176634e-19; // C
    return k_boltzmann * temp_kelvin / q_electron;
}

/// Compute critical voltage for PN junction voltage limiting
/// Above this voltage, limit_junction_step limits large Newton steps
///
/// @param vt Thermal voltage (V_T = kT/q ≈ 26mV at 300K)
/// @param is Saturation current [A]
/// @return Critical voltage [V] = V_T * ln(V_T / (√2 * I_S))
///
/// For typical diode (IS=1e-14A, VT=26mV): Vcrit ≈ 0.717V
inline Real compute_vcrit(Real vt, Real is) {
    constexpr Real sqrt2 = 1.41421356237;
    return vt * std::log(vt / (sqrt2 * is));
}

/// Limit the step of a p-n junction voltage between two Newton iterations.
///
/// The exponential of a junction makes a full Newton step from a small
/// voltage overshoot by far. SPICE therefore limits how far the junction
/// voltage moves from the voltage at which the junction was last
/// evaluated: above Vcrit, a step larger than 2·nV_T becomes logarithmic.
/// The limit depends on the previous iterate, not on the voltage alone, so
/// at a converged solution (vnew = vold) it returns vnew and the solution
/// is that of the device equation.
///
/// @param vnew  junction voltage of the present iterate [V]
/// @param vold  junction voltage at which the junction was last evaluated [V]
/// @param nvt   emission coefficient times thermal voltage, n·V_T [V]
/// @param vcrit critical voltage, compute_vcrit(nvt, is) [V]
/// @return the junction voltage at which to evaluate the device this iteration
inline Real limit_junction_step(Real vnew, Real vold, Real nvt, Real vcrit) {
    if (vnew <= vcrit || std::abs(vnew - vold) <= 2.0 * nvt) {
        return vnew;
    }
    if (vold > 0.0) {
        const Real arg = 1.0 + (vnew - vold) / nvt;
        return (arg > 0.0) ? vold + nvt * std::log(arg) : vcrit;
    }
    // From zero or reverse bias: start near the knee of the exponential.
    return (vnew > nvt) ? nvt * std::log(vnew / nvt) : vnew;
}

/// Limit the step of the gate-source voltage of a FET between two Newton
/// iterations, by SPICE3's rule for it (DEVfetlim in ngspice's
/// src/spicelib/devices/devsup.c, whose header gives only the copyright of
/// the Regents of the University of California; read 2026-11-21). With
/// hi = 2 |vold - vto| + 2, lo = |vold - vto| + 1 and vtox = vto + 3.5:
///   from vold >= vtox, a fall stops lo below vold while vnew >= vtox and
///   at vto + 2 below it, and a rise of hi or more stops at vold + hi;
///   from vto <= vold < vtox, a fall stops at vto - 0.5 and a rise at
///   vto + 4;
///   from vold < vto, a fall of more than hi stops at vold - hi, and a rise
///   stops at vto + 0.5, or, below that, at vold + lo.
/// A converged solution (vnew = vold) is returned unchanged.
/// @param vnew the voltage of the present iterate [V]
/// @param vold the voltage at which the device was last evaluated [V]
/// @param vto  the threshold voltage [V]
/// @return the voltage at which to evaluate the device this iteration
inline Real limit_fet_gate_step(Real vnew, Real vold, Real vto) {
    const Real hi = std::abs(2.0 * (vold - vto)) + 2.0;
    const Real lo = std::abs(vold - vto) + 1.0;
    const Real vtox = vto + 3.5;
    const Real delv = vnew - vold;
    if (vold >= vto) {
        if (vold >= vtox) {
            if (delv <= 0.0) {
                if (vnew >= vtox) {
                    return (-delv > lo) ? vold - lo : vnew;
                }
                return std::max(vnew, vto + 2.0);
            }
            return (delv >= hi) ? vold + hi : vnew;
        }
        return (delv <= 0.0) ? std::max(vnew, vto - 0.5) : std::min(vnew, vto + 4.0);
    }
    if (delv <= 0.0) {
        return (-delv > hi) ? vold - hi : vnew;
    }
    const Real vtemp = vto + 0.5;
    if (vnew <= vtemp) {
        return (delv > lo) ? vold + lo : vnew;
    }
    return vtemp;
}

/// Limit the step of the drain-source voltage of a FET (here the
/// collector-emitter voltage of the IGBT) between two Newton iterations, by
/// SPICE3's rule for VDS (DEVlimvds in ngspice's
/// src/spicelib/devices/devsup.c, Modified BSD, read 2026-11-15):
///   from vold >= 3.5 V, a rise stops at 3 vold + 2, and a fall below 3.5 V
///   stops at 2 V;
///   from vold < 3.5 V, a rise stops at 4 V and a fall at -0.5 V.
/// For a negative vold the rule applies to -v, as SPICE3's level 1 MOSFET
/// does in its reverse mode (mos1load.c, also Modified BSD). A converged
/// solution (vnew = vold) is returned unchanged.
/// @param vnew the voltage of the present iterate [V]
/// @param vold the voltage at which the device was last evaluated [V]
/// @return the voltage at which to evaluate the device this iteration
inline Real limit_fet_drain_step(Real vnew, Real vold) {
    const auto forward = [](Real v_new, Real v_old) {
        if (v_old >= 3.5) {
            if (v_new > v_old) return std::min(v_new, 3.0 * v_old + 2.0);
            return (v_new < 3.5) ? std::max(v_new, 2.0) : v_new;
        }
        return (v_new > v_old) ? std::min(v_new, 4.0) : std::max(v_new, -0.5);
    };
    return (vold >= 0.0) ? forward(vnew, vold) : -forward(-vnew, -vold);
}

// ============================================================================
// Temperature Scaling Utilities
// ============================================================================

/// Scale threshold voltage with temperature (MOSFET/IGBT)
/// VTO(T) = VTO(Tnom) + KT1×(T-Tnom) + KT2×(T-Tnom)²
///
/// @param vth0 Threshold voltage at Tnom [V]
/// @param kt1 Linear temperature coefficient [V/K] (typical: -0.002 to -0.005)
/// @param kt2 Quadratic temperature coefficient [V/K²] (typical: 1e-5 to 1e-6)
/// @param temp Current temperature [K]
/// @param tnom Nominal temperature [K] (default: 300.15K = 27°C)
/// @return Temperature-scaled threshold voltage [V]
inline Real scale_vth_temperature(Real vth0, Real kt1, Real kt2,
                                  Real temp, Real tnom = 300.15) {
    Real dT = temp - tnom;
    return vth0 + kt1 * dT + kt2 * dT * dT;
}

/// Scale mobility/transconductance parameter with temperature (MOSFET)
/// μ(T) = μ₀ × (T/Tnom)^UTE
///
/// @param mu0 Mobility/KP at Tnom
/// @param ute Mobility temperature exponent (typical: -1.5 to -2.0)
/// @param temp Current temperature [K]
/// @param tnom Nominal temperature [K] (default: 300.15K)
/// @return Temperature-scaled mobility/KP
inline Real scale_mobility_temperature(Real mu0, Real ute,
                                       Real temp, Real tnom = 300.15) {
    if (temp <= 0.0 || tnom <= 0.0) {
        return mu0;  // Guard against invalid temperature
    }
    return mu0 * std::pow(temp / tnom, ute);
}

/// Scale saturation current with temperature (Diode/BJT)
/// IS(T) = IS(Tnom) × (T/Tnom)^(XTI/N) × exp(-EG/N × (1/Vt(T) - 1/Vt(Tnom)))
///
/// @param is0 Saturation current at Tnom [A]
/// @param xti Saturation current temperature exponent (typical: 3.0 for Si)
/// @param eg Energy gap [eV] (typical: 1.11 for Si)
/// @param n Emission coefficient (typical: 1.0-2.0)
/// @param temp Current temperature [K]
/// @param tnom Nominal temperature [K] (default: 300.15K)
/// @return Temperature-scaled saturation current [A]
inline Real scale_saturation_current(Real is0, Real xti, Real eg, Real n,
                                     Real temp, Real tnom = 300.15) {
    if (temp <= 0.0 || tnom <= 0.0 || n <= 0.0) {
        return is0;  // Guard against invalid parameters
    }

    constexpr Real k_boltzmann = 1.380649e-23;  // J/K
    constexpr Real q_electron = 1.602176634e-19; // C

    Real vt_temp = k_boltzmann * temp / q_electron;
    Real vt_tnom = k_boltzmann * tnom / q_electron;

    // (T/Tnom)^(XTI/N)
    Real temp_ratio_term = std::pow(temp / tnom, xti / n);

    // exp(-EG/N × (1/Vt(T) - 1/Vt(Tnom)))
    Real exp_term = std::exp(-eg / n * (1.0 / vt_temp - 1.0 / vt_tnom));

    return is0 * temp_ratio_term * exp_term;
}

/// The reference temperature of SPICE's temperature laws of junction
/// potentials [K]
inline constexpr Real kSpiceReferenceTemperature = 300.15;

/// SPICE's band gap of silicon [eV]: Eg(T) = 1.16 - 7.02e-4 T^2 / (T + 1108)
inline Real silicon_band_gap(Real temp) {
    return 1.16 - 7.02e-4 * temp * temp / (temp + 1108.0);
}

/// SPICE's p(T) = -3 Vt(T) ln(T/Tref) + Eg(T) - Eg(Tref) T/Tref [V], with
/// Tref = 300.15 K: a junction potential (a diode's VJ, a MOSFET's PHI)
/// that is P at TNOM is P(T) = (P - p(TNOM)) T/TNOM + p(T), as the
/// diode's and the level-1 MOSFET's set_temperature take it
inline Real band_gap_potential(Real temp) {
    const Real fact = temp / kSpiceReferenceTemperature;
    return -3.0 * compute_thermal_voltage(temp) * std::log(fact) + silicon_band_gap(temp) -
           silicon_band_gap(kSpiceReferenceTemperature) * fact;
}

/// Scale resistance with temperature (positive temperature coefficient)
/// R(T) = R(Tnom) × (T/Tnom)^TC
///
/// @param r0 Resistance at Tnom [Ω]
/// @param tc Temperature coefficient exponent (typical: 1.0-2.5 for metals)
/// @param temp Current temperature [K]
/// @param tnom Nominal temperature [K] (default: 300.15K)
/// @return Temperature-scaled resistance [Ω]
inline Real scale_resistance_temperature(Real r0, Real tc,
                                         Real temp, Real tnom = 300.15) {
    if (temp <= 0.0 || tnom <= 0.0) {
        return r0;  // Guard against invalid temperature
    }
    return r0 * std::pow(temp / tnom, tc);
}

// ============================================================================
// Noise Calculation Utilities
// ============================================================================

/// Compute thermal noise spectral density (Johnson-Nyquist noise)
/// S_i = 4kT × γ × gm [A²/Hz]
///
/// @param gm Transconductance [S]
/// @param gamma Noise coefficient (2/3 for long-channel MOSFET, 8/3 for short-channel)
/// @param temp Temperature [K]
/// @return Thermal noise current spectral density [A²/Hz]
inline Real thermal_noise_current_sq(Real gm, Real gamma, Real temp) {
    constexpr Real k_boltzmann = 1.380649e-23;  // J/K
    return 4.0 * k_boltzmann * temp * gamma * gm;
}

/// Compute thermal noise for resistor
/// S_i = 4kT/R = 4kT × G [A²/Hz]
///
/// @param conductance Conductance G = 1/R [S]
/// @param temp Temperature [K]
/// @return Thermal noise current spectral density [A²/Hz]
inline Real thermal_noise_resistor_sq(Real conductance, Real temp) {
    constexpr Real k_boltzmann = 1.380649e-23;  // J/K
    return 4.0 * k_boltzmann * temp * conductance;
}

/// Compute flicker noise (1/f noise) spectral density
/// S_i = KF × |Id|^AF / (Cox × Leff × Weff × f^EF) [A²/Hz]
///
/// @param id Drain current [A]
/// @param kf Flicker noise coefficient
/// @param af Flicker noise current exponent (typical: 1.0-2.0)
/// @param ef Flicker noise frequency exponent (typical: 1.0)
/// @param frequency Frequency [Hz]
/// @param cox Gate oxide capacitance per unit area [F/m²]
/// @param area Effective channel area Leff × Weff [m²]
/// @return Flicker noise current spectral density [A²/Hz]
inline Real flicker_noise_current_sq(Real id, Real kf, Real af, Real ef,
                                     Real frequency, Real cox, Real area) {
    if (frequency <= 0.0 || area <= 0.0 || cox <= 0.0 || kf <= 0.0) {
        return 0.0;  // Guard against invalid parameters
    }
    return kf * std::pow(std::abs(id), af) / (cox * area * std::pow(frequency, ef));
}

/// Compute simplified flicker noise (without geometry normalization)
/// S_i = KF × |I|^AF / f^EF [A²/Hz]
///
/// @param current Device current [A]
/// @param kf Flicker noise coefficient
/// @param af Flicker noise current exponent (typical: 1.0-2.0)
/// @param ef Flicker noise frequency exponent (typical: 1.0)
/// @param frequency Frequency [Hz]
/// @return Flicker noise current spectral density [A²/Hz]
inline Real flicker_noise_simple_sq(Real current, Real kf, Real af, Real ef,
                                    Real frequency) {
    if (frequency <= 0.0 || kf <= 0.0) {
        return 0.0;  // Guard against invalid parameters
    }
    return kf * std::pow(std::abs(current), af) / std::pow(frequency, ef);
}

/// Compute shot noise spectral density
/// S_i = 2qI [A²/Hz]
///
/// @param current DC current [A]
/// @return Shot noise current spectral density [A²/Hz]
inline Real shot_noise_current_sq(Real current) {
    constexpr Real q_electron = 1.602176634e-19; // C
    return 2.0 * q_electron * std::abs(current);
}

// ============================================================================
// Parameter Access Utilities
// ============================================================================

/// Parameter definition for unified parameter access
/// Reduces if-else chains in get_parameter/set_parameter methods
struct ParamDef {
    /// Getter function type: returns parameter value
    using Getter = std::function<Real()>;

    /// Setter function type: sets parameter value with validation
    /// Returns true if set successfully, false if validation failed
    using Setter = std::function<bool(Real)>;

    Getter getter;       ///< Function to get parameter value
    Setter setter;       ///< Function to set parameter value (with validation)
    bool perturbable;    ///< Whether parameter can be perturbed in sensitivity analysis

    ParamDef(Getter g, Setter s, bool p = true)
        : getter(std::move(g)), setter(std::move(s)), perturbable(p) {}
};

/// Helper class for unified parameter access across device models
/// Converts if-else chains to map-based lookup, reducing code duplication
///
/// Usage:
/// ```cpp
/// class MyDevice : public Device {
///     Real kp_, vth_, w_, l_;
///     ParameterAccessor params_;
///
///     void setup_parameters() {
///         // Register parameter with validation and derived update
///         params_.add("KP", {"kp"},
///             [this]() { return kp_; },
///             [this](Real v) {
///                 if (v <= 0) return false;
///                 kp_ = v;
///                 beta_ = kp_ * w_ / l_;  // Update derived param
///                 return true;
///             });
///
///         // Simple parameter (no validation, just assignment)
///         params_.add_simple("VTO", {"VTH", "vto", "vth"}, vth_);
///     }
///
///     std::optional<Real> get_parameter(const std::string& name) const override {
///         return params_.get(name);
///     }
///
///     bool set_parameter(const std::string& name, Real value) override {
///         return params_.set(name, value);
///     }
///
///     std::vector<std::string> get_perturbable_parameters() const override {
///         return params_.get_perturbable();
///     }
/// };
/// ```
class ParameterAccessor {
public:
    /// Add a parameter with custom getter and setter
    /// @param primary_name Primary parameter name (canonical)
    /// @param aliases Alternative names for the parameter
    /// @param getter Function to get parameter value
    /// @param setter Function to set parameter value (should include validation)
    /// @param perturbable Whether parameter can be perturbed in sensitivity analysis
    void add(const std::string& primary_name,
             std::initializer_list<std::string> aliases,
             ParamDef::Getter getter,
             ParamDef::Setter setter,
             bool perturbable = true) {
        auto def = std::make_shared<ParamDef>(std::move(getter), std::move(setter), perturbable);

        // Register primary name
        std::string upper_primary = to_upper(primary_name);
        params_[upper_primary] = def;
        if (perturbable) {
            perturbable_params_.push_back(primary_name);
        }

        // Register aliases (all point to same definition)
        for (const auto& alias : aliases) {
            params_[to_upper(alias)] = def;
        }
    }

    /// Add a simple parameter (direct read/write, positive validation)
    /// @param primary_name Primary parameter name
    /// @param aliases Alternative names
    /// @param ref Reference to the parameter variable
    /// @param must_be_positive If true, setter rejects non-positive values
    /// @param perturbable Whether parameter can be perturbed
    template<typename T>
    void add_simple(const std::string& primary_name,
                    std::initializer_list<std::string> aliases,
                    T& ref,
                    bool must_be_positive = false,
                    bool perturbable = true) {
        if (must_be_positive) {
            add(primary_name, aliases,
                [&ref]() { return static_cast<Real>(ref); },
                [&ref](Real v) {
                    if (v <= 0.0) return false;
                    ref = static_cast<T>(v);
                    return true;
                },
                perturbable);
        } else {
            add(primary_name, aliases,
                [&ref]() { return static_cast<Real>(ref); },
                [&ref](Real v) {
                    ref = static_cast<T>(v);
                    return true;
                },
                perturbable);
        }
    }

    /// Get parameter value by name (case-insensitive)
    /// @param name Parameter name
    /// @return Parameter value if found, nullopt otherwise
    std::optional<Real> get(const std::string& name) const {
        auto it = params_.find(to_upper(name));
        if (it != params_.end() && it->second->getter) {
            return it->second->getter();
        }
        return std::nullopt;
    }

    /// Set parameter value by name (case-insensitive)
    /// @param name Parameter name
    /// @param value New value
    /// @return true if set successfully, false if name not found or validation failed
    bool set(const std::string& name, Real value) {
        auto it = params_.find(to_upper(name));
        if (it != params_.end() && it->second->setter) {
            return it->second->setter(value);
        }
        return false;
    }

    /// Check if parameter exists
    /// @param name Parameter name (case-insensitive)
    /// @return true if parameter is registered
    bool has(const std::string& name) const {
        return params_.find(to_upper(name)) != params_.end();
    }

    /// Get list of perturbable parameter names
    /// @return Vector of parameter names that can be perturbed
    std::vector<std::string> get_perturbable() const {
        return perturbable_params_;
    }

    /// Get count of registered parameters (excluding aliases)
    /// @return Number of unique parameters
    size_t size() const {
        return perturbable_params_.size();
    }

private:
    /// Convert string to uppercase for case-insensitive lookup
    static std::string to_upper(const std::string& s) {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::toupper(c); });
        return result;
    }

    /// Map from uppercase parameter name to definition
    std::unordered_map<std::string, std::shared_ptr<ParamDef>> params_;

    /// List of perturbable parameter names (primary names only)
    std::vector<std::string> perturbable_params_;
};

} // namespace kirimaspice::core

// ============================================================================
// MNA Stamping Utilities for Device Models
// ============================================================================

namespace kirimaspice::device_utils {

using Real = kirimaspice::Real;
using Complex = kirimaspice::Complex;

/// Thermal noise power spectral density: 4kT × γ × gm
inline Real thermal_noise_psd(Real temp_K, Real gm, Real gamma = 2.0/3.0) {
    constexpr Real k_boltzmann = 1.380649e-23;
    return 4.0 * k_boltzmann * temp_K * gamma * std::abs(gm);
}

/// Flicker noise power spectral density: KF × |I|^AF / f^EF
inline Real flicker_noise_psd(Real kf, Real af, Real ef, Real current, Real frequency) {
    if (frequency <= 0.0 || kf <= 0.0) return 0.0;
    return kf * std::pow(std::abs(current), af) / std::pow(frequency, ef);
}

/// Stamp voltage-controlled current source (3-terminal: D, G, S)
/// For N-channel: Id = gm × (Vg - Vs), from drain to source
/// For P-channel: Is = gm × (Vs - Vg), from source to drain (use polarity = -1)
/// In both cases the current from drain to source is gm × (Vg - Vs), so the
/// entries are the same: the drain row takes +gm at the gate and -gm at the
/// source, the source row the opposite. The drain row has nothing at the
/// drain, whose voltage does not control this current.
template<typename Matrix>
inline void stamp_vccs_3terminal(Matrix& G, int nd_idx, int ng_idx, int ns_idx, Real gm,
                                 Real /*polarity*/) {
    if (nd_idx >= 0) {
        if (ng_idx >= 0) G.add_entry(nd_idx, ng_idx, gm);
        if (ns_idx >= 0) G.add_entry(nd_idx, ns_idx, -gm);
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) G.add_entry(ns_idx, ng_idx, -gm);
        G.add_entry(ns_idx, ns_idx, gm);
    }
}

/// Stamp drain-source conductance (symmetric for both polarities)
template<typename Matrix>
inline void stamp_conductance_ds(Matrix& G, int nd_idx, int ns_idx, Real gds) {
    if (nd_idx >= 0) {
        G.add_entry(nd_idx, nd_idx, gds);
        if (ns_idx >= 0) G.add_entry(nd_idx, ns_idx, -gds);
    }
    if (ns_idx >= 0) {
        if (nd_idx >= 0) G.add_entry(ns_idx, nd_idx, -gds);
        G.add_entry(ns_idx, ns_idx, gds);
    }
}

/// Stamp capacitor companion model for transient analysis: the current
/// I = g×V + Ieq from n1 to n2. For backward Euler g = C/dt and
/// Ieq = -g×V_prev, so that I = C/dt (V - V_prev).
template<typename MNA>
inline void stamp_capacitor_companion(MNA& mna, int n1_idx, int n2_idx,
                                      Real g_cap, Real ieq, Real /*polarity*/) {
    // G matrix: capacitor acts as conductance g_cap
    if (n1_idx >= 0) {
        mna.G.add_entry(n1_idx, n1_idx, g_cap);
        if (n2_idx >= 0) mna.G.add_entry(n1_idx, n2_idx, -g_cap);
    }
    if (n2_idx >= 0) {
        if (n1_idx >= 0) mna.G.add_entry(n2_idx, n1_idx, -g_cap);
        mna.G.add_entry(n2_idx, n2_idx, g_cap);
    }

    // RHS: equivalent current source
    if (n1_idx >= 0) mna.b[n1_idx] -= ieq;
    if (n2_idx >= 0) mna.b[n2_idx] += ieq;
}

/// The companion of a capacitance C over one time step of the given method:
/// the current g v + i_eq from its first node to its second for the voltage
/// v at the end of the step. v1, v2, v3 are the voltages accepted at the last
/// three time points and i1 the capacitor's current at the last one, which
/// the trapezoidal rule needs (i = 2C/dt (v - v1) - i1). BDF2 and BDF3 take
/// the variable-step coefficients when given, as Capacitor does (g = C
/// alpha0 / beta0 and i_eq = C / beta0 sum alpha_k v_k, beta0 being the
/// step), and the fixed-step formulas otherwise. TR-BDF2 takes the
/// coefficients of its stage when given: stage 1 is the trapezoidal rule
/// over gamma dt (its stage_dt), stage 2 the modified BDF2 through v(n),
/// v(n + gamma) and v(n + 1), i = C (alpha0 v + alpha1 v1 + alpha2 v2) /
/// (beta0 full_dt), for a device that accepts the stage-1 point into its
/// history (v1 = v(n + gamma), v2 = v(n)); without them it is taken as
/// backward Euler. The same function gives the current at an accepted
/// point (g v + i_eq), which becomes i1 of the next step.
struct CapacitorCompanion {
    Real g = 0.0;
    Real i_eq = 0.0;
};
inline CapacitorCompanion capacitor_companion(IntegrationMethod method, Real c, Real dt, Real v1,
                                              Real v2, Real v3, Real i1,
                                              const BDFCoefficients* bdf,
                                              const TR_BDF2_Coefficients* tr_bdf2 = nullptr) {
    CapacitorCompanion cc;
    if (c <= 0.0 || dt <= 0.0) {
        return cc;
    }
    switch (method) {
        case IntegrationMethod::TRAPEZOIDAL:
            cc.g = 2.0 * c / dt;
            cc.i_eq = -cc.g * v1 - i1;
            break;
        case IntegrationMethod::GEAR2:
            if (bdf) {
                cc.g = c * bdf->alpha0 / bdf->beta0;
                cc.i_eq = (c / bdf->beta0) * (bdf->alpha1 * v1 + bdf->alpha2 * v2);
            } else {
                cc.g = 1.5 * c / dt;
                cc.i_eq = (c / dt) * (-2.0 * v1 + 0.5 * v2);
            }
            break;
        case IntegrationMethod::GEAR3:
            if (bdf) {
                cc.g = c * bdf->alpha0 / bdf->beta0;
                cc.i_eq = (c / bdf->beta0) * (bdf->alpha1 * v1 + bdf->alpha2 * v2 + bdf->alpha3 * v3);
            } else {
                cc.g = (11.0 / 6.0) * c / dt;
                cc.i_eq = (c / dt) * (-3.0 * v1 + 1.5 * v2 - v3 / 3.0);
            }
            break;
        case IntegrationMethod::TR_BDF2:
            if (tr_bdf2 && tr_bdf2->stage == 1 && tr_bdf2->stage_dt > 0.0) {
                cc.g = 2.0 * c / tr_bdf2->stage_dt;
                cc.i_eq = -cc.g * v1 - i1;
                break;
            }
            if (tr_bdf2 && tr_bdf2->stage == 2 && tr_bdf2->beta0 * tr_bdf2->full_dt > 0.0) {
                const Real d = tr_bdf2->beta0 * tr_bdf2->full_dt;
                cc.g = c * tr_bdf2->alpha0 / d;
                cc.i_eq = c * (tr_bdf2->alpha1 * v1 + tr_bdf2->alpha2 * v2) / d;
                break;
            }
            cc.g = c / dt;
            cc.i_eq = -cc.g * v1;
            break;
        default:  // backward Euler
            cc.g = c / dt;
            cc.i_eq = -cc.g * v1;
            break;
    }
    return cc;
}

/// Stamp complex VCCS for AC analysis: the small-signal current gm × (vg - vs)
/// from drain to source, the same for both polarities (stamp_vccs_3terminal)
template<typename ComplexMatrix>
inline void stamp_complex_vccs_3terminal(ComplexMatrix& Y, int nd_idx, int ng_idx, int ns_idx,
                                         Complex gm, Real /*polarity*/) {
    if (nd_idx >= 0) {
        if (ng_idx >= 0) Y.add_entry(nd_idx, ng_idx, gm);
        if (ns_idx >= 0) Y.add_entry(nd_idx, ns_idx, -gm);
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) Y.add_entry(ns_idx, ng_idx, -gm);
        Y.add_entry(ns_idx, ns_idx, gm);
    }
}

/// Stamp complex conductance for AC analysis
template<typename ComplexMatrix>
inline void stamp_complex_conductance_ds(ComplexMatrix& Y, int nd_idx, int ns_idx, Complex gds) {
    if (nd_idx >= 0) {
        Y.add_entry(nd_idx, nd_idx, gds);
        if (ns_idx >= 0) Y.add_entry(nd_idx, ns_idx, -gds);
    }
    if (ns_idx >= 0) {
        if (nd_idx >= 0) Y.add_entry(ns_idx, nd_idx, -gds);
        Y.add_entry(ns_idx, ns_idx, gds);
    }
}

/// Stamp complex capacitor (admittance Y = jωC)
template<typename ComplexMatrix>
inline void stamp_complex_capacitor(ComplexMatrix& Y, int n1_idx, int n2_idx,
                                    Complex y_cap, Real /*polarity*/) {
    if (n1_idx >= 0) {
        Y.add_entry(n1_idx, n1_idx, y_cap);
        if (n2_idx >= 0) Y.add_entry(n1_idx, n2_idx, -y_cap);
    }
    if (n2_idx >= 0) {
        if (n1_idx >= 0) Y.add_entry(n2_idx, n1_idx, -y_cap);
        Y.add_entry(n2_idx, n2_idx, y_cap);
    }
}

} // namespace kirimaspice::device_utils
