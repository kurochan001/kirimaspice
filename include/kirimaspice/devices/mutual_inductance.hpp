// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/mutual_inductance.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include <string>

namespace kirimaspice::devices {

/// Mutual Inductance (K element): Couples two inductors
/// Netlist syntax: K<name> L<inductor1> L<inductor2> <coupling_coefficient>
/// Example: K1 L1 L2 0.9
///
/// Coupling equations:
///   V1 = L1 * dI1/dt + M * dI2/dt
///   V2 = M * dI1/dt + L2 * dI2/dt
///   where M = k * sqrt(L1 * L2), 0 <= k <= 1
///
/// MNA impact:
///   DC: No additional equations (inductors are short circuits)
///   AC: Adds jωM cross-coupling terms
///   Transient: Adds M * dI/dt cross-coupling terms
class MutualInductance : public Device {
public:
    /// Constructor
    /// \param id Device ID
    /// \param name Device name
    /// \param inductor1_name Name of first inductor
    /// \param inductor2_name Name of second inductor
    /// \param coupling_coefficient Coupling coefficient k (0 <= k <= 1)
    MutualInductance(DeviceId id, const std::string& name,
                     const std::string& inductor1_name,
                     const std::string& inductor2_name,
                     Real coupling_coefficient);

    ~MutualInductance() override = default;

    const std::vector<NodeId>& nodes() const override {
        // Mutual inductance doesn't directly connect nodes
        // It couples two existing inductors
        static std::vector<NodeId> empty_nodes;
        return empty_nodes;
    }

    // DC analysis: No additional stamps (inductors are short circuits)
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        // No additional DC stamps needed
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Linear device
    }

    // Transient analysis: Add M * dI/dt cross-coupling
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                       const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;
    void accept_solution(const DenseVector& node_voltages) override;

    // AC analysis: Add jωM cross-coupling
    void stamp_ac(ComplexMNASystem& sys,
                  Real omega,
                  const DenseVector& dc_solution) const override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return true; }

    // Accessors
    const std::string& inductor1_name() const { return inductor1_name_; }
    const std::string& inductor2_name() const { return inductor2_name_; }
    Real coupling_coefficient() const { return k_; }

    // Set inductor pointers (called after circuit construction)
    void set_inductors(const Device* L1, const Device* L2);

    // Get mutual inductance value M = k * sqrt(L1 * L2)
    Real mutual_inductance() const;

private:
    std::string inductor1_name_;  // Name of L1
    std::string inductor2_name_;  // Name of L2
    Real k_;                      // Coupling coefficient (0 <= k <= 1)

    // Pointers to coupled inductors (set after circuit construction)
    const Device* L1_;
    const Device* L2_;
};

} // namespace kirimaspice::devices
