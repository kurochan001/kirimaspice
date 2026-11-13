// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/types.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <stdexcept>

namespace kirimaspice::util {

// Precision loss threshold
// IEEE 754 double has ~15-16 decimal digits of precision
// Warn if the exponent span exceeds this (e.g., femto to tera = 27 orders of magnitude)
constexpr int PRECISION_WARNING_EXPONENT_SPAN = 14;

// Parse a numeric value with an optional scale factor
// (docs/spec/NETLIST_CONVENTIONS.md, rule 1, as in ngspice):
// - Scale factors, in any case: T 1e12, G 1e9, MEG 1e6, K 1e3, MIL 25.4e-6,
//   M 1e-3, U 1e-6, N 1e-9, P 1e-12, F 1e-15, A 1e-18. M is milli whatever
//   its case; mega is MEG.
// - Letters after the number that are not a scale factor, and letters after a
//   scale factor, are ignored: "10mA" -> 0.01, "1kHz" -> 1000, "10MHz" -> 0.01.
// - Handles scientific notation (1e-3) and leading +/-, trims surrounding whitespace.
inline Real parse_value_with_unit(std::string_view str) {
    // Trim whitespace
    auto l = str.find_first_not_of(" \t\r\n");
    auto r = str.find_last_not_of(" \t\r\n");
    if (l == std::string_view::npos) {
        throw std::runtime_error("Invalid number format: empty");
    }
    str = str.substr(l, r - l + 1);

    std::string s(str);

    // Split numeric and suffix at the first non-number/non-exponent character
    std::string num_part;
    std::string suffix_part;

    bool in_exponent = false;
    bool at_start = true;  // Track if we're at the start of the number
    auto it = s.begin();
    for (; it != s.end(); ++it) {
        char c = *it;
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            at_start = false;
            continue;
        }
        // Allow leading +/- sign at the very start
        if (at_start && (c == '+' || c == '-')) {
            at_start = false;
            continue;
        }
        if (c == 'e' || c == 'E') {
            in_exponent = true;
            at_start = false;
            continue;
        }
        if (in_exponent && (c == '+' || c == '-')) {
            in_exponent = false; // sign consumed, keep parsing exponent digits
            continue;
        }
        // First character after the numeric/exponent part
        break;
    }

    num_part.assign(s.begin(), it);
    suffix_part.assign(it, s.end());

    if (num_part.empty()) {
        throw std::runtime_error("Invalid number format: missing digits");
    }

    // Add strtod error checking for overflow/underflow
    errno = 0;  // Clear previous error
    char* endptr = nullptr;
    Real value = std::strtod(num_part.c_str(), &endptr);

    if (errno == ERANGE) {
        throw std::runtime_error("Number overflow/underflow: " + num_part);
    }
    if (endptr == num_part.c_str()) {
        throw std::runtime_error("Invalid number format: " + num_part);
    }

    Real scale_factor = 1.0;  // Track scale for precision warning

    if (!suffix_part.empty()) {
        std::transform(suffix_part.begin(), suffix_part.end(), suffix_part.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        if (suffix_part.rfind("MEG", 0) == 0) {
            scale_factor = 1e6;
        } else if (suffix_part.rfind("MIL", 0) == 0) {
            scale_factor = 25.4e-6;
        } else {
            switch (suffix_part.front()) {
                case 'T': scale_factor = 1e12; break;   // tera
                case 'G': scale_factor = 1e9;  break;   // giga
                case 'K': scale_factor = 1e3;  break;   // kilo
                case 'M': scale_factor = 1e-3; break;   // milli
                case 'U': scale_factor = 1e-6; break;   // micro
                case 'N': scale_factor = 1e-9; break;   // nano
                case 'P': scale_factor = 1e-12; break;  // pico
                case 'F': scale_factor = 1e-15; break;  // femto
                case 'A': scale_factor = 1e-18; break;  // atto
                default: break;  // not a scale factor: ignored
            }
        }
        value *= scale_factor;
    }

    // Warn about potential precision loss
    // Check if the combined exponent span exceeds IEEE 754 double precision
    if (value != 0.0 && scale_factor != 1.0) {
        int value_exponent = static_cast<int>(std::floor(std::log10(std::fabs(value))));
        int original_exponent = static_cast<int>(std::floor(std::log10(std::fabs(value / scale_factor))));
        int exponent_span = std::abs(value_exponent - original_exponent);

        if (exponent_span > PRECISION_WARNING_EXPONENT_SPAN) {
            spdlog::warn("Unit conversion may lose precision: '{}' spans {} orders of magnitude "
                        "(IEEE 754 double precision is ~15 digits)",
                        std::string(str), exponent_span);
        }
    }

    return value;
}

} // namespace kirimaspice::util
