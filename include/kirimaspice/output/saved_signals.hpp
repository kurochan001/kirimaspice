// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/util/string_utils.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace kirimaspice::output {

/// The signals that the output files hold: every node voltage and branch
/// current, or, with .SAVE, those it names, as ngspice-45.2 keeps in its raw
/// file the scale and the vectors of its .save lines, of every analysis, and
/// everything for .save all (run 2026-11-28); the print command of a
/// .control block keeps everything there, and here (Output::saved_signals
/// holds the .SAVE lines' signals alone). A signal is v(<node>),
/// i(<source>) or a node's name; names are not case-sensitive. One that names
/// no node or source is left out with a warning. The raw files (ASCII and
/// binary) and the SSF file take their signals from here.
class SavedSignals {
public:
    /// @param node_names the circuit's node names, ground first
    ///        (CircuitGraph::get_node_names)
    /// @param branch_names the names of the branches, in their order
    SavedSignals(const SimulationConfig::Output& output, const std::vector<std::string>& node_names,
                 const std::vector<std::string>& branch_names)
        : node_names_(node_names), branch_names_(branch_names) {
        const std::size_t nodes = node_names.empty() ? 0 : node_names.size() - 1;
        const bool all = output.saved_signals.empty();
        keep_nodes_.assign(nodes, all);
        keep_branches_.assign(branch_names.size(), all);
        for (const auto& signal : output.saved_signals) {
            if (util::equals_icase(signal, "all")) {
                keep_nodes_.assign(nodes, true);
                keep_branches_.assign(branch_names.size(), true);
                continue;
            }
            std::string kind = "V";
            std::string name = signal;
            const auto open = signal.find('(');
            if (open != std::string::npos && signal.back() == ')') {
                kind = signal.substr(0, open);
                name = signal.substr(open + 1, signal.size() - open - 2);
            }
            bool found = false;
            if (util::equals_icase(kind, "V")) {
                for (std::size_t id = 1; id < node_names.size(); ++id) {
                    if (util::equals_icase(node_names[id], name)) {
                        keep_nodes_[id - 1] = true;
                        found = true;
                    }
                }
            } else if (util::equals_icase(kind, "I")) {
                for (std::size_t k = 0; k < branch_names.size(); ++k) {
                    if (util::equals_icase(branch_names[k], name)) {
                        keep_branches_[k] = true;
                        found = true;
                    }
                }
            }
            if (!found) {
                spdlog::warn(".SAVE {}: no such node or voltage source; left out", signal);
            }
        }
    }

    /// The node names the files take, ground first as the writers expect
    std::vector<std::string> node_names() const {
        std::vector<std::string> names;
        if (!node_names_.empty()) {
            names.push_back(node_names_.front());
        }
        for (std::size_t k = 0; k < keep_nodes_.size(); ++k) {
            if (keep_nodes_[k]) names.push_back(node_names_[k + 1]);
        }
        return names;
    }

    std::vector<std::string> branch_names() const {
        std::vector<std::string> names;
        for (std::size_t k = 0; k < keep_branches_.size(); ++k) {
            if (keep_branches_[k]) names.push_back(branch_names_[k]);
        }
        return names;
    }

    /// The kept node voltages of a solution (node k + 1 at element k)
    DenseVector nodes(const DenseVector& voltages) const { return pick(voltages, keep_nodes_); }
    DenseVector branches(const DenseVector& currents) const { return pick(currents, keep_branches_); }

    DCSweepResult apply(DCSweepResult result) const {
        for (auto& point : result.points) {
            point.node_voltages = nodes(point.node_voltages);
            point.branch_currents = branches(point.branch_currents);
        }
        return result;
    }

    solvers::TransientResult apply(solvers::TransientResult result) const {
        for (auto& point : result.timepoints) {
            point.node_voltages = nodes(point.node_voltages);
            point.branch_currents = branches(point.branch_currents);
        }
        return result;
    }

private:
    static DenseVector pick(const DenseVector& values, const std::vector<bool>& keep) {
        DenseVector kept;
        for (std::size_t k = 0; k < values.size() && k < keep.size(); ++k) {
            if (keep[k]) kept.push_back(values[k]);
        }
        return kept;
    }

    std::vector<std::string> node_names_, branch_names_;
    std::vector<bool> keep_nodes_, keep_branches_;
};

}  // namespace kirimaspice::output
