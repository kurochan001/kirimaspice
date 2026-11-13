// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/vccs.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Voltage-Controlled Current Source (VCCS): 4-terminal device
/// I_out = Gm * (V_c+ - V_c-)
/// MNA: No extra variables (directly stamps conductance)
/// Netlist syntax: G<name> <n+> <n-> <nc+> <nc-> <transconductance>
/// Example: G1 out 0 in 0 0.001
///
/// MNA Stamp (1x2):
///       n+    n-    nc+   nc-
/// n+  [  0     0     Gm   -Gm ]
/// n-  [  0     0    -Gm    Gm ]
///
/// Equation: I(n+ -> n-) = Gm * [V(nc+) - V(nc-)]
class VCCS : public Device {
public:
    /// Constructor
    /// \param id Device ID
    /// \param name Device name
    /// \param np Positive output node
    /// \param nn Negative output node
    /// \param ncp Positive control node
    /// \param ncn Negative control node
    /// \param transconductance Transconductance Gm [S] (Siemens)
    VCCS(DeviceId id, const std::string& name,
         NodeId np, NodeId nn, NodeId ncp, NodeId ncn, Real transconductance);

    ~VCCS() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: stamp VCCS
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // VCCS is linear
    }

    // Transient analysis: same as DC (linear device)
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp VCCS
    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }

    // Accessors
    Real transconductance() const { return gm_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    NodeId ncp() const { return nodes_[2]; }
    NodeId ncn() const { return nodes_[3]; }

private:
    std::vector<NodeId> nodes_;  // [np, nn, ncp, ncn]
    Real gm_;  // Transconductance Gm [S]
};

} // namespace kirimaspice::devices
