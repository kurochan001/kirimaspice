// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/output/touchstone_writer.cpp
#include "kirimaspice/output/touchstone_writer.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <utility>
#include <vector>

namespace kirimaspice::output {

bool TouchstoneWriter::write(const std::string& filename,
                             const SParameterResult& result,
                             Format format) {
    if (!result.success) {
        spdlog::error("Cannot write Touchstone file: S-parameter analysis failed");
        return false;
    }

    if (result.ports.empty()) {
        spdlog::error("Cannot write Touchstone file: no ports defined");
        return false;
    }

    if (result.frequency_points.empty()) {
        spdlog::error("Cannot write Touchstone file: no frequency points");
        return false;
    }

    std::uint32_t num_ports = static_cast<std::uint32_t>(result.ports.size());

    // Open output file
    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        spdlog::error("Failed to open Touchstone file for writing: {}", filename);
        return false;
    }

    // Write header
    std::string header = format_header(result, format);
    ofs << header;

    // Write comment line with column descriptions, in the order of the data
    ofs << "! " << num_ports << "-port S-parameters\n";
    ofs << "! Freq";
    for (const auto& data_line : data_layout(num_ports)) {
        for (const auto& [i, j] : data_line) {
            if (format == Format::MA || format == Format::DB) {
                ofs << fmt::format(" S{}{}_{} S{}{}_ang", i + 1, j + 1,
                                  (format == Format::MA) ? "mag" : "dB", i + 1, j + 1);
            } else {
                ofs << fmt::format(" S{}{}_re S{}{}_im", i + 1, j + 1, i + 1, j + 1);
            }
        }
    }
    ofs << "\n";

    // Write data lines
    for (const auto& point : result.frequency_points) {
        std::string data_lines = format_data_lines(point, format, num_ports);
        ofs << data_lines;
    }

    ofs.close();

    spdlog::info("Touchstone file written: {} ({} ports, {} frequencies, {} format)",
                 filename, num_ports, result.frequency_points.size(),
                 get_format_string(format));

    return true;
}

std::string TouchstoneWriter::format_header(const SParameterResult& result, Format format) {
    // Determine reference impedance (use first port's impedance)
    Real reference_impedance = 50.0;  // Default
    if (!result.ports.empty()) {
        reference_impedance = result.ports[0].impedance;
    }

    // Check if all ports have the same impedance
    bool uniform_impedance = true;
    for (const auto& port : result.ports) {
        if (std::abs(port.impedance - reference_impedance) > 1e-6) {
            uniform_impedance = false;
            break;
        }
    }

    if (!uniform_impedance) {
        spdlog::warn("Touchstone file: ports have different impedances, using first port's impedance ({} Ω) as reference",
                    reference_impedance);
    }

    // Format: # <freq_unit> S <format> R <reference_impedance>
    std::string format_str = get_format_string(format);
    return fmt::format("# Hz S {} R {}\n", format_str, reference_impedance);
}

std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>>
TouchstoneWriter::data_layout(std::uint32_t num_ports) {
    // Touchstone 1.1, section 3 (Data Lines): a 2-port file writes its four
    // entries on one line as N11 N21 N12 N22, the one case in column order.
    // Every other size writes the matrix row by row: each row starts a new
    // line, and a line holds at most four pairs, so a row of 5 or more ports
    // runs on to further lines. 1 port is the single line N11.
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> lines;
    if (num_ports == 2) {
        lines.push_back({{0, 0}, {1, 0}, {0, 1}, {1, 1}});
        return lines;
    }
    constexpr std::uint32_t max_pairs_per_line = 4;
    for (std::uint32_t i = 0; i < num_ports; ++i) {
        for (std::uint32_t j = 0; j < num_ports; ++j) {
            if (j % max_pairs_per_line == 0) {
                lines.emplace_back();
            }
            lines.back().emplace_back(i, j);
        }
    }
    return lines;
}

std::string TouchstoneWriter::format_data_lines(const SParameterPoint& point,
                                                Format format,
                                                std::uint32_t num_ports) {
    // The frequency starts the first line of the set (Touchstone 1.1,
    // section 3); the entries follow in the order of data_layout
    std::string text = fmt::format("{:15.6e}", point.frequency);
    bool first_line = true;
    for (const auto& data_line : data_layout(num_ports)) {
        if (!first_line) {
            text += std::string(15, ' ');
        }
        first_line = false;
        for (const auto& [i, j] : data_line) {
            Complex s_param = point.S_matrix[i][j];
            auto [val1, val2] = (format == Format::MA) ? to_ma_format(s_param) :
                               (format == Format::DB) ? to_db_format(s_param) :
                                                       to_ri_format(s_param);
            text += fmt::format(" {:15.6e} {:15.6e}", val1, val2);
        }
        text += "\n";
    }
    return text;
}

std::pair<Real, Real> TouchstoneWriter::to_ma_format(const Complex& s_param) {
    Real magnitude = std::abs(s_param);
    Real angle_rad = std::arg(s_param);
    Real angle_deg = angle_rad * 180.0 / M_PI;
    return {magnitude, angle_deg};
}

std::pair<Real, Real> TouchstoneWriter::to_db_format(const Complex& s_param) {
    Real magnitude = std::abs(s_param);
    Real magnitude_dB = 20.0 * std::log10(magnitude + 1e-30);  // Avoid log(0)
    Real angle_rad = std::arg(s_param);
    Real angle_deg = angle_rad * 180.0 / M_PI;
    return {magnitude_dB, angle_deg};
}

std::pair<Real, Real> TouchstoneWriter::to_ri_format(const Complex& s_param) {
    return {s_param.real(), s_param.imag()};
}

std::string TouchstoneWriter::get_format_string(Format format) {
    switch (format) {
    case Format::MA:
        return "MA";
    case Format::DB:
        return "DB";
    case Format::RI:
        return "RI";
    default:
        return "MA";
    }
}

std::string TouchstoneWriter::get_extension(std::uint32_t num_ports) {
    return fmt::format(".s{}p", num_ports);
}

} // namespace kirimaspice::output
