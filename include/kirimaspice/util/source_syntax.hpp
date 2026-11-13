// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/util/string_utils.hpp"

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace kirimaspice::util {

/// True if a token of an element line holds a value: a number or a brace
/// expression, as opposed to a keyword such as AC or PULSE(...).
inline bool is_value_token(const std::string& token) {
    if (token.empty()) return false;
    const unsigned char c = static_cast<unsigned char>(token.front());
    return std::isdigit(c) || c == '.' || c == '+' || c == '-' || c == '{';
}

/// The AC input of an independent source (docs/spec/NETLIST_CONVENTIONS.md,
/// rule 2): only a source whose line has the keyword AC is an input; AC
/// without a magnitude means 1, and the phase (degrees) defaults to 0.
struct AcInput {
    Real magnitude = 0.0;
    Real phase = 0.0;
};

/// Read "AC [<magnitude> [<phase>]]" at tokens[idx]. If tokens[idx] is not AC,
/// the source is not an AC input and idx is returned unchanged; otherwise the
/// index after the AC specification is returned. read_value turns a value
/// token into a number.
template <typename ReadValue>
std::size_t read_ac_input(const std::vector<std::string>& tokens, std::size_t idx,
                          AcInput& ac, ReadValue&& read_value) {
    ac = AcInput{};
    if (idx >= tokens.size() || !equals_icase(tokens[idx], "AC")) {
        return idx;
    }
    ++idx;
    ac.magnitude = 1.0;
    if (idx < tokens.size() && is_value_token(tokens[idx])) {
        ac.magnitude = read_value(tokens[idx++]);
        if (idx < tokens.size() && is_value_token(tokens[idx])) {
            ac.phase = read_value(tokens[idx++]);
        }
    }
    return idx;
}

}  // namespace kirimaspice::util
