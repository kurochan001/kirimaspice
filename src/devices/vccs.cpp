// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/vccs.cpp
#include "kirimaspice/devices/vccs.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

VCCS::VCCS(DeviceId id, const std::string& name,
           NodeId np, NodeId nn, NodeId ncp, NodeId ncn, Real transconductance)
    : Device(id, name, DeviceType::VCCS)
    , nodes_{np, nn, ncp, ncn}
    , gm_(transconductance) {

    spdlog::debug("Created VCCS {} (np={}, nn={}, ncp={}, ncn={}, Gm={}S)",
                  name, np, nn, ncp, ncn, gm_);
}

void VCCS::stamp_dc_linear(MNASystem& mna,
                           const DenseVector& node_voltages) const {
    // MNA stamp for VCCS:
    // I(n+ -> n-) = Gm * [V(ncp) - V(ncn)]
    //
    // MNA Stamp (1x2):
    //       n+    n-    nc+   nc-
    // n+  [  0     0     Gm   -Gm ]
    // n-  [  0     0    -Gm    Gm ]
    //
    // This is equivalent to a voltage-controlled admittance:
    //   I(np) = +Gm * V_control = +Gm*(V(ncp) - V(ncn))
    //   I(nn) = -Gm * V_control = -Gm*(V(ncp) - V(ncn))
    //
    // Expanding:
    //   I(np) = +Gm*V(ncp) - Gm*V(ncn)
    //   I(nn) = -Gm*V(ncp) + Gm*V(ncn)

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    NodeId ncp = nodes_[2];
    NodeId ncn = nodes_[3];

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);
    auto ncp_idx = (ncp == NODE_GND) ? -1 : static_cast<int>(ncp - 1);
    auto ncn_idx = (ncn == NODE_GND) ? -1 : static_cast<int>(ncn - 1);

    // Stamp G matrix: current contribution
    // Row np: I(np) = +Gm*V(ncp) - Gm*V(ncn)
    if (np_idx >= 0 && ncp_idx >= 0) {
        mna.G.add_entry(np_idx, ncp_idx, gm_);    // +Gm * V(ncp)
    }
    if (np_idx >= 0 && ncn_idx >= 0) {
        mna.G.add_entry(np_idx, ncn_idx, -gm_);   // -Gm * V(ncn)
    }

    // Row nn: I(nn) = -Gm*V(ncp) + Gm*V(ncn)
    if (nn_idx >= 0 && ncp_idx >= 0) {
        mna.G.add_entry(nn_idx, ncp_idx, -gm_);   // -Gm * V(ncp)
    }
    if (nn_idx >= 0 && ncn_idx >= 0) {
        mna.G.add_entry(nn_idx, ncn_idx, gm_);    // +Gm * V(ncn)
    }

    // No RHS contribution (homogeneous current source)
}

void VCCS::stamp_transient(MNASystem& mna,
                           const DenseVector& node_voltages,
                           Real timestep,
                           IntegrationMethod method,
                           const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // VCCS is a linear, static device: same as DC
    stamp_dc_linear(mna, node_voltages);
}

void VCCS::stamp_ac(ComplexMNASystem& sys,
                     Real omega,
                     const DenseVector& dc_solution) const {
    // AC analysis stamp for VCCS
    // Same structure as DC, but with complex matrices
    // I(n+ -> n-) = Gm * [V(ncp) - V(ncn)]

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    NodeId ncp = nodes_[2];
    NodeId ncn = nodes_[3];

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);
    auto ncp_idx = (ncp == NODE_GND) ? -1 : static_cast<int>(ncp - 1);
    auto ncn_idx = (ncn == NODE_GND) ? -1 : static_cast<int>(ncn - 1);

    // Stamp Y matrix (admittance): current contribution
    // Row np: I(np) = +Gm*V(ncp) - Gm*V(ncn)
    if (np_idx >= 0 && ncp_idx >= 0) {
        sys.Y.add_entry(np_idx, ncp_idx, Complex(gm_, 0.0));    // +Gm * V(ncp)
    }
    if (np_idx >= 0 && ncn_idx >= 0) {
        sys.Y.add_entry(np_idx, ncn_idx, Complex(-gm_, 0.0));   // -Gm * V(ncn)
    }

    // Row nn: I(nn) = -Gm*V(ncp) + Gm*V(ncn)
    if (nn_idx >= 0 && ncp_idx >= 0) {
        sys.Y.add_entry(nn_idx, ncp_idx, Complex(-gm_, 0.0));   // -Gm * V(ncp)
    }
    if (nn_idx >= 0 && ncn_idx >= 0) {
        sys.Y.add_entry(nn_idx, ncn_idx, Complex(gm_, 0.0));    // +Gm * V(ncn)
    }

    // No RHS contribution
    spdlog::trace("AC stamp: VCCS {}, Gm={}S, omega={}",
                  name(), gm_, omega);
}

} // namespace kirimaspice::devices
