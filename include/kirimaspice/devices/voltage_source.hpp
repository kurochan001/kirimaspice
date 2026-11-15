// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/voltage_source.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/waveform.hpp"

#include <memory>
#include <optional>

namespace kirimaspice::devices {

/// Independent Voltage Source: 2-terminal device
/// V = V_dc (independent)
/// MNA: Adds extra current variable
/// Equations:
///   V_np - V_nn = V_dc
///   I_source = ?  (determined by circuit)
class VoltageSource : public Device {
public:
    VoltageSource(DeviceId id, const std::string& name,
                  NodeId np, NodeId nn, Real dc_value,
                  Real ac_magnitude = 1.0, Real ac_phase = 0.0);

    ~VoltageSource() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: stamp voltage source
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Voltage source is linear
    }

    // Transient analysis: same as DC for independent source
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp voltage source constraint
    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }

    /// A transient waveform (PULSE, SIN, EXP, PWL, SFFM). As in SPICE, the
    /// transient and its operating point take the waveform at the time of
    /// the transient (Device::set_time); outside a transient the source has
    /// its DC value, or, when the netlist gives none (dc_given false), the
    /// waveform's value at time 0.
    void set_waveform(std::shared_ptr<const Waveform> waveform, bool dc_given) {
        waveform_ = std::move(waveform);
        dc_given_ = dc_given;
    }
    const Waveform* waveform() const { return waveform_.get(); }
    void set_time(std::optional<Real> time) override { time_ = time; }

    /// The value the next stamp takes, before source stepping
    Real present_value() const {
        if (waveform_ && time_) return waveform_->evaluate(*time_);
        if (waveform_ && !dc_given_) return waveform_->evaluate(0.0);
        return dc_value_;
    }

    // Accessors
    Real dc_value() const { return dc_value_; }
    Real ac_magnitude() const { return ac_magnitude_; }
    Real ac_phase() const { return ac_phase_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }
    BranchId branch_id() const { return branch_id_; }

    void set_branch_id(BranchId id) { branch_id_ = id; }

    // Source Stepping support
    void set_scale_factor(Real scale) { scale_factor_ = scale; }
    Real get_scale_factor() const { return scale_factor_; }

    // DC Sweep support
    void set_dc_value(Real value) {
        dc_value_ = value;
        dc_given_ = true;
    }

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    Real dc_value_;       // DC voltage value (V)
    Real ac_magnitude_;   // AC magnitude (V)
    Real ac_phase_;       // AC phase (degrees)
    BranchId branch_id_;  // Branch current variable index in MNA
    Real scale_factor_ = 1.0;  // Source stepping scale factor
    std::shared_ptr<const Waveform> waveform_;  // none for a DC source
    bool dc_given_ = true;
    std::optional<Real> time_;  // the transient's time, none outside one
};

} // namespace kirimaspice::devices
