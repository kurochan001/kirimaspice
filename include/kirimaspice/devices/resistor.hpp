// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/resistor.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// Resistor: Linear 2-terminal device
/// V = I * R
/// MNA: G * V = I (G = 1/R = conductance)
class Resistor : public Device {
public:
    Resistor(DeviceId id, const std::string& name,
             NodeId np, NodeId nn, Real resistance);

    ~Resistor() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: stamp conductance matrix
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Resistor is linear, no nonlinear contribution
    }

    // Transient analysis: same as DC (no dynamics)
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: stamp conductance (same as DC, frequency-independent)
    void stamp_ac(ComplexMNASystem& sys,
                 Real omega,
                 const DenseVector& dc_solution) const override;

    // Noise analysis: thermal noise
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                                Real frequency) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return false; }

    // Accessors
    Real resistance() const { return resistance_; }
    Real conductance() const { return conductance_; }
    NodeId np() const { return nodes_[0]; }
    NodeId nn() const { return nodes_[1]; }

    // Sensitivity analysis: Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

private:
    std::vector<NodeId> nodes_;  // [np, nn]
    Real resistance_;   // Resistance (Ω)
    Real conductance_;  // Conductance (S) = 1/R
};

} // namespace kirimaspice::devices
