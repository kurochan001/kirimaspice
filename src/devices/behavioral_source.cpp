// src/devices/behavioral_source.cpp
#include "kirimaspice/devices/behavioral_source.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

BehavioralSource::BehavioralSource(
    DeviceId id, const std::string& name,
    NodeId np, NodeId nn,
    BehavioralSourceType type,
    const std::string& expression,
    ExpressionEvaluator evaluator
)
    : Device(id, name, DeviceType::BEHAVIORAL_SOURCE)
    , nodes_({np, nn})
    , type_(type)
    , expression_(expression)
    , evaluator_(std::move(evaluator))
    , branch_id_(INVALID_BRANCH)
    , current_time_(0.0)
{
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

void BehavioralSource::stamp_dc_linear(
    MNASystem& mna,
    const DenseVector& node_voltages
) const {
    // Behavioral sources are inherently nonlinear (expression-based)
    // DC linear stamp is empty; use nonlinear stamp instead
}

void BehavioralSource::stamp_dc_nonlinear(
    MNASystem& mna,
    const DenseVector& node_voltages
) const {
    // Evaluate expression at DC (TIME = 0)
    Real value = evaluate_expression(0.0, node_voltages);

    if (type_ == BehavioralSourceType::VOLTAGE) {
        // Voltage source: V(np) - V(nn) = value
        // Extended MNA system with branch variable
        // [ G  C ] [ V ] = [ I ]
        // [ D  0 ] [ J ]   [ E ]
        //
        // C[np, br] = -1 (current out of np)
        // C[nn, br] = +1 (current into nn)
        // D[br, np] = +1 (voltage equation)
        // D[br, nn] = -1 (voltage equation)
        // E[br] = value

        if (branch_id_ == INVALID_BRANCH) {
            spdlog::error("BehavioralSource '{}' (V-mode): branch_id not set", name());
            return;
        }

        NodeId np_node = nodes_[0];
        NodeId nn_node = nodes_[1];
        std::size_t n = mna.num_nodes;
        std::size_t br = n + branch_id_;  // Branch variable index in extended system

        // Convert node IDs to MNA indices (0-indexed, -1 for GND)
        auto np_idx = (np_node == NODE_GND) ? -1 : static_cast<int>(np_node - 1);
        auto nn_idx = (nn_node == NODE_GND) ? -1 : static_cast<int>(nn_node - 1);

        // C matrix: branch current flow
        if (np_idx >= 0) {
            mna.G.add_entry(np_idx, br, -1.0);
        }
        if (nn_idx >= 0) {
            mna.G.add_entry(nn_idx, br, 1.0);
        }

        // D matrix: voltage constraint
        if (np_idx >= 0) {
            mna.G.add_entry(br, np_idx, 1.0);
        }
        if (nn_idx >= 0) {
            mna.G.add_entry(br, nn_idx, -1.0);
        }

        // E vector (RHS for branch equation)
        mna.b[br] += value;

        spdlog::trace("BehavioralSource '{}' (V-mode): DC stamp, value={}", name(), value);

    } else {
        // Current source: inject current into nodes
        // b[np] -= value  (current flows out of np)
        // b[nn] += value  (current flows into nn)

        NodeId np_node = nodes_[0];
        NodeId nn_node = nodes_[1];

        // Convert node IDs to MNA indices
        auto np_idx = (np_node == NODE_GND) ? -1 : static_cast<int>(np_node - 1);
        auto nn_idx = (nn_node == NODE_GND) ? -1 : static_cast<int>(nn_node - 1);

        if (np_idx >= 0) {
            mna.b[np_idx] -= value;
        }
        if (nn_idx >= 0) {
            mna.b[nn_idx] += value;
        }

        spdlog::trace("BehavioralSource '{}' (I-mode): DC stamp, value={}", name(), value);
    }
}

void BehavioralSource::stamp_transient(
    MNASystem& mna,
    const DenseVector& node_voltages,
    Real timestep,
    IntegrationMethod method,
    const BDFCoefficients* bdf_coef,
    const TR_BDF2_Coefficients* tr_bdf2_coef
) const {
    // Evaluate expression at current time
    Real value = evaluate_expression(current_time_, node_voltages);

    if (type_ == BehavioralSourceType::VOLTAGE) {
        // Voltage source stamp (same structure as DC)
        if (branch_id_ == INVALID_BRANCH) {
            spdlog::error("BehavioralSource '{}' (V-mode): branch_id not set", name());
            return;
        }

        NodeId np_node = nodes_[0];
        NodeId nn_node = nodes_[1];
        std::size_t n = mna.num_nodes;
        std::size_t br = n + branch_id_;

        auto np_idx = (np_node == NODE_GND) ? -1 : static_cast<int>(np_node - 1);
        auto nn_idx = (nn_node == NODE_GND) ? -1 : static_cast<int>(nn_node - 1);

        // C matrix
        if (np_idx >= 0) {
            mna.G.add_entry(np_idx, br, -1.0);
        }
        if (nn_idx >= 0) {
            mna.G.add_entry(nn_idx, br, 1.0);
        }

        // D matrix
        if (np_idx >= 0) {
            mna.G.add_entry(br, np_idx, 1.0);
        }
        if (nn_idx >= 0) {
            mna.G.add_entry(br, nn_idx, -1.0);
        }

        // E vector
        mna.b[br] += value;

        spdlog::trace("BehavioralSource '{}' (V-mode): Transient stamp, time={}, value={}",
                     name(), current_time_, value);

    } else {
        // Current source stamp (same as DC)
        NodeId np_node = nodes_[0];
        NodeId nn_node = nodes_[1];

        auto np_idx = (np_node == NODE_GND) ? -1 : static_cast<int>(np_node - 1);
        auto nn_idx = (nn_node == NODE_GND) ? -1 : static_cast<int>(nn_node - 1);

        if (np_idx >= 0) {
            mna.b[np_idx] -= value;
        }
        if (nn_idx >= 0) {
            mna.b[nn_idx] += value;
        }

        spdlog::trace("BehavioralSource '{}' (I-mode): Transient stamp, time={}, value={}",
                     name(), current_time_, value);
    }
}

void BehavioralSource::stamp_ac(
    ComplexMNASystem& sys,
    Real omega,
    const DenseVector& dc_solution
) const {
    // AC analysis: small-signal linearization
    // For now, not supported (requires computing dV/dV or dI/dV Jacobian)
    spdlog::warn("BehavioralSource '{}': AC analysis not yet supported", name());
}

} // namespace kirimaspice::devices
