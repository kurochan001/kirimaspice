// src/devices/ccvs.cpp
#include "kirimaspice/devices/ccvs.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

CCVS::CCVS(DeviceId id, const std::string& name,
           NodeId np, NodeId nn,
           const std::string& control_vsource_name, Real transresistance)
    : Device(id, name, DeviceType::CCVS)
    , nodes_{np, nn}
    , control_vsource_name_(control_vsource_name)
    , h_(transresistance)
    , control_branch_id_(INVALID_BRANCH)
    , branch_id_(INVALID_BRANCH) {

    spdlog::debug("Created CCVS {} (np={}, nn={}, control={}, H={}Ω)",
                  name, np, nn, control_vsource_name_, h_);
}

void CCVS::stamp_dc_linear(MNASystem& mna,
                           const DenseVector& node_voltages) const {
    // MNA stamp for CCVS:
    // V(n+) - V(n-) = H * I_control
    //
    // MNA Stamp (2x2):
    //       n+    n-    I_control   I_aux
    // n+  [  0     0       0         1  ]
    // n-  [  0     0       0        -1  ]
    // I   [  1    -1      -H         0  ] = 0
    //
    // where:
    //   I_control = branch current of controlling voltage source
    //   I_aux = branch current of CCVS itself (auxiliary variable)
    //   H = transresistance [Ω]

    if (control_branch_id_ == INVALID_BRANCH) {
        throw std::runtime_error(
            fmt::format("CCVS {}: controlling voltage source '{}' branch ID not set",
                       name(), control_vsource_name_));
    }

    if (branch_id_ == INVALID_BRANCH) {
        throw std::runtime_error(
            fmt::format("CCVS {}: branch ID not set", name()));
    }

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = mna.num_nodes;
    std::size_t ctrl_br = n + control_branch_id_;  // Control branch variable index
    std::size_t br = n + branch_id_;               // CCVS branch variable index

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp C matrix (top-right block): current contribution
    // Branch current I_aux flows out of positive terminal (np)
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, br, -1.0);  // KCL at np: -I_aux
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, br, 1.0);   // KCL at nn: +I_aux
    }

    // Stamp D matrix (bottom-left block): voltage constraint
    // Branch equation: V(np) - V(nn) - H*I_control = 0
    if (np_idx >= 0) {
        mna.G.add_entry(br, np_idx, 1.0);   // +V(np)
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(br, nn_idx, -1.0);  // -V(nn)
    }
    mna.G.add_entry(br, ctrl_br, -h_);      // -H*I_control

    // RHS is zero (homogeneous constraint)
    mna.b[br] = 0.0;
}

void CCVS::stamp_transient(MNASystem& mna,
                           const DenseVector& node_voltages,
                           Real timestep,
                           IntegrationMethod method,
                           const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // CCVS is a linear, static device: same as DC
    stamp_dc_linear(mna, node_voltages);
}

void CCVS::stamp_ac(ComplexMNASystem& sys,
                     Real omega,
                     const DenseVector& dc_solution) const {
    // AC analysis stamp for CCVS
    // Same structure as DC, but with complex matrices
    // V(n+) - V(n-) = H * I_control

    if (control_branch_id_ == INVALID_BRANCH) {
        throw std::runtime_error(
            fmt::format("CCVS {}: controlling voltage source '{}' branch ID not set",
                       name(), control_vsource_name_));
    }

    if (branch_id_ == INVALID_BRANCH) {
        throw std::runtime_error(
            fmt::format("CCVS {}: branch ID not set", name()));
    }

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = sys.num_nodes;
    std::size_t ctrl_br = n + control_branch_id_;  // Control branch variable index
    std::size_t br = n + branch_id_;               // CCVS branch variable index

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp Y matrix (admittance): current contribution
    // Branch current I_aux flows out of positive terminal (np)
    if (np_idx >= 0) {
        sys.Y.add_entry(np_idx, br, Complex(-1.0, 0.0));  // KCL at np: -I_aux
    }
    if (nn_idx >= 0) {
        sys.Y.add_entry(nn_idx, br, Complex(1.0, 0.0));   // KCL at nn: +I_aux
    }

    // Branch equation: V(np) - V(nn) - H*I_control = 0
    if (np_idx >= 0) {
        sys.Y.add_entry(br, np_idx, Complex(1.0, 0.0));   // +V(np)
    }
    if (nn_idx >= 0) {
        sys.Y.add_entry(br, nn_idx, Complex(-1.0, 0.0));  // -V(nn)
    }
    sys.Y.add_entry(br, ctrl_br, Complex(-h_, 0.0));      // -H*I_control

    // RHS is zero (homogeneous constraint)
    sys.I[br] = Complex(0.0, 0.0);

    spdlog::trace("AC stamp: CCVS {} (control={}), H={}Ω, omega={}",
                  name(), control_vsource_name_, h_, omega);
}

} // namespace kirimaspice::devices
