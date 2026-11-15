// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/current_source.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/waveform.hpp"

#include <memory>
#include <optional>

namespace kirimaspice::devices {

/// Independent Current Source: 2-terminal device
/// I = I_dc (independent)
/// MNA: Direct RHS contribution, no extra variables needed
class CurrentSource : public Device {
public:
    CurrentSource(DeviceId id, const std::string& name,
                  NodeId np, NodeId nn, Real dc_value,
                  Real ac_magnitude = 0.0, Real ac_phase = 0.0);

    ~CurrentSource() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: stamp current source
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Current source is linear
    }

    // Transient analysis: same as DC for independent source
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: inject complex current source
    void stamp_ac(ComplexMNASystem& sys,
                 Real omega,
                 const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }

    // Accessors
    /// A transient waveform, as for VoltageSource::set_waveform
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

    Real dc_value() const { return dc_value_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }

    // Source Stepping support
    void set_scale_factor(Real scale) { scale_factor_ = scale; }
    Real get_scale_factor() const { return scale_factor_; }

    Real ac_magnitude() const { return ac_magnitude_; }
    Real ac_phase() const { return ac_phase_deg_; }

    // DC Sweep support
    void set_dc_value(Real value) {
        dc_value_ = value;
        dc_given_ = true;
    }

    /// The DC value and whether the netlist gave one, to put back exactly
    /// after a sweep has set the value (DCSolver::solve_sweep)
    struct DcSetting {
        Real value = 0.0;
        bool given = true;
    };
    DcSetting dc_setting() const { return {dc_value_, dc_given_}; }
    void restore_dc_setting(const DcSetting& setting) {
        dc_value_ = setting.value;
        dc_given_ = setting.given;
    }

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    Real dc_value_;   // DC current value (A)
    Real scale_factor_ = 1.0;  // Source stepping scale factor
    Real ac_magnitude_ = 0.0;   // AC magnitude (A)
    Real ac_phase_deg_ = 0.0;   // AC phase [deg]
    std::shared_ptr<const Waveform> waveform_;  // none for a DC source
    bool dc_given_ = true;
    std::optional<Real> time_;  // the transient's time, none outside one
};

} // namespace kirimaspice::devices
