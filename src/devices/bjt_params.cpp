// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "kirimaspice/devices/bjt_params.hpp"

namespace kirimaspice::devices {

bool set_bjt_parameter(BJTParams& p, const std::string& key, Real value) {
    if (key == "is") p.is = value;
    else if (key == "bf" || key == "beta_f") p.beta_f = value;
    else if (key == "nf" || key == "n") p.nf = value;
    else if (key == "br") p.beta_r = value;
    else if (key == "nr") p.nr = value;
    else if (key == "vaf" || key == "va") p.vaf = value;
    else if (key == "var" || key == "vb") p.var = value;
    else if (key == "ikf" || key == "ik") p.ikf = value;
    else if (key == "ikr") p.ikr = value;
    else if (key == "rb") p.rb = value;
    else if (key == "rbm") p.rbm = value;
    else if (key == "irb") p.irb = value;
    else if (key == "ise") p.ise = value;
    else if (key == "ne") p.ne = value;
    else if (key == "isc") p.isc_leak = value;
    else if (key == "nc") p.nc = value;
    else if (key == "cje") p.cje = value;
    else if (key == "vje" || key == "pe") p.vje = value;
    else if (key == "mje" || key == "me") p.mje = value;
    else if (key == "cjc") p.cjc = value;
    else if (key == "vjc" || key == "pc") p.vjc = value;
    else if (key == "mjc" || key == "mc") p.mjc = value;
    else if (key == "tf") p.tf = value;
    else if (key == "tr") p.tr = value;
    else if (key == "kf") p.kf = value;
    else if (key == "af") p.af = value;
    else if (key == "ff") p.ff = value;
    else if (key == "xti") p.xti = value;
    else if (key == "xtb") p.xtb = value;
    else if (key == "eg") p.eg = value;
    else if (key == "tnom") p.tnom = value + 273.15;
    else return false;
    return true;
}

BJTParams bjt_params_from_model(const std::map<std::string, Real>& model_params) {
    BJTParams params;
    for (const auto& [key, value] : model_params) {
        set_bjt_parameter(params, key, value);
    }
    return params;
}

}  // namespace kirimaspice::devices
