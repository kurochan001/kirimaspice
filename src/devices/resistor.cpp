// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/devices/resistor.cpp
#include "kirimaspice/devices/resistor.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace kirimaspice::devices {

Resistor::Resistor(DeviceId id, const std::string& name,
                   NodeId np, NodeId nn, Real resistance)
    : Device(id, name, DeviceType::RESISTOR)
    , nodes_{np, nn}
    , resistance_(resistance)
    , conductance_(1.0 / resistance)
    , nominal_resistance_(resistance) {

    if (resistance <= 0.0) {
        throw std::runtime_error("Resistor resistance must be positive: " + name);
    }

    spdlog::debug("Created resistor {} (np={}, nn={}, R={}Ω, G={}S)",
                  name, np, nn, resistance_, conductance_);
}

void Resistor::stamp_dc_linear(MNASystem& mna,
                               const DenseVector& node_voltages) const {
    // MNA stamp for resistor:
    // G matrix contributions:
    //   G[np, np] += g
    //   G[np, nn] -= g
    //   G[nn, np] -= g
    //   G[nn, nn] += g
    // where g = 1/R = conductance

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    Real g = conductance_;

    // Convert node IDs to MNA indices (GND=0 → no entry, node_id → index = id-1)
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp conductance matrix
    if (np_idx >= 0) {
        mna.G.add_entry(np_idx, np_idx, g);
        if (nn_idx >= 0) {
            mna.G.add_entry(np_idx, nn_idx, -g);
        }
    }

    if (nn_idx >= 0) {
        mna.G.add_entry(nn_idx, nn_idx, g);
        if (np_idx >= 0) {
            mna.G.add_entry(nn_idx, np_idx, -g);
        }
    }

    // No RHS contribution for passive resistor
}

void Resistor::stamp_transient(MNASystem& mna,
                               const DenseVector& node_voltages,
                               Real timestep,
                               IntegrationMethod method,
                               const BDFCoefficients* bdf_coef,
                               const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Resistor is static - transient stamp same as DC (bdf_coef ignored)
    stamp_dc_linear(mna, node_voltages);
}

void Resistor::stamp_ac(ComplexMNASystem& sys,
                       Real omega,
                       const DenseVector& dc_solution) const {
    // AC analysis: Resistor is frequency-independent
    // Y_R = G (pure real, no imaginary part)

    NodeId np = nodes_[0];
    NodeId nn = nodes_[1];
    Real g = conductance_;

    // Complex conductance (real only, no frequency dependence)
    Complex Y_R(g, 0.0);

    // Convert node IDs to MNA indices
    auto np_idx = (np == NODE_GND) ? -1 : static_cast<int>(np - 1);
    auto nn_idx = (nn == NODE_GND) ? -1 : static_cast<int>(nn - 1);

    // Stamp admittance matrix (same pattern as DC)
    if (np_idx >= 0) {
        sys.Y.add_entry(np_idx, np_idx, Y_R);
        if (nn_idx >= 0) {
            sys.Y.add_entry(np_idx, nn_idx, -Y_R);
        }
    }

    if (nn_idx >= 0) {
        sys.Y.add_entry(nn_idx, nn_idx, Y_R);
        if (np_idx >= 0) {
            sys.Y.add_entry(nn_idx, np_idx, -Y_R);
        }
    }

    spdlog::trace("Resistor {} AC stamp: Y_R = {:.3e} S (frequency-independent)",
                  name_, g);
}

std::vector<NoiseSource> Resistor::get_noise_sources(const DenseVector& dc_solution,
                                                       Real frequency) const {
    // Thermal noise for adjoint noise analysis
    //
    // The adjoint method computes transfer functions from current sources to output.
    // For consistency, all noise sources are converted to equivalent Norton current sources.
    //
    // Physical model:
    // - Voltage noise: V_n² = 4kTR (V²/Hz) - Johnson-Nyquist thermal noise
    // - Equivalent current noise: I_n² = V_n²/R² = 4kT/R = 4kTG (A²/Hz)
    //   where G = 1/R is the conductance
    //
    // Physical constants:
    // - k_B = 1.380649e-23 J/K (Boltzmann constant)
    // - T = 300.15 K (27°C, typical room temperature)
    //
    // The noise current source is placed in parallel with the resistor (Norton equivalent).

    (void)dc_solution;  // Thermal noise is independent of DC operating point
    (void)frequency;    // White noise - frequency-independent

    constexpr Real k_B = 1.380649e-23;  // Boltzmann constant (J/K)
    constexpr Real T = 300.15;          // Temperature (K) - 27°C

    // Calculate equivalent current spectral density: I_n² = 4kT/R = 4kTG (A²/Hz)
    Real G = 1.0 / resistance_;  // Conductance
    Real I_n_squared = 4.0 * k_B * T * G;

    NoiseSource thermal;
    thermal.name = name_ + "_thermal";
    thermal.node_p = nodes_[0];  // Positive terminal
    thermal.node_n = nodes_[1];  // Negative terminal
    thermal.spectral_density = I_n_squared;
    thermal.is_current = true;  // Current noise source (Norton equivalent)

    spdlog::trace("Resistor {} thermal noise: I_n² = {:.3e} A²/Hz (R = {:.3e} Ω, G = {:.3e} S)",
                  name_, I_n_squared, resistance_, G);

    return {thermal};
}

// ========== Sensitivity Analysis: Parameter Access ==========

void Resistor::set_temperature_coefficients(Real tc1, Real tc2) {
    tc1_ = tc1;
    tc2_ = tc2;
    apply_temperature();
}

void Resistor::set_temperature(Real temp_K) {
    temperature_ = temp_K;
    apply_temperature();
}

void Resistor::apply_temperature() {
    const Real dt = temperature_ - tnom_;
    const Real factor = 1.0 + tc1_ * dt + tc2_ * dt * dt;
    if (factor <= 0.0) {
        spdlog::warn("Resistor {}: TC1 and TC2 make the resistance non-positive at {:.2f} C; "
                     "the value at TNOM is used",
                     name_, temperature_ - 273.15);
        resistance_ = nominal_resistance_;
    } else {
        resistance_ = nominal_resistance_ * factor;
    }
    conductance_ = 1.0 / resistance_;
}

std::optional<Real> Resistor::get_parameter(const std::string& param_name) const {
    if (param_name == "resistance" || param_name == "R") {
        return nominal_resistance_;
    }
    if (param_name == "tc1") return tc1_;
    if (param_name == "tc2") return tc2_;
    return std::nullopt;  // Parameter not found
}

bool Resistor::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "resistance" || param_name == "R") {
        if (value <= 0.0) {
            spdlog::warn("Resistor {}: resistance must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        nominal_resistance_ = value;
        apply_temperature();
        spdlog::trace("Resistor {}: set resistance = {} Ω, conductance = {} S",
                     name_, resistance_, conductance_);
        return true;
    }
    if (param_name == "tc1" || param_name == "tc2") {
        (param_name == "tc1" ? tc1_ : tc2_) = value;
        apply_temperature();
        return true;
    }
    return false;  // Parameter not found
}

std::vector<std::string> Resistor::get_perturbable_parameters() const {
    return {"resistance"};
}

} // namespace kirimaspice::devices
