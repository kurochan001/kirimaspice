// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/util/units.hpp"
#include <exprtk.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <array>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace kirimaspice::util {

/// The names of a .FUNC's arguments, from its list "a, b, c"
inline std::vector<std::string> split_function_arguments(const std::string& list) {
    std::vector<std::string> result;
    std::string current;
    auto flush = [&] {
        current.erase(0, current.find_first_not_of(" \t"));
        current.erase(current.find_last_not_of(" \t") + 1);
        if (!current.empty()) {
            result.push_back(current);
        }
        current.clear();
    };
    for (const char c : list) {
        if (c == ',') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return result;
}

/// One .FUNC, from its data "args|expression" (SimulationConfig::global_funcs),
/// as a function of ExprTk with one to three arguments. Its body sees its
/// arguments, ExprTk's constants and e.
class ExpressionFunction : public exprtk::ifunction<Real> {
public:
    static constexpr std::size_t kMaxArguments = 3;

    explicit ExpressionFunction(const std::string& data)
        : exprtk::ifunction<Real>(arity_of(data)) {
        const std::size_t pipe = data.find('|');
        names_ = split_function_arguments(data.substr(0, pipe));
        body_ = data.substr(pipe + 1);
        body_.erase(0, body_.find_first_not_of(" \t"));
        body_.erase(body_.find_last_not_of(" \t") + 1);
        constants_.add_constants();
        constants_.add_constant("e", 2.718281828459045);
    }

    Real operator()(const Real& a) override { return call({a, 0.0, 0.0}); }
    Real operator()(const Real& a, const Real& b) override { return call({a, b, 0.0}); }
    Real operator()(const Real& a, const Real& b, const Real& c) override {
        return call({a, b, c});
    }

    /// The number of arguments of the data "args|expression"; an error for
    /// data without the separator or with no or more than three arguments
    static std::size_t arity_of(const std::string& data) {
        const std::size_t pipe = data.find('|');
        if (pipe == std::string::npos) {
            throw std::runtime_error("Invalid .FUNC data format");
        }
        const std::size_t n = split_function_arguments(data.substr(0, pipe)).size();
        if (n == 0 || n > kMaxArguments) {
            throw std::runtime_error(
                fmt::format("unsupported argument count {} (1 to {})", n, kMaxArguments));
        }
        return n;
    }

private:
    Real call(const std::array<Real, kMaxArguments>& values) {
        exprtk::symbol_table<Real> table = constants_;
        values_ = values;
        for (std::size_t i = 0; i < names_.size(); ++i) {
            table.add_variable(names_[i], values_[i]);
        }
        exprtk::expression<Real> expression;
        expression.register_symbol_table(table);
        exprtk::parser<Real> parser;
        if (!parser.compile(expand_scale_factors(body_), expression)) {
            throw std::runtime_error(fmt::format("Failed to compile .FUNC expression: {}", body_));
        }
        return expression.value();
    }

    std::vector<std::string> names_;
    std::string body_;
    exprtk::symbol_table<Real> constants_;
    std::array<Real, kMaxArguments> values_{};
};

/// The .FUNC functions of a netlist (SimulationConfig::global_funcs), for
/// every expression in braces: the global .PARAM values, element values,
/// .IF conditions and the braces of a subcircuit body. A function that
/// cannot be made is named in a warning and left out.
class ExpressionFunctions {
public:
    ExpressionFunctions() = default;
    explicit ExpressionFunctions(const std::map<std::string, std::string>& definitions) {
        for (const auto& [name, data] : definitions) {
            try {
                functions_.emplace_back(name, std::make_unique<ExpressionFunction>(data));
            } catch (const std::exception& e) {
                spdlog::warn("Skipping .FUNC '{}': {}", name, e.what());
            }
        }
    }

    /// Add every function to `table`. The functions must outlive the table's use.
    void add_to(exprtk::symbol_table<Real>& table) {
        for (auto& [name, function] : functions_) {
            if (!table.add_function(name, *function)) {
                spdlog::warn("Failed to add .FUNC '{}' to the expression symbols", name);
            }
        }
    }

    /// Remove from `table` every function that add_to added
    void remove_from(exprtk::symbol_table<Real>& table) const {
        for (const auto& [name, function] : functions_) {
            table.remove_function(name);
        }
    }

private:
    std::vector<std::pair<std::string, std::unique_ptr<ExpressionFunction>>> functions_;
};

}  // namespace kirimaspice::util
