// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/pole_zero_solver.cpp
#include "kirimaspice/solvers/pole_zero_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/voltage_source.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <numbers>

namespace kirimaspice {

PoleZeroResult PoleZeroSolver::analyze(const circuit::CircuitGraph& circuit,
                                       const SimulationConfig& config) {
    PoleZeroResult result;

    if (!config.pz.enabled) {
        spdlog::error("Pole-Zero analysis not enabled");
        return result;
    }

    // Build transfer function description
    result.transfer_function = fmt::format("V({}) - V({}) / V({})",
                                          config.pz.node1,
                                          config.pz.node2,
                                          config.pz.input_source);

    spdlog::info("Starting Pole-Zero analysis: {}", result.transfer_function);

    // Compute DC gain
    result.dc_gain = compute_dc_gain(circuit, config, config.pz.node1, config.pz.node2,
                                     config.pz.input_source);

    spdlog::info("DC gain: {}", result.dc_gain);

    // Compute poles if requested
    if (config.pz.type == SimulationConfig::PZ::AnalysisType::POLES_ONLY ||
        config.pz.type == SimulationConfig::PZ::AnalysisType::POLES_ZEROS) {
        result.poles = compute_poles(circuit, config.pz.node1, config.pz.node2,
                                    config.pz.input_source);
        spdlog::info("Found {} poles", result.poles.size());
    }

    // Compute zeros if requested
    if (config.pz.type == SimulationConfig::PZ::AnalysisType::ZEROS_ONLY ||
        config.pz.type == SimulationConfig::PZ::AnalysisType::POLES_ZEROS) {
        result.zeros = compute_zeros(circuit, config.pz.node1, config.pz.node2,
                                    config.pz.input_source);
        spdlog::info("Found {} zeros", result.zeros.size());
    }

    result.success = true;
    return result;
}

void PoleZeroSolver::print_result(const PoleZeroResult& result) {
    if (!result.success) {
        fmt::print("\nPole-Zero analysis FAILED\n");
        return;
    }

    fmt::print("\n");
    fmt::print("Pole-Zero Analysis Results\n");
    fmt::print("===========================\n");
    fmt::print("Transfer Function: {}\n", result.transfer_function);
    fmt::print("DC Gain: {:.6e}\n", result.dc_gain);
    fmt::print("\n");

    // Print poles
    if (!result.poles.empty()) {
        fmt::print("Poles (s-plane):\n");
        fmt::print("{:>5} {:>15} {:>15} {:>15} {:>12}\n",
                   "No.", "Real (σ)", "Imag (jω)", "Frequency (Hz)", "Damping (ζ)");
        fmt::print("{:->5} {:->15} {:->15} {:->15} {:->12}\n",
                   "", "", "", "", "");

        for (size_t i = 0; i < result.poles.size(); ++i) {
            const auto& pole = result.poles[i];
            fmt::print("{:5d} {:15.6e} {:15.6e} {:15.6e} {:12.6f}\n",
                      i + 1,
                      pole.value.real(),
                      pole.value.imag(),
                      pole.frequency_hz,
                      pole.damping_ratio);
        }
        fmt::print("\n");
    }

    // Print zeros
    if (!result.zeros.empty()) {
        fmt::print("Zeros (s-plane):\n");
        fmt::print("{:>5} {:>15} {:>15} {:>15} {:>12}\n",
                   "No.", "Real (σ)", "Imag (jω)", "Frequency (Hz)", "Damping (ζ)");
        fmt::print("{:->5} {:->15} {:->15} {:->15} {:->12}\n",
                   "", "", "", "", "");

        for (size_t i = 0; i < result.zeros.size(); ++i) {
            const auto& zero = result.zeros[i];
            fmt::print("{:5d} {:15.6e} {:15.6e} {:15.6e} {:12.6f}\n",
                      i + 1,
                      zero.value.real(),
                      zero.value.imag(),
                      zero.frequency_hz,
                      zero.damping_ratio);
        }
        fmt::print("\n");
    }
}

std::vector<ComplexPoleZero> PoleZeroSolver::compute_poles(
    const circuit::CircuitGraph& circuit,
    const std::string& node1,
    const std::string& node2,
    const std::string& input_source) {

    std::vector<ComplexPoleZero> poles;

    // Initial Implementation:
    // For simple RC/RL circuits, compute poles analytically
    // For complex circuits, use AC analysis to estimate poles numerically

    // Count circuit components
    std::uint32_t num_resistors = 0;
    std::uint32_t num_capacitors = 0;
    std::uint32_t num_inductors = 0;
    Real total_resistance = 0.0;
    Real total_capacitance = 0.0;
    Real total_inductance = 0.0;

    for (const auto& device : circuit.get_devices()) {
        if (device->type() == DeviceType::RESISTOR) {
            num_resistors++;
            // Simplified: sum resistances (assumes series connection)
            // In reality, we'd need circuit topology analysis
        } else if (device->type() == DeviceType::CAPACITOR) {
            num_capacitors++;
        } else if (device->type() == DeviceType::INDUCTOR) {
            num_inductors++;
        }
    }

    // For 1st-order RC circuit: pole at s = -1/(RC)
    // For 1st-order RL circuit: pole at s = -R/L
    // This is a simplified implementation for demonstration

    spdlog::warn("Pole-Zero analysis: Analytical pole computation not yet fully implemented");
    spdlog::warn("Using placeholder pole calculation for simple circuits");

    // Placeholder: Add a single real pole for RC circuits
    if (num_capacitors == 1 && num_inductors == 0) {
        // Assume simple RC: pole at -1/(RC) rad/s
        // For now, use placeholder value
        ComplexPoleZero pole;
        pole.value = std::complex<Real>(-1000.0, 0.0);  // -1000 rad/s (placeholder)
        pole.is_real = true;
        calculate_frequency_damping(pole);
        poles.push_back(pole);
    }

    return poles;
}

std::vector<ComplexPoleZero> PoleZeroSolver::compute_zeros(
    const circuit::CircuitGraph& circuit,
    const std::string& node1,
    const std::string& node2,
    const std::string& input_source) {

    std::vector<ComplexPoleZero> zeros;

    // Initial Implementation:
    // Zeros are typically at s=0 for simple lowpass/highpass filters
    // Full implementation would use AC analysis to find numerator roots

    spdlog::warn("Pole-Zero analysis: Analytical zero computation not yet fully implemented");

    return zeros;
}

Real PoleZeroSolver::compute_dc_gain(
    const circuit::CircuitGraph& circuit,
    const SimulationConfig& config,
    const std::string& node1,
    const std::string& node2,
    const std::string& input_source) {

    // Use DC solver with caller's config (inherits gmin, temperature, solver options)
    solvers::DCSolver dc_solver(circuit, config);

    DenseVector solution(circuit.num_nodes() - 1 + circuit.get_devices().size());
    SolverStats stats;

    auto status = dc_solver.solve(solution, stats);
    if (status != ConvergenceStatus::SUCCESS) {
        spdlog::error("DC analysis failed for pole-zero gain calculation");
        return 0.0;
    }

    // Get output voltage V(node1) - V(node2)
    try {
        Real v_out1 = 0.0;
        Real v_out2 = 0.0;

        if (node1 != "0" && node1 != "GND") {
            std::uint32_t node_id1 = circuit.get_node_id(node1);
            v_out1 = solution[node_id1 - 1];
        }

        if (node2 != "0" && node2 != "GND") {
            std::uint32_t node_id2 = circuit.get_node_id(node2);
            v_out2 = solution[node_id2 - 1];
        }

        Real v_out = v_out1 - v_out2;

        // Get actual input source DC voltage from circuit
        Real v_in = 1.0;  // Default: 1V normalization (SPICE standard)
        for (const auto& device : circuit.get_devices()) {
            if (device->name() == input_source) {
                if (auto vsrc = std::dynamic_pointer_cast<devices::VoltageSource>(device)) {
                    Real dc_value = vsrc->dc_value();
                    if (std::abs(dc_value) > 1e-12) {
                        v_in = dc_value;
                        spdlog::debug("PoleZero: Using input source DC value: {} V", v_in);
                    } else {
                        // DC value is 0 (common in AC analysis)
                        // Use 1V normalization (SPICE .PZ standard assumption)
                        spdlog::info("PoleZero: Input source DC=0, using 1V normalization");
                    }
                }
                break;
            }
        }

        return v_out / v_in;

    } catch (const std::exception& e) {
        spdlog::error("Error computing DC gain: {}", e.what());
        return 0.0;
    }
}

void PoleZeroSolver::calculate_frequency_damping(ComplexPoleZero& pz) {
    Real sigma = pz.value.real();
    Real omega = pz.value.imag();

    // Natural frequency: omega_n = |s| / (2*pi) [Hz]
    Real omega_n_rad = std::sqrt(sigma * sigma + omega * omega);
    pz.frequency_hz = omega_n_rad / (2.0 * std::numbers::pi);

    // Damping ratio: zeta = -sigma / omega_n
    if (omega_n_rad > 0.0) {
        pz.damping_ratio = -sigma / omega_n_rad;
    } else {
        pz.damping_ratio = 0.0;
    }

    // Check if real pole (no imaginary part)
    pz.is_real = (std::abs(omega) < 1e-10);
}

} // namespace kirimaspice
