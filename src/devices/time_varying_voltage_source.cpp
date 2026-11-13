// src/devices/time_varying_voltage_source.cpp
#include "kirimaspice/devices/time_varying_voltage_source.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::devices {

TimeVaryingVoltageSource::TimeVaryingVoltageSource(
    DeviceId id, const std::string& name,
    NodeId np, NodeId nn,
    std::unique_ptr<Waveform> waveform)
    : Device(id, name, DeviceType::VSOURCE)
    , nodes_{np, nn}
    , waveform_(std::move(waveform))
    , branch_id_(INVALID_BRANCH)
    , current_time_(0.0) {

    if (!waveform_) {
        throw std::invalid_argument("TimeVaryingVoltageSource: waveform cannot be null");
    }

    spdlog::debug("Created time-varying voltage source {} (np={}, nn={}, DC={}V)",
                  name, np, nn, waveform_->dc_value());
}

void TimeVaryingVoltageSource::stamp_dc_linear(MNASystem& mna,
                                                const DenseVector& node_voltages) const {
    // DC analysis: use DC value from waveform
    // Same MNA stamp as constant voltage source, but with DC value

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = mna.num_nodes;
    std::size_t br = n + branch_id_;  // Branch variable index in extended system

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp C matrix (top-right block)
    // Branch current flows from nn to np (out of positive terminal)
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, br, -1.0);
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, br, 1.0);
    }

    // Stamp D matrix (bottom-left block)
    // Branch equation: V(np) - V(nn) = V_source
    if (np_idx >= 0) {
        mna.G.add_entry(br, np_idx, 1.0);
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(br, nn_idx, -1.0);
    }

    // Stamp E vector (RHS for branch equations) with DC value
    mna.b[br] = waveform_->dc_value();
}

void TimeVaryingVoltageSource::stamp_transient(MNASystem& mna,
                                                const DenseVector& node_voltages,
                                                Real timestep,
                                                IntegrationMethod method,
                                                const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Transient analysis: evaluate waveform at current time
    // Same MNA stamp structure, but with time-dependent value

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    std::size_t n = mna.num_nodes;
    std::size_t br = n + branch_id_;  // Branch variable index in extended system

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp C matrix (top-right block)
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, br, -1.0);
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, br, 1.0);
    }

    // Stamp D matrix (bottom-left block)
    if (np_idx >= 0) {
        mna.G.add_entry(br, np_idx, 1.0);
    }
    if (nn_idx >= 0) {
        mna.G.add_entry(br, nn_idx, -1.0);
    }

    // Stamp E vector with time-dependent value
    Real voltage = waveform_->evaluate(current_time_);
    mna.b[br] = voltage;

    spdlog::trace("TimeVaryingVoltageSource {}: t={:.6e}s, V={:.6e}V (br={})",
                   name(), current_time_, voltage, br);
}

} // namespace kirimaspice::devices
