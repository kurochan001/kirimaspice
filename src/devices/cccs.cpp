// src/devices/cccs.cpp
#include "kirimaspice/devices/cccs.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

CCCS::CCCS(DeviceId id, const std::string& name,
           NodeId np, NodeId nn,
           const std::string& control_vsource_name, Real gain)
    : Device(id, name, DeviceType::CCCS)
    , nodes_{np, nn}
    , control_vsource_name_(control_vsource_name)
    , gain_(gain)
    , control_branch_id_(INVALID_BRANCH) {

    spdlog::debug("Created CCCS {} (np={}, nn={}, control={}, gain={})",
                  name, np, nn, control_vsource_name_, gain_);
}

void CCCS::stamp_dc_linear(MNASystem& mna,
                           const DenseVector& node_voltages) const {
    // MNA stamp for CCCS:
    // I(n+ -> n-) = F * I_control
    //
    // MNA Stamp:
    //       n+    n-    I_control
    // n+  [  0     0      +F  ]
    // n-  [  0     0      -F  ]
    //
    // where I_control is the branch current variable of the controlling voltage source
    // The controlling voltage source must have been assigned a branch ID

    if (control_branch_id_ == INVALID_BRANCH) {
        throw std::runtime_error(
            fmt::format("CCCS {}: controlling voltage source '{}' branch ID not set",
                       name(), control_vsource_name_));
    }

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = mna.num_nodes;
    std::size_t ctrl_br = n + control_branch_id_;  // Control branch variable index

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp G matrix: current contribution
    // Current flows into np (positive terminal) and out of nn (negative terminal)
    // I(np) = +F * I_control
    // I(nn) = -F * I_control
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, ctrl_br, gain_);   // KCL at np: +F*I_control
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, ctrl_br, -gain_);  // KCL at nn: -F*I_control
    }

    // No RHS contribution (current source controlled by existing branch variable)
}

void CCCS::stamp_transient(MNASystem& mna,
                           const DenseVector& node_voltages,
                           Real timestep,
                           IntegrationMethod method,
                           const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // CCCS is a linear, static device: same as DC
    stamp_dc_linear(mna, node_voltages);
}

void CCCS::stamp_ac(ComplexMNASystem& sys,
                     Real omega,
                     const DenseVector& dc_solution) const {
    // AC analysis stamp for CCCS
    // Same structure as DC, but with complex matrices
    // I(n+ -> n-) = F * I_control

    if (control_branch_id_ == INVALID_BRANCH) {
        throw std::runtime_error(
            fmt::format("CCCS {}: controlling voltage source '{}' branch ID not set",
                       name(), control_vsource_name_));
    }

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = sys.num_nodes;
    std::size_t ctrl_br = n + control_branch_id_;  // Control branch variable index

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp Y matrix (admittance): current contribution
    // I(np) = +F * I_control
    // I(nn) = -F * I_control
    if (np_idx >= 0) {
        sys.Y.add_entry(np_idx, ctrl_br, Complex(gain_, 0.0));   // KCL at np
    }
    if (nn_idx >= 0) {
        sys.Y.add_entry(nn_idx, ctrl_br, Complex(-gain_, 0.0));  // KCL at nn
    }

    // No RHS contribution
    spdlog::trace("AC stamp: CCCS {} (control={}), gain={}, omega={}",
                  name(), control_vsource_name_, gain_, omega);
}

} // namespace kirimaspice::devices
