// src/solvers/transfer_function_solver.cpp
#include "kirimaspice/solvers/transfer_function_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/devices/resistor.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace kirimaspice {

TransferFunctionSolver::TransferFunctionSolver(const circuit::CircuitGraph& circuit,
                                               const SimulationConfig& config)
    : circuit_(circuit), config_(config) {}

TransferFunctionResult TransferFunctionSolver::analyze() {
    TransferFunctionResult result;
    result.output_signal = config_.tf.output_signal;
    result.input_source = config_.tf.input_source;

    spdlog::info("Starting transfer function analysis");
    spdlog::info("  Output: {}", config_.tf.output_signal);
    spdlog::info("  Input: {}", config_.tf.input_source);

    // 1. Extract node and device IDs from signal specifications
    NodeId output_node;
    DeviceId input_device;

    if (!extract_node_from_signal(config_.tf.output_signal, output_node)) {
        spdlog::error("Failed to extract output node from signal: {}", config_.tf.output_signal);
        return result;
    }

    if (!extract_source_device_id(config_.tf.input_source, input_device)) {
        spdlog::error("Failed to find input source: {}", config_.tf.input_source);
        return result;
    }

    // 2. Compute DC operating point
    // Create DC solver (needed for branch current extraction)
    solvers::DCSolver dc_solver(circuit_, config_);

    std::vector<Real> solution;
    if (!compute_dc_operating_point(dc_solver, solution)) {
        spdlog::error("Failed to compute DC operating point");
        return result;
    }

    spdlog::info("DC operating point computed successfully");
    spdlog::info("  Output node voltage: {} V", solution[output_node - 1]);

    // 3. Compute transfer characteristics
    result.transfer_gain = compute_transfer_gain(solution, output_node, input_device);
    result.input_resistance = compute_input_resistance(solution, input_device, dc_solver);
    result.output_resistance = compute_output_resistance(solution, output_node);

    result.success = true;
    return result;
}

void TransferFunctionSolver::print_result(const TransferFunctionResult& result) {
    if (!result.success) {
        fmt::print("\nTransfer function analysis FAILED\n");
        return;
    }

    fmt::print("\n");
    fmt::print("Transfer Function Analysis Results\n");
    fmt::print("===================================\n");
    fmt::print("Output: {}\n", result.output_signal);
    fmt::print("Input:  {}\n", result.input_source);
    fmt::print("\n");
    fmt::print("Transfer Gain:     {:.6e}\n", result.transfer_gain);
    fmt::print("Input Resistance:  {:.6e} Ω\n", result.input_resistance);
    fmt::print("Output Resistance: {:.6e} Ω\n", result.output_resistance);
    fmt::print("\n");
}

bool TransferFunctionSolver::compute_dc_operating_point(solvers::DCSolver& dc_solver,
                                                         std::vector<Real>& solution) {
    // Prepare solution vector
    solution.resize(circuit_.num_nodes());
    std::fill(solution.begin(), solution.end(), 0.0);

    // Solve DC operating point
    SolverStats stats;
    ConvergenceStatus status = dc_solver.solve(solution, stats);

    if (status != ConvergenceStatus::SUCCESS) {
        spdlog::error("DC solver failed with status: {}", static_cast<int>(status));
        return false;
    }

    spdlog::info("DC convergence: {} iterations", stats.total_iterations);
    return true;
}

bool TransferFunctionSolver::extract_node_from_signal(const std::string& signal,
                                                       NodeId& node_id) const {
    // Parse signal format: v(node_name)
    if (signal.size() < 4 || signal[0] != 'v' || signal[1] != '(') {
        return false;
    }

    size_t close_paren = signal.find(')');
    if (close_paren == std::string::npos) {
        return false;
    }

    std::string node_name = signal.substr(2, close_paren - 2);

    // Convert node name to NodeId
    node_id = circuit_.get_node_id(node_name);
    if (node_id == NODE_INVALID) {
        spdlog::error("Node not found: {}", node_name);
        return false;
    }

    return true;
}

bool TransferFunctionSolver::extract_source_device_id(const std::string& source_name,
                                                       DeviceId& device_id) const {
    // Find device by name
    const auto& devices = circuit_.get_devices();
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        if (device->name() == source_name) {
            // Check if it's a voltage source
            if (device->type() != DeviceType::VSOURCE) {
                spdlog::error("Input source must be a voltage source: {}", source_name);
                return false;
            }
            device_id = static_cast<DeviceId>(i);
            return true;
        }
    }

    spdlog::error("Source device not found: {}", source_name);
    return false;
}

Real TransferFunctionSolver::compute_transfer_gain(const std::vector<Real>& solution,
                                                    NodeId output_node,
                                                    DeviceId input_device) {
    // Small-signal transfer gain using finite difference on the input source.
    // This handles the common case where the source DC bias is 0 V (pure AC excitation).

    if (output_node == 0) {
        spdlog::error("Output node cannot be GND");
        return 0.0;
    }

    // Nominal output voltage
    Real v_out_nominal = solution[output_node - 1];

    // Input device (must be voltage source)
    const auto& input_device_ptr = circuit_.get_devices()[input_device];
    auto vsrc = std::dynamic_pointer_cast<devices::VoltageSource>(input_device_ptr);
    if (!vsrc) {
        spdlog::error("Input device is not a voltage source");
        return 0.0;
    }

    // Nominal input voltage and perturbation
    Real v_in_nominal = vsrc->dc_value();
    Real delta_v = std::max(std::abs(v_in_nominal) * 0.01, 1e-3); // ≥1 mV

    // Temporarily perturb source
    const Real original_dc = vsrc->dc_value();
    vsrc->set_dc_value(original_dc + delta_v);

    // Re-run DC operating point with perturbed source
    solvers::DCSolver dc_solver(circuit_, config_);
    std::vector<Real> pert_solution;
    SolverStats pert_stats;
    ConvergenceStatus status = dc_solver.solve(pert_solution, pert_stats);

    // Restore source immediately
    vsrc->set_dc_value(original_dc);

    if (status != ConvergenceStatus::SUCCESS || pert_solution.size() <= output_node - 1) {
        spdlog::warn("Perturbed DC solve failed (status={}), fallback to nominal ratio",
                     static_cast<int>(status));
        if (std::abs(v_in_nominal) < 1e-12) {
            return 0.0;
        }
        return v_out_nominal / v_in_nominal;
    }

    Real v_out_pert = pert_solution[output_node - 1];
    Real gain = (v_out_pert - v_out_nominal) / delta_v;

    spdlog::debug("Transfer gain (finite diff): Vout0={:.6e}, Vout+Δ={:.6e}, ΔV={:.6e}, gain={:.6e}",
                  v_out_nominal, v_out_pert, delta_v, gain);

    return gain;
}

Real TransferFunctionSolver::compute_input_resistance(const std::vector<Real>& solution,
                                                       DeviceId input_device,
                                                       const solvers::DCSolver& dc_solver) {
    // Input resistance calculation
    // R_in = V_in / I_in, where I_in is the branch current through the voltage source

    // Get input device
    const auto& input_device_ptr = circuit_.get_devices()[input_device];

    // Get device name
    std::string device_name = input_device_ptr->name();

    // Get input voltage (DC value)
    Real v_in = 0.0;
    if (auto vsrc = std::dynamic_pointer_cast<devices::VoltageSource>(input_device_ptr)) {
        v_in = vsrc->dc_value();
    } else {
        spdlog::error("Input device is not a voltage source");
        return 0.0;
    }

    // Get branch current through input voltage source
    auto i_in_opt = dc_solver.get_branch_current(device_name);
    if (!i_in_opt.has_value()) {
        spdlog::warn("Failed to get branch current for input source: {}", device_name);
        return 0.0; // Fall back to ideal voltage source assumption
    }

    Real i_in = i_in_opt.value();

    // Avoid division by zero
    if (std::abs(i_in) < 1e-12) {
        spdlog::debug("Input resistance: infinite (no current flowing, I_in={:.6e} A)", i_in);
        return 1e12; // Return large resistance (effectively open circuit)
    }

    // Compute input resistance: R_in = V_in / I_in
    Real r_in = v_in / i_in;

    spdlog::debug("Input resistance: {:.6e} Ω (V_in={:.6e} V, I_in={:.6e} A)",
                  r_in, v_in, i_in);

    return r_in;
}

Real TransferFunctionSolver::compute_output_resistance(const std::vector<Real>& solution,
                                                        NodeId output_node) {
    // Output resistance calculation using Thevenin equivalent
    // R_out = V_th / I_sc where:
    //   V_th = open-circuit voltage (already computed in solution)
    //   I_sc = short-circuit current (requires additional DC analysis)

    // For resistor networks, we can estimate R_out by looking at
    // adjacent resistances. For a simple voltage divider:
    // R_out = R1 || R2 (parallel combination)

    // Get all devices connected to the output node
    Real total_conductance = 0.0;
    const auto& devices = circuit_.get_devices();

    for (const auto& device : devices) {
        // Check if device is connected to output node
        const auto& nodes = device->nodes();
        bool connected_to_output = false;
        for (NodeId node : nodes) {
            if (node == output_node) {
                connected_to_output = true;
                break;
            }
        }

        if (connected_to_output && device->type() == DeviceType::RESISTOR) {
            // Get resistance value
            auto resistor = std::dynamic_pointer_cast<devices::Resistor>(device);
            if (resistor) {
                Real resistance = resistor->resistance();
                if (resistance > 0.0) {
                    total_conductance += 1.0 / resistance;
                }
            }
        }
    }

    // R_out = 1 / G_total (parallel combination)
    Real r_out = total_conductance > 0.0 ? 1.0 / total_conductance : 1e12;

    spdlog::debug("Output resistance: {:.6e} Ω (parallel resistor approximation, G_total={:.6e} S)",
                  r_out, total_conductance);

    return r_out;
}

} // namespace kirimaspice
