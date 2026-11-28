// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/transfer_function_solver.cpp
#include "kirimaspice/solvers/transfer_function_solver.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <optional>

#include "kirimaspice/devices/current_source.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/util/string_utils.hpp"

namespace kirimaspice {

namespace {

/// The resistance printed for an input that draws no current, as ngspice
/// prints 1e20 for an infinite impedance
constexpr Real kInfiniteResistance = 1e20;

/// The output of .TF: the voltage v(<node>) or v(<n1>,<n2>), by its two
/// nodes, or the current i(<source>) of a voltage source, by its branch
struct Output {
    bool current = false;
    NodeId first = NODE_GND, second = NODE_GND;
    BranchId branch = 0;
};

/// The output, or nothing for a node or source the circuit does not have,
/// a single node that is ground, or a current of anything but a voltage
/// source
std::optional<Output> output_of(const circuit::CircuitGraph& circuit, const std::string& signal) {
    const auto open = signal.find('(');
    if (open == std::string::npos || signal.back() != ')') {
        return std::nullopt;
    }
    const std::string kind = signal.substr(0, open);
    const std::string inside = signal.substr(open + 1, signal.size() - open - 2);
    if (util::equals_icase(kind, "i")) {
        std::string name = inside;
        util::trim_inplace(name);
        try {
            if (auto v = std::dynamic_pointer_cast<devices::VoltageSource>(circuit.get_device(name))) {
                Output output;
                output.current = true;
                output.branch = v->branch_id();
                return output;
            }
        } catch (const std::exception&) {
        }
        return std::nullopt;
    }
    if (!util::equals_icase(kind, "v")) {
        return std::nullopt;
    }
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
    Output output;
    output.first = *first;
    if (comma == std::string::npos) {
        if (*first == NODE_GND) {
            return std::nullopt;
        }
        return output;
    }
    const auto second = node(inside.substr(comma + 1));
    if (!second) {
        return std::nullopt;
    }
    output.second = *second;
    return output;
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

/// Sets `success` when the three results are finite. A value that is not
/// finite is no result: a gain beyond the range of a double is inf, and the
/// difference of two such nodes is nan. The analysis then fails with a
/// message, as .AC and .NOISE fail on such a value; ngspice-45.2 prints inf
/// and nan there (run 2026-11-28).
void finish(TransferFunctionResult& result) {
    const struct {
        const char* what;
        Real value;
    } values[] = {{"transfer function", result.transfer_gain},
                  {"input resistance", result.input_resistance},
                  {"output resistance", result.output_resistance}};
    for (const auto& v : values) {
        if (!std::isfinite(v.value)) {
            spdlog::error(".TF: the {} is not finite ({})", v.what, v.value);
            return;
        }
    }
    result.success = true;
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
// is the output resistance. For a current output, i(<source>) of a voltage
// source, the gain is that source's branch current (into its + terminal)
// and the output resistance 1e20, as ngspice-45.2 prints it for every
// current output tried (five circuits; why is not confirmed). ngspice-45.2
// gives these values for the circuits of tests/unit/test_transfer_function.cpp
// (run 2026-11-28).
TransferFunctionResult TransferFunctionSolver::analyze() {
    TransferFunctionResult result;
    result.output_signal = config_.tf.output_signal;
    result.input_source = config_.tf.input_source;

    const auto output = output_of(circuit_, config_.tf.output_signal);
    if (!output) {
        spdlog::error(".TF: the output must be v(<node>), v(<node>,<node>) or i(<voltage source>) of the "
                      "circuit, not {}",
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
    if (output->current) {
        const std::size_t k = nodes + output->branch;
        result.transfer_gain = k < x.size() ? x[k].real() : 0.0;
    } else {
        result.transfer_gain = voltage(x, output->first) - voltage(x, output->second);
    }
    if (input->voltage) {
        // The branch current flows into the + terminal: the source delivers -I
        const std::size_t k = nodes + input->branch;
        const Real current = k < x.size() ? -x[k].real() : 0.0;
        result.input_resistance = current != 0.0 ? 1.0 / current : kInfiniteResistance;
    } else {
        result.input_resistance = voltage(x, input->nn) - voltage(x, input->np);
    }

    if (output->current) {
        result.output_resistance = kInfiniteResistance;
        finish(result);
        return result;
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
    finish(result);
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
