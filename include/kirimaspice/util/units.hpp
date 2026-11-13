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

// Parse a numeric value with an optional SPICE-style SI prefix.
// - Recognizes MEG (1e6) and single-letter prefixes: T,G,K,M,U,N,P,F (milli/micro/nano/pico/femto)
// - Ignores trailing physical units (A/Hz/Ohm...) so "10mA" -> 0.01, "1kHz" -> 1000
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
        // Preserve original first character to disambiguate M(mega) vs m(milli)
        const char prefix_raw = suffix_part.front();

        // Upper-case for uniform handling of the rest
        std::transform(suffix_part.begin(), suffix_part.end(), suffix_part.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

        // MEG special-case (SPICE convention: mega = 1e6)
        // Also handle MHZ/MOHM etc where uppercase M precedes a unit symbol → mega
        if (suffix_part.rfind("MEG", 0) == 0 ||
            (suffix_part.size() >= 2 && prefix_raw == 'M' &&
             (suffix_part[1] == 'H' || suffix_part[1] == 'O' || suffix_part[1] == 'V' ||
              suffix_part[1] == 'W' || suffix_part[1] == 'S' || suffix_part[1] == 'F'))) {
            scale_factor = 1e6;
        } else {
            // Use raw prefix (case-sensitive) to decide milli vs mega
            char p_raw = prefix_raw;
            if (p_raw == 'm') {
                scale_factor = 1e-3; // milli regardless of following unit (e.g., mOhm)
            } else {
                // Fall back to upper-case prefix handling
                char p = suffix_part.front();
                switch (p) {
                    case 'T': scale_factor = 1e12; break;   // tera
                    case 'G': scale_factor = 1e9;  break;   // giga
                    case 'K': scale_factor = 1e3;  break;   // kilo
                    case 'M': scale_factor = 1e6;  break;   // mega (uppercase M only)
                    case 'U': scale_factor = 1e-6; break;   // micro
                    case 'N': scale_factor = 1e-9; break;   // nano
                    case 'P': scale_factor = 1e-12; break;  // pico
                    case 'F': scale_factor = 1e-15; break;  // femto
                    default: break; // Unrecognized prefix → no scaling
                }
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
