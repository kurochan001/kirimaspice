// src/circuit/subcircuit.cpp
#include "kirimaspice/circuit/subcircuit.hpp"
#include "kirimaspice/core/constants.hpp"
#include <algorithm>
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
    const std::map<std::string, Real>& instance_params) const {

    // Merge parameters: instance parameters override default parameters
    std::map<std::string, Real> merged_params = default_params;
    for (const auto& [key, value] : instance_params) {
        merged_params[key] = value;  // Override default with instance value
    }

    // Setup ExprTk symbol table with all parameters
    exprtk::symbol_table<Real> symbol_table;
    for (const auto& [name, value] : merged_params) {
        // ExprTk requires non-const variables, so we create a copy
        // We'll use a map to store the actual values
        symbol_table.add_constant(name, value);
    }

    // Add standard constants
    symbol_table.add_constants();

    std::vector<std::string> substituted_lines;
    substituted_lines.reserve(internal_netlist_lines.size());

    // Regular expression to match {expression} patterns
    std::regex param_pattern(R"(\{([^}]+)\})");

    for (const auto& line : internal_netlist_lines) {
        std::string result_line = line;
        std::smatch match;
        std::string search_str = result_line;
        std::string::const_iterator search_start(search_str.cbegin());

        // Find and replace all {expression} patterns
        std::string replaced_line;
        auto words_begin = std::sregex_iterator(line.begin(), line.end(), param_pattern);
        auto words_end = std::sregex_iterator();

        if (words_begin == words_end) {
            // No parameters to substitute
            substituted_lines.push_back(line);
            continue;
        }

        // Process each match
        size_t last_pos = 0;
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::smatch m = *i;
            std::string expression = m[1].str();  // Content inside {}

            // Add text before the match
            replaced_line += line.substr(last_pos, m.position() - last_pos);

            try {
                // Compile and evaluate expression with ExprTk
                exprtk::expression<Real> expr;
                expr.register_symbol_table(symbol_table);

                exprtk::parser<Real> parser;
                if (!parser.compile(expression, expr)) {
                    throw std::runtime_error(fmt::format(
                        "Failed to parse parameter expression '{{{}}}' in subcircuit '{}': {}",
                        expression, name, parser.error()));
                }

                Real result = expr.value();

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
