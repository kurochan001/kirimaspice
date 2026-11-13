// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/ccvs.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Current-Controlled Voltage Source (CCVS): 4-terminal device
/// V_out = H * I_control
/// MNA: Adds extra current variable (branch current)
/// Netlist syntax: H<name> <n+> <n-> V<control> <transresistance>
/// Example: H1 out 0 Vsense 1000
///
/// MNA Stamp (2x2):
///       n+    n-    I_control   I_aux
/// n+  [  0     0       0         1  ]
/// n-  [  0     0       0        -1  ]
/// I   [  1    -1      -H         0  ] = 0
///
/// where:
///   I_control = branch current of controlling voltage source
///   I_aux = branch current of CCVS itself
///   H = transresistance [Ω]
///
/// Equation: V(n+) - V(n-) = H * I_control
class CCVS : public Device {
public:
    /// Constructor
    /// \param id Device ID
    /// \param name Device name
    /// \param np Positive output node
    /// \param nn Negative output node
    /// \param control_vsource_name Name of controlling voltage source
    /// \param transresistance Transresistance H [Ω]
    CCVS(DeviceId id, const std::string& name,
         NodeId np, NodeId nn,
         const std::string& control_vsource_name, Real transresistance);

    ~CCVS() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: stamp CCVS
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // CCVS is linear
    }

    // Transient analysis: same as DC (linear device)
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp CCVS
    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }

    // Accessors
    Real transresistance() const { return h_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    const std::string& control_vsource_name() const { return control_vsource_name_; }
    BranchId control_branch_id() const { return control_branch_id_; }
    BranchId branch_id() const { return branch_id_; }

    void set_control_branch_id(BranchId id) { control_branch_id_ = id; }
    void set_branch_id(BranchId id) { branch_id_ = id; }

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    std::string control_vsource_name_;  // Name of controlling voltage source
    Real h_;                      // Transresistance H [Ω]
    BranchId control_branch_id_;  // Branch ID of controlling voltage source
    BranchId branch_id_;          // Branch ID of CCVS itself (auxiliary variable)
};

} // namespace kirimaspice::devices
