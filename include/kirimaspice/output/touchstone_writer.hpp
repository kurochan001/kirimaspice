// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/output/touchstone_writer.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include <string>
#include <utility>
#include <vector>

namespace kirimaspice::output {

/// Touchstone file format writer
/// Writes S-parameter data in industry-standard Touchstone format
/// Supports .s2p (2-port), .s4p (4-port), etc.
class TouchstoneWriter {
public:
    /// Output format for S-parameters
    enum class Format {
        MA,  ///< Magnitude-Angle (magnitude in linear, angle in degrees)
        DB,  ///< dB-Angle (magnitude in dB, angle in degrees)
        RI   ///< Real-Imaginary (real and imaginary parts)
    };

    /// Write S-parameter result to Touchstone file
    /// @param filename Output filename (e.g., "output.s2p")
    /// @param result S-parameter analysis result
    /// @param format Output format (MA/DB/RI, default: MA)
    /// @return true if successful, false otherwise
    static bool write(const std::string& filename,
                     const SParameterResult& result,
                     Format format = Format::MA);

private:
    /// Generate file header
    /// Format: # <freq_unit> S <format> R <reference_impedance>
    /// Example: # Hz S MA R 50
    /// @param result S-parameter result
    /// @param format Output format
    /// @return Header string
    static std::string format_header(const SParameterResult& result, Format format);

    /// The matrix entries (row, column, from 0) of each data line of one
    /// frequency, in the order the format sets: N11 N21 N12 N22 for 2 ports,
    /// otherwise row by row, each row on new lines of at most four pairs
    /// @param num_ports Number of ports
    /// @return One list of entries per data line
    static std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>>
    data_layout(std::uint32_t num_ports);

    /// Format a single frequency point as data line(s)
    /// @param point Frequency point with S-matrix
    /// @param format Output format
    /// @param num_ports Number of ports
    /// @return Data line(s) for this frequency
    static std::string format_data_lines(const SParameterPoint& point,
                                        Format format,
                                        std::uint32_t num_ports);

    /// Convert complex S-parameter to MA format (magnitude, angle in degrees)
    /// @param s_param Complex S-parameter
    /// @return Pair of (magnitude, angle_degrees)
    static std::pair<Real, Real> to_ma_format(const Complex& s_param);

    /// Convert complex S-parameter to DB format (dB, angle in degrees)
    /// @param s_param Complex S-parameter
    /// @return Pair of (magnitude_dB, angle_degrees)
    static std::pair<Real, Real> to_db_format(const Complex& s_param);

    /// Convert complex S-parameter to RI format (real, imaginary)
    /// @param s_param Complex S-parameter
    /// @return Pair of (real_part, imaginary_part)
    static std::pair<Real, Real> to_ri_format(const Complex& s_param);

    /// Get format string for Touchstone header
    /// @param format Format enum
    /// @return Format string ("MA", "DB", or "RI")
    static std::string get_format_string(Format format);

    /// Determine file extension from number of ports
    /// @param num_ports Number of ports
    /// @return Extension (e.g., ".s2p", ".s4p")
    static std::string get_extension(std::uint32_t num_ports);
};

} // namespace kirimaspice::output
