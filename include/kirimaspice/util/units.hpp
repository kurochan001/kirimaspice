// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/types.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <string_view>
#include <stdexcept>

namespace kirimaspice::util {

// Parse a numeric value with an optional scale factor
// (docs/spec/NETLIST_CONVENTIONS.md, rule 1, as in ngspice):
// - Scale factors, in any case: T 1e12, G 1e9, MEG 1e6, K 1e3, MIL 25.4e-6,
//   M 1e-3, U 1e-6, N 1e-9, P 1e-12, F 1e-15, A 1e-18. M is milli whatever
//   its case; mega is MEG.
// - Letters after the number that are not a scale factor, and letters after a
//   scale factor, are ignored: "10mA" -> 0.01, "1kHz" -> 1000, "10MHz" -> 0.01.
// - Anything else after the number is an error, not dropped: "2*1k", "1.5.3k",
//   "1k5" and "2k_x" are not numbers (ngspice-47 stops on the first and reads
//   the others as 1.5, 1k and 2k). A token that is not a number may be an
//   expression, which the caller evaluates.
// - Handles scientific notation (1e-3) and leading +/-, trims surrounding whitespace.
// - The scale factor multiplies the number in double precision. IEEE 754
//   rounds the product correctly, so its relative error is one rounding,
//   plus that of the factor itself, whatever the factor's exponent.
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

    // What strtod leaves of the numeric part ("1.5.3" leaves ".3"; "1e"
    // leaves "e") belongs with the suffix, and only letters may follow the
    // number: a scale factor and a unit
    const std::string rest = std::string(endptr) + suffix_part;
    if (!std::all_of(rest.begin(), rest.end(),
                     [](unsigned char c) { return std::isalpha(c) != 0; })) {
        throw std::runtime_error("Invalid number format: '" + s +
                                 "' (only a scale factor and a unit, letters, may follow the number)");
    }

    Real scale_factor = 1.0;

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

    return value;
}

// The value of a .PARAM assignment: an expression in braces; a number with
// an optional scale factor (parse_value_with_unit); or else an expression
// without braces, as ngspice-47 reads ".param r = 2*1000" (r = 2000), not the
// number at its start. evaluate(expression) gives the value of an
// expression, or throws. The .PARAM lines of the top level
// (parse_param_directive) and of a subcircuit body
// (SubcircuitDefinition::substitute_parameters) read their values here.
template <typename Evaluate>
Real read_parameter_value(const std::string& text, Evaluate&& evaluate) {
    if (text.size() >= 2 && text.front() == '{' && text.back() == '}') {
        return evaluate(text.substr(1, text.size() - 2));
    }
    try {
        return parse_value_with_unit(text);
    } catch (const std::runtime_error&) {
        return evaluate(text);
    }
}

} // namespace kirimaspice::util
