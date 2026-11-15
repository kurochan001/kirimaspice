// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/behavioral_source.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/types.hpp"
#include <functional>

namespace kirimaspice::devices {

/// Behavioral Source: Arbitrary voltage or current source defined by expression
/// B{name} n+ n- V={expression}  (voltage source)
/// B{name} n+ n- I={expression}  (current source)
///
/// Expressions can reference:
/// - TIME: current simulation time
/// - V(node): voltage at node
/// - I(source): current through voltage source
/// - .PARAM parameters
/// - .FUNC user-defined functions
/// - Math functions: sin, cos, exp, log, sqrt, etc.
///
/// Basic implementation
class BehavioralSource : public Device {
public:
    using ExpressionEvaluator = std::function<Real(Real time, const DenseVector& node_voltages)>;

    BehavioralSource(DeviceId id, const std::string& name,
                     NodeId np, NodeId nn,
                     BehavioralSourceType type,
                     const std::string& expression,
                     ExpressionEvaluator evaluator);

    ~BehavioralSource() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: evaluate expression and stamp
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override;

    // Transient analysis: evaluate expression at current time
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: small-signal linearization (not supported yet)
    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return false; }  // Expression can be nonlinear
    bool is_dynamic() const override { return false; }  // No internal state

    // Accessors
    BehavioralSourceType type() const { return type_; }
    const std::string& expression() const { return expression_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    BranchId branch_id() const { return branch_id_; }

    void set_branch_id(BranchId id) { branch_id_ = id; }
    /// TIME in the expression (Device::set_time), 0 outside a transient
    void set_time(std::optional<Real> time) override { current_time_ = time.value_or(0.0); }

private:
    std::vector<NodeId> nodes_;       // [np, nn]
    BehavioralSourceType type_;       // VOLTAGE or CURRENT
    std::string expression_;          // Expression string
    ExpressionEvaluator evaluator_;   // Expression evaluator function
    BranchId branch_id_;              // Branch current variable (for voltage source)
    mutable Real current_time_ = 0.0; // Current simulation time (for TIME variable)

    // Helper: evaluate expression at current state
    Real evaluate_expression(Real time, const DenseVector& node_voltages) const;
};

} // namespace kirimaspice::devices
