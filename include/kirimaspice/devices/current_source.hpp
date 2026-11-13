// include/kirimaspice/devices/current_source.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

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
    Real dc_value() const { return dc_value_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }

    // Source Stepping support
    void set_scale_factor(Real scale) { scale_factor_ = scale; }
    Real get_scale_factor() const { return scale_factor_; }

    Real ac_magnitude() const { return ac_magnitude_; }
    Real ac_phase() const { return ac_phase_deg_; }

    // DC Sweep support
    void set_dc_value(Real value) { dc_value_ = value; }

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    Real dc_value_;   // DC current value (A)
    Real scale_factor_ = 1.0;  // Source stepping scale factor
    Real ac_magnitude_ = 0.0;   // AC magnitude (A)
    Real ac_phase_deg_ = 0.0;   // AC phase [deg]
};

} // namespace kirimaspice::devices
