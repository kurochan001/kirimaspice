// include/kirimaspice/devices/cccs.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Current-Controlled Current Source (CCCS): 4-terminal device
/// I_out = F * I_control
/// MNA: No extra variables (current-controlled)
/// Netlist syntax: F<name> <n+> <n-> V<control> <gain>
/// Example: F1 out 0 Vsense 10
///
/// MNA Stamp:
///       n+    n-    I_control
/// n+  [  0     0      +F  ]
/// n-  [  0     0      -F  ]
///
/// where I_control is the branch current of the controlling voltage source
/// Equation: I(n+ -> n-) = F * I_control
class CCCS : public Device {
public:
    /// Constructor
    /// \param id Device ID
    /// \param name Device name
    /// \param np Positive output node
    /// \param nn Negative output node
    /// \param control_vsource_name Name of controlling voltage source
    /// \param gain Current gain F (dimensionless)
    CCCS(DeviceId id, const std::string& name,
         NodeId np, NodeId nn,
         const std::string& control_vsource_name, Real gain);

    ~CCCS() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: stamp CCCS
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // CCCS is linear
    }

    // Transient analysis: same as DC (linear device)
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp CCCS
    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }

    // Accessors
    Real gain() const { return gain_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    const std::string& control_vsource_name() const { return control_vsource_name_; }
    BranchId control_branch_id() const { return control_branch_id_; }

    void set_control_branch_id(BranchId id) { control_branch_id_ = id; }

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    std::string control_vsource_name_;  // Name of controlling voltage source
    Real gain_;                   // Current gain F (dimensionless)
    BranchId control_branch_id_;  // Branch ID of controlling voltage source
};

} // namespace kirimaspice::devices
