// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/transfer_function_solver.cpp
#include "kirimaspice/solvers/transfer_function_solver.hpp"
#include "kirimaspice/devices/current_source.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/util/string_utils.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <optional>

namespace kirimaspice {

namespace {

/// The resistance printed for an input that draws no current, as ngspice
/// prints 1e20 for an infinite impedance
constexpr Real kInfiniteResistance = 1e20;

/// The two nodes of an output v(<node>) or v(<n1>,<n2>), or nothing for any
/// other output (a current, i(<source>), is not taken) or a node the circuit
/// does not have. A single node must not be ground.
std::optional<std::pair<NodeId, NodeId>> output_nodes(const circuit::CircuitGraph& circuit,
                                                      const std::string& signal) {
    const auto open = signal.find('(');
    if (open == std::string::npos || signal.back() != ')' || !util::equals_icase(signal.substr(0, open), "v")) {
        return std::nullopt;
    }
    const std::string inside = signal.substr(open + 1, signal.size() - open - 2);
    const auto comma = inside.find(',');
    auto node = [&](std::string name) -> std::optional<NodeId> {
        util::trim_inplace(name);
        if (name.empty() || !circuit.has_node(name)) {
            return std::nullopt;
        }
        return circuit.get_node_id(name);
    };
    const auto first = node(inside.substr(0, comma));
    if (!first) {
        return std::nullopt;
    }
    if (comma == std::string::npos) {
        if (*first == NODE_GND) {
            return std::nullopt;
        }
        return std::make_pair(*first, NODE_GND);
    }
    const auto second = node(inside.substr(comma + 1));
    if (!second) {
        return std::nullopt;
    }
    return std::make_pair(*first, *second);
}

/// The input of .TF: an independent voltage source, by its branch, or an
/// independent current source, by its nodes
struct Input {
    bool voltage = true;
    BranchId branch = 0;
    NodeId np = NODE_GND, nn = NODE_GND;
};

std::optional<Input> input_source(const circuit::CircuitGraph& circuit, const std::string& name) {
    std::shared_ptr<devices::Device> device;
    try {
        device = circuit.get_device(name);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (auto v = std::dynamic_pointer_cast<devices::VoltageSource>(device)) {
        return Input{true, v->branch_id(), NODE_GND, NODE_GND};
    }
    if (auto i = std::dynamic_pointer_cast<devices::CurrentSource>(device)) {
        return Input{false, 0, i->nodes()[0], i->nodes()[1]};
    }
    return std::nullopt;
}

}  // namespace

TransferFunctionSolver::TransferFunctionSolver(const circuit::CircuitGraph& circuit,
                                               const SimulationConfig& config)
    : circuit_(circuit), config_(config) {}

// The small-signal DC transfer function, as SPICE's .TF gives it: the
// circuit linearized at its operating point (every device's AC stamp at
// omega = 0, ACSolver::solve_excited), the sources' own values left out.
// With 1 at the input (1 V across a voltage source, 1 A through a current
// source, from n+ to n-), the output is the gain, and the input draws the
// current, or has the voltage, of the input resistance. With 1 A into the
// output (out of its second node), the input set to 0, the output's voltage
// is the output resistance. ngspice-45.2 gives these three values for the
// circuits of tests/unit/test_transfer_function.cpp (run 2026-11-28).
TransferFunctionResult TransferFunctionSolver::analyze() {
    TransferFunctionResult result;
    result.output_signal = config_.tf.output_signal;
    result.input_source = config_.tf.input_source;

    const auto output = output_nodes(circuit_, config_.tf.output_signal);
    if (!output) {
        spdlog::error(".TF: the output must be v(<node>) or v(<node>,<node>) of the circuit, not {}",
                      config_.tf.output_signal);
        return result;
    }
    const auto input = input_source(circuit_, config_.tf.input_source);
    if (!input) {
        spdlog::error(".TF: the input must be an independent voltage or current source, not {}",
                      config_.tf.input_source);
        return result;
    }

    const std::size_t nodes = circuit_.num_nodes() - 1;
    auto voltage = [](const ComplexDenseVector& x, NodeId node) {
        return (node == NODE_GND || node - 1 >= x.size()) ? 0.0 : x[node - 1].real();
    };
    auto inject = [](ComplexMNASystem& sys, NodeId node, Real current) {
        if (node != NODE_GND) {
            sys.I[node - 1] += Complex(current, 0.0);
        }
    };

    solvers::ACSolver small_signal(circuit_, config_);
    const ComplexDenseVector x = small_signal.solve_excited(0.0, [&](ComplexMNASystem& sys) {
        if (input->voltage) {
            sys.I[sys.num_nodes + input->branch] = Complex(1.0, 0.0);
        } else {
            inject(sys, input->np, -1.0);
            inject(sys, input->nn, 1.0);
        }
    });
    if (x.empty()) {
        spdlog::error(".TF: the operating point or the small-signal solution failed");
        return result;
    }
    result.transfer_gain = voltage(x, output->first) - voltage(x, output->second);
    if (input->voltage) {
        // The branch current flows into the + terminal: the source delivers -I
        const std::size_t k = nodes + input->branch;
        const Real current = k < x.size() ? -x[k].real() : 0.0;
        result.input_resistance = current != 0.0 ? 1.0 / current : kInfiniteResistance;
    } else {
        result.input_resistance = voltage(x, input->nn) - voltage(x, input->np);
    }

    const ComplexDenseVector y = small_signal.solve_excited(0.0, [&](ComplexMNASystem& sys) {
        inject(sys, output->first, 1.0);
        inject(sys, output->second, -1.0);
    });
    if (y.empty()) {
        spdlog::error(".TF: the small-signal solution for the output resistance failed");
        return result;
    }
    result.output_resistance = voltage(y, output->first) - voltage(y, output->second);
    result.success = true;
    return result;
}

void TransferFunctionSolver::print_result(const TransferFunctionResult& result) {
    if (!result.success) {
        fmt::print("\nTransfer function analysis FAILED\n");
        return;
    }
    fmt::print("\nTransfer Function Analysis Results\n");
    fmt::print("===================================\n");
    fmt::print("Output: {}\n", result.output_signal);
    fmt::print("Input:  {}\n\n", result.input_source);
    fmt::print("Transfer Gain:     {:.6e}\n", result.transfer_gain);
    fmt::print("Input Resistance:  {:.6e} Ω\n", result.input_resistance);
    fmt::print("Output Resistance: {:.6e} Ω\n\n", result.output_resistance);
}

} // namespace kirimaspice
