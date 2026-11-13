// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/voltage_source.cpp
#include "kirimaspice/devices/voltage_source.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

VoltageSource::VoltageSource(DeviceId id, const std::string& name,
                             NodeId np, NodeId nn, Real dc_value,
                             Real ac_magnitude, Real ac_phase)
    : Device(id, name, DeviceType::VSOURCE)
    , nodes_{np, nn}
    , dc_value_(dc_value)
    , ac_magnitude_(ac_magnitude)
    , ac_phase_(ac_phase)
    , branch_id_(INVALID_BRANCH) {

    spdlog::debug("Created voltage source {} (np={}, nn={}, DC={}V, AC={}V∠{}°)",
                  name, np, nn, dc_value_, ac_magnitude_, ac_phase_);
}

void VoltageSource::stamp_dc_linear(MNASystem& mna,
                                    const DenseVector& node_voltages) const {
    // MNA stamp for independent voltage source:
    // Extended MNA system: [ G  C ] [ V ] = [ I ]
    //                      [ D  0 ] [ J ]   [ E ]
    // where:
    //   G: conductance matrix (n x n)
    //   C: branch-to-node incidence (-1, +1) (n x m)
    //   D: node-to-branch incidence (+1, -1) (m x n)
    //   J: branch currents (m x 1)
    //   E: source values (m x 1)
    //
    // For voltage source from np to nn with voltage V:
    //   C[np, branch] = +1
    //   C[nn, branch] = -1
    //   D[branch, np] = +1
    //   D[branch, nn] = -1
    //   E[branch] = V

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = mna.num_nodes;
    std::size_t br = n + branch_id_;  // Branch variable index in extended system

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp C matrix (top-right block)
    // Branch current flows from nn to np (out of positive terminal)
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, br, -1.0);
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, br, 1.0);
    }

    // Stamp D matrix (bottom-left block)
    // Branch equation: V(np) - V(nn) = V_source
    if (np_idx >= 0) {
        mna.G.add_entry(br, np_idx, 1.0);
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(br, nn_idx, -1.0);
    }

    // Stamp E vector (RHS for branch equations)
    // Apply source stepping scale factor
    mna.b[br] = dc_value_ * scale_factor_;
}

void VoltageSource::stamp_transient(MNASystem& mna,
                                    const DenseVector& node_voltages,
                                    Real timestep,
                                    IntegrationMethod method,
                                    const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Independent voltage source: same as DC (bdf_coef ignored)
    stamp_dc_linear(mna, node_voltages);
}

void VoltageSource::stamp_ac(ComplexMNASystem& sys,
                              Real omega,
                              const DenseVector& dc_solution) const {
    // AC analysis stamp for voltage source
    // Complex voltage constraint: V(np) - V(nn) = V_ac * e^(jφ)
    // Same MNA structure as DC, but with complex RHS

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = sys.num_nodes;
    std::size_t br = n + branch_id_;  // Branch variable index in extended system

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp Y matrix (admittance)
    // Branch current flows from nn to np (out of positive terminal)
    if (np_idx >= 0) {
        sys.Y.add_entry(np_idx, br, Complex(-1.0, 0.0));
    }
    if (nn_idx >= 0) {
        sys.Y.add_entry(nn_idx, br, Complex(1.0, 0.0));
    }

    // Branch equation: V(np) - V(nn) = V_source
    if (np_idx >= 0) {
        sys.Y.add_entry(br, np_idx, Complex(1.0, 0.0));
    }
    if (nn_idx >= 0) {
        sys.Y.add_entry(br, nn_idx, Complex(-1.0, 0.0));
    }

    // Compute complex voltage: V_ac * e^(jφ)
    // φ is in degrees, convert to radians
    Real phase_rad = ac_phase_ * M_PI / 180.0;
    Complex V_complex = ac_magnitude_ * std::exp(Complex(0.0, phase_rad));

    // Stamp RHS vector (complex voltage source value)
    sys.I[br] = V_complex;

    spdlog::trace("AC stamp: Vsrc {} at branch {}, V_ac={}V∠{}° = {} + j{}",
                  name(), branch_id_, ac_magnitude_, ac_phase_,
                  std::real(V_complex), std::imag(V_complex));
}

} // namespace kirimaspice::devices
