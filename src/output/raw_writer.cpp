#include "kirimaspice/output/raw_writer.hpp"

#include <chrono>
#include <ctime>
#include <fmt/format.h>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace kirimaspice::output {

RawWriter::RawWriter(std::string filepath, bool append_mode)
    : filepath_(std::move(filepath))
    , append_mode_(append_mode) {}

std::ofstream RawWriter::open_stream() {
    std::ios::openmode mode = std::ios::out;
    mode |= append_mode_ ? std::ios::app : std::ios::trunc;
    std::ofstream out(filepath_, mode);

    if (!out.is_open() || !out.good()) {
        throw std::runtime_error(
            fmt::format("Failed to open RAW output file: {}", filepath_));
    }

    append_mode_ = true; // Subsequent writes append
    return out;
}

std::string RawWriter::current_timestamp() const {
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

std::vector<std::string> RawWriter::align_node_names(const std::vector<std::string>& node_names,
                                                     std::size_t expected) const {
    std::vector<std::string> aligned;
    aligned.reserve(expected);

    // Prefer to drop ground node if solution excludes it
    for (const auto& name : node_names) {
        if ((name == "0" || name == "GND") && node_names.size() > expected) {
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

void RawWriter::write_variables(std::ofstream& out,
                                const std::vector<std::pair<std::string, std::string>>& vars) const {
    out << "Variables:\n";
    for (std::size_t i = 0; i < vars.size(); ++i) {
        out << "\t" << i << "\t" << vars[i].first << "\t" << vars[i].second << "\n";
    }
}

void RawWriter::write_header(std::ofstream& out,
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

void RawWriter::write_values_block(std::ofstream& out,
                                   std::size_t point_index,
                                   Real abscissa,
                                   const std::vector<Real>& values) const {
    out << point_index << "\t" << std::scientific << std::setprecision(OUTPUT_PRECISION) << abscissa << "\n";
    out << std::scientific << std::setprecision(OUTPUT_PRECISION);
    for (Real value : values) {
        out << "\t" << value << "\n";
    }
}

std::vector<Real> RawWriter::build_dc_values(const DenseVector& node_voltages,
                                             const DenseVector& branch_currents) const {
    std::vector<Real> values;
    values.reserve(node_voltages.size() + branch_currents.size());
    values.insert(values.end(), node_voltages.begin(), node_voltages.end());
    values.insert(values.end(), branch_currents.begin(), branch_currents.end());
    return values;
}

void RawWriter::write_dc(const std::vector<std::string>& node_names,
                         const DenseVector& node_voltages,
                         const std::vector<std::string>& branch_names,
                         const DenseVector& branch_currents) {
    auto aligned_nodes = align_node_names(node_names, node_voltages.size());
    std::vector<std::pair<std::string, std::string>> variables;
    for (const auto& name : aligned_nodes) {
        variables.emplace_back(fmt::format("v({})", name), "voltage");
    }
    for (std::size_t i = 0; i < branch_currents.size() && i < branch_names.size(); ++i) {
        variables.emplace_back(fmt::format("i({})", branch_names[i]), "current");
    }

    auto values = build_dc_values(node_voltages, branch_currents);

    std::ofstream out = open_stream();
    write_header(out, "DC Operating Point", "real", variables.size(), 1);
    write_variables(out, variables);
    out << "Values:\n";
    write_values_block(out, 0, 0.0, values);
    out << "\n";
}

void RawWriter::write_dc_sweep(const std::string& sweep_name,
                               const std::optional<std::string>& nested_name,
                               const std::vector<std::string>& node_names,
                               const std::vector<std::string>& branch_names,
                               const DCSweepResult& result) {
    if (result.points.empty()) {
        spdlog::warn("RAW writer: DC sweep result is empty, skipping output");
        return;
    }

    std::size_t node_count = result.points.front().node_voltages.size();
    std::size_t branch_count = result.points.front().branch_currents.size();

    auto aligned_nodes = align_node_names(node_names, node_count);
    auto aligned_branches = align_branch_names(branch_names, branch_count);

    std::vector<std::pair<std::string, std::string>> variables;
    variables.emplace_back(sweep_name, "sweep");
    if (nested_name) {
        variables.emplace_back(*nested_name, "sweep");
    }
    for (const auto& name : aligned_nodes) {
        variables.emplace_back(fmt::format("v({})", name), "voltage");
    }
    for (const auto& bname : aligned_branches) {
        variables.emplace_back(fmt::format("i({})", bname), "current");
    }

    std::ofstream out = open_stream();
    const bool has_nested = nested_name.has_value();
    write_header(out,
                 has_nested ? "DC Sweep (2D)" : "DC Sweep",
                 "real",
                 variables.size(),
                 result.points.size(),
                 fmt::format("{} sweep", sweep_name));
    write_variables(out, variables);
    out << "Values:\n";

    std::vector<Real> value_buffer(variables.size() - 1);
    for (std::size_t idx = 0; idx < result.points.size(); ++idx) {
        const auto& point = result.points[idx];
        std::size_t offset = 0;

        if (has_nested) {
            value_buffer[offset++] = point.nested_sweep_value.value_or(0.0);
        }

        for (std::size_t n = 0; n < aligned_nodes.size(); ++n) {
            if (n < point.node_voltages.size()) {
                value_buffer[offset++] = point.node_voltages[n];
            } else {
                value_buffer[offset++] = 0.0;
            }
        }

        for (std::size_t b = 0; b < aligned_branches.size(); ++b) {
            if (b < point.branch_currents.size()) {
                value_buffer[offset++] = point.branch_currents[b];
            } else {
                value_buffer[offset++] = 0.0;
            }
        }

        write_values_block(out, idx, point.sweep_value, value_buffer);
    }
    out << "\n";
}

void RawWriter::write_transient(const std::vector<std::string>& node_names,
                                const std::vector<std::string>& branch_names,
                                const solvers::TransientResult& result) {
    std::size_t num_points = result.timepoints.size();
    if (num_points == 0) {
        spdlog::warn("RAW writer: Transient result is empty, skipping output");
        return;
    }

    const auto& first_tp = result.timepoints.front();
    std::size_t node_count = first_tp.node_voltages.size();
    std::size_t branch_count = first_tp.branch_currents.size();

    auto aligned_nodes = align_node_names(node_names, node_count);
    auto aligned_branches = align_branch_names(branch_names, branch_count);

    std::vector<std::pair<std::string, std::string>> variables;
    variables.emplace_back("time", "time");
    for (const auto& name : aligned_nodes) {
        variables.emplace_back(fmt::format("v({})", name), "voltage");
    }

    for (const auto& bname : aligned_branches) {
        variables.emplace_back(fmt::format("i({})", bname), "current");
    }

    std::ofstream out = open_stream();
    write_header(out, "Transient Analysis", "real", variables.size(), num_points);
    write_variables(out, variables);
    out << "Values:\n";

    std::vector<Real> value_buffer;
    value_buffer.resize(variables.size() - 1); // exclude time

    for (std::size_t idx = 0; idx < result.timepoints.size(); ++idx) {
        const auto& tp = result.timepoints[idx];
        std::size_t offset = 0;
        // Node voltages
        for (std::size_t n = 0; n < aligned_nodes.size(); ++n) {
            if (n < tp.node_voltages.size()) {
                value_buffer[offset++] = tp.node_voltages[n];
            } else {
                value_buffer[offset++] = 0.0;
            }
        }
        // Branch currents
        for (std::size_t b = 0; b < aligned_branches.size(); ++b) {
            if (b < tp.branch_currents.size()) {
                value_buffer[offset++] = tp.branch_currents[b];
            } else {
                value_buffer[offset++] = 0.0;
            }
        }

        write_values_block(out, idx, tp.time, value_buffer);
    }
    out << "\n";
}

} // namespace kirimaspice::output
