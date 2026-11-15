// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/behavioral_source.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/types.hpp"
#include <functional>
#include <utility>
#include <vector>

namespace kirimaspice::devices {

/// Behavioral Source: Arbitrary voltage or current source defined by expression
/// B{name} n+ n- V={expression}  (voltage source)
/// B{name} n+ n- I={expression}  (current source)
///
/// Expressions can reference TIME, V(node) and ExprTk's constants and
/// functions; not I(source), .PARAM names or .FUNC functions
/// (REFERENCE_MANUAL, Behavioural sources). Every stamp linearizes the
/// expression at the present voltages (linearize).
class BehavioralSource : public Device {
public:
    using ExpressionEvaluator = std::function<Real(Real time, const DenseVector& node_voltages)>;

    /// @param controlling_nodes the nodes whose voltages the expression
    ///        reads; the stamps take its derivatives with respect to them
    BehavioralSource(DeviceId id, const std::string& name,
                     NodeId np, NodeId nn,
                     BehavioralSourceType type,
                     const std::string& expression,
                     ExpressionEvaluator evaluator,
                     std::vector<NodeId> controlling_nodes = {});

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

    // AC analysis: the source linearized at the operating point
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
    std::vector<NodeId> controlling_nodes_;  // Nodes the expression reads, ground left out

    // Helper: evaluate expression at current state
    Real evaluate_expression(Real time, const DenseVector& node_voltages) const;

    /// The value of the expression at the present voltages and its derivative
    /// with respect to each controlling node's voltage (central differences),
    /// for Newton's linearization. Every stamp takes the source from here.
    struct Linearization {
        Real value = 0.0;
        std::vector<std::pair<int, Real>> derivatives;  // (MNA column, d value / dV)
        Real norton = 0.0;  // value - sum of derivative * V
    };
    Linearization linearize(Real time, const DenseVector& node_voltages) const;

    /// The DC and transient stamp, at the given TIME
    void stamp_at(MNASystem& mna, const DenseVector& node_voltages, Real time) const;
};

} // namespace kirimaspice::devices
