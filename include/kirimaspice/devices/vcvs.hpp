// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/vcvs.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Voltage-Controlled Voltage Source (VCVS): 4-terminal device
/// V_out = A * (V_c+ - V_c-)
/// MNA: Adds extra current variable (branch current)
/// Netlist syntax: E<name> <n+> <n-> <nc+> <nc-> <gain>
/// Example: E1 out 0 in 0 100
///
/// MNA Stamp (2x2):
///       n+    n-    nc+   nc-   I_aux
/// n+  [  0     0     0     0      1  ]
/// n-  [  0     0     0     0     -1  ]
/// I   [  1    -1    -A     A      0  ] = 0
///
/// Equation: V(n+) - V(n-) = A * [V(nc+) - V(nc-)]
class VCVS : public Device {
public:
    /// Constructor
    /// \param id Device ID
    /// \param name Device name
    /// \param np Positive output node
    /// \param nn Negative output node
    /// \param ncp Positive control node
    /// \param ncn Negative control node
    /// \param gain Voltage gain A (dimensionless)
    VCVS(DeviceId id, const std::string& name,
         NodeId np, NodeId nn, NodeId ncp, NodeId ncn, Real gain);

    ~VCVS() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: stamp VCVS constraint
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // VCVS is linear
    }

    // Transient analysis: same as DC (linear device)
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp VCVS constraint
    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }

    // Accessors
    Real gain() const { return gain_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    NodeId ncp() const { return nodes_[2]; }
    NodeId ncn() const { return nodes_[3]; }
    BranchId branch_id() const { return branch_id_; }

    void set_branch_id(BranchId id) { branch_id_ = id; }

private:
    std::vector<NodeId> nodes_;  // [np, nn, ncp, ncn]
    Real gain_;           // Voltage gain A (dimensionless)
    BranchId branch_id_;  // Branch current variable index in MNA
};

} // namespace kirimaspice::devices
