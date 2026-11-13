// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/vcvs.cpp
#include "kirimaspice/devices/vcvs.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

VCVS::VCVS(DeviceId id, const std::string& name,
           NodeId np, NodeId nn, NodeId ncp, NodeId ncn, Real gain)
    : Device(id, name, DeviceType::VCVS)
    , nodes_{np, nn, ncp, ncn}
    , gain_(gain)
    , branch_id_(INVALID_BRANCH) {

    spdlog::debug("Created VCVS {} (np={}, nn={}, ncp={}, ncn={}, gain={})",
                  name, np, nn, ncp, ncn, gain_);
}

void VCVS::stamp_dc_linear(MNASystem& mna,
                           const DenseVector& node_voltages) const {
    // MNA stamp for VCVS:
    // Extended MNA system: [ G  C ] [ V ] = [ I ]
    //                      [ D  0 ] [ J ]   [ E ]
    //
    // VCVS equation: V(np) - V(nn) = A * [V(ncp) - V(ncn)]
    //
    // MNA Stamp (2x2):
    //       n+    n-    nc+   nc-   I_aux
    // n+  [  0     0     0     0      1  ]
    // n-  [  0     0     0     0     -1  ]
    // I   [  1    -1    -A     A      0  ] = 0
    //
    // where:
    //   n+ = np (positive output node)
    //   n- = nn (negative output node)
    //   nc+ = ncp (positive control node)
    //   nc- = ncn (negative control node)
    //   I_aux = branch current variable
    //   A = gain

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    NodeId ncp = nodes_[2];
    NodeId ncn = nodes_[3];
    std::size_t n = mna.num_nodes;
    std::size_t br = n + branch_id_;  // Branch variable index in extended system

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);
    auto ncp_idx = (ncp == NODE_GND) ? -1 : static_cast<int>(ncp - 1);
    auto ncn_idx = (ncn == NODE_GND) ? -1 : static_cast<int>(ncn - 1);

    // Stamp C matrix (top-right block): current contribution
    // Branch current flows out of positive terminal (np)
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, br, 1.0);   // KCL at np: +I_aux (current into n+ through the source)
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, br, -1.0);  // KCL at nn: -I_aux
    }

    // Stamp D matrix (bottom-left block): voltage constraint
    // Branch equation: V(np) - V(nn) - A*[V(ncp) - V(ncn)] = 0
    if (np_idx >= 0) {
        mna.G.add_entry(br, np_idx, 1.0);   // +V(np)
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(br, nn_idx, -1.0);  // -V(nn)
    }
    if (ncp_idx >= 0) {
        mna.G.add_entry(br, ncp_idx, -gain_);  // -A*V(ncp)
    }
    if (ncn_idx >= 0) {
        mna.G.add_entry(br, ncn_idx, gain_);   // +A*V(ncn)
    }

    // RHS is zero (homogeneous constraint)
    mna.b[br] = 0.0;
}

void VCVS::stamp_transient(MNASystem& mna,
                           const DenseVector& node_voltages,
                           Real timestep,
                           IntegrationMethod method,
                           const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // VCVS is a linear, static device: same as DC
    stamp_dc_linear(mna, node_voltages);
}

void VCVS::stamp_ac(ComplexMNASystem& sys,
                     Real omega,
                     const DenseVector& dc_solution) const {
    // AC analysis stamp for VCVS
    // Same structure as DC, but with complex matrices
    // Constraint: V(np) - V(nn) = A * [V(ncp) - V(ncn)]

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    NodeId ncp = nodes_[2];
    NodeId ncn = nodes_[3];
    std::size_t n = sys.num_nodes;
    std::size_t br = n + branch_id_;  // Branch variable index in extended system

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);
    auto ncp_idx = (ncp == NODE_GND) ? -1 : static_cast<int>(ncp - 1);
    auto ncn_idx = (ncn == NODE_GND) ? -1 : static_cast<int>(ncn - 1);

    // Stamp Y matrix (admittance): current contribution
    // Branch current flows out of positive terminal (np)
    if (np_idx >= 0) {
        sys.Y.add_entry(np_idx, br, Complex(1.0, 0.0));   // KCL at np: +I_aux (current into n+ through the source)
    }
    if (nn_idx >= 0) {
        sys.Y.add_entry(nn_idx, br, Complex(-1.0, 0.0));  // KCL at nn: -I_aux
    }

    // Branch equation: V(np) - V(nn) - A*[V(ncp) - V(ncn)] = 0
    if (np_idx >= 0) {
        sys.Y.add_entry(br, np_idx, Complex(1.0, 0.0));   // +V(np)
    }
    if (nn_idx >= 0) {
        sys.Y.add_entry(br, nn_idx, Complex(-1.0, 0.0));  // -V(nn)
    }
    if (ncp_idx >= 0) {
        sys.Y.add_entry(br, ncp_idx, Complex(-gain_, 0.0));  // -A*V(ncp)
    }
    if (ncn_idx >= 0) {
        sys.Y.add_entry(br, ncn_idx, Complex(gain_, 0.0));   // +A*V(ncn)
    }

    // RHS is zero (homogeneous constraint)
    sys.I[br] = Complex(0.0, 0.0);

    spdlog::trace("AC stamp: VCVS {} at branch {}, gain={}, omega={}",
                  name(), branch_id_, gain_, omega);
}

} // namespace kirimaspice::devices
