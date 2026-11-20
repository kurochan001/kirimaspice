// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/types.hpp"

#include <map>
#include <string>

namespace kirimaspice::devices {

/// The parameters of a bipolar transistor, as the constructors of BJT_NPN and
/// BJT_PNP take them, with SPICE's defaults (0 for VAF, VAR, IKF and IKR is
/// infinite). TNOM is in kelvin.
struct BJTParams {
    Real is = 1e-14, beta_f = 100.0, nf = 1.0;
    Real isc = -1.0;  ///< B-C saturation current of the transport model, -1: IS
    Real beta_r = 1.0, nr = 1.0;
    Real vaf = 0.0, var = 0.0, ikf = 0.0, ikr = 0.0;
    Real rb = 0.0, rbm = 0.0, irb = 0.0;
    Real ise = 0.0, ne = 1.5;
    Real isc_leak = 0.0, nc = 2.0;  ///< SPICE's ISC and NC, the B-C leakage
    Real cje = 0.0, vje = 0.75, mje = 0.33;
    Real cjc = 0.0, vjc = 0.75, mjc = 0.33;
    Real tf = 0.0, tr = 0.0;
    Real kf = 0.0, af = 1.0, ff = 1.0;
    Real xti = 3.0, xtb = 0.0, eg = 1.11;
    Real tnom = 300.15;
};

/// Set one parameter by its SPICE name in lower case (IS, BF, NF, BR, NR,
/// VAF or VA, VAR or VB, IKF or IK, IKR, RB, RBM, IRB, ISE, NE, ISC, NC,
/// CJE, VJE or PE, MJE or ME, CJC, VJC or PC, MJC or MC, TF, TR, KF, AF, FF,
/// XTI, XTB, EG and TNOM in degrees Celsius; N for NF on an element line)
/// and say whether the name is one. The model card and the element line both
/// set the parameters here, the element line last.
bool set_bjt_parameter(BJTParams& params, const std::string& key, Real value);

/// The parameters of a .MODEL card (names in lower case)
/// The names that set_bjt_parameter does not take are appended to unused
BJTParams bjt_params_from_model(const std::map<std::string, Real>& model_params,
                                std::vector<std::string>* unused = nullptr);

}  // namespace kirimaspice::devices
