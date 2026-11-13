// src/output/touchstone_writer.cpp
#include "kirimaspice/output/touchstone_writer.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <fstream>
#include <cmath>
#include <algorithm>

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

    // Write comment line with column descriptions
    ofs << "! " << num_ports << "-port S-parameters\n";
    ofs << "! Freq";
    for (std::uint32_t i = 1; i <= num_ports; ++i) {
        for (std::uint32_t j = 1; j <= num_ports; ++j) {
            std::string format_str = get_format_string(format);
            if (format == Format::MA || format == Format::DB) {
                ofs << fmt::format(" S{}{}_{} S{}{}_ang", i, j,
                                  (format == Format::MA) ? "mag" : "dB", i, j);
            } else {
                ofs << fmt::format(" S{}{}_re S{}{}_im", i, j, i, j);
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

std::string TouchstoneWriter::format_data_lines(const SParameterPoint& point,
                                                Format format,
                                                std::uint32_t num_ports) {
    std::string line = fmt::format("{:15.6e}", point.frequency);

    // Touchstone format:
    // - 2-port: all S-parameters on one line (freq S11 S21 S12 S22)
    // - 3-port and above: freq on first line, then S-parameters (with line breaks)
    //   For 3-port: freq\n S11 S21 S31\n S12 S22 S32\n S13 S23 S33
    //   For 4-port: freq\n S11 S21 S31 S41\n S12 S22 S32 S42\n S13 S23 S33 S43\n S14 S24 S34 S44

    if (num_ports == 2) {
        // 2-port: all on one line
        // Order: S11, S21, S12, S22
        std::vector<std::pair<std::uint32_t, std::uint32_t>> order = {
            {0, 0}, {1, 0}, {0, 1}, {1, 1}  // S11, S21, S12, S22
        };

        for (const auto& [i, j] : order) {
            Complex s_param = point.S_matrix[i][j];
            auto [val1, val2] = (format == Format::MA) ? to_ma_format(s_param) :
                               (format == Format::DB) ? to_db_format(s_param) :
                                                       to_ri_format(s_param);
            line += fmt::format(" {:15.6e} {:15.6e}", val1, val2);
        }
        line += "\n";
    } else {
        // 3-port and above: multi-line format
        line += "\n";

        for (std::uint32_t j = 0; j < num_ports; ++j) {
            for (std::uint32_t i = 0; i < num_ports; ++i) {
                Complex s_param = point.S_matrix[i][j];
                auto [val1, val2] = (format == Format::MA) ? to_ma_format(s_param) :
                                   (format == Format::DB) ? to_db_format(s_param) :
                                                           to_ri_format(s_param);
                line += fmt::format(" {:15.6e} {:15.6e}", val1, val2);
            }
            line += "\n";
        }
    }

    return line;
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
