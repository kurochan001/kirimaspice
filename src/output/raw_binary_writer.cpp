// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "kirimaspice/output/raw_binary_writer.hpp"
#include "kirimaspice/output/output_error.hpp"
#include "kirimaspice/output/signal_names.hpp"

#include <chrono>
#include <ctime>
#include <fmt/format.h>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace kirimaspice::output {

RawBinaryWriter::RawBinaryWriter(std::string filepath, bool append_mode)
    : filepath_(std::move(filepath))
    , append_mode_(append_mode) {}

std::ofstream RawBinaryWriter::open_stream() {
    std::ios::openmode mode = std::ios::out | std::ios::binary;
    mode |= append_mode_ ? std::ios::app : std::ios::trunc;
    std::ofstream out(filepath_, mode);

    if (!out.is_open() || !out.good()) {
        throw OutputFileError(
            fmt::format("Failed to open RAW Binary output file: {}", filepath_));
    }

    append_mode_ = true; // Subsequent writes append
    return out;
}

std::string RawBinaryWriter::current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now_time);
#else
    localtime_r(&now_time, &tm_buf);
#endif
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%a %b %d %H:%M:%S %Y", &tm_buf);
    return buffer;
}

std::vector<std::string> RawBinaryWriter::align_node_names(
    const std::vector<std::string>& node_names,
    std::size_t expected) const {
    std::vector<std::string> aligned;
    aligned.reserve(expected);

    // Prefer to drop ground node if solution excludes it
    for (const auto& name : node_names) {
        if (name == NODE_GND_NAME && node_names.size() > expected) {
            continue;
        }
        aligned.push_back(name);
        if (aligned.size() == expected) {
            break;
        }
    }

    while (aligned.size() < expected) {
        aligned.push_back(fmt::format("node{}", aligned.size() + 1));
    }
    return aligned;
}

static std::vector<std::string> align_branch_names(const std::vector<std::string>& branch_names,
                                                   std::size_t expected) {
    std::vector<std::string> aligned;
    aligned.reserve(expected);
    for (std::size_t i = 0; i < expected; ++i) {
        if (i < branch_names.size()) {
            aligned.push_back(branch_names[i]);
        } else {
            aligned.push_back(fmt::format("branch{}", i));
        }
    }
    return aligned;
}

void RawBinaryWriter::write_variables(
    std::ofstream& out,
    const std::vector<std::pair<std::string, std::string>>& vars) const {
    out << "Variables:\n";
    for (std::size_t i = 0; i < vars.size(); ++i) {
        out << "\t" << i << "\t" << vars[i].first << "\t" << vars[i].second << "\n";
    }
}

void RawBinaryWriter::write_header(std::ofstream& out,
                                   const std::string& plotname,
                                   const std::string& flags,
                                   std::size_t num_vars,
                                   std::size_t num_points,
                                   const std::string& title_suffix) const {
    out << "Title: kirimaspice " << (title_suffix.empty() ? plotname : title_suffix) << "\n";
    out << "Date: " << current_timestamp() << "\n";
    out << "Plotname: " << plotname << "\n";
    out << "Flags: " << flags << "\n";
    out << "No. Variables: " << num_vars << "\n";
    out << "No. Points: " << num_points << "\n";
}

void RawBinaryWriter::write_binary_marker(std::ofstream& out) const {
    out << "Binary:\n";
}

void RawBinaryWriter::write_binary_values(std::ofstream& out,
                                          const std::vector<Real>& values) const {
    // Write binary data as 8-byte doubles (little-endian)
    for (Real value : values) {
        double d = static_cast<double>(value);
        out.write(reinterpret_cast<const char*>(&d), sizeof(double));
    }
}

std::vector<Real> RawBinaryWriter::build_dc_values(
    const DenseVector& node_voltages,
    const DenseVector& branch_currents) const {
    std::vector<Real> values;
    values.reserve(node_voltages.size() + branch_currents.size());

    // First: node voltages
    for (std::size_t i = 0; i < node_voltages.size(); ++i) {
        values.push_back(node_voltages[i]);
    }

    // Then: branch currents
    for (std::size_t i = 0; i < branch_currents.size(); ++i) {
        values.push_back(branch_currents[i]);
    }

    return values;
}

void RawBinaryWriter::write_dc(const std::vector<std::string>& node_names,
                                const DenseVector& node_voltages,
                                const std::vector<std::string>& branch_names,
                                const DenseVector& branch_currents) {
    auto out = open_stream();

    const auto aligned_nodes = align_node_names(node_names, node_voltages.size());
    const auto aligned_branches = align_branch_names(branch_names, branch_currents.size());

    std::vector<std::pair<std::string, std::string>> vars;
    vars.reserve(aligned_nodes.size() + aligned_branches.size());

    for (const auto& node : aligned_nodes) {
        vars.emplace_back(voltage_signal(node), "voltage");
    }

    for (const auto& branch : aligned_branches) {
        vars.emplace_back(current_signal(branch), "current");
    }

    write_header(out, "DC Operating Point", "real", vars.size(), 1);
    write_variables(out, vars);
    write_binary_marker(out);

    auto values = build_dc_values(node_voltages, branch_currents);
    write_binary_values(out, values);

    spdlog::info("Wrote DC operating point to RAW Binary file: {} ({} variables)",
                 filepath_, vars.size());
}

void RawBinaryWriter::write_dc_sweep(const std::string& sweep_name,
                                      const std::optional<std::string>& nested_name,
                                      const std::vector<std::string>& node_names,
                                      const std::vector<std::string>& branch_names,
                                      const DCSweepResult& result) {
    auto out = open_stream();

    const std::size_t num_points = result.points.size();
    if (num_points == 0) {
        spdlog::warn("DC sweep result is empty, skipping RAW Binary output");
        return;
    }

    const auto& first_point = result.points.front();
    const auto aligned_nodes = align_node_names(node_names, first_point.node_voltages.size());
    const auto aligned_branches = align_branch_names(
        branch_names, first_point.branch_currents.size());

    std::vector<std::pair<std::string, std::string>> vars;
    vars.reserve(1 + aligned_nodes.size() + aligned_branches.size());

    // First variable: sweep parameter
    std::string sweep_var_name = sweep_name;
    if (nested_name) {
        sweep_var_name += "-" + *nested_name;
    }
    vars.emplace_back(sweep_var_name, "sweep");

    for (const auto& node : aligned_nodes) {
        vars.emplace_back(voltage_signal(node), "voltage");
    }

    for (const auto& branch : aligned_branches) {
        vars.emplace_back(current_signal(branch), "current");
    }

    write_header(out, "DC Sweep", "real", vars.size(), num_points, "DC Sweep Analysis");
    write_variables(out, vars);
    write_binary_marker(out);

    // Write data points
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        const auto& point = result.points[pt];
        std::vector<Real> point_values;
        point_values.reserve(vars.size());

        // Sweep parameter value
        point_values.push_back(point.sweep_value);

        // Node voltages
        for (std::size_t i = 0; i < aligned_nodes.size(); ++i) {
            if (i < point.node_voltages.size()) {
                point_values.push_back(point.node_voltages[i]);
            } else {
                point_values.push_back(0.0);
            }
        }

        // Branch currents
        for (std::size_t i = 0; i < aligned_branches.size(); ++i) {
            if (i < point.branch_currents.size()) {
                point_values.push_back(point.branch_currents[i]);
            } else {
                point_values.push_back(0.0);
            }
        }

        write_binary_values(out, point_values);
    }

    spdlog::info("Wrote DC sweep to RAW Binary file: {} ({} points, {} variables)",
                 filepath_, num_points, vars.size());
}

void RawBinaryWriter::write_transient(const std::vector<std::string>& node_names,
                                       const std::vector<std::string>& branch_names,
                                       const solvers::TransientResult& result) {
    auto out = open_stream();

    const std::size_t num_points = result.timepoints.size();
    if (num_points == 0) {
        spdlog::warn("Transient result is empty, skipping RAW Binary output");
        return;
    }

    const auto& first_tp = result.timepoints.front();
    const auto aligned_nodes = align_node_names(node_names, first_tp.node_voltages.size());
    const auto aligned_branches = align_branch_names(branch_names, first_tp.branch_currents.size());

    std::vector<std::pair<std::string, std::string>> vars;
    vars.reserve(1 + aligned_nodes.size() + aligned_branches.size());

    // First variable: time
    vars.emplace_back("time", "time");

    for (const auto& node : aligned_nodes) {
        vars.emplace_back(voltage_signal(node), "voltage");
    }

    for (const auto& branch : aligned_branches) {
        vars.emplace_back(current_signal(branch), "current");
    }

    write_header(out, "Transient Analysis", "real", vars.size(), num_points);
    write_variables(out, vars);
    write_binary_marker(out);

    // Write data points
    for (std::size_t pt = 0; pt < num_points; ++pt) {
        const auto& tp = result.timepoints[pt];
        std::vector<Real> point_values;
        point_values.reserve(vars.size());

        // Time value
        point_values.push_back(tp.time);

        // Node voltages
        for (std::size_t i = 0; i < aligned_nodes.size(); ++i) {
            if (i < tp.node_voltages.size()) {
                point_values.push_back(tp.node_voltages[i]);
            } else {
                point_values.push_back(0.0);
            }
        }

        // Branch currents
        for (std::size_t i = 0; i < aligned_branches.size(); ++i) {
            if (i < tp.branch_currents.size()) {
                point_values.push_back(tp.branch_currents[i]);
            } else {
                point_values.push_back(0.0);
            }
        }

        write_binary_values(out, point_values);
    }

    spdlog::info("Wrote transient analysis to RAW Binary file: {} ({} points, {} variables)",
                 filepath_, num_points, vars.size());
}

} // namespace kirimaspice::output
