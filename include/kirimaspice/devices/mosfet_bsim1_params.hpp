// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mosfet_bsim1_params.hpp
#pragma once

#include "kirimaspice/core/types.hpp"

namespace kirimaspice::devices {

/// MOSFET BSIM1 (Level 9) model parameters
/// Berkeley Short-channel IGFET Model version 1
/// Reference: BSIM1 User's Manual, UC Berkeley (1988)
struct MOSFETBsim1Params {
    // Basic process parameters
    Real vfb = -1.0;       ///< Flat-band voltage [V]
    Real phi = 0.7;        ///< Surface potential (2×ψB) [V]
    Real k1 = 0.5;         ///< Body effect coefficient 1 [V^0.5]
    Real k2 = 0.0;         ///< Body effect coefficient 2 [dimensionless]

    // Mobility parameters
    Real muz = 600.0;      ///< Zero-bias mobility (U0) [cm^2/V-s] (600 for N, 250 for P)
    Real u0 = 0.06;        ///< Mobility degradation coefficient [1/V]
    Real u1 = 0.0;         ///< Velocity saturation coefficient [µm/V^2]

    // Short-channel effects for mobility
    Real x2mz = 0.0;       ///< MUZ short-channel coefficient [µm]
    Real x2ms = 0.0;       ///< MUZ saturation coefficient [µm]
    Real x2m = 0.0;        ///< MUZ short-channel exponent
    Real x2u0 = 0.0;       ///< U0 short-channel coefficient [µm]
    Real x2u1 = 0.0;       ///< U1 short-channel coefficient [µm]

    // DIBL (Drain-Induced Barrier Lowering)
    Real eta = 0.0;        ///< DIBL coefficient
    Real x2e = 0.0;        ///< ETA short-channel coefficient [µm]
    Real x3e = 0.0;        ///< ETA second-order coefficient [µm]
    Real x3ms = 0.0;       ///< Saturation voltage coefficient [µm]

    // Subthreshold parameters
    Real n0 = 1.4;         ///< Subthreshold coefficient
    Real nb = 0.5;         ///< Body effect coefficient for subthreshold
    Real nd = 0.0;         ///< Drain effect coefficient for subthreshold

    // Saturation velocity
    Real vsat = 8e4;       ///< Saturation velocity [m/s] (80000 m/s default)

    // Channel length/width corrections
    Real dl = 0.0;         ///< Channel length reduction [m]
    Real dw = 0.0;         ///< Channel width reduction [m]

    // Junction parameters
    Real cbd = 0.0;        ///< Zero-bias B-D junction capacitance [F]
    Real cbs = 0.0;        ///< Zero-bias B-S junction capacitance [F]
    Real is = 1e-14;       ///< Bulk junction saturation current [A]
    Real pb = 0.8;         ///< Bulk junction potential [V]
    Real cj = 0.0;         ///< Bottom junction capacitance [F/m^2]
    Real mj = 0.5;         ///< Bulk junction grading coefficient
    Real cjsw = 0.0;       ///< Sidewall junction capacitance [F/m]
    Real mjsw = 0.33;      ///< Sidewall junction grading coefficient

    // Gate capacitances
    Real cgso = 0.0;       ///< Gate-source overlap capacitance [F/m]
    Real cgdo = 0.0;       ///< Gate-drain overlap capacitance [F/m]
    Real cgbo = 0.0;       ///< Gate-bulk overlap capacitance [F/m]

    // Oxide parameters
    Real tox = 1e-7;       ///< Oxide thickness [m] (100nm default, accepts µm inputs)

    // Temperature parameters
    Real tnom = 300.15;    ///< Parameter measurement temperature [K]; not used by the model

    // Noise parameters
    Real kf = 0.0;         ///< Flicker noise coefficient
    Real af = 1.0;         ///< Flicker noise exponent

    // Geometry parameters (instance-specific)
    Real w = 1e-4;         ///< Channel width [m] (100μm default)
    Real l = 1e-5;         ///< Channel length [m] (10μm default)
    Real ad = 0.0;         ///< Drain area [m^2]
    Real as = 0.0;         ///< Source area [m^2]
    Real pd = 0.0;         ///< Drain perimeter [m]
    Real ps = 0.0;         ///< Source perimeter [m]

    /// Get effective channel length [m]
    Real get_leff() const {
        return l - dl;
    }

    /// Get effective channel width [m]
    Real get_weff() const {
        return w - dw;
    }

    /// Get oxide thickness in SI [m], auto-converting µm-based inputs if necessary.
    Real get_tox_si() const {
        if (tox > 1e-5) {
            // Many legacy BSIM1 decks specify tox in µm (e.g., 0.02 for 20 nm).
            return tox * 1e-6;
        }
        return tox;
    }

    /// Validate parameters
    /// @return true if all parameters are valid
    bool validate() const {
        if (get_leff() <= 0.0) return false;
        if (get_weff() <= 0.0) return false;
        if (tox <= 0.0) return false;
        if (muz <= 0.0) return false;
        if (phi <= 0.0) return false;
        return true;
    }
};

} // namespace kirimaspice::devices
