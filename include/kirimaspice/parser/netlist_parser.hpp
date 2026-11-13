// include/kirimaspice/parser/netlist_parser.hpp
#pragma once
#include "kirimaspice/core/types.hpp"
#include <string>
#include <memory>
#include <stdexcept>
#include <optional>

namespace kirimaspice {

// Forward declarations
namespace circuit {
class CircuitGraph;
}

namespace parser {

/// Parse error exception
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& msg, std::size_t line_number)
        : std::runtime_error(msg), line_(line_number) {}

    std::size_t line() const { return line_; }

private:
    std::size_t line_;
};

/// Netlist parse result
/// Contains circuit graph and simulation configuration
struct NetlistParseResult {
    std::unique_ptr<circuit::CircuitGraph> circuit;
    SimulationConfig config;

    NetlistParseResult() = default;
    NetlistParseResult(std::unique_ptr<circuit::CircuitGraph> circ, SimulationConfig cfg)
        : circuit(std::move(circ)), config(std::move(cfg)) {}
};

/// Netlist parser (PEGTL-based)
class NetlistParser {
public:
    NetlistParser();
    ~NetlistParser();

    /// Parse from file (returns circuit and simulation config)
    /// @param filepath Path to netlist file
    /// @param initial_params Initial parameter values (override .PARAM definitions)
    /// @throws ParseError on parse failure
    NetlistParseResult parse_file(
        const std::string& filepath,
        const std::unordered_map<std::string, Real>& initial_params = {}
    );

    /// Parse from string (for testing)
    /// @param netlist Netlist content
    /// @param initial_params Initial parameter values (override .PARAM definitions)
    NetlistParseResult parse_string(
        const std::string& netlist,
        const std::unordered_map<std::string, Real>& initial_params = {}
    );

    /// Legacy: Parse from file (returns circuit only, for backward compatibility)
    /// @deprecated Use parse_file() instead
    std::unique_ptr<circuit::CircuitGraph> parse_file_legacy(const std::string& filepath);

    /// Legacy: Parse from string (returns circuit only, for backward compatibility)
    /// @deprecated Use parse_string() instead
    std::unique_ptr<circuit::CircuitGraph> parse_string_legacy(const std::string& netlist);

private:
    class Impl;  ///< PIMPL pattern to hide PEGTL details
    std::unique_ptr<Impl> pimpl_;
};

/// Expression evaluator (ExprTk-based)
class ExpressionEvaluator {
public:
    ExpressionEvaluator();
    ~ExpressionEvaluator();

    /// Set variable (e.g., .PARAM definitions)
    void set_variable(const std::string& name, Real value);

    /// Set user-defined functions from .FUNC directives
    void set_functions(const std::map<std::string, std::string>& global_funcs);

    /// Evaluate expression
    /// @throws std::runtime_error on evaluation error
    Real evaluate(const std::string& expression);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace parser
} // namespace kirimaspice
