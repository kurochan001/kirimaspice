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
#include <utility>
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

class ExpressionFunction;

/// What the body of every .FUNC of a netlist sees beside its arguments: the
/// other functions and the parameters of the scope it is called in
struct ExpressionFunctionScope {
    std::vector<std::pair<std::string, std::unique_ptr<ExpressionFunction>>> functions;
    std::map<std::string, Real> parameters;
};

/// One .FUNC, from its data "args|expression" (SimulationConfig::global_funcs),
/// as a function of ExprTk with one to three arguments. Its body sees, in this
/// order, its arguments, ExprTk's constants and e, the parameters of the
/// scope and the netlist's other functions, as ngspice-45.2 reads them (run
/// 2026-11-28): an argument hides a parameter of its name, and in a
/// subcircuit the parameter is the instance's. A call nested deeper than
/// kMaxDepth, as by a function that calls itself, is an error; ngspice-45.2
/// stops with a crash there.
class ExpressionFunction : public exprtk::ifunction<Real> {
public:
    static constexpr std::size_t kMaxArguments = 3;
    static constexpr int kMaxDepth = 100;

    ExpressionFunction(std::string name, const std::string& data, const ExpressionFunctionScope& scope)
        : exprtk::ifunction<Real>(arity_of(data)), name_(std::move(name)), scope_(scope) {
        const std::size_t pipe = data.find('|');
        names_ = split_function_arguments(data.substr(0, pipe));
        body_ = data.substr(pipe + 1);
        body_.erase(0, body_.find_first_not_of(" \t"));
        body_.erase(body_.find_last_not_of(" \t") + 1);
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
    Real call(std::array<Real, kMaxArguments> values) {
        static thread_local int depth = 0;
        struct Nesting {
            Nesting() { ++depth; }
            ~Nesting() { --depth; }
        } nesting;
        if (depth > kMaxDepth) {
            throw std::runtime_error(fmt::format(
                ".FUNC {} nests more than {} calls: does it call itself?",
                name_, kMaxDepth));
        }
        // A name already in the table is not added again: the arguments come
        // first, so they hide parameters and functions of their names.
        exprtk::symbol_table<Real> table;
        for (std::size_t i = 0; i < names_.size(); ++i) {
            table.add_variable(names_[i], values[i]);
        }
        table.add_constants();
        table.add_constant("e", 2.718281828459045);
        for (const auto& [name, value] : scope_.parameters) {
            if (!table.symbol_exists(name)) {
                table.add_constant(name, value);
            }
        }
        for (const auto& [name, function] : scope_.functions) {
            if (!table.symbol_exists(name)) {
                table.add_function(name, *function);
            }
        }
        exprtk::expression<Real> expression;
        expression.register_symbol_table(table);
        exprtk::parser<Real> parser;
        if (!parser.compile(expand_scale_factors(body_), expression)) {
            throw std::runtime_error(fmt::format("Failed to compile .FUNC expression: {}: {}", body_,
                                                 parser.error()));
        }
        return expression.value();
    }

    std::string name_;
    const ExpressionFunctionScope& scope_;
    std::vector<std::string> names_;
    std::string body_;
};

/// The .FUNC functions of a netlist (SimulationConfig::global_funcs), for
/// every expression in braces and every B expression: the global .PARAM
/// values, element values, .IF conditions and the braces of a subcircuit
/// body. `parameters` are those of the scope the functions are called in. A
/// function that cannot be made is named in a warning and left out.
class ExpressionFunctions {
public:
    ExpressionFunctions() : scope_(std::make_shared<ExpressionFunctionScope>()) {}
    explicit ExpressionFunctions(const std::map<std::string, std::string>& definitions,
                                 std::map<std::string, Real> parameters = {})
        : ExpressionFunctions() {
        scope_->parameters = std::move(parameters);
        for (const auto& [name, data] : definitions) {
            try {
                scope_->functions.emplace_back(name,
                                               std::make_unique<ExpressionFunction>(name, data, *scope_));
            } catch (const std::exception& e) {
                spdlog::warn("Skipping .FUNC '{}': {}", name, e.what());
            }
        }
    }

    /// The parameters that the bodies see from now on
    void set_parameters(std::map<std::string, Real> parameters) {
        scope_->parameters = std::move(parameters);
    }

    /// Add every function to `table`. The functions must outlive the table's use.
    void add_to(exprtk::symbol_table<Real>& table) {
        for (auto& [name, function] : scope_->functions) {
            if (!table.add_function(name, *function)) {
                spdlog::warn("Failed to add .FUNC '{}' to the expression symbols", name);
            }
        }
    }

    /// Remove from `table` every function that add_to added
    void remove_from(exprtk::symbol_table<Real>& table) const {
        for (const auto& [name, function] : scope_->functions) {
            table.remove_function(name);
        }
    }

private:
    // Held where it does not move, as each function refers to it
    std::shared_ptr<ExpressionFunctionScope> scope_;
};

}  // namespace kirimaspice::util
