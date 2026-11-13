// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/current_source.cpp
#include "kirimaspice/devices/current_source.hpp"
#include <spdlog/spdlog.h>
#include <cmath>

namespace kirimaspice::devices {

CurrentSource::CurrentSource(DeviceId id, const std::string& name,
                             NodeId np, NodeId nn, Real dc_value,
                             Real ac_magnitude, Real ac_phase)
    : Device(id, name, DeviceType::ISOURCE)
    , nodes_{np, nn}
    , dc_value_(dc_value)
    , ac_magnitude_(ac_magnitude)
    , ac_phase_deg_(ac_phase) {

    spdlog::debug("Created current source {} (np={}, nn={}, IDC={}A, AC={}A∠{}°)",
                  name, np, nn, dc_value_, ac_magnitude_, ac_phase_deg_);
}

void CurrentSource::stamp_dc_linear(MNASystem& mna,
                                    const DenseVector& node_voltages) const {
    // MNA stamp for independent current source:
    // Current flows from nn to np (convention)
    // KCL at np: +I (current entering)
    // KCL at nn: -I (current leaving)
    //
    // RHS contributions:
    //   b[np] -= I  (current flowing into np)
    //   b[nn] += I  (current flowing out of nn)

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    // Apply source stepping scale factor
    Real current = dc_value_ * scale_factor_;

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp RHS
    // SPICE convention: I <n+> <n-> means current flows FROM n+ TO n-
    // At n+ (np): current LEAVES → negative injection (-current)
    // At n- (nn): current ENTERS → positive injection (+current)
    if (np_idx >= 0) {
        mna.b[np_idx] -= current;  // Current leaving n+
    }
    if (nn_idx >= 0) {
        mna.b[nn_idx] += current;  // Current entering n-
    }
}

void CurrentSource::stamp_transient(MNASystem& mna,
                                    const DenseVector& node_voltages,
                                    Real timestep,
                                    IntegrationMethod method,
                                    const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Independent current source: same as DC (bdf_coef ignored)
    stamp_dc_linear(mna, node_voltages);
}

void CurrentSource::stamp_ac(ComplexMNASystem& sys,
                             Real omega,
                             const DenseVector& dc_solution) const {
    (void)omega;
    (void)dc_solution;

    if (ac_magnitude_ == 0.0) {
        return;
    }

    static constexpr Real DEG_TO_RAD = M_PI / 180.0;
    Complex j(0.0, 1.0);
    Real phase_rad = ac_phase_deg_ * DEG_TO_RAD;
    Complex I_complex = ac_magnitude_ * std::exp(j * phase_rad);

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // SPICE convention: I <n+> <n-> means current flows FROM n+ TO n-
    // At n+ (np): current LEAVES → negative injection (-I)
    // At n- (nn): current ENTERS → positive injection (+I)
    if (np_idx >= 0 && static_cast<std::size_t>(np_idx) < sys.I.size()) {
        sys.I[np_idx] -= I_complex;
    }
    if (nn_idx >= 0 && static_cast<std::size_t>(nn_idx) < sys.I.size()) {
        sys.I[nn_idx] += I_complex;
    }

    spdlog::trace("AC current source {} stamped: {}A∠{}°", name(), ac_magnitude_, ac_phase_deg_);
}

} // namespace kirimaspice::devices
