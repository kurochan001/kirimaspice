// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/behavioral_source.cpp
#include "kirimaspice/devices/behavioral_source.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace kirimaspice::devices {

BehavioralSource::BehavioralSource(
    DeviceId id, const std::string& name,
    NodeId np, NodeId nn,
    BehavioralSourceType type,
    const std::string& expression,
    ExpressionEvaluator evaluator,
    std::vector<NodeId> controlling_nodes
)
    : Device(id, name, DeviceType::BEHAVIORAL_SOURCE)
    , nodes_({np, nn})
    , type_(type)
    , expression_(expression)
    , evaluator_(std::move(evaluator))
    , branch_id_(INVALID_BRANCH)
    , current_time_(0.0)
{
    for (const NodeId node : controlling_nodes) {
        if (node != NODE_GND &&
            std::find(controlling_nodes_.begin(), controlling_nodes_.end(), node) ==
                controlling_nodes_.end()) {
            controlling_nodes_.push_back(node);
        }
    }
    spdlog::debug("BehavioralSource '{}': type={}, expr='{}'",
                 name, (type == BehavioralSourceType::VOLTAGE ? "V" : "I"), expression);
}

Real BehavioralSource::evaluate_expression(Real time, const DenseVector& node_voltages) const {
    // Check if evaluator is valid before calling
    if (!evaluator_) {
        spdlog::error("BehavioralSource '{}': evaluator is not set", name());
        return 0.0;  // Fallback to zero
    }
    try {
        return evaluator_(time, node_voltages);
    } catch (const std::exception& e) {
        spdlog::error("BehavioralSource '{}': expression evaluation failed: {}", name(), e.what());
        return 0.0;  // Fallback to zero
    }
}

BehavioralSource::Linearization BehavioralSource::linearize(
    Real time, const DenseVector& node_voltages) const {
    Linearization lin;
    lin.value = evaluate_expression(time, node_voltages);
    lin.norton = lin.value;
    if (controlling_nodes_.empty()) {
        return lin;
    }
    DenseVector x = node_voltages;
    for (const NodeId node : controlling_nodes_) {
        const auto k = static_cast<std::size_t>(node - 1);
        if (k >= x.size()) {
            continue;
        }
        const Real v = x[k];
        const Real h = 1e-6 * std::max(Real(1.0), std::abs(v));
        x[k] = v + h;
        const Real up = evaluate_expression(time, x);
        x[k] = v - h;
        const Real down = evaluate_expression(time, x);
        x[k] = v;
        const Real derivative = (up - down) / (2.0 * h);
        if (derivative != 0.0 && std::isfinite(derivative)) {
            lin.derivatives.emplace_back(static_cast<int>(k), derivative);
            lin.norton -= derivative * v;
        }
    }
    return lin;
}

void BehavioralSource::stamp_dc_linear(
    MNASystem& mna,
    const DenseVector& node_voltages
) const {
    (void)mna;
    (void)node_voltages;
    // Behavioral sources are inherently nonlinear (expression-based)
    // DC linear stamp is empty; use nonlinear stamp instead
}

void BehavioralSource::stamp_at(MNASystem& mna, const DenseVector& node_voltages,
                                Real time) const {
    // Newton's linearization of the source, value ~ norton + sum d * V
    const Linearization lin = linearize(time, node_voltages);
    const int np_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0] - 1);
    const int nn_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1] - 1);

    if (type_ == BehavioralSourceType::VOLTAGE) {
        // V(np) - V(nn) - sum d * V = norton, with the branch current from np
        // through the source to nn
        if (branch_id_ == INVALID_BRANCH) {
            spdlog::error("BehavioralSource '{}' (V-mode): branch_id not set", name());
            return;
        }
        const auto br = static_cast<int>(mna.num_nodes + branch_id_);
        if (np_idx >= 0) {
            mna.G.add_entry(np_idx, br, 1.0);
            mna.G.add_entry(br, np_idx, 1.0);
        }
        if (nn_idx >= 0) {
            mna.G.add_entry(nn_idx, br, -1.0);
            mna.G.add_entry(br, nn_idx, -1.0);
        }
        for (const auto& [col, d] : lin.derivatives) {
            mna.G.add_entry(br, col, -d);
        }
        mna.b[static_cast<std::size_t>(br)] += lin.norton;
    } else {
        // The current value flows from np through the source to nn
        for (const auto& [col, d] : lin.derivatives) {
            if (np_idx >= 0) mna.G.add_entry(np_idx, col, d);
            if (nn_idx >= 0) mna.G.add_entry(nn_idx, col, -d);
        }
        if (np_idx >= 0) mna.b[static_cast<std::size_t>(np_idx)] -= lin.norton;
        if (nn_idx >= 0) mna.b[static_cast<std::size_t>(nn_idx)] += lin.norton;
    }
    spdlog::trace("BehavioralSource '{}': stamp at TIME={}, value={}, {} derivatives",
                  name(), time, lin.value, lin.derivatives.size());
}

void BehavioralSource::stamp_dc_nonlinear(
    MNASystem& mna,
    const DenseVector& node_voltages
) const {
    // TIME is 0 in the operating point, also in the one that starts a transient
    stamp_at(mna, node_voltages, 0.0);
}

void BehavioralSource::stamp_transient(
    MNASystem& mna,
    const DenseVector& node_voltages,
    Real timestep,
    IntegrationMethod method,
    const BDFCoefficients* bdf_coef,
    const TR_BDF2_Coefficients* tr_bdf2_coef
) const {
    (void)timestep; (void)method; (void)bdf_coef; (void)tr_bdf2_coef;
    stamp_at(mna, node_voltages, current_time_);
}

void BehavioralSource::stamp_ac(
    ComplexMNASystem& sys,
    Real omega,
    const DenseVector& dc_solution
) const {
    (void)omega;
    // The small-signal source: the derivatives of the expression at the
    // operating point (TIME 0), with no AC input of its own
    const Linearization lin = linearize(0.0, dc_solution);
    const int np_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0] - 1);
    const int nn_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1] - 1);

    if (type_ == BehavioralSourceType::VOLTAGE) {
        if (branch_id_ == INVALID_BRANCH) {
            spdlog::error("BehavioralSource '{}' (V-mode): branch_id not set", name());
            return;
        }
        const auto br = static_cast<int>(sys.num_nodes + branch_id_);
        if (np_idx >= 0) {
            sys.Y.add_entry(np_idx, br, Complex(1.0, 0.0));
            sys.Y.add_entry(br, np_idx, Complex(1.0, 0.0));
        }
        if (nn_idx >= 0) {
            sys.Y.add_entry(nn_idx, br, Complex(-1.0, 0.0));
            sys.Y.add_entry(br, nn_idx, Complex(-1.0, 0.0));
        }
        for (const auto& [col, d] : lin.derivatives) {
            sys.Y.add_entry(br, col, Complex(-d, 0.0));
        }
    } else {
        for (const auto& [col, d] : lin.derivatives) {
            if (np_idx >= 0) sys.Y.add_entry(np_idx, col, Complex(d, 0.0));
            if (nn_idx >= 0) sys.Y.add_entry(nn_idx, col, Complex(-d, 0.0));
        }
    }
}

} // namespace kirimaspice::devices
