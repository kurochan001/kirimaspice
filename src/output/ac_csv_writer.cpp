// src/output/ac_csv_writer.cpp
#include "kirimaspice/output/ac_csv_writer.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace kirimaspice::output {

ACCSVWriter::ACCSVWriter(const std::string& filepath,
                         const circuit::CircuitGraph& circuit)
    : filepath_(filepath)
    , circuit_(circuit) {

    out_.open(filepath_, std::ios::out);
    if (!out_.is_open()) {
        throw std::runtime_error(fmt::format("Failed to open file for writing: {}", filepath_));
    }

    spdlog::debug("ACCSVWriter created: {}", filepath_);
}

void ACCSVWriter::write(const solvers::ACResult& result,
                       const std::string& title) {
    if (result.frequency_points.empty()) {
        spdlog::warn("AC result is empty, nothing to write");
        return;
    }

    // Write metadata header
    write_metadata(title, result);

    // Build list of node names (exclude ground)
    std::vector<std::string> node_names;
    std::uint32_t num_nodes = circuit_.num_nodes() - 1; // Exclude GND
    for (NodeId node_id = 1; node_id <= num_nodes; ++node_id) {
        node_names.push_back(get_node_name(node_id));
    }

    // Write column headers
    write_column_headers(node_names);

    // Write data rows
    write_data_rows(result);

    out_.close();

    spdlog::info("AC analysis result written to: {} ({} frequency points, {} nodes)",
                 filepath_, result.frequency_points.size(), node_names.size());
}

void ACCSVWriter::write_metadata(const std::string& title,
                                 const solvers::ACResult& result) {
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char time_buffer[128];
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));

    // Write metadata as comment lines
    out_ << "# AC Analysis Result\n";
    out_ << "# Title: " << title << "\n";
    out_ << "# Date: " << time_buffer << "\n";
    out_ << "# Simulator: kirimaspice v0.27\n";
    out_ << "# Analysis Type: AC (Small-Signal Frequency Domain)\n";
    out_ << "# Num Frequency Points: " << result.frequency_points.size() << "\n";
    out_ << "# Num Nodes: " << (circuit_.num_nodes() - 1) << "\n";

    if (!result.frequency_points.empty()) {
        out_ << fmt::format("# Frequency Range: {:.3e} Hz to {:.3e} Hz\n",
                           result.frequency_points.front().frequency,
                           result.frequency_points.back().frequency);
    }

    out_ << "# Status: " << (result.status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << "\n";
    out_ << fmt::format("# Linear Solves: {}\n", result.stats.linear_solves);
    out_ << fmt::format("# Total Time: {:.6f} s\n", result.stats.total_time_seconds);
    out_ << "#\n";
}

void ACCSVWriter::write_column_headers(const std::vector<std::string>& node_names) {
    // First column: frequency
    out_ << "frequency";

    // For each node: real, imag, mag_dB, phase_deg
    for (const auto& node_name : node_names) {
        out_ << fmt::format(",v({})_real", node_name);
        out_ << fmt::format(",v({})_imag", node_name);
        out_ << fmt::format(",v({})_mag_dB", node_name);
        out_ << fmt::format(",v({})_phase_deg", node_name);
    }

    out_ << "\n";
}

void ACCSVWriter::write_data_rows(const solvers::ACResult& result) {
    std::uint32_t num_nodes = circuit_.num_nodes() - 1; // Exclude GND

    for (const auto& point : result.frequency_points) {
        // First column: frequency
        out_ << fmt::format("{:.15e}", point.frequency);

        // For each node: real, imag, mag_dB, phase_deg
        for (NodeId node_id = 1; node_id <= num_nodes; ++node_id) {
            std::uint32_t idx = node_id - 1; // 0-indexed in solution vector

            // Boundary check: ensure idx is within solution vector bounds
            if (idx >= point.solution.size()) {
                spdlog::warn("ACCSVWriter: node_id {} (idx {}) exceeds solution size {}",
                             node_id, idx, point.solution.size());
                // Write zeros for missing data
                out_ << ",0.0,0.0,-inf,0.0";
                continue;
            }

            Complex V = point.solution[idx];

            Real re = std::real(V);
            Real im = std::imag(V);
            Real mag_dB = magnitude_db(V);
            Real phase_deg = phase_degrees(V);

            out_ << fmt::format(",{:.15e}", re);
            out_ << fmt::format(",{:.15e}", im);
            out_ << fmt::format(",{:.15e}", mag_dB);
            out_ << fmt::format(",{:.15e}", phase_deg);
        }

        out_ << "\n";
    }
}

std::string ACCSVWriter::get_node_name(NodeId node_id) const {
    // Try to find node name from circuit graph
    // Get all node names and check which one matches the node_id
    std::vector<std::string> node_names = circuit_.get_node_names();
    for (const auto& name : node_names) {
        if (circuit_.get_node_id(name) == node_id) {
            return name;
        }
    }

    // Fallback: use numeric ID
    return fmt::format("n{}", node_id);
}

} // namespace kirimaspice::output
