// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mosfet_level3_params.hpp
#pragma once

#include "kirimaspice/core/types.hpp"

namespace kirimaspice::devices {

/// MOSFET Level 3 model parameters
/// Semi-empirical model with short-channel effects
/// Reference: SPICE3 User's Manual, UC Berkeley
struct MOSFETLevel3Params {
    // Process parameters
    Real tox = 1e-7;       ///< Oxide thickness [m] (100nm default)
    Real nsub = 0.0;       ///< Substrate doping [cm^-3]
    Real nss = 0.0;        ///< Surface state density [cm^-2]
    Real tpg = 1.0;        ///< Gate material type (+1: opposite to substrate, -1: same as substrate, 0: Al)
    Real xj = 0.0;         ///< Metallurgical junction depth [m]; 0 leaves out the short-channel factor
    Real nfs = 0.0;        ///< Fast surface state density [cm^-2]; 0 leaves out weak inversion

    // Threshold voltage parameters
    Real vto = 0.0;        ///< Zero-bias threshold voltage [V]
    Real phi = 0.6;        ///< Surface inversion potential [V]
    Real gamma = 0.0;      ///< Body effect parameter [V^0.5]
    // Whether VTO, PHI and GAMMA were given: a card without them takes them
    // from NSUB (and TPG, NSS). Values set in code count as given.
    bool vto_given = true;
    bool phi_given = true;
    bool gamma_given = true;

    // Mobility parameters
    Real uo = 600.0;       ///< Low-field mobility [cm^2/V-s] (600 for N, 250 for P)
    Real kp = 0.0;         ///< Transconductance parameter [A/V^2]; 0 takes UO Cox
    Real theta = 0.0;      ///< Mobility degradation [1/V]
    Real vmax = 0.0;       ///< Maximum drift velocity [m/s] (0 = infinite)

    // Short-channel effect parameters
    Real eta = 0.0;        ///< Static feedback on the threshold [dimensionless]
    Real kappa = 0.2;      ///< Saturation field factor of channel-length modulation
    Real delta = 0.0;      ///< Width effect on threshold [dimensionless]

    // Not a level 3 parameter (SPICE's level 3 has KAPPA instead); kept for
    // code that sets it, and not used
    Real lambda = 0.0;

    // Parasitic resistances
    Real rd = 0.0;         ///< Drain ohmic resistance [Ω]
    Real rs = 0.0;         ///< Source ohmic resistance [Ω]
    Real rsh = 0.0;        ///< Sheet resistance [Ω/square]

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

    // Temperature parameters
    Real tnom = 300.15;    ///< Parameter measurement temperature [K]; not used by the model

    // Noise parameters
    Real kf = 0.0;         ///< Flicker noise coefficient
    Real af = 1.0;         ///< Flicker noise exponent

    // Geometry parameters (instance-specific)
    Real w = 1e-4;         ///< Channel width [m] (100μm default)
    Real l = 1e-5;         ///< Channel length [m] (10μm default)
    Real ld = 0.0;         ///< Lateral diffusion length [m]
    Real wd = 0.0;         ///< Width reduction [m]
    Real ad = 0.0;         ///< Drain area [m^2]
    Real as = 0.0;         ///< Source area [m^2]
    Real pd = 0.0;         ///< Drain perimeter [m]
    Real ps = 0.0;         ///< Source perimeter [m]
    Real nrd = 0.0;        ///< Number of squares for drain resistance
    Real nrs = 0.0;        ///< Number of squares for source resistance

    /// Get effective channel length [m]
    Real get_leff() const {
        return l - 2.0 * ld;
    }

    /// Get effective channel width [m]
    Real get_weff() const {
        return w - 2.0 * wd;
    }

    /// Validate parameters
    /// @return true if all parameters are valid
    bool validate() const {
        if (get_leff() <= 0.0) return false;
        if (get_weff() <= 0.0) return false;
        if (tox <= 0.0) return false;
        if (uo <= 0.0) return false;
        return true;
    }
};

} // namespace kirimaspice::devices
