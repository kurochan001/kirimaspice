// include/kirimaspice/devices/time_varying_voltage_source.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/waveform.hpp"
#include <memory>

namespace kirimaspice::devices {

/// Time-Varying Voltage Source: 2-terminal device with time-dependent voltage
/// V(t) = waveform->evaluate(t)
/// MNA: Adds extra current variable (same as VoltageSource)
/// Equations:
///   V_np - V_nn = V(t)
///   I_source = ?  (determined by circuit)
class TimeVaryingVoltageSource : public Device {
public:
    TimeVaryingVoltageSource(DeviceId id, const std::string& name,
                             NodeId np, NodeId nn,
                             std::unique_ptr<Waveform> waveform);

    ~TimeVaryingVoltageSource() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: use DC value from waveform
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Voltage source is linear
    }

    // Transient analysis: evaluate waveform at current time
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    /// Set current simulation time for transient analysis
    void set_time(Real time) { current_time_ = time; }

    /// Get current simulation time
    Real get_time() const { return current_time_; }

    /// Evaluate waveform at given time
    Real value_at(Real time) const { return waveform_->evaluate(time); }

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }  // Not a dynamic element (no history)

    // Accessors
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    BranchId branch_id() const { return branch_id_; }

    void set_branch_id(BranchId id) { branch_id_ = id; }

    const Waveform& waveform() const { return *waveform_; }

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    std::unique_ptr<Waveform> waveform_;  // Time-varying waveform
    BranchId branch_id_;  // Branch current variable index in MNA
    mutable Real current_time_;  // Current simulation time (for stamp methods)
};

} // namespace kirimaspice::devices
