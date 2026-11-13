// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/bsim3_core.hpp
// BSIM3v3.3 DC core evaluator (Ids/Vth/Vdsat)
// This core is a first step toward a full ngspice-compatible BSIM3 implementation.

#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/devices/mosfet_bsim3_params.hpp"

namespace kirimaspice::devices {

struct BSIM3OperatingPoint {
    Real ids = 0.0;    ///< Drain current [A] (positive from drain to source for NMOS)
    Real vth = 0.0;    ///< Effective threshold voltage [V]
    Real vdsat = 0.0;  ///< Saturation drain-source voltage [V]
};

/// Lightweight BSIM3 DC evaluator (NMOS-oriented).
/// For now this core focuses on DC Ids, Vth, Vdsat for NMOS.
/// P-channel support and full ngspice equation parity can be added incrementally.
class BSIM3Core {
public:
    explicit BSIM3Core(const BSIM3Params& params);

    /// Evaluate BSIM3 DC operating point for given terminal biases.
    /// Bias conventions (NMOS):
    ///   vgs = Vg - Vs, vds = Vd - Vs, vbs = Vb - Vs.
    /// @param vgs Gate-source voltage [V]
    /// @param vds Drain-source voltage [V]
    /// @param vbs Bulk-source voltage [V]
    /// @param temperature Device temperature [K]
    /// @return Operating point (Ids, Vth, Vdsat)
    BSIM3OperatingPoint evaluate(Real vgs, Real vds, Real vbs,
                                 Real temperature) const;

private:
    const BSIM3Params& params_;

    // Helper functions (local to core, NMOS-oriented)
    Real calc_leff() const;
    Real calc_weff() const;
    Real calc_vth(Real vbs, Real leff, Real weff) const;
    Real apply_temp_vth(Real vth_base, Real vbs, Real temperature) const;
    Real calc_mobility(Real vgs, Real vds, Real vbs,
                       Real vth, Real leff, Real weff) const;
    Real apply_temp_mobility(Real u0_base, Real temperature) const;
    Real calc_vdsat(Real vgs, Real vds, Real vbs, Real vth,
                    Real leff, Real weff, Real temperature) const;
    Real apply_temp_vsat(Real vsat_base, Real temperature) const;
    Real calc_dibl(Real vds, Real leff) const;
    Real calc_clm_factor(Real vds, Real vdsat, Real leff) const;
    Real calc_vdseff(Real vds, Real vdsat) const;

    /// Subthreshold drain current (Vgs below effective Vth, but > 0).
    /// BSIM3 スタイルの指数テール＋W/L スケーリング＋nfactor・VOFF・Vds 依存を近似的に実装する。
    Real compute_ids_subthreshold(Real vgs, Real vds, Real vbs, Real vth,
                                  Real leff, Real weff, Real temperature) const;

    /// Raw Ids calculation (no derivatives).
    /// Returns Ids and fills vth_out / vdsat_out.
    Real compute_ids_raw(Real vgs, Real vds, Real vbs, Real temperature,
                         Real& vth_out, Real& vdsat_out) const;
};

} // namespace kirimaspice::devices
