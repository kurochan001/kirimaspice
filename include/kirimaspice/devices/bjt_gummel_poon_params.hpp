// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/bjt_gummel_poon_params.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include <limits>

namespace kirimaspice::devices {

/// BJT Gummel-Poon model parameters
/// High-precision BJT model with Early effect, base-width modulation, and high-current effects
/// Reference: SPICE2G User's Manual, H.K. Gummel and H.C. Poon, "An Integral Charge Control Model of Bipolar Transistors", BSTJ 1970
struct BJTGummelPoonParams {
    // ========================================================================
    // Basic Ebers-Moll parameters (inherited from simple model)
    // ========================================================================

    // DC model parameters
    Real is = 1e-16;       ///< Transport saturation current [A]
    Real bf = 100.0;       ///< Ideal maximum forward beta (hFE)
    Real nf = 1.0;         ///< Forward current emission coefficient
    Real br = 1.0;         ///< Ideal maximum reverse beta
    Real nr = 1.0;         ///< Reverse current emission coefficient

    // ========================================================================
    // Gummel-Poon specific parameters (new)
    // ========================================================================

    // Early effect (output conductance)
    Real vaf = 0.0;        ///< Forward Early voltage [V] (0 = infinite, no Early effect)
    Real var = 0.0;        ///< Reverse Early voltage [V] (0 = infinite)

    // High-current effects (Webster effect, base-width modulation)
    Real ikf = 0.0;        ///< Forward knee current [A] (0 = no high-current roll-off)
    Real ikr = 0.0;        ///< Reverse knee current [A] (0 = no high-current roll-off)
    Real nk = 0.5;         ///< High-current roll-off coefficient [dimensionless] (0.5 typical)

    // Base resistance model (complex, temperature-dependent)
    Real rb = 0.0;         ///< Zero bias base resistance [Ω]
    Real rbm = 0.0;        ///< Minimum base resistance at high currents [Ω] (default: rb)
    Real irb = 0.0;        ///< Current where base resistance falls halfway [A] (default: infinite)

    // Leakage currents
    Real ise = 0.0;        ///< B-E leakage saturation current [A] (non-ideal base current)
    Real ne = 1.5;         ///< B-E leakage emission coefficient (typ. 1.5-2.0)
    Real isc = 0.0;        ///< B-C leakage saturation current [A]
    Real nc = 2.0;         ///< B-C leakage emission coefficient (typ. 1.5-2.0)

    // ========================================================================
    // Capacitance model parameters
    // ========================================================================

    // B-E junction capacitance
    Real cje = 0.0;        ///< B-E zero-bias depletion capacitance [F]
    Real vje = 0.75;       ///< B-E junction potential [V]
    Real mje = 0.33;       ///< B-E grading coefficient
    Real fc = 0.5;         ///< Forward-bias depletion capacitance coefficient [dimensionless]

    // B-C junction capacitance (complex, split model)
    Real cjc = 0.0;        ///< B-C zero-bias depletion capacitance [F]
    Real vjc = 0.75;       ///< B-C junction potential [V]
    Real mjc = 0.33;       ///< B-C grading coefficient
    Real xcjc = 1.0;       ///< Fraction of CJC connected to internal base node [dimensionless] (0-1)

    // Transit time parameters (forward)
    Real tf = 0.0;         ///< Ideal forward transit time [s]
    Real itf = 0.0;        ///< Transit time dependency on Ic [A] (0 = no bias dependence)
    Real vtf = 0.0;        ///< Transit time dependency on Vbc [V] (0 = no bias dependence)
    Real xtf = 0.0;        ///< Transit time bias dependence exponent [dimensionless]
    Real ptf = 0.0;        ///< Excess phase at 1/(2πTF) [degrees]

    // Transit time parameters (reverse)
    Real tr = 0.0;         ///< Ideal reverse transit time [s]

    // Substrate/collector junction capacitance (optional, for 4-terminal devices)
    Real cjs = 0.0;        ///< Zero-bias substrate-collector capacitance [F]
    Real vjs = 0.75;       ///< Substrate junction potential [V]
    Real mjs = 0.0;        ///< Substrate grading coefficient

    // ========================================================================
    // Temperature parameters
    // ========================================================================

    Real xtb = 0.0;        ///< Forward/reverse beta temperature exponent [dimensionless]
    Real xti = 3.0;        ///< IS temperature effect exponent (3.0 for Si, 2.0 for Schottky)
    Real eg = 1.11;        ///< Bandgap energy [eV] (1.11 for Si at 300K)
    Real tnom = 27.0;      ///< Parameter measurement temperature [°C]

    // ========================================================================
    // Noise parameters
    // ========================================================================

    Real kf = 0.0;         ///< Flicker noise coefficient
    Real af = 1.0;         ///< Flicker noise current exponent
    Real ff = 1.0;         ///< Flicker noise frequency exponent

    // ========================================================================
    // Parasitic resistances
    // ========================================================================

    Real re = 0.0;         ///< Emitter ohmic resistance [Ω]
    Real rc = 0.0;         ///< Collector ohmic resistance [Ω]
    // rb is defined above in base resistance model

    // ========================================================================
    // Helper methods
    // ========================================================================

    /// Get effective base resistance at given base current
    /// R_B(I_B) = R_BM + (R_B - R_BM) / sqrt(1 + I_B/I_RB)
    /// @param base_current: Base current [A]
    /// @return Effective base resistance [Ω]
    Real get_base_resistance(Real base_current) const {
        if (rb <= 0.0) return 0.0;
        if (irb <= 0.0 || rbm <= 0.0) return rb;

        Real i_ratio = std::abs(base_current) / irb;
        return rbm + (rb - rbm) / std::sqrt(1.0 + i_ratio);
    }

    /// Get thermal voltage at given temperature
    /// V_t = kT/q
    /// @param temp_kelvin: Temperature [K]
    /// @return Thermal voltage [V]
    static Real get_thermal_voltage(Real temp_kelvin) {
        constexpr Real k_boltzmann = 1.380649e-23;  // [J/K]
        constexpr Real q_electron = 1.602176634e-19; // [C]
        return (k_boltzmann * temp_kelvin) / q_electron;
    }

    /// Validate parameters
    /// @return true if all parameters are physically valid
    bool validate() const {
        if (is <= 0.0) return false;
        if (bf <= 0.0) return false;
        if (br < 0.0) return false;
        if (nf <= 0.0) return false;
        if (nr <= 0.0) return false;
        if (ne <= 0.0) return false;
        if (nc <= 0.0) return false;
        if (nk < 0.0) return false;
        if (fc < 0.0 || fc > 0.95) return false;  // Physical constraint: 0 < FC < 1
        if (xcjc < 0.0 || xcjc > 1.0) return false; // Fraction: 0 ≤ XCJC ≤ 1
        if (rb < 0.0) return false;
        if (rbm < 0.0) return false;
        if (re < 0.0) return false;
        if (rc < 0.0) return false;
        if (cje < 0.0) return false;
        if (cjc < 0.0) return false;
        if (cjs < 0.0) return false;
        if (vje <= 0.0) return false;
        if (vjc <= 0.0) return false;
        if (vjs <= 0.0) return false;
        if (tf < 0.0) return false;
        if (tr < 0.0) return false;
        if (eg <= 0.0) return false;
        return true;
    }

    /// Check if Early effect is enabled
    bool has_early_effect() const {
        return (vaf > 0.0 || var > 0.0);
    }

    /// Check if high-current roll-off is enabled
    bool has_high_current_effects() const {
        return (ikf > 0.0 || ikr > 0.0);
    }

    /// Check if base resistance modulation is enabled
    bool has_base_resistance_modulation() const {
        return (rb > 0.0 && rbm > 0.0 && irb > 0.0);
    }

    /// Check if non-ideal base current (leakage) is present
    bool has_leakage_currents() const {
        return (ise > 0.0 || isc > 0.0);
    }

    /// Check if transit time bias dependence is present
    bool has_transit_time_bias_dependence() const {
        return (itf > 0.0 && vtf > 0.0);
    }
};

} // namespace kirimaspice::devices
