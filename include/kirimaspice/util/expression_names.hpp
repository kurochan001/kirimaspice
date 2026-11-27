// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/util/string_utils.hpp"
#include <fmt/format.h>
#include <cctype>
#include <functional>
#include <optional>
#include <string>

namespace kirimaspice::util {

/// `expression` with each name for which `value_of` gives a value written as
/// that value, in parentheses and with every digit of the double: the
/// parameters of a B source's expression, which is compiled later with its
/// node voltages. The arguments of V( and I(, the names of functions and
/// numbers with their scale factors or units are kept as written.
inline std::string substitute_parameter_names(
    const std::string& expression,
    const std::function<std::optional<Real>(const std::string&)>& value_of) {
    auto is_ident = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::string out;
    std::size_t i = 0;
    while (i < expression.size()) {
        const char c = expression[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < expression.size() &&
                                                            std::isdigit(static_cast<unsigned char>(expression[i + 1])))) {
            // A number, its exponent and its scale factor or unit
            std::size_t j = i;
            while (j < expression.size() &&
                   (std::isdigit(static_cast<unsigned char>(expression[j])) || expression[j] == '.')) ++j;
            if (j < expression.size() && (expression[j] == 'e' || expression[j] == 'E') && j + 1 < expression.size() &&
                (std::isdigit(static_cast<unsigned char>(expression[j + 1])) ||
                 ((expression[j + 1] == '+' || expression[j + 1] == '-') && j + 2 < expression.size() &&
                  std::isdigit(static_cast<unsigned char>(expression[j + 2]))))) {
                j += 2;
                while (j < expression.size() && std::isdigit(static_cast<unsigned char>(expression[j]))) ++j;
            }
            while (j < expression.size() && std::isalpha(static_cast<unsigned char>(expression[j]))) ++j;
            out.append(expression, i, j - i);
            i = j;
            continue;
        }
        if (!std::isalpha(static_cast<unsigned char>(c)) && c != '_') {
            out += c;
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < expression.size() && is_ident(expression[j])) ++j;
        const std::string name = expression.substr(i, j - i);
        std::size_t k = j;
        while (k < expression.size() && std::isspace(static_cast<unsigned char>(expression[k]))) ++k;
        if (k < expression.size() && expression[k] == '(') {
            if (util::equals_icase(name, "V") || util::equals_icase(name, "I")) {
                // V(node[, node]) or I(source): its arguments as written
                const std::size_t close = expression.find(')', k);
                const std::size_t end = (close == std::string::npos) ? expression.size() : close + 1;
                out.append(expression, i, end - i);
                i = end;
                continue;
            }
            out += name;  // a function: its arguments are read on
            i = j;
            continue;
        }
        if (const auto value = value_of(name)) {
            out += fmt::format("({:.17g})", *value);
        } else {
            out += name;
        }
        i = j;
    }
    return out;
}

}  // namespace kirimaspice::util
