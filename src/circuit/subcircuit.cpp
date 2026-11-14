// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/circuit/subcircuit.cpp
#include "kirimaspice/circuit/subcircuit.hpp"
#include "kirimaspice/core/constants.hpp"
#include "kirimaspice/util/units.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <regex>
#include <sstream>
#include <fmt/format.h>
#include <exprtk.hpp>

namespace kirimaspice::circuit {

//==============================================================================
// SubcircuitRegistry Implementation
//==============================================================================

void SubcircuitRegistry::register_subcircuit(const SubcircuitDefinition& def) {
    // Convert name to upper case for case-insensitive lookup
    std::string name_upper = def.name;
    std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);

    // Check for duplicate definition
    if (definitions_.find(name_upper) != definitions_.end()) {
        throw std::runtime_error(fmt::format(
            "Subcircuit '{}' is already defined (line {})",
            def.name, def.definition_line));
    }

    // Store definition
    definitions_[name_upper] = def;
}

bool SubcircuitRegistry::has_subcircuit(const std::string& name) const {
    std::string name_upper = name;
    std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);
    return definitions_.find(name_upper) != definitions_.end();
}

const SubcircuitDefinition& SubcircuitRegistry::get_subcircuit(const std::string& name) const {
    std::string name_upper = name;
    std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);

    auto it = definitions_.find(name_upper);
    if (it == definitions_.end()) {
        throw std::runtime_error(fmt::format("Subcircuit '{}' is not defined", name));
    }

    return it->second;
}

void SubcircuitRegistry::add_instance(const SubcircuitInstance& instance) {
    // Verify that the subcircuit type is defined
    if (!has_subcircuit(instance.subcircuit_type)) {
        throw std::runtime_error(fmt::format(
            "Subcircuit instance '{}' references undefined subcircuit type '{}' (line {})",
            instance.instance_name, instance.subcircuit_type, instance.instance_line));
    }

    instances_.push_back(instance);
}

std::vector<std::string> SubcircuitRegistry::check_circular_references() const {
    // Build dependency graph: subcircuit_name -> list of used subcircuits
    std::map<std::string, std::vector<std::string>> dependencies;

    for (const auto& [name, def] : definitions_) {
        dependencies[name] = {};

        // Scan internal netlist for X device lines
        for (const auto& line : def.internal_netlist_lines) {
            if (line.empty() || line[0] != 'X' || line.size() < 2) {
                continue;
            }

            // Simple heuristic: Look for subcircuit type name (last token before parameters)
            // X1 in out SUBCKT_NAME [PARAMS...]
            std::istringstream iss(line);
            std::vector<std::string> tokens;
            std::string token;
            while (iss >> token) {
                tokens.push_back(token);
            }

            // Find subcircuit type (first token that doesn't contain '=')
            // Skip instance name (tokens[0] = "X1", "X2", etc.)
            for (size_t i = 1; i < tokens.size(); ++i) {
                if (tokens[i].find('=') == std::string::npos) {
                    // This might be the subcircuit type
                    std::string possible_subckt = tokens[i];
                    std::transform(possible_subckt.begin(), possible_subckt.end(),
                                 possible_subckt.begin(), ::toupper);

                    if (has_subcircuit(possible_subckt) && possible_subckt != name) {
                        dependencies[name].push_back(possible_subckt);
                        break;  // Found subcircuit type, stop searching
                    }
                }
            }
        }
    }

    // Topological sort using DFS (post-order traversal)
    std::map<std::string, int> visited;  // 0=unvisited, 1=visiting, 2=visited
    std::vector<std::string> path;       // Current DFS path (for error messages)
    std::vector<std::string> sorted_order;  // Post-order result (leaves last)

    // Initialize all nodes as unvisited
    for (const auto& [name, _] : definitions_) {
        visited[name] = 0;
    }

    // Perform DFS from each unvisited node
    for (const auto& [name, _] : definitions_) {
        if (visited[name] == 0) {
            dfs_topological_sort(name, visited, dependencies, sorted_order, path);
        }
    }

    // Reverse to get dependency order (leaves first)
    std::reverse(sorted_order.begin(), sorted_order.end());

    return sorted_order;
}

void SubcircuitRegistry::dfs_topological_sort(
    const std::string& subckt_name,
    std::map<std::string, int>& visited,
    const std::map<std::string, std::vector<std::string>>& dependencies,
    std::vector<std::string>& result,
    std::vector<std::string>& path,
    int depth) const {

    // Check recursion depth to prevent stack overflow
    if (depth > core::MAX_SUBCIRCUIT_DEPTH) {
        throw std::runtime_error(fmt::format(
            "Subcircuit nesting depth exceeded maximum ({}) at '{}'. "
            "Check for excessive nesting.",
            core::MAX_SUBCIRCUIT_DEPTH, subckt_name));
    }

    // Mark as visiting
    visited[subckt_name] = 1;
    path.push_back(subckt_name);

    // Visit all dependencies
    if (dependencies.find(subckt_name) != dependencies.end()) {
        for (const auto& dep : dependencies.at(subckt_name)) {
            if (visited[dep] == 1) {
                // Circular reference detected!
                // Build cycle path from the point where cycle starts
                std::string cycle_path;
                bool in_cycle = false;
                for (const auto& node : path) {
                    if (node == dep) {
                        in_cycle = true;
                    }
                    if (in_cycle) {
                        if (!cycle_path.empty()) {
                            cycle_path += " -> ";
                        }
                        cycle_path += node;
                    }
                }
                cycle_path += " -> " + dep;

                throw std::runtime_error(fmt::format(
                    "Circular subcircuit reference detected: {}", cycle_path));
            }

            // Recursively visit unvisited dependencies
            if (visited[dep] == 0) {
                dfs_topological_sort(dep, visited, dependencies, result, path, depth + 1);
            }
        }
    }

    // Mark as visited (post-order: after all dependencies)
    visited[subckt_name] = 2;
    path.pop_back();

    // Add to result in post-order (leaves will be last)
    result.push_back(subckt_name);
}

//==============================================================================
// SubcircuitDefinition Parameter Substitution
//==============================================================================

std::vector<std::string> SubcircuitDefinition::substitute_parameters(
    const std::map<std::string, Real>& instance_params,
    const std::map<std::string, Real>& global_params) const {

    // Scope, from the lowest priority: the global .PARAM values, the defaults
    // on the .SUBCKT line, the values on the X line. A .PARAM line inside the
    // body adds or replaces values for the lines after it, in this instance
    // only.
    std::map<std::string, Real> scope = global_params;
    for (const auto& [key, value] : default_params) {
        scope[key] = value;
    }
    for (const auto& [key, value] : instance_params) {
        scope[key] = value;
    }

    auto evaluate = [&](const std::string& expression) -> Real {
        exprtk::symbol_table<Real> symbol_table;
        for (const auto& [param_name, value] : scope) {
            symbol_table.add_constant(param_name, value);
        }
        symbol_table.add_constants();

        exprtk::expression<Real> expr;
        expr.register_symbol_table(symbol_table);
        exprtk::parser<Real> parser;
        if (!parser.compile(expression, expr)) {
            throw std::runtime_error(fmt::format(
                "Failed to parse parameter expression '{{{}}}' in subcircuit '{}': {}",
                expression, name, parser.error()));
        }
        return expr.value();
    };

    // NAME=value or NAME={expression}, as on a global .PARAM line
    static const std::regex assignment(R"(([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(\{[^}]*\}|[^\s{}=]+))");
    auto is_param_line = [](const std::string& line) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.size() - first < 6) {
            return false;
        }
        std::string head = line.substr(first, 6);
        std::transform(head.begin(), head.end(), head.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return head == ".PARAM" &&
               (line.size() == first + 6 || std::isspace(static_cast<unsigned char>(line[first + 6])));
    };

    std::vector<std::string> substituted_lines;
    substituted_lines.reserve(internal_netlist_lines.size());

    // Regular expression to match {expression} patterns
    static const std::regex param_pattern(R"(\{([^}]+)\})");

    for (const auto& line : internal_netlist_lines) {
        if (is_param_line(line)) {
            for (auto it = std::sregex_iterator(line.begin(), line.end(), assignment);
                 it != std::sregex_iterator(); ++it) {
                const std::string param_name = (*it)[1].str();
                const std::string value_str = (*it)[2].str();
                try {
                    scope[param_name] = (value_str.front() == '{')
                        ? evaluate(value_str.substr(1, value_str.size() - 2))
                        : util::parse_value_with_unit(value_str);
                } catch (const std::exception& e) {
                    throw std::runtime_error(fmt::format(
                        "Error evaluating .PARAM {} in subcircuit '{}': {}",
                        param_name, name, e.what()));
                }
            }
            continue;  // a .PARAM line is not an element
        }

        auto words_begin = std::sregex_iterator(line.begin(), line.end(), param_pattern);
        auto words_end = std::sregex_iterator();

        if (words_begin == words_end) {
            // No parameters to substitute
            substituted_lines.push_back(line);
            continue;
        }

        // Replace each {expression} by its value
        std::string replaced_line;
        size_t last_pos = 0;
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch m = *i;
            std::string expression = m[1].str();  // Content inside {}

            // Add text before the match
            replaced_line += line.substr(last_pos, m.position() - last_pos);

            try {
                Real result = evaluate(expression);

                // Convert result to string
                // Use scientific notation for very large/small values
                std::ostringstream oss;
                if (std::abs(result) < 1e-3 || std::abs(result) > 1e6) {
                    oss << std::scientific << result;
                } else {
                    oss << result;
                }

                replaced_line += oss.str();

            } catch (const std::exception& e) {
                throw std::runtime_error(fmt::format(
                    "Error evaluating parameter expression '{{{}}}' in subcircuit '{}': {}",
                    expression, name, e.what()));
            }

            last_pos = m.position() + m.length();
        }

        // Add remaining text after last match
        replaced_line += line.substr(last_pos);
        substituted_lines.push_back(replaced_line);
    }

    return substituted_lines;
}

} // namespace kirimaspice::circuit
