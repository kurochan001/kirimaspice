// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/parser/netlist_parser.cpp
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/circuit/subcircuit.hpp"
#include "kirimaspice/core/constants.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/mosfet_level3_params.hpp"
#include "kirimaspice/devices/mosfet_bsim1_params.hpp"  // BSIM1 (Level 9)
#include "kirimaspice/devices/mosfet_bsim3_params.hpp"
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include "kirimaspice/devices/tline_lossless.hpp"
#include "kirimaspice/devices/tline_lossy.hpp"
#include <tao/pegtl.hpp>
#include <exprtk.hpp>
#include <spdlog/spdlog.h>
#include <fmt/ranges.h>  // fmt::join
#include <fstream>
#include <sstream>
#include <cmath>
#include <cctype>
#include <optional>  // For safe_stod/safe_stoi
#include <algorithm>
#include <limits>
#include <regex>
#include <filesystem>
#include <set>

#include "kirimaspice/util/units.hpp"
#include "kirimaspice/util/expression_constants.hpp"
#include "kirimaspice/util/string_utils.hpp"
#include "kirimaspice/util/source_syntax.hpp"

namespace kirimaspice::parser {
namespace pegtl = tao::pegtl;

//==============================================================================
// Helper Functions
//==============================================================================

// Safe string-to-number conversion with exception handling
// Returns std::nullopt on parse failure instead of throwing
inline std::optional<double> safe_stod(const std::string& str, const std::string& param_name = "") {
    try {
        size_t pos = 0;
        double value = std::stod(str, &pos);
        // Check if entire string was consumed (no trailing garbage)
        if (pos != str.size()) {
            spdlog::warn("Invalid numeric value '{}' for {}: trailing characters", str, param_name);
            return std::nullopt;
        }
        return value;
    } catch (const std::invalid_argument&) {
        spdlog::warn("Invalid numeric value '{}' for {}: not a number", str, param_name);
        return std::nullopt;
    } catch (const std::out_of_range&) {
        spdlog::warn("Numeric value '{}' for {} out of range", str, param_name);
        return std::nullopt;
    }
}

// The number of points of an AC sweep, per decade, per octave or in all: a
// whole number of at least 1. .AC and the ac control command both read it
// here.
static std::uint32_t read_ac_point_count(const std::string& token) {
    std::size_t end = 0;
    long value = 0;
    try {
        value = std::stol(token, &end);
    } catch (const std::exception&) {
        end = 0;
    }
    if (end != token.size() || value < 1 || value > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error(fmt::format(
            "the number of points of an AC sweep must be a whole number of at least 1, not '{}'",
            token));
    }
    return static_cast<std::uint32_t>(value);
}

inline std::optional<int> safe_stoi(const std::string& str, const std::string& param_name = "") {
    try {
        size_t pos = 0;
        int value = std::stoi(str, &pos);
        // Check if entire string was consumed (no trailing garbage)
        if (pos != str.size()) {
            spdlog::warn("Invalid integer value '{}' for {}: trailing characters", str, param_name);
            return std::nullopt;
        }
        return value;
    } catch (const std::invalid_argument&) {
        spdlog::warn("Invalid integer value '{}' for {}: not a number", str, param_name);
        return std::nullopt;
    } catch (const std::out_of_range&) {
        spdlog::warn("Integer value '{}' for {} out of range", str, param_name);
        return std::nullopt;
    }
}

using kirimaspice::util::parse_value_with_unit;

// The text of a netlist with each line ended by a line feed alone: the
// carriage return of a CRLF line end is removed, as ngspice-47 reads such a
// netlist. Every text the parser reads goes through here once, as it is
// read (read_netlist_file, parse_string), so that no '\r' is left at the end
// of a line to be joined into the line a '+' line continues, or to keep a
// directive such as .ENDIF from being recognized.
static std::string without_carriage_returns(std::string text) {
    std::size_t out = 0;
    for (std::size_t in = 0; in < text.size(); ++in) {
        if (text[in] == '\r' && (in + 1 == text.size() || text[in + 1] == '\n')) {
            continue;
        }
        text[out++] = text[in];
    }
    text.resize(out);
    return text;
}

// The whole text of a netlist file, or of a file it includes
// (without_carriage_returns)
static std::string read_netlist_file(std::ifstream& file) {
    std::stringstream buffer;
    buffer << file.rdbuf();
    return without_carriage_returns(buffer.str());
}

// Preprocess continuation lines
// SPICE syntax: lines starting with '+' are continuations of previous line
// @param content: netlist content with potential continuation lines
// @return: netlist with continuation lines joined to previous lines
std::string preprocess_continuations(const std::string& content) {
    std::istringstream input(content);
    std::ostringstream output;
    std::string line;
    std::string accumulated_line;
    // Comment lines and lines of blanks met while a line is being continued:
    // they do not end it, and are written after it
    std::vector<std::string> held_lines;
    bool first_line = true;

    auto flush = [&]() {
        if (!accumulated_line.empty()) {
            output << accumulated_line << '\n';
            accumulated_line.clear();
        }
        for (const auto& held : held_lines) {
            output << held << '\n';
        }
        held_lines.clear();
    };

    while (std::getline(input, line)) {
        // The first line that is not empty is the title, whatever it holds,
        // ';' included (leading empty lines are skipped; ngspice takes an
        // empty first line as the title, KNOWN_ISSUES). A '+' line after it
        // continues the title, so it is not an element, as in ngspice-47.
        if (first_line) {
            if (!line.empty()) {
                first_line = false;
                accumulated_line = line;
            }
            continue;
        }

        // Remove inline comments (after ';')
        size_t comment_pos = line.find(';');
        std::string line_no_comment = (comment_pos != std::string::npos)
            ? line.substr(0, comment_pos)
            : line;

        // Trim whitespace
        std::string trimmed = line_no_comment;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), trimmed.end());

        // Check if this is a continuation line (starts with '+')
        if (!trimmed.empty() && trimmed[0] == '+') {
            // Remove leading '+' and trim again
            std::string continuation = trimmed.substr(1);
            continuation.erase(continuation.begin(), std::find_if(continuation.begin(), continuation.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));

            // Append to accumulated line with space separator
            if (!accumulated_line.empty()) {
                accumulated_line += " " + continuation;
            } else {
                // Continuation without previous line - treat as regular line (parser will warn)
                accumulated_line = continuation;
            }
        } else if (!accumulated_line.empty() && (trimmed.empty() || trimmed[0] == '*')) {
            // A comment line or a line of blanks inside a continued line
            if (!line.empty()) {
                held_lines.push_back(line);
            }
        } else {
            // Not a continuation line: write the accumulated one first
            flush();

            // Start accumulating this line, without its ';' comment, so that
            // a continuation does not end up inside the comment
            if (!line.empty()) {
                accumulated_line = (trimmed.empty() || trimmed[0] == '*') ? line : line_no_comment;
            }
        }
    }

    flush();
    return output.str();
}

// Preprocess .INCLUDE directives recursively
// @param content: netlist content (may contain .INCLUDE directives)
// @param base_path: base directory for resolving relative paths
// @param included_files: set of already included files (for circular detection)
// @param depth: current include depth (for depth limit)
// @return: preprocessed netlist with all .INCLUDE directives expanded
std::string preprocess_includes(
    const std::string& content,
    const std::filesystem::path& base_path,
    std::set<std::filesystem::path>& included_files,
    int depth = 0)
{
    // Depth limit: 10 levels max (ngspice-compatible)
    constexpr int MAX_INCLUDE_DEPTH = 10;
    if (depth > MAX_INCLUDE_DEPTH) {
        throw std::runtime_error(
            fmt::format("Include depth limit exceeded (max {})", MAX_INCLUDE_DEPTH));
    }

    std::istringstream input(content);
    std::ostringstream output;
    std::string line;
    int line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;

        // Remove inline comments
        size_t comment_pos = line.find(';');
        std::string line_no_comment = (comment_pos != std::string::npos)
            ? line.substr(0, comment_pos)
            : line;

        // Trim whitespace
        std::string trimmed = line_no_comment;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), trimmed.end());

        // Check for .INCLUDE directive, or its short form .INC (as in ngspice)
        std::string directive_upper = trimmed.substr(0, trimmed.find_first_of(" \t"));
        std::transform(directive_upper.begin(), directive_upper.end(),
                       directive_upper.begin(), ::toupper);
        {
            if (directive_upper == ".INCLUDE" || directive_upper == ".INC") {
                // Extract filepath (everything after the keyword)
                std::string filepath_str = trimmed.substr(directive_upper.size());

                // Trim leading/trailing whitespace
                filepath_str.erase(filepath_str.begin(), std::find_if(filepath_str.begin(), filepath_str.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                filepath_str.erase(std::find_if(filepath_str.rbegin(), filepath_str.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), filepath_str.end());

                // Remove quotes if present
                if (filepath_str.size() >= 2 &&
                    ((filepath_str.front() == '"' && filepath_str.back() == '"') ||
                     (filepath_str.front() == '\'' && filepath_str.back() == '\''))) {
                    filepath_str = filepath_str.substr(1, filepath_str.size() - 2);
                }

                if (filepath_str.empty()) {
                    throw ParseError("Invalid .INCLUDE syntax: missing filepath", line_number);
                }

                // Resolve filepath (absolute or relative to base_path)
                std::filesystem::path include_path(filepath_str);
                if (!include_path.is_absolute()) {
                    include_path = base_path / include_path;
                }

                // Canonicalize path (resolve .. and symlinks)
                std::filesystem::path canonical_path;
                try {
                    canonical_path = std::filesystem::canonical(include_path);
                } catch (const std::filesystem::filesystem_error& e) {
                    throw ParseError(
                        fmt::format("Cannot resolve include path '{}': {}",
                                  include_path.string(), e.what()),
                        line_number);
                }

                // Check for circular includes
                if (included_files.count(canonical_path)) {
                    throw ParseError(
                        fmt::format("Circular include detected: '{}'", canonical_path.string()),
                        line_number);
                }

                // Check if file exists
                if (!std::filesystem::exists(canonical_path)) {
                    throw ParseError(
                        fmt::format("Include file not found: '{}'", canonical_path.string()),
                        line_number);
                }

                // Check if readable
                if (!std::filesystem::is_regular_file(canonical_path)) {
                    throw ParseError(
                        fmt::format("Include path is not a regular file: '{}'", canonical_path.string()),
                        line_number);
                }

                // Read included file
                std::ifstream include_file(canonical_path);
                if (!include_file) {
                    throw ParseError(
                        fmt::format("Cannot open include file: '{}'", canonical_path.string()),
                        line_number);
                }

                const std::string include_content = read_netlist_file(include_file);

                spdlog::debug("Including file: {} (depth {})", canonical_path.string(), depth);

                // Add to included files set
                included_files.insert(canonical_path);

                // Recursively preprocess included file
                std::string processed_content = preprocess_includes(
                    include_content,
                    canonical_path.parent_path(),  // Base path for included file
                    included_files,
                    depth + 1
                );

                // Remove from set (allow re-including from different branches)
                included_files.erase(canonical_path);

                // Insert processed content
                output << processed_content;

                // Continue to next line (don't output .INCLUDE directive itself)
                continue;
            }
        }

        // Not an .INCLUDE directive, output as-is
        output << line << '\n';
    }

    return output.str();
}

// A directive of .IF preprocessing: its name in upper case, .IF, .ELIF, .ELSE
// or .ENDIF, and the text after it with leading blanks removed
struct ConditionalDirective {
    std::string name;
    std::string rest;
};

// The directive of .IF preprocessing that a line (without its comment and
// surrounding blanks) holds, if any. As in ngspice-47, the name ends at a
// blank or at the '(' of the condition (.if(0)), and .ELSEIF is another
// spelling of .ELIF. .ELSE and .ENDIF stand alone on their lines.
static std::optional<ConditionalDirective> conditional_directive(const std::string& trimmed) {
    if (trimmed.empty() || trimmed[0] != '.') {
        return std::nullopt;
    }
    std::size_t end = 1;
    while (end < trimmed.size() && std::isalpha(static_cast<unsigned char>(trimmed[end]))) {
        ++end;
    }
    std::string name = trimmed.substr(0, end);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (name == ".ELSEIF") {
        name = ".ELIF";
    }
    if (name != ".IF" && name != ".ELIF" && name != ".ELSE" && name != ".ENDIF") {
        return std::nullopt;
    }
    if (end < trimmed.size() && trimmed[end] != ' ' && trimmed[end] != '\t' &&
        trimmed[end] != '(') {
        return std::nullopt;
    }
    std::string rest = trimmed.substr(end);
    rest.erase(0, rest.find_first_not_of(" \t"));
    if ((name == ".ELSE" || name == ".ENDIF") && !rest.empty()) {
        return std::nullopt;
    }
    return ConditionalDirective{name, rest};
}

// Preprocess .IF/.ELIF/.ELSE/.ENDIF conditional directives
// @param content: netlist content (may contain conditional directives)
// @param evaluator: expression evaluator for condition evaluation
// @return: preprocessed netlist with conditional blocks resolved
std::string preprocess_conditionals(
    const std::string& content,
    parser::ExpressionEvaluator& evaluator)
{
    std::istringstream input(content);
    std::ostringstream output;
    std::string line;
    int line_number = 0;

    // Stack to track nested conditionals
    // Each entry: {condition_met, else_taken, inside_true_branch}
    struct ConditionalState {
        bool condition_met;  // True if any condition (.IF or .ELIF) was met
        bool else_taken;     // True if we're in .ELSE block
        bool inside_true_branch;  // True if we're currently in a true branch
    };
    std::vector<ConditionalState> cond_stack;

    while (std::getline(input, line)) {
        ++line_number;

        // Remove inline comments
        size_t comment_pos = line.find(';');
        std::string line_no_comment = (comment_pos != std::string::npos)
            ? line.substr(0, comment_pos)
            : line;

        // Trim whitespace
        std::string trimmed = line_no_comment;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

        if (trimmed.empty()) {
            // Empty line - output if not in false branch
            if (cond_stack.empty() || cond_stack.back().inside_true_branch) {
                output << line << '\n';
            }
            continue;
        }

        // Check for conditional directives (conditional_directive)
        const std::optional<ConditionalDirective> directive = conditional_directive(trimmed);
        const std::string directive_upper = directive ? directive->name : std::string();

        if (directive_upper == ".IF") {
            // .IF directive
            const std::string& condition = directive->rest;

            if (condition.empty()) {
                throw ParseError(".IF directive missing condition", line_number);
            }

            // Evaluate condition only if parent branch is true
            bool eval_result = false;
            if (cond_stack.empty() || cond_stack.back().inside_true_branch) {
                try {
                    Real value = evaluator.evaluate(condition);
                    eval_result = (value != 0.0);  // Non-zero is true
                    spdlog::debug("Line {}: .IF {} evaluated to {}", line_number, condition, eval_result);
                } catch (const std::exception& e) {
                    throw ParseError(
                        fmt::format("Failed to evaluate .IF condition '{}': {}", condition, e.what()),
                        line_number);
                }
            }

            // Push new conditional state
            cond_stack.push_back({eval_result, false, eval_result});
            continue;  // Don't output .IF directive itself

        } else if (directive_upper == ".ELIF") {
            // .ELIF directive (or .ELSEIF)
            if (cond_stack.empty()) {
                throw ParseError(".ELIF without matching .IF", line_number);
            }

            if (cond_stack.back().else_taken) {
                throw ParseError(".ELIF after .ELSE", line_number);
            }

            const std::string& condition = directive->rest;

            if (condition.empty()) {
                throw ParseError(".ELIF directive missing condition", line_number);
            }

            // Evaluate only if no previous condition was met
            bool eval_result = false;
            if (!cond_stack.back().condition_met) {
                // Only evaluate if parent branch is true
                if (cond_stack.size() == 1 || cond_stack[cond_stack.size() - 2].inside_true_branch) {
                    try {
                        Real value = evaluator.evaluate(condition);
                        eval_result = (value != 0.0);
                        spdlog::debug("Line {}: .ELIF {} evaluated to {}", line_number, condition, eval_result);
                    } catch (const std::exception& e) {
                        throw ParseError(
                            fmt::format("Failed to evaluate .ELIF condition '{}': {}", condition, e.what()),
                            line_number);
                    }
                }
            }

            // Update state
            if (eval_result) {
                cond_stack.back().condition_met = true;
                cond_stack.back().inside_true_branch = true;
            } else {
                cond_stack.back().inside_true_branch = false;
            }
            continue;  // Don't output .ELIF directive itself

        } else if (directive_upper == ".ELSE") {
            // .ELSE directive
            if (cond_stack.empty()) {
                throw ParseError(".ELSE without matching .IF", line_number);
            }

            if (cond_stack.back().else_taken) {
                throw ParseError("Multiple .ELSE in same conditional block", line_number);
            }

            cond_stack.back().else_taken = true;

            // Enter .ELSE branch only if no condition was met and the
            // enclosing branch is active
            const bool parent_active =
                cond_stack.size() == 1 || cond_stack[cond_stack.size() - 2].inside_true_branch;
            cond_stack.back().inside_true_branch = parent_active && !cond_stack.back().condition_met;
            continue;  // Don't output .ELSE directive itself

        } else if (directive_upper == ".ENDIF") {
            // .ENDIF directive
            if (cond_stack.empty()) {
                throw ParseError(".ENDIF without matching .IF", line_number);
            }

            cond_stack.pop_back();
            continue;  // Don't output .ENDIF directive itself

        } else {
            // Regular line - output if in true branch
            if (cond_stack.empty() || cond_stack.back().inside_true_branch) {
                output << line << '\n';
            }
        }
    }

    // Check for unclosed conditionals
    if (!cond_stack.empty()) {
        throw ParseError(
            fmt::format("{} unclosed .IF directive(s)", cond_stack.size()),
            line_number);
    }

    return output.str();
}

//==============================================================================
// .CONTROL/.ENDC Block Preprocessor
//==============================================================================

/// Parse a single control command line
/// Recognizes command type and extracts arguments
static ControlCommand parse_control_command(const std::string& line) {
    ControlCommand cmd;
    cmd.raw_command = line;
    cmd.type = ControlCommandType::UNKNOWN;

    // Tokenize the line
    std::vector<std::string> tokens = util::split_netlist_tokens(line);
    if (tokens.empty()) {
        return cmd;
    }

    // Get command keyword (case-insensitive)
    std::string keyword = tokens[0];
    std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::toupper);

    // Recognize command type
    if (keyword == "DC") {
        cmd.type = ControlCommandType::DC;
        // dc <source> <start> <stop> <step>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "AC") {
        cmd.type = ControlCommandType::AC;
        // ac <type> <npoints> <fstart> <fstop>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "TRAN") {
        cmd.type = ControlCommandType::TRAN;
        // tran <tstep> <tstop> [<tstart>] [<tmax>]
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "PRINT") {
        cmd.type = ControlCommandType::PRINT;
        // print <signal> [<signal> ...]
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "ECHO") {
        cmd.type = ControlCommandType::ECHO;
        // echo <message> - join remaining tokens as single message
        if (tokens.size() > 1) {
            std::string message = tokens[1];
            for (size_t i = 2; i < tokens.size(); ++i) {
                message += " " + tokens[i];
            }
            cmd.args.push_back(message);
        }
    } else if (keyword == "LET") {
        cmd.type = ControlCommandType::LET;
        // let <var> = <expression>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "MEAS" || keyword == "MEASURE") {
        cmd.type = ControlCommandType::MEAS;
        // meas <analysis> <name> <type> <signal> ...
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "ALTER") {
        cmd.type = ControlCommandType::ALTER;
        // alter <device> <param>=<value>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "RUN") {
        cmd.type = ControlCommandType::RUN;
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "PLOT") {
        cmd.type = ControlCommandType::PLOT;
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "SET") {
        cmd.type = ControlCommandType::SET;
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "IF") {
        cmd.type = ControlCommandType::IF;
        // if <condition>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "ELSE") {
        cmd.type = ControlCommandType::ELSE;
        // else (no arguments)
    } else if (keyword == "END") {
        cmd.type = ControlCommandType::END;
        // end (no arguments)
    } else if (keyword == "FOREACH") {
        cmd.type = ControlCommandType::FOREACH;
        // foreach <var> <list...>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "WHILE") {
        cmd.type = ControlCommandType::WHILE;
        // while <condition>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "REPEAT") {
        cmd.type = ControlCommandType::REPEAT;
        // repeat <count>
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
    } else if (keyword == "QUIT" || keyword == "EXIT") {
        cmd.type = ControlCommandType::QUIT;
    } else {
        // Unknown command - keep as UNKNOWN
        cmd.args = std::vector<std::string>(tokens.begin() + 1, tokens.end());
        spdlog::warn("Unknown .CONTROL command: {}", keyword);
    }

    spdlog::debug("Parsed control command: type={}, args={}",
                  static_cast<int>(cmd.type), cmd.args.size());
    return cmd;
}

/// Preprocess .CONTROL/.ENDC blocks
/// Extracts control block commands and returns netlist with blocks removed
/// @param content: netlist content (may contain .CONTROL/.ENDC blocks)
/// @param control_block: output ControlBlock structure (will be populated if block found)
/// @return: preprocessed netlist with control blocks removed
std::string preprocess_control_blocks(
    const std::string& content,
    ControlBlock& control_block)
{
    std::istringstream input(content);
    std::ostringstream output;
    std::string line;
    int line_number = 0;
    bool inside_control = false;
    bool found_control = false;

    while (std::getline(input, line)) {
        ++line_number;

        // Remove inline comments
        size_t comment_pos = line.find(';');
        std::string line_no_comment = (comment_pos != std::string::npos)
            ? line.substr(0, comment_pos)
            : line;

        // Trim whitespace
        std::string trimmed = line_no_comment;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

        if (trimmed.empty()) {
            // Empty line - output if not in control block
            if (!inside_control) {
                output << line << '\n';
            }
            continue;
        }

        // Check for .CONTROL/.ENDC directives
        std::string directive_upper = trimmed;
        std::transform(directive_upper.begin(), directive_upper.end(), directive_upper.begin(), ::toupper);

        if (directive_upper == ".CONTROL") {
            // .CONTROL directive
            if (inside_control) {
                throw ParseError("Nested .CONTROL blocks are not allowed", line_number);
            }
            if (found_control) {
                throw ParseError("Multiple .CONTROL blocks are not supported", line_number);
            }
            inside_control = true;
            found_control = true;
            control_block.enabled = true;
            continue;  // Don't output .CONTROL directive

        } else if (directive_upper == ".ENDC") {
            // .ENDC directive
            if (!inside_control) {
                throw ParseError(".ENDC without matching .CONTROL", line_number);
            }
            inside_control = false;
            continue;  // Don't output .ENDC directive

        } else if (inside_control) {
            // Command line inside control block - parse it
            ControlCommand cmd = parse_control_command(trimmed);
            control_block.commands.push_back(cmd);

        } else {
            // Regular line - output if not in control block
            output << line << '\n';
        }
    }

    // Check for unclosed control block
    if (inside_control) {
        throw ParseError("Unclosed .CONTROL directive", line_number);
    }

    return output.str();
}

//==============================================================================
// .CONTROL/.ENDC Command Execution
//==============================================================================

/// Find the matching ELSE or END for an IF statement
/// @param commands: command list
/// @param if_index: index of the IF command
/// @param find_else: if true, find ELSE; if false, find END
/// @return: index of matching ELSE/END, or -1 if not found
static int find_matching_block_end(
    const std::vector<ControlCommand>& commands,
    size_t if_index,
    bool find_else)
{
    int nesting_level = 0;
    for (size_t i = if_index + 1; i < commands.size(); ++i) {
        const auto& cmd = commands[i];

        if (cmd.type == ControlCommandType::IF || cmd.type == ControlCommandType::FOREACH ||
            cmd.type == ControlCommandType::WHILE || cmd.type == ControlCommandType::REPEAT) {
            nesting_level++;
        } else if (cmd.type == ControlCommandType::END) {
            if (nesting_level == 0) {
                return static_cast<int>(i);
            }
            nesting_level--;
        } else if (cmd.type == ControlCommandType::ELSE && find_else) {
            if (nesting_level == 0) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;  // Not found
}

/// Execute a range of commands (for FOREACH loop implementation)
/// @param commands: command list
/// @param start_index: start index (inclusive)
/// @param end_index: end index (exclusive)
/// @param evaluator: expression evaluator
/// @param config: simulation configuration
/// @param variables: variable map (will be modified)
/// @return: new current index (for control flow)
static void execute_commands_range(
    const std::vector<ControlCommand>& commands,
    size_t start_index,
    size_t end_index,
    parser::ExpressionEvaluator& evaluator,
    SimulationConfig& config,
    std::map<std::string, Real>& variables,
    circuit::CircuitGraph& circuit);  // Forward declaration

/// Execute control block commands and update SimulationConfig
/// Supports echo, let, dc, ac, tran, print, if/else/end, foreach, alter commands
static void execute_control_commands(
    ControlBlock& control_block,
    parser::ExpressionEvaluator& evaluator,
    SimulationConfig& config,
    circuit::CircuitGraph& circuit)
{
    if (!control_block.enabled || control_block.commands.empty()) {
        return;
    }

    spdlog::info("Executing .CONTROL block with {} commands", control_block.commands.size());

    for (size_t i = 0; i < control_block.commands.size(); ++i) {
        const auto& cmd = control_block.commands[i];

        switch (cmd.type) {
            case ControlCommandType::IF: {
                // IF command: if <condition>
                // Evaluate condition and skip to ELSE or END if false
                if (cmd.args.empty()) {
                    spdlog::warn("IF command missing condition: {}", cmd.raw_command);
                    break;
                }

                // Join all args as the condition expression
                std::string condition = cmd.args[0];
                for (size_t j = 1; j < cmd.args.size(); ++j) {
                    condition += " " + cmd.args[j];
                }

                try {
                    Real result = evaluator.evaluate(condition);
                    bool condition_true = (result != 0.0);  // Non-zero is true
                    spdlog::info("IF: condition='{}' evaluated to {} ({})",
                               condition, result, condition_true ? "true" : "false");

                    if (!condition_true) {
                        // Skip to ELSE (if exists) or END
                        int else_index = find_matching_block_end(control_block.commands, i, true);
                        int end_index = find_matching_block_end(control_block.commands, i, false);

                        if (else_index != -1) {
                            // Skip to ELSE
                            i = static_cast<size_t>(else_index);
                            spdlog::debug("  Skipping to ELSE at index {}", i);
                        } else if (end_index != -1) {
                            // Skip to END
                            i = static_cast<size_t>(end_index);
                            spdlog::debug("  Skipping to END at index {}", i);
                        } else {
                            spdlog::error("IF command without matching END: {}", cmd.raw_command);
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::error("IF condition evaluation failed: {}", e.what());
                }
                break;
            }

            case ControlCommandType::ELSE: {
                // ELSE command: skip to END (reached when IF condition was true)
                int end_index = find_matching_block_end(control_block.commands, i, false);
                if (end_index != -1) {
                    i = static_cast<size_t>(end_index);
                    spdlog::debug("ELSE: Skipping to END at index {}", i);
                } else {
                    spdlog::error("ELSE command without matching END");
                }
                break;
            }

            case ControlCommandType::END: {
                // END command: do nothing (block terminator)
                spdlog::debug("END command reached");
                break;
            }

            case ControlCommandType::ECHO:
                // Echo command: output message to log
                if (!cmd.args.empty()) {
                    spdlog::info("ECHO: {}", cmd.args[0]);
                }
                break;

            case ControlCommandType::LET: {
                // Let command: variable assignment
                // Format: let <var> = <expression>
                if (cmd.args.size() >= 3 && cmd.args[1] == "=") {
                    std::string var_name = cmd.args[0];

                    // Join remaining tokens as expression
                    std::string expression = cmd.args[2];
                    for (size_t i = 3; i < cmd.args.size(); ++i) {
                        expression += " " + cmd.args[i];
                    }

                    try {
                        Real value = evaluator.evaluate(expression);
                        control_block.variables[var_name] = value;
                        evaluator.set_variable(var_name, value);
                        spdlog::info("LET: {} = {} (evaluated: {})", var_name, expression, value);
                    } catch (const std::exception& e) {
                        spdlog::error("LET command failed for '{}': {}", var_name, e.what());
                    }
                } else {
                    spdlog::warn("Invalid LET command syntax: {}", cmd.raw_command);
                }
                break;
            }

            case ControlCommandType::PRINT:
                // Print command: configure output signals
                if (!cmd.args.empty()) {
                    spdlog::info("PRINT: Configuring {} signal(s) for output", cmd.args.size());

                    // Parse analysis type (first arg could be dc/tran/ac)
                    std::string first_arg = cmd.args[0];
                    std::transform(first_arg.begin(), first_arg.end(), first_arg.begin(), ::tolower);

                    size_t signal_start = 0;
                    if (first_arg == "dc" || first_arg == "tran" || first_arg == "ac") {
                        signal_start = 1;  // Skip analysis type
                    }

                    // Add signals to output config
                    for (size_t i = signal_start; i < cmd.args.size(); ++i) {
                        config.output.save_signals.push_back(cmd.args[i]);
                        spdlog::debug("  Signal: {}", cmd.args[i]);
                    }

                    if (!config.output.save_signals.empty()) {
                        config.output.save_all = false;  // Only save specified signals
                    }
                }
                break;

            case ControlCommandType::DC: {
                // DC command: dc <source> <start> <stop> <step>
                if (cmd.args.size() >= 4) {
                    try {
                        config.dc_sweep.source_name = cmd.args[0];
                        config.dc_sweep.start_value = parse_value_with_unit(cmd.args[1]);
                        config.dc_sweep.stop_value = parse_value_with_unit(cmd.args[2]);
                        config.dc_sweep.step_value = parse_value_with_unit(cmd.args[3]);
                        config.dc_sweep_enabled = true;

                        spdlog::info("DC: source={}, start={}, stop={}, step={}",
                                   config.dc_sweep.source_name,
                                   config.dc_sweep.start_value,
                                   config.dc_sweep.stop_value,
                                   config.dc_sweep.step_value);
                    } catch (const std::exception& e) {
                        spdlog::error("DC command parsing failed: {}", e.what());
                    }
                } else {
                    spdlog::warn("Invalid DC command syntax: {}", cmd.raw_command);
                }
                break;
            }

            case ControlCommandType::AC: {
                // AC command: ac <type> <npoints> <fstart> <fstop>
                if (cmd.args.size() >= 4) {
                    try {
                        std::string type_str = cmd.args[0];
                        std::transform(type_str.begin(), type_str.end(), type_str.begin(), ::tolower);

                        if (type_str == "dec") {
                            config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
                        } else if (type_str == "oct") {
                            config.ac.sweep_type = SimulationConfig::AC::SweepType::OCT;
                        } else if (type_str == "lin") {
                            config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
                        } else {
                            throw std::runtime_error("Invalid AC sweep type: " + type_str);
                        }

                        config.ac.num_points = read_ac_point_count(cmd.args[1]);
                        config.ac.start_freq = parse_value_with_unit(cmd.args[2]);
                        config.ac.stop_freq = parse_value_with_unit(cmd.args[3]);
                        config.ac_requested = true;

                        spdlog::info("AC: type={}, npoints={}, fstart={}, fstop={}",
                                   type_str, config.ac.num_points,
                                   config.ac.start_freq, config.ac.stop_freq);
                    } catch (const std::exception& e) {
                        spdlog::error("AC command parsing failed: {}", e.what());
                    }
                } else {
                    spdlog::warn("Invalid AC command syntax: {}", cmd.raw_command);
                }
                break;
            }

            case ControlCommandType::TRAN: {
                // TRAN command: tran <tstep> <tstop> [<tstart>] [<tmax>]
                // Note: tstep is used to set initial_step and max_step
                if (cmd.args.size() >= 2) {
                    try {
                        Real tstep = parse_value_with_unit(cmd.args[0]);
                        Real tstop = parse_value_with_unit(cmd.args[1]);

                        config.transient.initial_step = tstep;
                        config.transient.max_step = tstep;
                        config.transient.end_time = tstop;

                        if (cmd.args.size() >= 3) {
                            config.transient.start_time = parse_value_with_unit(cmd.args[2]);
                        }
                        if (cmd.args.size() >= 4) {
                            // Optional tmax overrides the max_step
                            config.transient.max_step = parse_value_with_unit(cmd.args[3]);
                        }

                        spdlog::info("TRAN: tstep={}, tstop={}, tstart={}, tmax={}",
                                   tstep, tstop,
                                   config.transient.start_time,
                                   config.transient.max_step);
                    } catch (const std::exception& e) {
                        spdlog::error("TRAN command parsing failed: {}", e.what());
                    }
                } else {
                    spdlog::warn("Invalid TRAN command syntax: {}", cmd.raw_command);
                }
                break;
            }

            case ControlCommandType::MEAS:
                spdlog::info("MEAS command recognized (not executed in this phase)");
                break;

            case ControlCommandType::ALTER: {
                // ALTER command: alter <device_name> <param>=<value>
                if (cmd.args.size() < 2) {
                    spdlog::warn("ALTER command requires at least 2 arguments: {}", cmd.raw_command);
                    break;
                }

                std::string device_name = cmd.args[0];
                std::string param_assignment = cmd.args[1];

                // Parse param=value
                size_t eq_pos = param_assignment.find('=');
                if (eq_pos == std::string::npos) {
                    spdlog::error("ALTER command: invalid syntax, expected <param>=<value>: {}", param_assignment);
                    break;
                }

                std::string param_name = param_assignment.substr(0, eq_pos);
                std::string value_str = param_assignment.substr(eq_pos + 1);

                try {
                    // Get device from circuit
                    auto device = circuit.get_device(device_name);
                    if (!device) {
                        spdlog::error("ALTER command: device '{}' not found", device_name);
                        break;
                    }

                    // Parse value with unit support
                    Real value = parse_value_with_unit(value_str);

                    // Set parameter
                    bool success = device->set_parameter(param_name, value);
                    if (success) {
                        spdlog::info("ALTER: device '{}', parameter '{}' = {}", device_name, param_name, value);
                    } else {
                        spdlog::warn("ALTER: failed to set parameter '{}' on device '{}'", param_name, device_name);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("ALTER command failed: {}", e.what());
                }
                break;
            }

            case ControlCommandType::RUN:
                spdlog::info("RUN command recognized (not executed in this phase)");
                break;

            case ControlCommandType::PLOT:
                spdlog::info("PLOT command recognized (not executed in this phase)");
                break;

            case ControlCommandType::SET:
                spdlog::info("SET command recognized (not executed in this phase)");
                break;

            case ControlCommandType::FOREACH: {
                // FOREACH command: foreach <var> <value1> <value2> ...
                if (cmd.args.size() < 2) {
                    spdlog::warn("FOREACH command requires at least 2 arguments: {}", cmd.raw_command);
                    break;
                }

                std::string loop_var = cmd.args[0];
                std::vector<std::string> values(cmd.args.begin() + 1, cmd.args.end());

                // Find matching END
                int end_index = find_matching_block_end(control_block.commands, i, false);
                if (end_index == -1) {
                    spdlog::error("FOREACH command without matching END: {}", cmd.raw_command);
                    break;
                }

                spdlog::info("FOREACH: var={}, values={}", loop_var, values.size());

                // Execute loop body for each value
                for (const auto& value_str : values) {
                    try {
                        // Try to parse value as number
                        Real value = parse_value_with_unit(value_str);
                        control_block.variables[loop_var] = value;
                        evaluator.set_variable(loop_var, value);
                        spdlog::debug("  {} = {}", loop_var, value);

                        // Ensure all variables are set in evaluator before executing commands
                        for (const auto& [var_name, var_value] : control_block.variables) {
                            evaluator.set_variable(var_name, var_value);
                        }

                        // Execute commands in range [i+1, end_index)
                        execute_commands_range(
                            control_block.commands,
                            i + 1,
                            static_cast<size_t>(end_index),
                            evaluator,
                            config,
                            control_block.variables,
                            circuit);
                    } catch (const std::exception& e) {
                        spdlog::error("FOREACH iteration failed for value '{}': {}", value_str, e.what());
                    }
                }

                // Skip to END
                i = static_cast<size_t>(end_index);
                break;
            }

            case ControlCommandType::WHILE: {
                // WHILE command: while <condition>
                if (cmd.args.empty()) {
                    spdlog::warn("WHILE command requires a condition: {}", cmd.raw_command);
                    break;
                }

                // Join all args to form the condition expression
                std::string condition = cmd.args[0];
                for (size_t j = 1; j < cmd.args.size(); ++j) {
                    condition += " " + cmd.args[j];
                }

                // Find matching END
                int end_index = find_matching_block_end(control_block.commands, i, false);
                if (end_index == -1) {
                    spdlog::error("WHILE command without matching END: {}", cmd.raw_command);
                    break;
                }

                spdlog::info("WHILE: condition={}", condition);

                // Execute while loop (with maximum iteration limit for safety)
                const int MAX_ITERATIONS = 10000;
                int iteration = 0;

                while (iteration < MAX_ITERATIONS) {
                    try {
                        // Evaluate condition
                        Real result = evaluator.evaluate(condition);
                        if (result == 0.0) {
                            // Condition is false, exit loop
                            break;
                        }

                        spdlog::debug("  WHILE iteration {}, condition = {}", iteration, result);

                        // Ensure all variables are set in evaluator before executing commands
                        for (const auto& [var_name, var_value] : control_block.variables) {
                            evaluator.set_variable(var_name, var_value);
                        }

                        // Execute commands in range [i+1, end_index)
                        execute_commands_range(
                            control_block.commands,
                            i + 1,
                            static_cast<size_t>(end_index),
                            evaluator,
                            config,
                            control_block.variables,
                            circuit);

                        iteration++;
                    } catch (const std::exception& e) {
                        spdlog::error("WHILE iteration {} failed: {}", iteration, e.what());
                        break;
                    }
                }

                if (iteration >= MAX_ITERATIONS) {
                    spdlog::warn("WHILE loop exceeded maximum iterations ({})", MAX_ITERATIONS);
                }

                // Skip to END
                i = static_cast<size_t>(end_index);
                break;
            }

            case ControlCommandType::REPEAT: {
                // REPEAT command: repeat <count>
                if (cmd.args.empty()) {
                    spdlog::warn("REPEAT command requires a count: {}", cmd.raw_command);
                    break;
                }

                // Parse repeat count
                int repeat_count = 0;
                try {
                    repeat_count = static_cast<int>(parse_value_with_unit(cmd.args[0]));
                } catch (const std::exception& e) {
                    spdlog::error("REPEAT count parse error: {}", e.what());
                    break;
                }

                if (repeat_count <= 0) {
                    spdlog::warn("REPEAT count must be positive: {}", repeat_count);
                    break;
                }

                // Find matching END
                int end_index = find_matching_block_end(control_block.commands, i, false);
                if (end_index == -1) {
                    spdlog::error("REPEAT command without matching END: {}", cmd.raw_command);
                    break;
                }

                spdlog::info("REPEAT: count={}", repeat_count);

                // Execute repeat loop
                for (int iteration = 0; iteration < repeat_count; ++iteration) {
                    try {
                        spdlog::debug("  REPEAT iteration {}/{}", iteration + 1, repeat_count);

                        // Ensure all variables are set in evaluator before executing commands
                        for (const auto& [var_name, var_value] : control_block.variables) {
                            evaluator.set_variable(var_name, var_value);
                        }

                        // Execute commands in range [i+1, end_index)
                        execute_commands_range(
                            control_block.commands,
                            i + 1,
                            static_cast<size_t>(end_index),
                            evaluator,
                            config,
                            control_block.variables,
                            circuit);
                    } catch (const std::exception& e) {
                        spdlog::error("REPEAT iteration {} failed: {}", iteration, e.what());
                        break;
                    }
                }

                // Skip to END
                i = static_cast<size_t>(end_index);
                break;
            }

            case ControlCommandType::QUIT:
                spdlog::info("QUIT command recognized");
                break;

            default:
                spdlog::warn("Unknown command type: {}", cmd.raw_command);
                break;
        }
    }

    spdlog::info(".CONTROL block execution complete");
}

/// Execute a range of commands (for FOREACH loop implementation)
/// This function executes commands in the range [start_index, end_index)
/// It handles nested IF/ELSE/END and FOREACH blocks
static void execute_commands_range(
    const std::vector<ControlCommand>& commands,
    size_t start_index,
    size_t end_index,
    parser::ExpressionEvaluator& evaluator,
    SimulationConfig& config,
    std::map<std::string, Real>& variables,
    circuit::CircuitGraph& circuit)
{
    for (size_t i = start_index; i < end_index; ++i) {
        const auto& cmd = commands[i];

        switch (cmd.type) {
            case ControlCommandType::IF: {
                // IF command in nested context
                if (cmd.args.empty()) {
                    break;
                }

                std::string condition = cmd.args[0];
                for (size_t j = 1; j < cmd.args.size(); ++j) {
                    condition += " " + cmd.args[j];
                }

                try {
                    Real result = evaluator.evaluate(condition);
                    bool condition_true = (result != 0.0);

                    if (!condition_true) {
                        int else_index = find_matching_block_end(commands, i, true);
                        int end_idx = find_matching_block_end(commands, i, false);

                        if (else_index != -1) {
                            i = static_cast<size_t>(else_index);
                        } else if (end_idx != -1) {
                            i = static_cast<size_t>(end_idx);
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::error("IF condition evaluation failed: {}", e.what());
                }
                break;
            }

            case ControlCommandType::ELSE: {
                int end_idx = find_matching_block_end(commands, i, false);
                if (end_idx != -1) {
                    i = static_cast<size_t>(end_idx);
                }
                break;
            }

            case ControlCommandType::END:
                // Do nothing - block terminator
                break;

            case ControlCommandType::ECHO:
                if (!cmd.args.empty()) {
                    spdlog::info("ECHO: {}", cmd.args[0]);
                }
                break;

            case ControlCommandType::LET: {
                if (cmd.args.size() >= 3 && cmd.args[1] == "=") {
                    std::string var_name = cmd.args[0];
                    std::string expression = cmd.args[2];
                    for (size_t j = 3; j < cmd.args.size(); ++j) {
                        expression += " " + cmd.args[j];
                    }

                    try {
                        // Ensure all current variables are set in evaluator before evaluating
                        for (const auto& [name, val] : variables) {
                            evaluator.set_variable(name, val);
                        }

                        Real value = evaluator.evaluate(expression);
                        variables[var_name] = value;
                        evaluator.set_variable(var_name, value);
                        spdlog::debug("LET: {} = {}", var_name, value);
                    } catch (const std::exception& e) {
                        spdlog::error("LET failed: {}", e.what());
                    }
                }
                break;
            }

            case ControlCommandType::ALTER: {
                // ALTER command: alter <device_name> <param>=<value>
                if (cmd.args.size() < 2) {
                    spdlog::warn("ALTER command requires at least 2 arguments: {}", cmd.raw_command);
                    break;
                }

                std::string device_name = cmd.args[0];
                std::string param_assignment = cmd.args[1];

                // Parse param=value
                size_t eq_pos = param_assignment.find('=');
                if (eq_pos == std::string::npos) {
                    spdlog::error("ALTER command: invalid syntax, expected <param>=<value>: {}", param_assignment);
                    break;
                }

                std::string param_name = param_assignment.substr(0, eq_pos);
                std::string value_str = param_assignment.substr(eq_pos + 1);

                try {
                    // Get device from circuit
                    auto device = circuit.get_device(device_name);
                    if (!device) {
                        spdlog::error("ALTER command: device '{}' not found", device_name);
                        break;
                    }

                    // Parse value with unit support
                    Real value = parse_value_with_unit(value_str);

                    // Set parameter
                    bool success = device->set_parameter(param_name, value);
                    if (success) {
                        spdlog::info("ALTER: device '{}', parameter '{}' = {}", device_name, param_name, value);
                    } else {
                        spdlog::warn("ALTER: failed to set parameter '{}' on device '{}'", param_name, device_name);
                    }
                } catch (const std::exception& e) {
                    spdlog::error("ALTER command failed: {}", e.what());
                }
                break;
            }

            case ControlCommandType::FOREACH: {
                // Nested FOREACH command: foreach <var> <value1> <value2> ...
                if (cmd.args.size() < 2) {
                    spdlog::warn("FOREACH command requires at least 2 arguments: {}", cmd.raw_command);
                    break;
                }

                std::string loop_var = cmd.args[0];
                std::vector<std::string> values(cmd.args.begin() + 1, cmd.args.end());

                // Find matching END within current range
                int end_idx = find_matching_block_end(commands, i, false);
                if (end_idx == -1 || static_cast<size_t>(end_idx) >= end_index) {
                    spdlog::error("FOREACH command without matching END: {}", cmd.raw_command);
                    break;
                }

                spdlog::debug("Nested FOREACH: var={}, values={}", loop_var, values.size());

                // Execute loop body for each value
                for (const auto& value_str : values) {
                    try {
                        // Try to parse value as number
                        Real value = parse_value_with_unit(value_str);
                        variables[loop_var] = value;
                        evaluator.set_variable(loop_var, value);

                        // Ensure all variables are set in evaluator before executing commands
                        for (const auto& [var_name, var_value] : variables) {
                            evaluator.set_variable(var_name, var_value);
                        }

                        // Execute commands in range [i+1, end_idx)
                        execute_commands_range(
                            commands,
                            i + 1,
                            static_cast<size_t>(end_idx),
                            evaluator,
                            config,
                            variables,
                            circuit);
                    } catch (const std::exception& e) {
                        spdlog::error("Nested FOREACH iteration failed for value '{}': {}", value_str, e.what());
                    }
                }

                // Skip to END
                i = static_cast<size_t>(end_idx);
                break;
            }

            case ControlCommandType::WHILE: {
                // Nested WHILE command: while <condition>
                if (cmd.args.empty()) {
                    spdlog::warn("WHILE command requires a condition: {}", cmd.raw_command);
                    break;
                }

                // Join all args to form the condition expression
                std::string condition = cmd.args[0];
                for (size_t j = 1; j < cmd.args.size(); ++j) {
                    condition += " " + cmd.args[j];
                }

                // Find matching END within current range
                int end_idx = find_matching_block_end(commands, i, false);
                if (end_idx == -1 || static_cast<size_t>(end_idx) >= end_index) {
                    spdlog::error("WHILE command without matching END: {}", cmd.raw_command);
                    break;
                }

                spdlog::debug("Nested WHILE: condition={}", condition);

                // Execute while loop (with maximum iteration limit for safety)
                const int MAX_ITERATIONS = 10000;
                int iteration = 0;

                while (iteration < MAX_ITERATIONS) {
                    try {
                        // Evaluate condition
                        Real result = evaluator.evaluate(condition);
                        if (result == 0.0) {
                            // Condition is false, exit loop
                            break;
                        }

                        spdlog::trace("  Nested WHILE iteration {}, condition = {}", iteration, result);

                        // Ensure all variables are set in evaluator before executing commands
                        for (const auto& [var_name, var_value] : variables) {
                            evaluator.set_variable(var_name, var_value);
                        }

                        // Execute commands in range [i+1, end_idx)
                        execute_commands_range(
                            commands,
                            i + 1,
                            static_cast<size_t>(end_idx),
                            evaluator,
                            config,
                            variables,
                            circuit);

                        iteration++;
                    } catch (const std::exception& e) {
                        spdlog::error("Nested WHILE iteration {} failed: {}", iteration, e.what());
                        break;
                    }
                }

                if (iteration >= MAX_ITERATIONS) {
                    spdlog::warn("Nested WHILE loop exceeded maximum iterations ({})", MAX_ITERATIONS);
                }

                // Skip to END
                i = static_cast<size_t>(end_idx);
                break;
            }

            case ControlCommandType::REPEAT: {
                // Nested REPEAT command: repeat <count>
                if (cmd.args.empty()) {
                    spdlog::warn("REPEAT command requires a count: {}", cmd.raw_command);
                    break;
                }

                // Parse repeat count
                int repeat_count = 0;
                try {
                    repeat_count = static_cast<int>(parse_value_with_unit(cmd.args[0]));
                } catch (const std::exception& e) {
                    spdlog::error("REPEAT count parse error: {}", e.what());
                    break;
                }

                if (repeat_count <= 0) {
                    spdlog::warn("REPEAT count must be positive: {}", repeat_count);
                    break;
                }

                // Find matching END within current range
                int end_idx = find_matching_block_end(commands, i, false);
                if (end_idx == -1 || static_cast<size_t>(end_idx) >= end_index) {
                    spdlog::error("REPEAT command without matching END: {}", cmd.raw_command);
                    break;
                }

                spdlog::debug("Nested REPEAT: count={}", repeat_count);

                // Execute repeat loop
                for (int iteration = 0; iteration < repeat_count; ++iteration) {
                    try {
                        spdlog::trace("  Nested REPEAT iteration {}/{}", iteration + 1, repeat_count);

                        // Ensure all variables are set in evaluator before executing commands
                        for (const auto& [var_name, var_value] : variables) {
                            evaluator.set_variable(var_name, var_value);
                        }

                        // Execute commands in range [i+1, end_idx)
                        execute_commands_range(
                            commands,
                            i + 1,
                            static_cast<size_t>(end_idx),
                            evaluator,
                            config,
                            variables,
                            circuit);
                    } catch (const std::exception& e) {
                        spdlog::error("Nested REPEAT iteration {} failed: {}", iteration, e.what());
                        break;
                    }
                }

                // Skip to END
                i = static_cast<size_t>(end_idx);
                break;
            }

            // Note: Other commands (DC, AC, TRAN, etc.) are not typically used inside loops
            // but we could add them if needed

            default:
                break;
        }
    }
}

//==============================================================================
// .LIB Section Selection Preprocessor
//==============================================================================

/// Extract a specific section from a library file
/// Searches for .LIB section_name ... .ENDL blocks
static std::string extract_lib_section(
    const std::string& content,
    const std::string& section_name)
{
    std::istringstream input(content);
    std::ostringstream output;
    std::string line;
    bool inside_section = false;
    bool found_section = false;

    while (std::getline(input, line)) {
        // Trim whitespace
        std::string trimmed = line;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), trimmed.end());

        // Check for .LIB directive
        if (trimmed.size() >= 4) {
            std::string directive_upper = trimmed.substr(0, std::min<size_t>(5, trimmed.size()));
            std::transform(directive_upper.begin(), directive_upper.end(),
                         directive_upper.begin(), ::toupper);

            if (directive_upper == ".LIB " || directive_upper.substr(0, 4) == ".LIB") {
                // Extract section name
                std::string lib_section = trimmed.substr(4);
                lib_section.erase(lib_section.begin(), std::find_if(lib_section.begin(), lib_section.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                lib_section.erase(std::find_if(lib_section.rbegin(), lib_section.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), lib_section.end());

                // Case-insensitive section name comparison
                std::string lib_section_upper = lib_section;
                std::string target_section_upper = section_name;
                std::transform(lib_section_upper.begin(), lib_section_upper.end(),
                             lib_section_upper.begin(), ::toupper);
                std::transform(target_section_upper.begin(), target_section_upper.end(),
                             target_section_upper.begin(), ::toupper);

                if (lib_section_upper == target_section_upper) {
                    inside_section = true;
                    found_section = true;
                    continue;  // Don't output .LIB directive
                }
            } else if (trimmed.size() >= 5 && directive_upper == ".ENDL") {
                if (inside_section) {
                    inside_section = false;
                    continue;  // Don't output .ENDL directive
                }
            }
        }

        // Output lines inside the target section
        if (inside_section) {
            output << line << '\n';
        }
    }

    if (!found_section) {
        throw std::runtime_error(
            fmt::format("Library section '{}' not found", section_name));
    }

    return output.str();
}

/// The library sections being inserted, each a file and the upper-case name
/// of its section ("" for a whole file). A section that is already being
/// inserted would never end; another section of the same file is fine.
using LibraryChain = std::set<std::pair<std::filesystem::path, std::string>>;

/// Preprocess .LIB directives with optional section selection
static std::string preprocess_lib_sections(
    const std::string& content,
    const std::filesystem::path& base_path,
    LibraryChain& inserting,
    int depth = 0)
{
    // Depth limit: 10 levels max (ngspice-compatible)
    constexpr int MAX_LIB_DEPTH = 10;
    if (depth > MAX_LIB_DEPTH) {
        throw std::runtime_error(
            fmt::format("Library include depth limit exceeded (max {})", MAX_LIB_DEPTH));
    }

    std::istringstream input(content);
    std::ostringstream output;
    std::string line;
    int line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;

        // Remove inline comments
        size_t comment_pos = line.find(';');
        std::string line_no_comment = (comment_pos != std::string::npos)
            ? line.substr(0, comment_pos)
            : line;

        // Trim whitespace
        std::string trimmed = line_no_comment;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), trimmed.end());

        // Check for .LIB directive
        if (trimmed.size() >= 4) {
            std::string directive_upper = trimmed.substr(0, 4);
            std::transform(directive_upper.begin(), directive_upper.end(),
                         directive_upper.begin(), ::toupper);

            if (directive_upper == ".LIB") {
                // Extract filepath and optional section
                std::string args = trimmed.substr(4);
                args.erase(args.begin(), std::find_if(args.begin(), args.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                args.erase(std::find_if(args.rbegin(), args.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), args.end());

                if (args.empty()) {
                    throw ParseError("Invalid .LIB syntax: missing filepath", line_number);
                }

                // Split filepath and section name
                std::string filepath_str;
                std::string section_name;

                size_t space_pos = args.find(' ');
                if (space_pos != std::string::npos) {
                    // Has section name
                    filepath_str = args.substr(0, space_pos);
                    section_name = args.substr(space_pos + 1);
                    // Trim section name
                    section_name.erase(section_name.begin(), std::find_if(section_name.begin(), section_name.end(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }));
                    section_name.erase(std::find_if(section_name.rbegin(), section_name.rend(), [](unsigned char ch) {
                        return !std::isspace(ch);
                    }).base(), section_name.end());
                } else {
                    // No section name - include entire file
                    filepath_str = args;
                }

                // Remove quotes from filepath if present
                if (filepath_str.size() >= 2 &&
                    ((filepath_str.front() == '"' && filepath_str.back() == '"') ||
                     (filepath_str.front() == '\'' && filepath_str.back() == '\''))) {
                    filepath_str = filepath_str.substr(1, filepath_str.size() - 2);
                }

                if (filepath_str.empty()) {
                    throw ParseError("Invalid .LIB syntax: empty filepath", line_number);
                }

                // Resolve filepath (absolute or relative to base_path)
                std::filesystem::path lib_path(filepath_str);
                if (!lib_path.is_absolute()) {
                    lib_path = base_path / lib_path;
                }

                // Canonicalize path
                std::filesystem::path canonical_path;
                try {
                    canonical_path = std::filesystem::canonical(lib_path);
                } catch (const std::filesystem::filesystem_error& e) {
                    throw ParseError(
                        fmt::format("Cannot resolve library path '{}': {}",
                                  lib_path.string(), e.what()),
                        line_number);
                }

                // Check for circular includes: the same section of the same file
                std::string section_upper = section_name;
                std::transform(section_upper.begin(), section_upper.end(), section_upper.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                const auto key = std::make_pair(canonical_path, section_upper);
                if (inserting.count(key)) {
                    throw ParseError(
                        section_name.empty()
                            ? fmt::format("Circular library include detected: '{}'",
                                          canonical_path.string())
                            : fmt::format("Circular library include detected: section '{}' of '{}'",
                                          section_name, canonical_path.string()),
                        line_number);
                }

                // Check if file exists
                if (!std::filesystem::exists(canonical_path)) {
                    throw ParseError(
                        fmt::format("Library file not found: '{}'", canonical_path.string()),
                        line_number);
                }

                // Check if readable
                if (!std::filesystem::is_regular_file(canonical_path)) {
                    throw ParseError(
                        fmt::format("Library path is not a regular file: '{}'", canonical_path.string()),
                        line_number);
                }

                // Read library file
                std::ifstream lib_file(canonical_path);
                if (!lib_file) {
                    throw ParseError(
                        fmt::format("Cannot open library file: '{}'", canonical_path.string()),
                        line_number);
                }

                std::string file_content = read_netlist_file(lib_file);

                // Extract section if specified
                if (!section_name.empty()) {
                    spdlog::debug("Loading library section '{}' from: {} (depth {})",
                                section_name, canonical_path.string(), depth);
                    try {
                        file_content = extract_lib_section(file_content, section_name);
                    } catch (const std::exception& e) {
                        throw ParseError(
                            fmt::format("Failed to extract section '{}' from '{}': {}",
                                      section_name, canonical_path.string(), e.what()),
                            line_number);
                    }
                } else {
                    spdlog::debug("Loading entire library file: {} (depth {})",
                                canonical_path.string(), depth);
                }

                // Recursively preprocess library file, with this section on
                // the chain while it is inserted (it may be inserted again in
                // a different branch)
                inserting.insert(key);
                std::string processed_content = preprocess_lib_sections(
                    file_content,
                    canonical_path.parent_path(),
                    inserting,
                    depth + 1);
                inserting.erase(key);

                // Output processed content
                output << processed_content;

                continue;  // Skip original .LIB directive
            }
        }

        // Regular line - output as-is
        output << line << '\n';
    }

    return output.str();
}

//==============================================================================
// User-Defined Functions for ExprTk
//==============================================================================

// Helper function to split comma-separated argument list
static std::vector<std::string> split_args(const std::string& args_str) {
    std::vector<std::string> result;
    std::string current;

    for (char c : args_str) {
        if (c == ',') {
            // Trim whitespace
            current.erase(0, current.find_first_not_of(" \t"));
            current.erase(current.find_last_not_of(" \t") + 1);
            if (!current.empty()) {
                result.push_back(current);
            }
            current.clear();
        } else {
            current += c;
        }
    }

    // Last argument
    current.erase(0, current.find_first_not_of(" \t"));
    current.erase(current.find_last_not_of(" \t") + 1);
    if (!current.empty()) {
        result.push_back(current);
    }

    return result;
}

// Base template for user-defined functions (1-5 arguments)
template<std::size_t N>
class UserDefinedFunction;

// 1-argument function
template<>
class UserDefinedFunction<1> : public exprtk::ifunction<Real> {
public:
    UserDefinedFunction(const std::string& func_data)
        : exprtk::ifunction<Real>(1) {
        // Parse "args|expression" format
        size_t pipe_pos = func_data.find('|');
        if (pipe_pos == std::string::npos) {
            throw std::runtime_error("Invalid .FUNC data format");
        }

        std::string args_str = func_data.substr(0, pipe_pos);
        expression_ = func_data.substr(pipe_pos + 1);

        // Trim whitespace from expression
        expression_.erase(0, expression_.find_first_not_of(" \t"));
        expression_.erase(expression_.find_last_not_of(" \t") + 1);

        // Parse argument names
        arg_names_ = split_args(args_str);

        if (arg_names_.size() != 1) {
            throw std::runtime_error(fmt::format(
                "Function expects 1 argument, got {}", arg_names_.size()));
        }

        // Setup symbol table with constants
        symbol_table_.add_constants();
        // Check ExprTk return value
        if (!symbol_table_.add_constant("e", 2.718281828459045)) {
            spdlog::warn("UserDefinedFunction<1>: Failed to add constant 'e'");
        }
    }

    Real operator()(const Real& arg1) override {
        // Create local symbol table with argument
        exprtk::symbol_table<Real> local_table = symbol_table_;

        // Store argument values (ExprTk requires persistent storage)
        arg_values_[0] = arg1;
        // Check ExprTk return value
        if (!local_table.add_variable(arg_names_[0], arg_values_[0])) {
            spdlog::warn("UserDefinedFunction<1>: Failed to add variable '{}'", arg_names_[0]);
        }

        // Compile and evaluate expression
        exprtk::expression<Real> expr;
        expr.register_symbol_table(local_table);

        exprtk::parser<Real> parser;
        if (!parser.compile(expression_, expr)) {
            throw std::runtime_error(fmt::format(
                "Failed to compile .FUNC expression: {}", expression_));
        }

        return expr.value();
    }

private:
    std::vector<std::string> arg_names_;
    std::string expression_;
    exprtk::symbol_table<Real> symbol_table_;
    mutable std::array<Real, 1> arg_values_;  // Persistent storage
};

// 2-argument function
template<>
class UserDefinedFunction<2> : public exprtk::ifunction<Real> {
public:
    UserDefinedFunction(const std::string& func_data)
        : exprtk::ifunction<Real>(2) {
        size_t pipe_pos = func_data.find('|');
        if (pipe_pos == std::string::npos) {
            throw std::runtime_error("Invalid .FUNC data format");
        }

        std::string args_str = func_data.substr(0, pipe_pos);
        expression_ = func_data.substr(pipe_pos + 1);

        expression_.erase(0, expression_.find_first_not_of(" \t"));
        expression_.erase(expression_.find_last_not_of(" \t") + 1);

        arg_names_ = split_args(args_str);

        if (arg_names_.size() != 2) {
            throw std::runtime_error(fmt::format(
                "Function expects 2 arguments, got {}", arg_names_.size()));
        }

        symbol_table_.add_constants();
        // Check ExprTk return value
        if (!symbol_table_.add_constant("e", 2.718281828459045)) {
            spdlog::warn("UserDefinedFunction<2>: Failed to add constant 'e'");
        }
    }

    Real operator()(const Real& arg1, const Real& arg2) override {
        exprtk::symbol_table<Real> local_table = symbol_table_;

        arg_values_[0] = arg1;
        arg_values_[1] = arg2;
        // Check ExprTk return values
        if (!local_table.add_variable(arg_names_[0], arg_values_[0])) {
            spdlog::warn("UserDefinedFunction<2>: Failed to add variable '{}'", arg_names_[0]);
        }
        if (!local_table.add_variable(arg_names_[1], arg_values_[1])) {
            spdlog::warn("UserDefinedFunction<2>: Failed to add variable '{}'", arg_names_[1]);
        }

        exprtk::expression<Real> expr;
        expr.register_symbol_table(local_table);

        exprtk::parser<Real> parser;
        if (!parser.compile(expression_, expr)) {
            throw std::runtime_error(fmt::format(
                "Failed to compile .FUNC expression: {}", expression_));
        }

        return expr.value();
    }

private:
    std::vector<std::string> arg_names_;
    std::string expression_;
    exprtk::symbol_table<Real> symbol_table_;
    mutable std::array<Real, 2> arg_values_;
};

// 3-argument function
template<>
class UserDefinedFunction<3> : public exprtk::ifunction<Real> {
public:
    UserDefinedFunction(const std::string& func_data)
        : exprtk::ifunction<Real>(3) {
        size_t pipe_pos = func_data.find('|');
        if (pipe_pos == std::string::npos) {
            throw std::runtime_error("Invalid .FUNC data format");
        }

        std::string args_str = func_data.substr(0, pipe_pos);
        expression_ = func_data.substr(pipe_pos + 1);

        expression_.erase(0, expression_.find_first_not_of(" \t"));
        expression_.erase(expression_.find_last_not_of(" \t") + 1);

        arg_names_ = split_args(args_str);

        if (arg_names_.size() != 3) {
            throw std::runtime_error(fmt::format(
                "Function expects 3 arguments, got {}", arg_names_.size()));
        }

        symbol_table_.add_constants();
        // Check ExprTk return value
        if (!symbol_table_.add_constant("e", 2.718281828459045)) {
            spdlog::warn("UserDefinedFunction<3>: Failed to add constant 'e'");
        }
    }

    Real operator()(const Real& arg1, const Real& arg2, const Real& arg3) override {
        exprtk::symbol_table<Real> local_table = symbol_table_;

        arg_values_[0] = arg1;
        arg_values_[1] = arg2;
        arg_values_[2] = arg3;
        // Check ExprTk return values
        if (!local_table.add_variable(arg_names_[0], arg_values_[0])) {
            spdlog::warn("UserDefinedFunction<3>: Failed to add variable '{}'", arg_names_[0]);
        }
        if (!local_table.add_variable(arg_names_[1], arg_values_[1])) {
            spdlog::warn("UserDefinedFunction<3>: Failed to add variable '{}'", arg_names_[1]);
        }
        if (!local_table.add_variable(arg_names_[2], arg_values_[2])) {
            spdlog::warn("UserDefinedFunction<3>: Failed to add variable '{}'", arg_names_[2]);
        }

        exprtk::expression<Real> expr;
        expr.register_symbol_table(local_table);

        exprtk::parser<Real> parser;
        if (!parser.compile(expression_, expr)) {
            throw std::runtime_error(fmt::format(
                "Failed to compile .FUNC expression: {}", expression_));
        }

        return expr.value();
    }

private:
    std::vector<std::string> arg_names_;
    std::string expression_;
    exprtk::symbol_table<Real> symbol_table_;
    mutable std::array<Real, 3> arg_values_;
};

//==============================================================================
// PEGTL Grammar Definition (Simplified line-by-line approach)
//==============================================================================
namespace grammar {

// Line content (everything except newline)
struct line_content : pegtl::star<pegtl::not_one<'\n', '\r'>> {};

// Line with newline
struct content_line : pegtl::seq<line_content, pegtl::eol> {};

// Netlist structure: first line is title, then other lines, then EOF
struct netlist : pegtl::seq<
    content_line,  // Title line
    pegtl::star<content_line>,  // Other lines
    pegtl::eof
> {};

} // namespace grammar

//==============================================================================
// Parse State
//==============================================================================
struct ParseState {
    circuit::CircuitGraph* circuit = nullptr;
    ExpressionEvaluator* evaluator = nullptr;
    SimulationConfig* config = nullptr;  // NEW: Simulation configuration
    std::size_t line_number = 1;

    // Subcircuit parsing state
    std::optional<circuit::SubcircuitDefinition> current_subcircuit;
    bool inside_subcircuit = false;

    // Elements whose model or subcircuit had not been read yet, with their
    // line numbers. They are parsed again after the whole netlist has been
    // read.
    std::vector<std::pair<std::string, std::size_t>> deferred_elements;
    bool resolving_deferred = false;
    std::set<std::string> warned_directives;  // each named in one warning
    std::set<std::string> warned_card_names;  // "<what>: <name>", each in one warning
};

// Thrown by an element parser when the element names a model or a
// subcircuit that has not been read. A .MODEL line and a .SUBCKT definition
// apply to the whole netlist wherever they stand, so process_line puts the
// element aside and parses it again at the end; only then is a missing
// definition an error.
class UnresolvedDefinition : public ParseError {
public:
    using ParseError::ParseError;
};

// The letter that gives an element's type, in upper case: the first of its
// name, or of the last part of a name in a subcircuit (X1.Vs is a V); 0 for an
// empty name
static char element_letter(const std::string& name) {
    const auto dot = name.rfind('.');
    const std::size_t first = (dot == std::string::npos) ? 0 : dot + 1;
    return first < name.size() ? static_cast<char>(std::toupper(static_cast<unsigned char>(name[first]))) : '\0';
}

// The value of a token on an element line: a brace expression, evaluated with
// the parameters read so far, or a number with an optional scale factor.
static Real read_element_value(const std::string& token, ParseState& state) {
    if (!token.empty() && token.front() == '{') {
        return state.evaluator->evaluate(token.substr(1, token.size() - 2));
    }
    return parse_value_with_unit(token);
}

//==============================================================================
// PEGTL Actions
//==============================================================================
template<typename Rule>
struct action {};


static void parse_ac_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .AC directive
    // Syntax: .AC DEC|LIN|OCT num_points start_freq stop_freq
    if (tokens.size() < 5) {
        throw ParseError("Invalid .AC syntax: .AC <DEC|LIN|OCT> <num_points> <start_freq> <stop_freq>",
                        state.line_number);
    }

    std::string sweep_type_str = tokens[1];
    std::transform(sweep_type_str.begin(), sweep_type_str.end(), sweep_type_str.begin(), ::toupper);

    if (sweep_type_str == "DEC") {
        state.config->ac.sweep_type = SimulationConfig::AC::SweepType::DEC;
    } else if (sweep_type_str == "LIN") {
        state.config->ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    } else if (sweep_type_str == "OCT") {
        state.config->ac.sweep_type = SimulationConfig::AC::SweepType::OCT;
    } else {
        throw ParseError(fmt::format("Unknown AC sweep type: {}", sweep_type_str), state.line_number);
    }

    try {
        state.config->ac.num_points = read_ac_point_count(tokens[2]);
    } catch (const std::runtime_error& e) {
        throw ParseError(e.what(), state.line_number);
    }
    state.config->ac.start_freq = parse_value_with_unit(tokens[3]);
    state.config->ac.stop_freq = parse_value_with_unit(tokens[4]);
    state.config->ac_requested = true;

    spdlog::debug("Parsed .AC: sweep={}, num_points={}, start={} Hz, stop={} Hz",
                 sweep_type_str, state.config->ac.num_points,
                 state.config->ac.start_freq, state.config->ac.stop_freq);
}

static void parse_dc_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .DC directive (DC Sweep)
    // Syntax 1D: .DC source_name start stop step
    // Syntax 2D: .DC source1 start1 stop1 step1 source2 start2 stop2 step2
    // Example 1D: .DC V1 0 10 1
    // Example 2D: .DC V1 0 5 0.5 V2 0 3 1

    // Skip if incomplete (for backward compatibility with existing tests)
    if (tokens.size() < 5) {
        spdlog::warn("Skipping incomplete .DC directive at line {} (expected: .DC source start stop step)",
                    state.line_number);
        return;
    }

    // Parse primary sweep (required)
    auto primary_sweep = std::make_shared<DCSweepConfig>();
    primary_sweep->source_name = tokens[1];
    primary_sweep->start_value = parse_value_with_unit(tokens[2]);
    primary_sweep->stop_value = parse_value_with_unit(tokens[3]);
    primary_sweep->step_value = parse_value_with_unit(tokens[4]);
    primary_sweep->nested_sweep = nullptr;  // Will be set if 2D sweep

    // Validate primary sweep
    if (primary_sweep->step_value == 0.0) {
        throw ParseError(fmt::format("Invalid .DC: step value cannot be zero"),
                        state.line_number);
    }

    // Check for nested sweep (2D)
    if (tokens.size() >= 9) {
        // Parse secondary sweep
        auto nested_sweep = std::make_shared<DCSweepConfig>();
        nested_sweep->source_name = tokens[5];
        nested_sweep->start_value = parse_value_with_unit(tokens[6]);
        nested_sweep->stop_value = parse_value_with_unit(tokens[7]);
        nested_sweep->step_value = parse_value_with_unit(tokens[8]);
        nested_sweep->nested_sweep = nullptr;  // No 3D sweeps supported

        // Validate nested sweep
        if (nested_sweep->step_value == 0.0) {
            throw ParseError(fmt::format("Invalid .DC: nested step value cannot be zero"),
                            state.line_number);
        }

        primary_sweep->nested_sweep = nested_sweep;

        spdlog::info("Parsed .DC (2D): primary={} [{}, {}, step={}], nested={} [{}, {}, step={}]",
                    primary_sweep->source_name,
                    primary_sweep->start_value, primary_sweep->stop_value, primary_sweep->step_value,
                    nested_sweep->source_name,
                    nested_sweep->start_value, nested_sweep->stop_value, nested_sweep->step_value);
    } else {
        spdlog::info("Parsed .DC (1D): source={}, start={}, stop={}, step={}",
                    primary_sweep->source_name,
                    primary_sweep->start_value, primary_sweep->stop_value, primary_sweep->step_value);
    }

    // Store DC sweep configuration
    state.config->dc_sweep = *primary_sweep;
    state.config->dc_sweep_enabled = true;

}

static void parse_tran_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .TRAN directive (Transient analysis)
    // Syntax: .TRAN TSTEP TSTOP [TSTART [TMAX]] [UIC]
    // Example: .tran 0.1m 5m
    // Example: .tran 1u 100u 0 0.5u
    // Example: .tran 0.5m 50m UIC

    if (tokens.size() < 3) {
        spdlog::warn("Skipping incomplete .TRAN directive at line {} (expected: .TRAN tstep tstop [tstart] [tmax] [UIC])",
                    state.line_number);
        return;
    }

    // Helper to check if a token is the UIC flag
    auto is_uic = [](const std::string& s) {
        std::string upper = s;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        return upper == "UIC";
    };

    // Collect numeric tokens (skip UIC flag)
    std::vector<std::string> numeric_tokens;
    bool has_uic = false;
    for (size_t i = 1; i < tokens.size(); ++i) {
        if (is_uic(tokens[i])) {
            has_uic = true;
        } else {
            numeric_tokens.push_back(tokens[i]);
        }
    }

    if (numeric_tokens.size() < 2) {
        spdlog::warn("Skipping incomplete .TRAN directive at line {} (expected: .TRAN tstep tstop [tstart] [tmax] [UIC])",
                    state.line_number);
        return;
    }

    Real tstep = parse_value_with_unit(numeric_tokens[0]);
    Real tstop = parse_value_with_unit(numeric_tokens[1]);

    state.config->transient.initial_step = tstep;
    state.config->transient.max_step = tstep;
    state.config->transient.end_time = tstop;

    // Parse optional TSTART
    if (numeric_tokens.size() >= 3) {
        state.config->transient.start_time = parse_value_with_unit(numeric_tokens[2]);
    }

    // Parse optional TMAX
    if (numeric_tokens.size() >= 4) {
        state.config->transient.max_step = parse_value_with_unit(numeric_tokens[3]);
    }

    // Set UIC flag
    if (has_uic) {
        state.config->transient.use_initial_conditions = true;
    }
    state.config->tran_requested = true;

    spdlog::info("Parsed .TRAN: tstep={}, tstop={}, tstart={}, tmax={}, UIC={}",
                state.config->transient.initial_step,
                state.config->transient.end_time,
                state.config->transient.start_time,
                state.config->transient.max_step,
                has_uic);

}

static void parse_options_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .OPTION directive (ngspice-compatible)
    // Syntax: .OPTION optname=value optname=value ...
    // Supported options:
    //   Integration methods: method=trap|gear|euler|bdf2|bdf3|gear2|gear3|tr_bdf2
    //   Error estimation: error_est=physical|predictor_corrector|combined
    //   Tolerances: trtol, chgtol, abstol, reltol, vntol, lte_abstol, lte_reltol
    //   Iteration limits: itl1, itl2, itl4
    //   Numerical stability: gmin, temp
    //   Integration parameters: maxord (1-6)
    //   Source Stepping: source_stepping, source_steps, source_start
    //   Gmin Stepping: gmin_stepping, gmin_steps, gmin_start, gmin_end
    //   Voltage Limiting: voltage_limiting

    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string option = tokens[i];

        // Find '=' separator
        size_t eq_pos = option.find('=');
        if (eq_pos == std::string::npos) {
            spdlog::warn("Skipping malformed .OPTION parameter (no '='): {}", option);
            continue;
        }

        std::string opt_name = option.substr(0, eq_pos);
        std::string opt_value = option.substr(eq_pos + 1);

        // Convert option name to lowercase for comparison
        std::transform(opt_name.begin(), opt_name.end(), opt_name.begin(), ::tolower);
        std::transform(opt_value.begin(), opt_value.end(), opt_value.begin(), ::tolower);

        // Parse integration method
        if (opt_name == "method") {
            if (opt_value == "trap" || opt_value == "trapezoidal") {
                state.config->transient.method = IntegrationMethod::TRAPEZOIDAL;
                spdlog::debug("Set method=TRAPEZOIDAL");
            } else if (opt_value == "euler" || opt_value == "be" || opt_value == "bdf1") {
                state.config->transient.method = IntegrationMethod::BACKWARD_EULER;
                spdlog::debug("Set method=BACKWARD_EULER");
            } else if (opt_value == "gear" || opt_value == "gear2" || opt_value == "bdf2") {
                state.config->transient.method = IntegrationMethod::GEAR2;
                spdlog::debug("Set method=GEAR2");
            } else if (opt_value == "gear3" || opt_value == "bdf3") {
                state.config->transient.method = IntegrationMethod::GEAR3;
                spdlog::debug("Set method=GEAR3");
            } else if (opt_value == "tr_bdf2" || opt_value == "trbdf2") {
                state.config->transient.method = IntegrationMethod::TR_BDF2;
                spdlog::debug("Set method=TR_BDF2");
            } else {
                spdlog::warn("Unknown integration method: {}, using default", opt_value);
            }
        }
        // Parse error estimation method (KirimaSpice extension)
        else if (opt_name == "error_est" || opt_name == "errmethod") {
            if (opt_value == "physical" || opt_value == "charge" || opt_value == "embedded") {
                state.config->transient.error_est = ErrorEstimation::PHYSICAL;
                spdlog::debug("Set error_est=PHYSICAL");
            } else if (opt_value == "predictor_corrector" || opt_value == "pc" || opt_value == "dvdt") {
                state.config->transient.error_est = ErrorEstimation::PREDICTOR_CORRECTOR;
                spdlog::debug("Set error_est=PREDICTOR_CORRECTOR");
            } else if (opt_value == "combined" || opt_value == "both") {
                state.config->transient.error_est = ErrorEstimation::COMBINED;
                spdlog::debug("Set error_est=COMBINED");
            } else {
                spdlog::warn("Unknown error estimation method: {}, using default", opt_value);
            }
        }
        // Parse tolerance parameters (safe parsing)
        else if (opt_name == "trtol") {
            if (auto v = safe_stod(opt_value, "trtol")) {
                state.config->transient.trtol = *v;
                spdlog::debug("Set trtol={}", *v);
            }
        } else if (opt_name == "chgtol") {
            if (auto v = safe_stod(opt_value, "chgtol")) {
                state.config->transient.chgtol = *v;
                spdlog::debug("Set chgtol={}", *v);
            }
        } else if (opt_name == "abstol") {
            if (auto v = safe_stod(opt_value, "abstol")) {
                state.config->transient.abstol = *v;
                state.config->dc.abstol = *v;
                spdlog::debug("Set abstol={}", *v);
            }
        } else if (opt_name == "reltol") {
            if (auto v = safe_stod(opt_value, "reltol")) {
                state.config->transient.reltol = *v;
                state.config->dc.reltol = *v;
                spdlog::debug("Set reltol={}", *v);
            }
        } else if (opt_name == "vntol") {
            if (auto v = safe_stod(opt_value, "vntol")) {
                state.config->transient.vntol = *v;
                state.config->dc.vntol = *v;
                spdlog::debug("Set vntol={}", *v);
            }
        } else if (opt_name == "lte_abstol") {
            if (auto v = safe_stod(opt_value, "lte_abstol")) {
                state.config->transient.lte_abstol = *v;
                spdlog::debug("Set lte_abstol={}", *v);
            }
        } else if (opt_name == "lte_reltol") {
            if (auto v = safe_stod(opt_value, "lte_reltol")) {
                state.config->transient.lte_reltol = *v;
                spdlog::debug("Set lte_reltol={}", *v);
            }
        }
        // Parse iteration limits (SPICE standard)
        else if (opt_name == "itl1") {
            if (auto v = safe_stoi(opt_value, "itl1")) {
                state.config->dc.max_iterations = *v;
                spdlog::debug("Set ITL1 (DC max iterations)={}", *v);
            }
        } else if (opt_name == "itl2") {
            if (auto v = safe_stoi(opt_value, "itl2")) {
                state.config->dc.dc_sweep_max_iterations = *v;
                spdlog::debug("Set ITL2 (DC sweep max iterations)={}", *v);
            }
        } else if (opt_name == "itl4") {
            if (auto v = safe_stoi(opt_value, "itl4")) {
                state.config->transient.max_iterations = *v;
                spdlog::debug("Set ITL4 (Transient max iterations)={}", *v);
            }
        } else if (opt_name == "itl5") {
            if (auto v = safe_stoi(opt_value, "itl5")) {
                state.config->transient.max_timesteps = *v;
                spdlog::debug("Set ITL5 (Transient max timesteps)={}", *v);
            }
        } else if (opt_name == "itl6") {
            if (auto v = safe_stoi(opt_value, "itl6")) {
                state.config->dc.source_stepping_max_iterations = *v;
                spdlog::debug("Set ITL6 (Source stepping iterations)={}", *v);
            }
        }
        // Parse numerical stability parameters
        else if (opt_name == "gmin") {
            if (auto v = safe_stod(opt_value, "gmin")) {
                state.config->gmin = *v;
                spdlog::debug("Set GMIN (minimum conductance)={}", *v);
            }
        } else if (opt_name == "temp") {
            // Temperature in Celsius, convert to Kelvin
            if (auto v = safe_stod(opt_value, "temp")) {
                state.config->temperature = core::celsius_to_kelvin(*v);
                state.config->temperature_explicitly_set = true;
                spdlog::debug("Set TEMP={}°C ({}K)", *v, state.config->temperature);
            }
        }
        // Parse integration method parameters
        else if (opt_name == "maxord" || opt_name == "maxorder") {
            if (auto v = safe_stoi(opt_value, "maxord")) {
                if (*v >= 1 && *v <= 6) {
                    state.config->transient.max_order = static_cast<std::uint32_t>(*v);
                    spdlog::debug("Set MAXORD (max BDF order)={}", *v);
                } else {
                    spdlog::warn("MAXORD={} out of range [1,6], using default", *v);
                }
            }
        }
        // Parse Source Stepping parameters
        else if (opt_name == "source_stepping") {
            if (auto v = safe_stoi(opt_value, "source_stepping")) {
                state.config->dc.enable_source_stepping = (*v != 0);
                spdlog::debug("Set source_stepping={}", state.config->dc.enable_source_stepping);
            }
        } else if (opt_name == "source_steps") {
            if (auto v = safe_stoi(opt_value, "source_steps")) {
                state.config->dc.source_stepping_steps = *v;
                spdlog::debug("Set source_steps={}", *v);
            }
        } else if (opt_name == "source_start") {
            if (auto v = safe_stod(opt_value, "source_start")) {
                state.config->dc.source_stepping_start = *v;
                spdlog::debug("Set source_start={}", *v);
            }
        }
        // Parse Gmin Stepping parameters
        else if (opt_name == "gmin_stepping") {
            if (auto v = safe_stoi(opt_value, "gmin_stepping")) {
                state.config->dc.enable_gmin_stepping = (*v != 0);
                spdlog::debug("Set gmin_stepping={}", state.config->dc.enable_gmin_stepping);
            }
        } else if (opt_name == "gmin_steps") {
            if (auto v = safe_stoi(opt_value, "gmin_steps")) {
                state.config->dc.gmin_stepping_steps = *v;
                spdlog::debug("Set gmin_steps={}", *v);
            }
        } else if (opt_name == "gmin_start") {
            if (auto v = safe_stod(opt_value, "gmin_start")) {
                state.config->dc.gmin_start = *v;
                spdlog::debug("Set gmin_start={}", *v);
            }
        } else if (opt_name == "gmin_end") {
            if (auto v = safe_stod(opt_value, "gmin_end")) {
                state.config->dc.gmin_end = *v;
                spdlog::debug("Set gmin_end={}", *v);
            }
        }
        // Parse Dynamic Gmin parameters
        else if (opt_name == "dyn_gmin") {
            if (auto v = safe_stoi(opt_value, "dyn_gmin")) {
                state.config->dc.enable_dynamic_gmin = (*v != 0);
                spdlog::debug("Set dyn_gmin={}", state.config->dc.enable_dynamic_gmin);
            }
        } else if (opt_name == "dyn_gmin_start") {
            if (auto v = safe_stod(opt_value, "dyn_gmin_start")) {
                state.config->dc.dynamic_gmin_start = *v;
                spdlog::debug("Set dyn_gmin_start={}", *v);
            }
        } else if (opt_name == "dyn_gmin_min") {
            if (auto v = safe_stod(opt_value, "dyn_gmin_min")) {
                state.config->dc.dynamic_gmin_min = *v;
                spdlog::debug("Set dyn_gmin_min={}", *v);
            }
        } else if (opt_name == "dyn_gmin_max") {
            if (auto v = safe_stod(opt_value, "dyn_gmin_max")) {
                state.config->dc.dynamic_gmin_max = *v;
                spdlog::debug("Set dyn_gmin_max={}", *v);
            }
        } else if (opt_name == "dyn_gmin_inc") {
            if (auto v = safe_stod(opt_value, "dyn_gmin_inc")) {
                state.config->dc.dynamic_gmin_increase_factor = *v;
                spdlog::debug("Set dyn_gmin_inc={}", *v);
            }
        } else if (opt_name == "dyn_gmin_dec") {
            if (auto v = safe_stod(opt_value, "dyn_gmin_dec")) {
                state.config->dc.dynamic_gmin_decrease_factor = *v;
                spdlog::debug("Set dyn_gmin_dec={}", *v);
            }
        } else if (opt_name == "dyn_gmin_stall") {
            if (auto v = safe_stoi(opt_value, "dyn_gmin_stall")) {
                state.config->dc.dynamic_gmin_stall_iterations = *v;
                spdlog::debug("Set dyn_gmin_stall={}", *v);
            }
        } else if (opt_name == "dyn_gmin_improve") {
            if (auto v = safe_stod(opt_value, "dyn_gmin_improve")) {
                state.config->dc.dynamic_gmin_improvement_threshold = *v;
                spdlog::debug("Set dyn_gmin_improve={}", *v);
            }
        } else if (opt_name == "dyn_gmin_target") {
            if (auto v = safe_stod(opt_value, "dyn_gmin_target")) {
                state.config->dc.dynamic_gmin_residual_target = *v;
                spdlog::debug("Set dyn_gmin_target={}", *v);
            }
        }
        // Continuation method
        else if (opt_name == "continuation") {
            if (auto v = safe_stoi(opt_value, "continuation")) {
                state.config->dc.enable_continuation_method = (*v != 0);
                spdlog::debug("Set continuation={}", state.config->dc.enable_continuation_method);
            }
        } else if (opt_name == "continuation_steps") {
            if (auto v = safe_stoi(opt_value, "continuation_steps")) {
                state.config->dc.continuation_steps = *v;
                spdlog::debug("Set continuation_steps={}", *v);
            }
        } else if (opt_name == "continuation_start") {
            if (auto v = safe_stod(opt_value, "continuation_start")) {
                state.config->dc.continuation_start = *v;
                spdlog::debug("Set continuation_start={}", *v);
            }
        }
        // Pseudo transient
        else if (opt_name == "pseudo_transient") {
            if (auto v = safe_stoi(opt_value, "pseudo_transient")) {
                state.config->dc.enable_pseudo_transient = (*v != 0);
                spdlog::debug("Set pseudo_transient={}", state.config->dc.enable_pseudo_transient);
            }
        } else if (opt_name == "pseudo_dt") {
            if (auto v = safe_stod(opt_value, "pseudo_dt")) {
                state.config->dc.pseudo_transient_dt = *v;
                spdlog::debug("Set pseudo_dt={}", *v);
            }
        } else if (opt_name == "pseudo_steps") {
            if (auto v = safe_stoi(opt_value, "pseudo_steps")) {
                state.config->dc.pseudo_transient_steps = *v;
                spdlog::debug("Set pseudo_steps={}", *v);
            }
        } else if (opt_name == "pseudo_cap") {
            if (auto v = safe_stod(opt_value, "pseudo_cap")) {
                state.config->dc.pseudo_transient_capacitance = *v;
                spdlog::debug("Set pseudo_cap={}", *v);
            }
        }
        // Parse Voltage Limiting parameter
        else if (opt_name == "voltage_limiting") {
            if (auto v = safe_stoi(opt_value, "voltage_limiting")) {
                state.config->dc.enable_voltage_limiting = (*v != 0);
                spdlog::debug("Set voltage_limiting={}", state.config->dc.enable_voltage_limiting);
            }
        }
        else {
            spdlog::warn("Unknown .OPTION parameter: {}", opt_name);
        }
    }

    spdlog::debug("Parsed .OPTION directive with {} parameters", tokens.size() - 1);
}

static void parse_ic_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .IC directive (Initial Conditions for transient analysis)
    // Syntax: .IC V(node1)=value V(node2)=value ...
    // Example: .IC V(out)=2.5 V(n1)=1.2
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string ic_spec = tokens[i];

        // Parse V(node)=value
        size_t eq_pos = ic_spec.find('=');
        if (eq_pos == std::string::npos) {
            spdlog::warn("Skipping malformed .IC specification (no '='): {}", ic_spec);
            continue;
        }

        std::string node_spec = ic_spec.substr(0, eq_pos);  // "V(node)"
        std::string value_str = ic_spec.substr(eq_pos + 1);  // "value"

        // Extract node name from V(node)
        if (node_spec.size() < 3 || node_spec[0] != 'V' || node_spec[1] != '(') {
            spdlog::warn("Invalid .IC node specification (expected V(node)): {}", node_spec);
            continue;
        }

        size_t close_paren = node_spec.find(')');
        if (close_paren == std::string::npos) {
            spdlog::warn("Invalid .IC node specification (missing ')'): {}", node_spec);
            continue;
        }

        std::string node_name = node_spec.substr(2, close_paren - 2);
        Real value = parse_value_with_unit(value_str);

        // Store in config
        state.config->ic[node_name] = value;
        spdlog::debug("Set .IC V({})={}", node_name, value);
    }

    spdlog::debug("Parsed .IC directive with {} initial conditions", tokens.size() - 1);
}

static void parse_nodeset_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .NODESET directive (Initial guess for DC analysis convergence)
    // Syntax: .NODESET V(node1)=value V(node2)=value ...
    // Example: .NODESET V(out)=2.5 V(n1)=1.2
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string nodeset_spec = tokens[i];

        // Parse V(node)=value
        size_t eq_pos = nodeset_spec.find('=');
        if (eq_pos == std::string::npos) {
            spdlog::warn("Skipping malformed .NODESET specification (no '='): {}", nodeset_spec);
            continue;
        }

        std::string node_spec = nodeset_spec.substr(0, eq_pos);  // "V(node)"
        std::string value_str = nodeset_spec.substr(eq_pos + 1);  // "value"

        // Extract node name from V(node)
        if (node_spec.size() < 3 || node_spec[0] != 'V' || node_spec[1] != '(') {
            spdlog::warn("Invalid .NODESET node specification (expected V(node)): {}", node_spec);
            continue;
        }

        size_t close_paren = node_spec.find(')');
        if (close_paren == std::string::npos) {
            spdlog::warn("Invalid .NODESET node specification (missing ')'): {}", node_spec);
            continue;
        }

        std::string node_name = node_spec.substr(2, close_paren - 2);
        Real value = parse_value_with_unit(value_str);

        // Store in config
        state.config->nodeset[node_name] = value;
        spdlog::debug("Set .NODESET V({})={}", node_name, value);
    }

    spdlog::debug("Parsed .NODESET directive with {} node hints", tokens.size() - 1);
}

static void parse_print_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .PRINT directive (Selective signal output)
    // Syntax: .PRINT dc|tran|ac signal1 signal2 ...
    // Examples:
    //   .PRINT dc v(out) i(v1)
    //   .PRINT tran v(in) v(out) i(r1)
    //   .PRINT ac vdb(out) vp(out) vm(out)
    // Supported signal types:
    //   v(node)    - Node voltage
    //   i(source)  - Branch current
    //   vdb(node)  - Voltage in dB (20*log10|V|)
    //   vp(node)   - Voltage phase (degrees)
    //   vm(node)   - Voltage magnitude

    if (tokens.size() < 3) {
        spdlog::warn("Invalid .PRINT syntax (expected: .PRINT dc|tran|ac signal1 signal2 ...)");
        return;
    }

    std::string analysis_type = tokens[1];
    std::transform(analysis_type.begin(), analysis_type.end(), analysis_type.begin(), ::tolower);

    // Validate analysis type
    if (analysis_type != "dc" && analysis_type != "tran" && analysis_type != "ac") {
        spdlog::warn("Invalid .PRINT analysis type: {} (expected dc|tran|ac)", analysis_type);
        return;
    }

    // .PRINT DC, .PRINT TRAN and .PRINT AC are recorded as directives of
    // their own (SimulationConfig::directives_read), since a program may
    // print one kind and not another
    std::string kind = analysis_type;
    util::to_upper_inplace(kind);
    state.config->directives_read.insert(".PRINT " + kind);

    // Parse signal specifications
    for (size_t i = 2; i < tokens.size(); ++i) {
        std::string signal = tokens[i];

        // Store signal in config (analysis type is implicit, can be extracted later)
        state.config->output.print_signals.push_back(signal);
        if (analysis_type == "tran") {
            state.config->output.print_tran_signals.push_back(signal);
        }
        spdlog::debug("Added .PRINT signal: {}", signal);
    }

    spdlog::debug("Parsed .PRINT {} directive with {} signals", analysis_type, tokens.size() - 2);
}

static void parse_save_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .SAVE directive (Selective signal saving to SSF)
    // Syntax: .SAVE signal1 signal2 ...
    // Examples:
    //   .SAVE v(out) v(in)
    //   .SAVE v(out) i(v1) i(r1)
    // Note: .SAVE signals are also added to .PRINT automatically

    if (tokens.size() < 2) {
        spdlog::warn("Invalid .SAVE syntax (expected: .SAVE signal1 signal2 ...)");
        return;
    }

    // Parse signal specifications
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string signal = tokens[i];

        // Store signal in config
        state.config->output.save_signals.push_back(signal);
        // Also add to print_signals (save implies print)
        state.config->output.print_signals.push_back(signal);
        spdlog::debug("Added .SAVE signal: {}", signal);
    }

    // Set save_all to false (selective save mode)
    state.config->output.save_all = false;

    spdlog::debug("Parsed .SAVE directive with {} signals (selective save mode enabled)", tokens.size() - 1);
}

static void parse_global_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .GLOBAL directive
    // Syntax: .GLOBAL node1 [node2 node3 ...]
    // Example: .GLOBAL VDD GND CLK
    // Global nodes are accessible across subcircuit boundaries without being in pin lists
    if (tokens.size() < 2) {
        throw ParseError("Invalid .GLOBAL syntax: .GLOBAL node1 [node2 ...]",
                        state.line_number);
    }

    // Add each node to the global nodes set
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string node_name = tokens[i];

        // Trim whitespace
        node_name.erase(0, node_name.find_first_not_of(" \t"));
        node_name.erase(node_name.find_last_not_of(" \t") + 1);

        if (!node_name.empty()) {
            state.config->global_nodes.insert(node_name);
            spdlog::debug("Added global node: {}", node_name);
        }
    }

    // Also set global nodes in CircuitGraph's SubcircuitRegistry
    // This allows subcircuit expansion to access global nodes
    if (state.circuit) {
        state.circuit->subcircuits().set_global_nodes(state.config->global_nodes);
    }

}

static void parse_param_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .PARAM directive (Global parameters)
    // Syntax: .PARAM name1=value1 [name2=value2 ...]
    // Example: .PARAM Vcc=5 R_load=1k tau={R_load*1u}
    if (tokens.size() < 2) {
        throw ParseError("Invalid .PARAM syntax: .PARAM name=value [name2=value2 ...]",
                        state.line_number);
    }

    // Setup ExprTk symbol table with the constants of every expression and
    // the existing global parameters
    exprtk::symbol_table<Real> symbol_table;
    for (const auto& constant : util::kExpressionConstants) {
        symbol_table.add_constant(constant.name, constant.value);
    }
    for (const auto& [name, value] : state.config->global_params) {
        // Check ExprTk return value
        if (!symbol_table.add_constant(name, value)) {
            spdlog::warn(".PARAM: Failed to add constant '{}' to symbol table", name);
        }
    }

    // Register user-defined functions from .FUNC directives
    // Store function objects (must persist while symbol_table is in use)
    std::vector<std::unique_ptr<UserDefinedFunction<1>>> funcs_1arg;
    std::vector<std::unique_ptr<UserDefinedFunction<2>>> funcs_2arg;
    std::vector<std::unique_ptr<UserDefinedFunction<3>>> funcs_3arg;

    for (const auto& [func_name, func_data] : state.config->global_funcs) {
        // Parse function data to determine number of arguments
        size_t pipe_pos = func_data.find('|');
        if (pipe_pos == std::string::npos) {
            spdlog::warn("Skipping invalid .FUNC '{}': missing pipe separator", func_name);
            continue;
        }

        std::string args_str = func_data.substr(0, pipe_pos);
        std::vector<std::string> arg_names = split_args(args_str);

        try {
            // Check ExprTk return values
            if (arg_names.size() == 1) {
                funcs_1arg.push_back(std::make_unique<UserDefinedFunction<1>>(func_data));
                if (!symbol_table.add_function(func_name, *funcs_1arg.back())) {
                    spdlog::warn(".PARAM: Failed to add function '{}' to symbol table", func_name);
                }
                spdlog::debug("Registered 1-arg .FUNC: {}", func_name);
            } else if (arg_names.size() == 2) {
                funcs_2arg.push_back(std::make_unique<UserDefinedFunction<2>>(func_data));
                if (!symbol_table.add_function(func_name, *funcs_2arg.back())) {
                    spdlog::warn(".PARAM: Failed to add function '{}' to symbol table", func_name);
                }
                spdlog::debug("Registered 2-arg .FUNC: {}", func_name);
            } else if (arg_names.size() == 3) {
                funcs_3arg.push_back(std::make_unique<UserDefinedFunction<3>>(func_data));
                if (!symbol_table.add_function(func_name, *funcs_3arg.back())) {
                    spdlog::warn(".PARAM: Failed to add function '{}' to symbol table", func_name);
                }
                spdlog::debug("Registered 3-arg .FUNC: {}", func_name);
            } else {
                spdlog::warn("Skipping .FUNC '{}': unsupported argument count {} (max 3)",
                            func_name, arg_names.size());
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to register .FUNC '{}': {}", func_name, e.what());
        }
    }

    // Parse each parameter assignment
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string param_spec = tokens[i];
        size_t eq_pos = param_spec.find('=');

        if (eq_pos == std::string::npos) {
            throw ParseError(fmt::format("Invalid .PARAM assignment '{}': expected name=value",
                                        param_spec), state.line_number);
        }

        std::string param_name = param_spec.substr(0, eq_pos);
        std::string value_str = param_spec.substr(eq_pos + 1);

        // Trim whitespace
        param_name.erase(0, param_name.find_first_not_of(" \t"));
        param_name.erase(param_name.find_last_not_of(" \t") + 1);
        value_str.erase(0, value_str.find_first_not_of(" \t"));
        value_str.erase(value_str.find_last_not_of(" \t") + 1);

        spdlog::debug(".PARAM parsing: param_name='{}', value_str='{}'", param_name, value_str);

        Real value;

        // Check if value contains {expression}
        if (!value_str.empty() && value_str.front() == '{' && value_str.back() == '}') {
            // Evaluate expression
            std::string expr = value_str.substr(1, value_str.size() - 2);

            try {
                exprtk::expression<Real> expression;
                expression.register_symbol_table(symbol_table);

                exprtk::parser<Real> parser;
                if (!parser.compile(expr, expression)) {
                    throw ParseError(fmt::format(
                        "Failed to parse .PARAM expression '{{{}}}': {}",
                        expr, parser.error()), state.line_number);
                }

                value = expression.value();
            } catch (const std::exception& e) {
                throw ParseError(fmt::format(
                    "Error evaluating .PARAM expression '{{{}}}': {}",
                    expr, e.what()), state.line_number);
            }
        } else {
            // Parse as literal value with units
            value = parse_value_with_unit(value_str);
        }

        // Check if parameter is already set by initial_params
        // If so, use the initial value instead of overriding it
        auto it = state.config->global_params.find(param_name);
        if (it != state.config->global_params.end()) {
            // Parameter already exists (set by initial_params)
            // Use the existing value, don't override
            value = it->second;
            spdlog::debug("Preserving initial_params override: {}={} (netlist value ignored)",
                         param_name, value);
        } else {
            // Store parameter (new parameter, not in initial_params)
            state.config->global_params[param_name] = value;
            state.evaluator->set_variable(param_name, value);
            spdlog::debug("Parsed .PARAM {}={}", param_name, value);
        }

        // Add to symbol table for subsequent parameters in same line
        // Check ExprTk return value
        if (!symbol_table.add_constant(param_name, value)) {
            spdlog::warn(".PARAM: Failed to add '{}' to symbol table for subsequent parameters",
                        param_name);
        }
    }

}

static void parse_step_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .STEP directive (Parameter sweep)
    // Syntax: .STEP [dec|oct] param <param_name> <start> <stop> <step>
    // Examples:
    //   .STEP param R 1k 10k 1k          (LIN sweep)
    //   .STEP dec param C 1p 1u 10       (DEC sweep: 10 points per decade)
    //   .STEP oct param L 1n 1m 5        (OCT sweep: 5 points per octave)
    //   .STEP param R list 1k 2k 5k 10k  (LIST sweep)

    if (tokens.size() < 5) {
        throw ParseError("Invalid .STEP syntax: .STEP [dec|oct] param <name> <start> <stop> <step>",
                        state.line_number);
    }

    StepConfig step_config;
    size_t param_idx = 1;

    // Check for sweep type (dec/oct)
    std::string first_arg = tokens[1];
    std::transform(first_arg.begin(), first_arg.end(), first_arg.begin(), ::tolower);

    if (first_arg == "dec") {
        step_config.sweep_type = StepType::DEC;
        param_idx = 2;
    } else if (first_arg == "oct") {
        step_config.sweep_type = StepType::OCT;
        param_idx = 2;
    } else {
        step_config.sweep_type = StepType::LIN;
        param_idx = 1;
    }

    // Next token should be "param"
    if (param_idx >= tokens.size()) {
        throw ParseError("Invalid .STEP syntax: missing 'param' keyword",
                        state.line_number);
    }

    std::string keyword = tokens[param_idx];
    std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);
    if (keyword != "param") {
        throw ParseError(fmt::format("Invalid .STEP syntax: expected 'param', got '{}'", keyword),
                        state.line_number);
    }
    ++param_idx;

    // Parameter name
    if (param_idx >= tokens.size()) {
        throw ParseError("Invalid .STEP syntax: missing parameter name",
                        state.line_number);
    }
    step_config.param_name = tokens[param_idx++];

    // Check for LIST sweep
    bool is_list_sweep = false;
    if (param_idx < tokens.size()) {
        std::string next_token = tokens[param_idx];
        std::transform(next_token.begin(), next_token.end(), next_token.begin(), ::tolower);
        if (next_token == "list") {
            is_list_sweep = true;
            step_config.sweep_type = StepType::LIST;
            ++param_idx;  // Skip "list" keyword
        }
    }

    if (is_list_sweep) {
        // Parse LIST values: all remaining tokens
        if (param_idx >= tokens.size()) {
            throw ParseError("Invalid .STEP LIST: no values provided",
                            state.line_number);
        }

        step_config.list_values.clear();
        while (param_idx < tokens.size()) {
            Real value = parse_value_with_unit(tokens[param_idx++]);
            step_config.list_values.push_back(value);
        }

        // Set dummy start/stop/step (not used for LIST)
        step_config.start_value = 0.0;
        step_config.stop_value = 0.0;
        step_config.step_value = 0.0;

    } else {
        // Parse start, stop, step values for LIN/DEC/OCT
        if (param_idx + 2 >= tokens.size()) {
            throw ParseError("Invalid .STEP syntax: missing start/stop/step values",
                            state.line_number);
        }

        step_config.start_value = parse_value_with_unit(tokens[param_idx++]);
        step_config.stop_value = parse_value_with_unit(tokens[param_idx++]);
        step_config.step_value = parse_value_with_unit(tokens[param_idx++]);
    }

    // Validate sweep parameters
    if (step_config.sweep_type == StepType::LIN) {
        if (step_config.step_value == 0.0) {
            throw ParseError("Invalid .STEP: step size cannot be zero for LIN sweep",
                            state.line_number);
        }
        if ((step_config.stop_value > step_config.start_value && step_config.step_value < 0) ||
            (step_config.stop_value < step_config.start_value && step_config.step_value > 0)) {
            throw ParseError("Invalid .STEP: step direction doesn't match start/stop range",
                            state.line_number);
        }
    }

    if (step_config.sweep_type == StepType::DEC || step_config.sweep_type == StepType::OCT) {
        if (step_config.start_value <= 0 || step_config.stop_value <= 0) {
            throw ParseError("Invalid .STEP: start and stop values must be positive for DEC/OCT sweep",
                            state.line_number);
        }
        if (step_config.step_value < 1) {
            throw ParseError("Invalid .STEP: points per decade/octave must be >= 1",
                            state.line_number);
        }
    }

    // Store step configuration
    state.config->step_configs.push_back(step_config);

    // Log parsed .STEP directive
    std::string type_str;
    switch (step_config.sweep_type) {
        case StepType::LIN: type_str = "LIN"; break;
        case StepType::DEC: type_str = "DEC"; break;
        case StepType::OCT: type_str = "OCT"; break;
        case StepType::LIST: type_str = "LIST"; break;
    }

    if (step_config.sweep_type == StepType::LIST) {
        auto joined_values = fmt::format("{}", fmt::join(step_config.list_values, ", "));
        spdlog::info("Parsed .STEP: param={}, type={}, values=[{}], num_points={}",
                    step_config.param_name,
                    type_str,
                    joined_values,
                    step_config.num_points());
    } else {
        spdlog::info("Parsed .STEP: param={}, type={}, start={}, stop={}, step={}, num_points={}",
                    step_config.param_name,
                    type_str,
                    step_config.start_value,
                    step_config.stop_value,
                    step_config.step_value,
                    step_config.num_points());
    }

}

static void parse_sens_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .SENS directive (Sensitivity analysis)
    // Syntax: .SENS v(node1) v(node2) ...
    // Example: .SENS v(out) v(internal)
    if (tokens.size() < 2) {
        throw ParseError("Invalid .SENS syntax: .SENS v(node1) [v(node2) ...]",
                        state.line_number);
    }

    // Parse output nodes
    for (size_t i = 1; i < tokens.size(); ++i) {
        std::string signal = tokens[i];

        // Extract node name from v(node) format
        if (signal.size() >= 4 && signal[0] == 'v' && signal[1] == '(') {
            size_t close_paren = signal.find(')');
            if (close_paren != std::string::npos) {
                std::string node_name = signal.substr(2, close_paren - 2);
                state.config->sensitivity.output_nodes.push_back(node_name);
            } else {
                throw ParseError(fmt::format("Invalid .SENS signal format: {} (expected v(node))", signal),
                                state.line_number);
            }
        } else {
            throw ParseError(fmt::format("Invalid .SENS signal format: {} (expected v(node))", signal),
                            state.line_number);
        }
    }

    spdlog::info("Parsed .SENS: {} output nodes",
                state.config->sensitivity.output_nodes.size());

}

static void parse_temp_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .TEMP directive (Temperature sweep)
    // Syntax: .TEMP temp1 temp2 temp3 ... (temperatures in Celsius)
    // Example: .TEMP -40 0 27 85 125

    if (tokens.size() < 2) {
        throw ParseError("Invalid .TEMP syntax: .TEMP temp1 temp2 temp3 ... (at least one temperature required)",
                        state.line_number);
    }

    state.config->temp_sweep.temperatures.clear();
    state.config->temp_sweep.enabled = true;

    // Parse all temperature values (in Celsius) and convert to Kelvin
    for (size_t i = 1; i < tokens.size(); ++i) {
        Real temp_celsius = parse_value_with_unit(tokens[i]);
        Real temp_kelvin = core::celsius_to_kelvin(temp_celsius);

        // Validate temperature (must be above absolute zero)
        if (temp_kelvin < 0.0) {
            throw ParseError(fmt::format("Invalid .TEMP: temperature {}°C is below absolute zero", temp_celsius),
                            state.line_number);
        }

        state.config->temp_sweep.temperatures.push_back(temp_kelvin);
    }

    // Log parsed .TEMP directive
    std::vector<Real> temps_celsius;
    for (Real temp_k : state.config->temp_sweep.temperatures) {
        temps_celsius.push_back(temp_k - 273.15);
    }

    spdlog::info("Parsed .TEMP: num_points={}, temperatures=[{}]°C",
                state.config->temp_sweep.temperatures.size(),
                fmt::join(temps_celsius, ", "));

}

static void parse_mc_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .MC directive (Monte Carlo analysis)
    // Syntax: .MC <num_runs> <analysis_type> [seed=N]
    // Example: .MC 1000 DC
    // Example: .MC 500 DC seed=42
    // Note: DEV/VARY variations will be added
    if (tokens.size() < 3) {
        throw ParseError("Invalid .MC syntax: .MC <num_runs> <analysis_type> [seed=N]",
                        state.line_number);
    }

    // Parse num_runs
    try {
        state.config->monte_carlo.num_runs = std::stoul(tokens[1]);
    } catch (const std::invalid_argument& e) {
        throw ParseError(fmt::format("Invalid .MC num_runs '{}': not a valid number", tokens[1]),
                        state.line_number);
    } catch (const std::out_of_range& e) {
        throw ParseError(fmt::format("Invalid .MC num_runs '{}': value out of range", tokens[1]),
                        state.line_number);
    }

    if (state.config->monte_carlo.num_runs == 0) {
        throw ParseError(".MC num_runs must be > 0",
                        state.line_number);
    }

    // Parse analysis type (DC, AC, TRAN)
    std::string analysis_type = tokens[2];
    std::transform(analysis_type.begin(), analysis_type.end(), analysis_type.begin(), ::toupper);
    // For now, we only validate that it's a recognized type
    if (analysis_type != "DC" && analysis_type != "AC" && analysis_type != "TRAN") {
        throw ParseError(fmt::format("Invalid .MC analysis type: {} (expected DC/AC/TRAN)", tokens[2]),
                        state.line_number);
    }

    // Parse optional seed parameter
    if (tokens.size() >= 4) {
        std::string seed_param = tokens[3];
        std::transform(seed_param.begin(), seed_param.end(), seed_param.begin(), ::toupper);
        if (seed_param.find("SEED=") == 0) {
            std::string seed_str = seed_param.substr(5);
            try {
                state.config->monte_carlo.seed = std::stoul(seed_str);
            } catch (const std::invalid_argument& e) {
                throw ParseError(fmt::format("Invalid .MC seed '{}': not a valid number", seed_str),
                                state.line_number);
            } catch (const std::out_of_range& e) {
                throw ParseError(fmt::format("Invalid .MC seed '{}': value out of range", seed_str),
                                state.line_number);
            }
        }
    }

    spdlog::info("Parsed .MC: num_runs={}, seed={}",
                state.config->monte_carlo.num_runs,
                state.config->monte_carlo.seed);

    // TODO Parse DEV/VARY continuation lines for parameter variations
    // For now, Monte Carlo will vary all parameters with default tolerances

}

static void parse_tf_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .TF directive (Transfer function analysis)
    // Syntax: .TF output_signal input_source
    // Example: .TF v(out) Vin
    // Example: .TF i(Vload) Vin
    if (tokens.size() != 3) {
        throw ParseError("Invalid .TF syntax: .TF output_signal input_source",
                        state.line_number);
    }

    std::string output_signal = tokens[1];
    std::string input_source = tokens[2];

    // Validate output signal format (should be v(node) or i(source))
    bool valid_output = false;
    if (output_signal.size() >= 4) {
        if ((output_signal[0] == 'v' || output_signal[0] == 'i') &&
            output_signal[1] == '(' &&
            output_signal.find(')') != std::string::npos) {
            valid_output = true;
        }
    }

    if (!valid_output) {
        throw ParseError(fmt::format("Invalid .TF output signal format: {} (expected v(node) or i(source))", output_signal),
                        state.line_number);
    }

    // Store configuration
    state.config->tf.output_signal = output_signal;
    state.config->tf.input_source = input_source;
    state.config->tf.enabled = true;

    spdlog::info("Parsed .TF: output={}, input={}", output_signal, input_source);

}

static void parse_four_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .FOUR directive (Fourier analysis)
    // Syntax: .FOUR fundamental_freq signal
    // Example: .FOUR 1kHz v(out)
    // Example: .FOUR 60 i(Vload)
    if (tokens.size() < 3) {
        throw ParseError("Invalid .FOUR syntax: .FOUR fundamental_freq signal",
                        state.line_number);
    }

    // Parse fundamental frequency (with unit suffix support)
    std::string freq_str = tokens[1];

    // Strip "Hz" or "hz" suffix if present (e.g., "1kHz" -> "1k")
    if (freq_str.size() >= 2) {
        std::string last_two = freq_str.substr(freq_str.size() - 2);
        if (last_two == "Hz" || last_two == "hz" || last_two == "HZ") {
            freq_str = freq_str.substr(0, freq_str.size() - 2);
        }
    }

    Real fundamental_freq = parse_value_with_unit(freq_str);
    if (fundamental_freq <= 0.0) {
        throw ParseError(fmt::format("Invalid .FOUR fundamental frequency: {} (must be positive)", freq_str),
                        state.line_number);
    }

    // The signals: each v(node) or i(source), in any case
    std::vector<std::string> signals(tokens.begin() + 2, tokens.end());
    for (const auto& signal : signals) {
        bool valid_signal = false;
        if (signal.size() >= 4) {
            const char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(signal[0])));
            if ((kind == 'v' || kind == 'i') && signal[1] == '(' && signal.back() == ')') {
                valid_signal = true;
            }
        }
        if (!valid_signal) {
            throw ParseError(fmt::format("Invalid .FOUR signal format: {} (expected v(node) or i(source))", signal),
                            state.line_number);
        }
    }

    // Store configuration
    state.config->fourier.fundamental_freq = fundamental_freq;
    state.config->fourier.signal = signals.front();
    state.config->fourier.signals = signals;
    state.config->fourier.enabled = true;

    spdlog::info("Parsed .FOUR: freq={} Hz, signals={}", fundamental_freq, fmt::join(signals, " "));

}

static void parse_pz_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .PZ directive (Pole-Zero analysis)
    // Syntax: .PZ node1 node2 input_source {POL|ZER|PZ}
    // Example: .PZ out 0 Vin PZ
    // Example: .PZ out 0 Vin POL
    if (tokens.size() < 5) {
        throw ParseError("Invalid .PZ syntax: .PZ node1 node2 input_source {POL|ZER|PZ}",
                        state.line_number);
    }

    std::string node1 = tokens[1];
    std::string node2 = tokens[2];
    std::string input_source = tokens[3];
    std::string type_str = tokens[4];

    // Parse analysis type (case-insensitive)
    SimulationConfig::PZ::AnalysisType type;
    std::string type_upper = type_str;
    std::transform(type_upper.begin(), type_upper.end(), type_upper.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    if (type_upper == "POL" || type_upper == "POLES") {
        type = SimulationConfig::PZ::AnalysisType::POLES_ONLY;
    } else if (type_upper == "ZER" || type_upper == "ZEROS") {
        type = SimulationConfig::PZ::AnalysisType::ZEROS_ONLY;
    } else if (type_upper == "PZ") {
        type = SimulationConfig::PZ::AnalysisType::POLES_ZEROS;
    } else {
        throw ParseError(fmt::format("Invalid .PZ analysis type: {} (expected POL, ZER, or PZ)", type_str),
                        state.line_number);
    }

    // Store configuration
    state.config->pz.node1 = node1;
    state.config->pz.node2 = node2;
    state.config->pz.input_source = input_source;
    state.config->pz.type = type;
    state.config->pz.enabled = true;

    spdlog::info("Parsed .PZ: nodes=({}, {}), source={}, type={}",
                node1, node2, input_source, type_str);

}

static void parse_disto_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .DISTO directive (Distortion analysis)
    // Syntax: .DISTO RLOAD INTER [SKEW]
    // Example: .DISTO 1k 0        (harmonic distortion only)
    // Example: .DISTO 10k 1       (intermodulation distortion only)
    // Example: .DISTO 1k 2 1.1    (both, with skew=1.1)
    // INTER: 0=harmonic, 1=intermodulation, 2=both
    // SKEW: F2/F1 ratio for IMD (optional, default=1.0)
    if (tokens.size() < 3) {
        throw ParseError("Invalid .DISTO syntax: .DISTO RLOAD INTER [SKEW]",
                        state.line_number);
    }

    // Parse RLOAD
    Real rload = parse_value_with_unit(tokens[1]);
    if (rload <= 0.0) {
        throw ParseError("Invalid .DISTO: RLOAD must be positive",
                        state.line_number);
    }

    // Parse INTER (analysis mode)
    int inter_value;
    try {
        inter_value = std::stoi(tokens[2]);
    } catch (const std::invalid_argument& e) {
        throw ParseError(fmt::format("Invalid .DISTO INTER value '{}': not a valid integer (expected 0, 1, or 2)", tokens[2]),
                        state.line_number);
    } catch (const std::out_of_range& e) {
        throw ParseError(fmt::format("Invalid .DISTO INTER value '{}': value out of range (expected 0, 1, or 2)", tokens[2]),
                        state.line_number);
    }

    SimulationConfig::Distortion::AnalysisMode mode;
    if (inter_value == 0) {
        mode = SimulationConfig::Distortion::AnalysisMode::HARMONIC;
    } else if (inter_value == 1) {
        mode = SimulationConfig::Distortion::AnalysisMode::INTERMODULATION;
    } else if (inter_value == 2) {
        mode = SimulationConfig::Distortion::AnalysisMode::BOTH;
    } else {
        throw ParseError(fmt::format("Invalid .DISTO INTER value: {} (expected 0, 1, or 2)", inter_value),
                        state.line_number);
    }

    // Parse optional SKEW
    Real skew = 1.0;
    if (tokens.size() >= 4) {
        skew = parse_value_with_unit(tokens[3]);
        if (skew <= 0.0) {
            throw ParseError("Invalid .DISTO: SKEW must be positive",
                            state.line_number);
        }
    }

    // Store configuration
    state.config->distortion.rload = rload;
    state.config->distortion.mode = mode;
    state.config->distortion.f2_f1_ratio = skew;
    state.config->distortion.enabled = true;

    spdlog::info("Parsed .DISTO: rload={} Ω, mode={}, skew={}",
                rload, inter_value, skew);

}

static void parse_port_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .PORT directive (S-parameter port definition)
    // Syntax: .PORT <port_number> <node_positive> <node_negative> Z=<impedance>
    // Example: .PORT 1 in 0 Z=50
    // Example: .PORT 2 out 0 Z=75
    if (tokens.size() < 4) {
        throw ParseError("Invalid .PORT syntax: .PORT <port_number> <node_positive> <node_negative> [Z=<impedance>]",
                        state.line_number);
    }

    // Parse port number
    std::uint32_t port_number;
    try {
        port_number = static_cast<std::uint32_t>(std::stoul(tokens[1]));
    } catch (const std::invalid_argument& e) {
        throw ParseError(fmt::format("Invalid .PORT port number '{}': not a valid integer", tokens[1]),
                        state.line_number);
    } catch (const std::out_of_range& e) {
        throw ParseError(fmt::format("Invalid .PORT port number '{}': value out of range", tokens[1]),
                        state.line_number);
    }

    if (port_number == 0) {
        throw ParseError("Invalid .PORT: port number must be >= 1",
                        state.line_number);
    }

    // Parse node names
    std::string node_positive = tokens[2];
    std::string node_negative = tokens[3];

    // Parse impedance (optional, default: 50Ω)
    Real impedance = 50.0;  // Default characteristic impedance
    if (tokens.size() >= 5) {
        // Check for Z=<value> format
        std::string impedance_str = tokens[4];
        if (impedance_str.size() > 2 && impedance_str[0] == 'Z' && impedance_str[1] == '=') {
            std::string value_str = impedance_str.substr(2);
            impedance = parse_value_with_unit(value_str);
            if (impedance <= 0.0) {
                throw ParseError("Invalid .PORT: impedance must be positive",
                                state.line_number);
            }
        } else {
            throw ParseError(fmt::format("Invalid .PORT impedance format: {} (expected Z=<value>)", impedance_str),
                            state.line_number);
        }
    }

    // Create port definition
    PortDefinition port;
    port.port_number = port_number;
    port.node_positive = node_positive;
    port.node_negative = node_negative;
    port.impedance = impedance;

    // Add to configuration
    state.config->sparam.ports.push_back(port);

    spdlog::info("Parsed .PORT: port={}, nodes=({}, {}), Z={} Ω",
                port_number, node_positive, node_negative, impedance);

}

static void parse_wcase_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .WCASE directive (Worst-case analysis)
    // Syntax: .WCASE <analysis_type> <output_variable> <objective> [VAR=param:dev%,...]
    // Example: .WCASE DC v(out) MAX
    // Example: .WCASE TRAN i(vload) MIN
    // Example: .WCASE AC vdb(out) MAX VAR=R1.resistance:10%,Q1.BF:20%
    if (tokens.size() < 4) {
        throw ParseError("Invalid .WCASE syntax: .WCASE <analysis_type> <output_variable> <objective>",
                        state.line_number);
    }

    // Parse analysis type
    std::string analysis_type_str = tokens[1];
    std::transform(analysis_type_str.begin(), analysis_type_str.end(),
                  analysis_type_str.begin(), ::toupper);
    WorstCaseConfig::AnalysisType analysis_type;
    if (analysis_type_str == "DC") {
        analysis_type = WorstCaseConfig::AnalysisType::DC;
    } else if (analysis_type_str == "TRAN" || analysis_type_str == "TRANSIENT") {
        analysis_type = WorstCaseConfig::AnalysisType::TRAN;
    } else if (analysis_type_str == "AC") {
        analysis_type = WorstCaseConfig::AnalysisType::AC;
    } else {
        throw ParseError(fmt::format("Invalid .WCASE analysis type: {} (expected DC, TRAN, or AC)", tokens[1]),
                        state.line_number);
    }

    // Parse output variable (e.g., "v(out)", "i(vload)")
    std::string output_variable = tokens[2];

    // Parse objective
    std::string objective_str = tokens[3];
    std::transform(objective_str.begin(), objective_str.end(),
                  objective_str.begin(), ::toupper);
    WorstCaseConfig::Objective objective;
    if (objective_str == "MAX" || objective_str == "MAXIMIZE") {
        objective = WorstCaseConfig::Objective::MAXIMIZE;
    } else if (objective_str == "MIN" || objective_str == "MINIMIZE") {
        objective = WorstCaseConfig::Objective::MINIMIZE;
    } else {
        throw ParseError(fmt::format("Invalid .WCASE objective: {} (expected MAX or MIN)", tokens[3]),
                        state.line_number);
    }

    // Store configuration
    state.config->wcase.analysis_type = analysis_type;
    state.config->wcase.output_variable = output_variable;
    state.config->wcase.objective = objective;
    state.config->wcase.enabled = true;

    // Parse optional VAR=... parameter (comma-separated variations)
    for (std::size_t i = 4; i < tokens.size(); ++i) {
        std::string var_str = tokens[i];
        if (var_str.size() > 4 && var_str.substr(0, 4) == "VAR=") {
            std::string var_list_str = var_str.substr(4);
            // Split by comma: "R1.resistance:10%,Q1.BF:20%"
            std::vector<std::string> var_entries;
            std::size_t start = 0;
            std::size_t comma_pos;
            while ((comma_pos = var_list_str.find(',', start)) != std::string::npos) {
                var_entries.push_back(var_list_str.substr(start, comma_pos - start));
                start = comma_pos + 1;
            }
            var_entries.push_back(var_list_str.substr(start));  // Last entry

            for (const auto& entry : var_entries) {
                // Split by colon: "R1.resistance:10%"
                std::size_t colon_pos = entry.find(':');
                if (colon_pos == std::string::npos) {
                    throw ParseError(fmt::format("Invalid .WCASE VAR format: {} (expected param:dev%)", entry),
                                    state.line_number);
                }
                std::string param_name = entry.substr(0, colon_pos);
                std::string dev_str = entry.substr(colon_pos + 1);

                // Parse deviation percentage
                Real deviation_percent;
                if (dev_str.back() == '%') {
                    dev_str.pop_back();
                    deviation_percent = parse_value_with_unit(dev_str);
                } else {
                    throw ParseError(fmt::format("Invalid .WCASE deviation format: {} (expected X%)", entry),
                                    state.line_number);
                }

                if (deviation_percent < 0.0 || deviation_percent > 100.0) {
                    throw ParseError(fmt::format("Invalid .WCASE deviation: {}% (must be 0-100%)", deviation_percent),
                                    state.line_number);
                }

                // Create parameter variation
                WorstCaseConfig::ParameterVariation var;
                var.parameter_name = param_name;
                var.deviation_percent = deviation_percent;
                state.config->wcase.variations.push_back(var);

                spdlog::trace("  VAR: {} ±{}%", param_name, deviation_percent);
            }
        }
    }

    spdlog::info("Parsed .WCASE: {} {} {} ({} parameters)",
                tokens[1], output_variable, tokens[3],
                state.config->wcase.variations.size());

}

static void parse_optimize_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .OPTIMIZE directive (Circuit optimization)
    // Syntax: .OPTIMIZE <algorithm> OBJECTIVE=<measure> <MIN|MAX> PARAM=<name>:<init>:<lb>:<ub> [CONSTRAINT=<name>:<lb>:<ub>]
    // Example: .OPTIMIZE COBYLA OBJECTIVE=v(out) MAX PARAM=R1.resistance:1k:100:10k
    if (tokens.size() < 4) {
        throw ParseError("Invalid .OPTIMIZE syntax: .OPTIMIZE <algorithm> OBJECTIVE=<measure> <MIN|MAX>",
                        state.line_number);
    }

    // Parse algorithm
    std::string algorithm_str = tokens[1];
    std::transform(algorithm_str.begin(), algorithm_str.end(),
                  algorithm_str.begin(), ::toupper);
    OptimizerConfig::Algorithm algorithm;
    if (algorithm_str == "COBYLA") {
        algorithm = OptimizerConfig::Algorithm::COBYLA;
    } else if (algorithm_str == "BOBYQA") {
        algorithm = OptimizerConfig::Algorithm::BOBYQA;
    } else if (algorithm_str == "SLSQP") {
        algorithm = OptimizerConfig::Algorithm::SLSQP;
    } else if (algorithm_str == "NELDERMEAD" || algorithm_str == "NELDER-MEAD") {
        algorithm = OptimizerConfig::Algorithm::NELDERMEAD;
    } else {
        throw ParseError(fmt::format("Invalid .OPTIMIZE algorithm: {} (expected COBYLA, BOBYQA, SLSQP, or NELDERMEAD)", tokens[1]),
                        state.line_number);
    }

    // Parse OBJECTIVE=<measure>
    std::string objective_measure;
    if (tokens.size() > 2 && tokens[2].find("OBJECTIVE=") == 0) {
        objective_measure = tokens[2].substr(10);  // Skip "OBJECTIVE="
    } else if (tokens.size() > 2 && tokens[2].find("objective=") == 0) {
        objective_measure = tokens[2].substr(10);  // Case-insensitive
    } else {
        throw ParseError("Invalid .OPTIMIZE syntax: missing OBJECTIVE=<measure>",
                        state.line_number);
    }

    // Parse MIN/MAX
    if (tokens.size() < 4) {
        throw ParseError("Invalid .OPTIMIZE syntax: missing MIN or MAX",
                        state.line_number);
    }
    std::string objective_str = tokens[3];
    std::transform(objective_str.begin(), objective_str.end(),
                  objective_str.begin(), ::toupper);
    OptimizerConfig::Objective objective;
    if (objective_str == "MAX" || objective_str == "MAXIMIZE") {
        objective = OptimizerConfig::Objective::MAXIMIZE;
    } else if (objective_str == "MIN" || objective_str == "MINIMIZE") {
        objective = OptimizerConfig::Objective::MINIMIZE;
    } else {
        throw ParseError(fmt::format("Invalid .OPTIMIZE objective: {} (expected MAX or MIN)", tokens[3]),
                        state.line_number);
    }

    // Store configuration
    state.config->optimizer.algorithm = algorithm;
    state.config->optimizer.objective_measure = objective_measure;
    state.config->optimizer.objective = objective;
    state.config->optimizer.enabled = true;

    // Parse optional PARAM=... and CONSTRAINT=... parameters
    for (std::size_t i = 4; i < tokens.size(); ++i) {
        std::string param_str = tokens[i];
        std::string param_str_upper = param_str;
        std::transform(param_str_upper.begin(), param_str_upper.end(),
                      param_str_upper.begin(), ::toupper);

        // Check for PARAM=<name>:<init>:<lb>:<ub> (case-insensitive)
        if (param_str_upper.size() > 6 && param_str_upper.substr(0, 6) == "PARAM=") {
            std::string param_def = param_str.substr(6);
            // Split by colon: "R1.resistance:1k:100:10k"
            std::vector<std::string> parts;
            std::size_t start = 0;
            std::size_t colon_pos;
            while ((colon_pos = param_def.find(':', start)) != std::string::npos) {
                parts.push_back(param_def.substr(start, colon_pos - start));
                start = colon_pos + 1;
            }
            parts.push_back(param_def.substr(start));  // Last part

            if (parts.size() != 4) {
                throw ParseError(fmt::format("Invalid .OPTIMIZE PARAM format: {} (expected name:init:lb:ub)", param_def),
                                state.line_number);
            }

            std::string param_name = parts[0];
            Real initial_value = parse_value_with_unit(parts[1]);
            Real lower_bound = parse_value_with_unit(parts[2]);
            Real upper_bound = parse_value_with_unit(parts[3]);

            if (lower_bound >= upper_bound) {
                throw ParseError(fmt::format("Invalid .OPTIMIZE PARAM bounds: lb={} >= ub={}", lower_bound, upper_bound),
                                state.line_number);
            }

            OptimizerConfig::OptimizationParameter param;
            param.parameter_name = param_name;
            param.initial_value = initial_value;
            param.lower_bound = lower_bound;
            param.upper_bound = upper_bound;
            state.config->optimizer.parameters.push_back(param);

            spdlog::trace("  PARAM: {} init={:.3e} lb={:.3e} ub={:.3e}",
                         param_name, initial_value, lower_bound, upper_bound);
        }

        // Check for CONSTRAINT=<name>:<lb>:<ub> (case-insensitive)
        if (param_str_upper.size() > 11 && param_str_upper.substr(0, 11) == "CONSTRAINT=") {
            std::string constraint_def = param_str.substr(11);
            // Split by colon: "power:0:1"
            std::vector<std::string> parts;
            std::size_t start = 0;
            std::size_t colon_pos;
            while ((colon_pos = constraint_def.find(':', start)) != std::string::npos) {
                parts.push_back(constraint_def.substr(start, colon_pos - start));
                start = colon_pos + 1;
            }
            parts.push_back(constraint_def.substr(start));  // Last part

            if (parts.size() != 3) {
                throw ParseError(fmt::format("Invalid .OPTIMIZE CONSTRAINT format: {} (expected name:lb:ub)", constraint_def),
                                state.line_number);
            }

            std::string measure_name = parts[0];
            Real lower_bound = parse_value_with_unit(parts[1]);
            Real upper_bound = parse_value_with_unit(parts[2]);

            OptimizerConfig::Constraint constraint;
            constraint.measure_name = measure_name;
            constraint.lower_bound = lower_bound;
            constraint.upper_bound = upper_bound;
            state.config->optimizer.constraints.push_back(constraint);

            spdlog::trace("  CONSTRAINT: {} lb={:.3e} ub={:.3e}",
                         measure_name, lower_bound, upper_bound);
        }
    }

    if (state.config->optimizer.parameters.empty()) {
        throw ParseError(".OPTIMIZE requires at least one PARAM=...",
                        state.line_number);
    }

    spdlog::info("Parsed .OPTIMIZE: {} {} {} ({} parameters, {} constraints)",
                tokens[1], objective_measure, tokens[3],
                state.config->optimizer.parameters.size(),
                state.config->optimizer.constraints.size());

}

static void parse_func_directive(const std::vector<std::string>& tokens, const std::string& line, ParseState& state) {
    // Parse .FUNC directive (Global function definitions)
    // Syntax: .FUNC fname(arg1, arg2, ...) {expression}
    // Example: .FUNC RC(r, c) {r*c}
    if (tokens.size() < 2) {
        throw ParseError("Invalid .FUNC syntax: .FUNC name(args) {expr}",
                        state.line_number);
    }

    // Reconstruct full line (to handle spaces in expression)
    std::string func_def = line;
    size_t func_start = func_def.find(".FUNC");
    if (func_start == std::string::npos) {
        func_start = func_def.find(".func");
    }
    func_def = func_def.substr(func_start + 5);  // Skip ".FUNC"

    // Trim whitespace
    func_def.erase(0, func_def.find_first_not_of(" \t"));

    // Parse function signature: fname(arg1, arg2, ...)
    size_t paren_open = func_def.find('(');
    size_t paren_close = func_def.find(')');

    if (paren_open == std::string::npos || paren_close == std::string::npos) {
        throw ParseError("Invalid .FUNC syntax: missing parentheses",
                        state.line_number);
    }

    std::string func_name = func_def.substr(0, paren_open);
    func_name.erase(0, func_name.find_first_not_of(" \t"));
    func_name.erase(func_name.find_last_not_of(" \t") + 1);

    std::string args = func_def.substr(paren_open + 1, paren_close - paren_open - 1);

    // Parse expression: {expression}
    size_t brace_open = func_def.find('{', paren_close);
    size_t brace_close = func_def.rfind('}');

    if (brace_open == std::string::npos || brace_close == std::string::npos) {
        throw ParseError("Invalid .FUNC syntax: missing braces around expression",
                        state.line_number);
    }

    std::string expression = func_def.substr(brace_open + 1, brace_close - brace_open - 1);

    // Store function definition as "args|expression"
    std::string func_data = args + "|" + expression;
    state.config->global_funcs[func_name] = func_data;

    spdlog::debug("Parsed .FUNC {}({})", func_name, args);
}

static void parse_model_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .MODEL directive (Device model definitions)
    // Syntax: .MODEL <model_name> <type>(<param1>=<value1> <param2>=<value2> ...)
    // Example: .MODEL 2N3904 NPN(IS=1e-14 BF=200 VAF=100)
    if (tokens.size() < 3) {
        throw ParseError("Invalid .MODEL syntax: .MODEL <name> <type>(<params>)",
                        state.line_number);
    }

    std::string model_name = tokens[1];

    // Extract device type (may have parameters attached like "NPN(IS=1e-14")
    std::string device_type_str = tokens[2];
    size_t paren_pos = device_type_str.find('(');
    if (paren_pos != std::string::npos) {
        device_type_str = device_type_str.substr(0, paren_pos);
    }
    std::transform(device_type_str.begin(), device_type_str.end(),
                 device_type_str.begin(), ::toupper);

    // Parse device type
    DeviceType device_type;
    if (device_type_str == "NPN") {
        device_type = DeviceType::BJT_NPN;
    } else if (device_type_str == "PNP") {
        device_type = DeviceType::BJT_PNP;
    } else if (device_type_str == "D" || device_type_str == "DIODE") {
        device_type = DeviceType::DIODE;
    } else if (device_type_str == "NMOS") {
        device_type = DeviceType::MOSFET_N;
    } else if (device_type_str == "PMOS") {
        device_type = DeviceType::MOSFET_P;
    } else if (device_type_str == "NMF") {
        device_type = DeviceType::MESFET_N;
    } else if (device_type_str == "PMF") {
        device_type = DeviceType::MESFET_P;
    } else {
        throw ParseError(fmt::format("Unknown .MODEL device type: {} (expected NPN, PNP, D, NMOS, PMOS, NMF, PMF)",
                                    device_type_str), state.line_number);
    }

    // Parse parameters from parentheses or space-separated
    std::map<std::string, Real> params;

    // Reconstruct parameter string (may span multiple tokens)
    // Include tokens[2] in case it contains parameters (e.g., "NPN(IS=1e-14")
    std::string param_str;
    for (size_t i = 2; i < tokens.size(); ++i) {
        param_str += tokens[i];
        if (i + 1 < tokens.size()) param_str += " ";
    }

    // Extract content inside parentheses OR use space-separated parameters
    size_t paren_open = param_str.find('(');
    size_t paren_close = param_str.rfind(')');

    std::string params_content;
    if (paren_open != std::string::npos && paren_close != std::string::npos
        && paren_close > paren_open) {
        // Parentheses syntax: NMOS(LEVEL=8 VTH0=0.7)
        params_content = param_str.substr(paren_open + 1,
                                         paren_close - paren_open - 1);
    } else if (tokens.size() > 3) {
        // Space-separated syntax: NMOS LEVEL=8 VTH0=0.7
        // tokens[3] onwards are parameters
        for (size_t i = 3; i < tokens.size(); ++i) {
            params_content += tokens[i];
            if (i + 1 < tokens.size()) params_content += " ";
        }
    }

    // Parse key=value pairs. Each is one token, whatever blanks stood around
    // its '=' (util::split_netlist_line), and a brace expression stays in its
    // token when the text is split again.
    if (!params_content.empty()) {
        for (const std::string& current : util::split_netlist_tokens(params_content)) {
            const size_t eq_pos = current.find('=');
            if (eq_pos == std::string::npos) {
                continue;
            }
            std::string key = current.substr(0, eq_pos);
            const std::string value_str = current.substr(eq_pos + 1);

            if (key.empty() || value_str.empty()) {
                continue;
            }

            // Convert key to lowercase
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);

            // Parse value (support expressions in { } )
            Real value;
            if (value_str.size() >= 2 && value_str.front() == '{' && value_str.back() == '}') {
                std::string expr = value_str.substr(1, value_str.size() - 2);
                value = state.evaluator->evaluate(expr);
            } else {
                value = parse_value_with_unit(value_str);
            }

            params[key] = value;
        }
    }

    // The LEVEL of a MOSFET card selects the device. A LEVEL that no device
    // has is an error, as in ngspice-47, not level 1.
    if (device_type == DeviceType::MOSFET_N || device_type == DeviceType::MOSFET_P) {
        auto level_it = params.find("level");
        if (level_it != params.end()) {
            int level = static_cast<int>(level_it->second);
            static constexpr int known_levels[] = {1, 3, 8, 9, 10, 11, 12, 14, 20, 21, 49, 54};
            if (level_it->second != static_cast<Real>(level) ||
                std::find(std::begin(known_levels), std::end(known_levels), level) ==
                    std::end(known_levels)) {
                throw ParseError(
                    fmt::format("MOSFET model {}: LEVEL={} is not implemented (levels 1, 3, "
                                "8/49, 9, 10, 11, 12, 14/54, 20, 21)",
                                model_name, level_it->second),
                    state.line_number);
            }
            if (level == 3) {
                // MOSFET Level 3 model
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::MOSFET_LEVEL3_N;
                    spdlog::info("Detected Level 3 N-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::MOSFET_LEVEL3_P;
                    spdlog::info("Detected Level 3 P-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 9) {
                // BSIM1 model (Berkeley Short-channel IGFET Model v1)
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::MOSFET_BSIM1_N;
                    spdlog::info("Detected BSIM1 N-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::MOSFET_BSIM1_P;
                    spdlog::info("Detected BSIM1 P-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 8 || level == 49) {
                // BSIM3v3 model (ngspice uses LEVEL=8 or LEVEL=49)
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::MOSFET_BSIM3_N;
                    spdlog::info("Detected BSIM3 N-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::MOSFET_BSIM3_P;
                    spdlog::info("Detected BSIM3 P-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 14 || level == 54) {
                // BSIM4 model: LEVEL 14 (as in ngspice) or 54, another number in use for it
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::MOSFET_BSIM4_N;
                    spdlog::info("Detected BSIM4 N-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::MOSFET_BSIM4_P;
                    spdlog::info("Detected BSIM4 P-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 10) {
                // SiC MOSFET model
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::MOSFET_SIC_N;
                    spdlog::info("Detected SiC N-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::MOSFET_SIC_P;
                    spdlog::info("Detected SiC P-channel MOSFET model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 11) {
                // GaN HEMT model
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::GAN_HEMT_N;
                    spdlog::info("Detected GaN N-channel HEMT model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::GAN_HEMT_P;
                    spdlog::info("Detected GaN P-channel HEMT model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 12) {
                // FinFET model
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::FINFET_N;
                    spdlog::info("Detected FinFET N-channel model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::FINFET_P;
                    spdlog::info("Detected FinFET P-channel model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 20) {
                // VDMOS model
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::VDMOS_N;
                    spdlog::info("Detected VDMOS N-channel model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::VDMOS_P;
                    spdlog::info("Detected VDMOS P-channel model: {} (LEVEL={})",
                                model_name, level);
                }
            } else if (level == 21) {
                // IGBT model
                if (device_type == DeviceType::MOSFET_N) {
                    device_type = DeviceType::IGBT_N;
                    spdlog::info("Detected IGBT N-channel model: {} (LEVEL={})",
                                model_name, level);
                } else {
                    device_type = DeviceType::IGBT_P;
                    spdlog::info("Detected IGBT P-channel model: {} (LEVEL={})",
                                model_name, level);
                }
            }
        }
    }

    // Store model definition
    ModelDefinition model;
    model.name = model_name;
    model.device_type = device_type;
    model.params = params;

    // Model names are compared without regard to case (find_model), so a
    // definition replaces an earlier one spelled with other cases.
    std::erase_if(state.config->models, [&](const auto& entry) {
        return entry.first != model_name && util::equals_icase(entry.first, model_name);
    });
    state.config->models[model_name] = model;

    spdlog::debug("Parsed .MODEL {} {} ({} parameters)",
                 model_name, device_type_str, params.size());
}

static void parse_subckt_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // Parse .SUBCKT directive
    // Syntax: .SUBCKT <name> <pin1> <pin2> ... [PARAM=VALUE ...]
    if (tokens.size() < 2) {
        throw ParseError("Invalid .SUBCKT syntax: .SUBCKT <name> <pin1> <pin2> ...",
                        state.line_number);
    }

    if (state.inside_subcircuit) {
        throw ParseError("Nested .SUBCKT definitions are not allowed (use X instances instead)",
                        state.line_number);
    }

    std::string subckt_name = tokens[1];
    std::vector<std::string> pins;
    std::map<std::string, Real> params;

    // Parse pins and parameters
    for (size_t i = 2; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        // Check if parameter (contains '=')
        size_t eq_pos = token.find('=');
        if (eq_pos != std::string::npos) {
            // Parameter: PARAM=VALUE
            std::string param_name = token.substr(0, eq_pos);
            std::string param_value_str = token.substr(eq_pos + 1);

            const Real param_value = read_element_value(param_value_str, state);

            // Convert param name to upper case (SPICE convention)
            std::transform(param_name.begin(), param_name.end(), param_name.begin(), ::toupper);
            params[param_name] = param_value;
        } else {
            // Pin name
            pins.push_back(token);
        }
    }

    // Create subcircuit definition
    circuit::SubcircuitDefinition def(subckt_name, pins, state.line_number);
    def.default_params = params;

    state.current_subcircuit = def;
    state.inside_subcircuit = true;

    spdlog::debug("Started .SUBCKT: name={}, pins={}, params={}",
                 subckt_name, pins.size(), params.size());
}

static void parse_ends_directive(const std::vector<std::string>& tokens, ParseState& state) {
    // End of subcircuit definition
    if (!state.inside_subcircuit) {
        throw ParseError(".ENDS without matching .SUBCKT", state.line_number);
    }

    // Optionally verify subcircuit name matches
    if (tokens.size() > 1) {
        std::string ends_name = tokens[1];
        if (ends_name != state.current_subcircuit->name) {
            spdlog::warn("Line {}: .ENDS name '{}' does not match .SUBCKT name '{}'",
                        state.line_number, ends_name, state.current_subcircuit->name);
        }
    }

    // Register subcircuit in circuit
    state.circuit->subcircuits().register_subcircuit(*state.current_subcircuit);

    spdlog::debug("Ended .SUBCKT: name={}, internal_lines={}",
                 state.current_subcircuit->name,
                 state.current_subcircuit->internal_netlist_lines.size());

    // Reset state
    state.current_subcircuit.reset();
    state.inside_subcircuit = false;
}

static void parse_resistor_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    if (tokens.size() < 4) {
        throw ParseError("Invalid resistor syntax", state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];
    std::string value_str = tokens[3];

    const Real value = read_element_value(value_str, state);

    state.circuit->add_resistor(dev_name, np, nn, value);
    spdlog::debug("Added resistor: {} {} {} {}Ω", dev_name, np, nn, value);
}

static void parse_behavioral_source_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // Behavioral source: Bname n+ n- V={expr} or I={expr}
    if (tokens.size() < 4) {
        throw ParseError("Invalid behavioral source syntax: Bname n+ n- V={expr} or I={expr}",
                       state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];

    // Parse V={expr} or I={expr}
    BehavioralSourceType type;
    std::string expression;
    bool found = false;

    for (size_t i = 3; i < tokens.size(); ++i) {
        std::string token = tokens[i];

        // Check for V= or I= prefix (case insensitive)
        std::string prefix_upper = token.substr(0, std::min<size_t>(2, token.size()));
        std::transform(prefix_upper.begin(), prefix_upper.end(), prefix_upper.begin(), ::toupper);

        if (prefix_upper.size() >= 2 && prefix_upper[1] == '=') {
            char specifier = prefix_upper[0];
            if (specifier == 'V') {
                type = BehavioralSourceType::VOLTAGE;
            } else if (specifier == 'I') {
                type = BehavioralSourceType::CURRENT;
            } else {
                continue;  // Not V= or I=, try next token
            }

            // The expression follows V= or I=: a brace group, or, without
            // braces, the rest of the line, blanks and all, as in ngspice
            expression = token.substr(2);
            if (!expression.empty() && expression.front() == '{' && expression.back() == '}') {
                expression = expression.substr(1, expression.size() - 2);
            } else {
                for (size_t j = i + 1; j < tokens.size(); ++j) {
                    expression += " " + tokens[j];
                }
            }

            found = true;
            break;
        }
    }

    if (!found) {
        throw ParseError("Behavioral source missing V={expr} or I={expr}",
                       state.line_number);
    }

    state.circuit->add_behavioral_source(dev_name, np, nn, type, expression);
    spdlog::debug("Added behavioral source: {} {} {} {}={}",
                 dev_name, np, nn,
                 (type == BehavioralSourceType::VOLTAGE ? "V" : "I"),
                 expression);
}

// IC=<value> after the value of a C or L line: the capacitor's voltage or
// the inductor's current at the start of a transient under UIC. Anything
// else after the value is not used, and named in a warning.
static std::optional<Real> read_initial_condition(const std::vector<std::string>& tokens,
                                                  ParseState& state) {
    std::optional<Real> ic;
    for (std::size_t i = 4; i < tokens.size(); ++i) {
        std::string token = tokens[i];
        util::to_upper_inplace(token);
        if (token.rfind("IC=", 0) == 0 && token.size() > 3) {
            ic = read_element_value(tokens[i].substr(3), state);
        } else {
            spdlog::warn("Line {}: {}: '{}' is not used", state.line_number, tokens[0], tokens[i]);
        }
    }
    return ic;
}

static void parse_capacitor_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    if (tokens.size() < 4) {
        throw ParseError("Invalid capacitor syntax", state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];
    std::string value_str = tokens[3];

    const Real value = read_element_value(value_str, state);

    state.circuit->add_capacitor(dev_name, np, nn, value, read_initial_condition(tokens, state));
    spdlog::debug("Added capacitor: {} {} {} {}F", dev_name, np, nn, value);
}

static void parse_inductor_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    if (tokens.size() < 4) {
        throw ParseError("Invalid inductor syntax", state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];
    std::string value_str = tokens[3];

    const Real value = read_element_value(value_str, state);

    state.circuit->add_inductor(dev_name, np, nn, value, read_initial_condition(tokens, state));
    spdlog::debug("Added inductor: {} {} {} {}H", dev_name, np, nn, value);
}

// The specification of an independent source after its nodes, in any order:
// [DC] <value>, AC [<magnitude> [<phase>]], and a transient waveform
// PULSE, SIN, EXP, PWL or SFFM with its arguments in parentheses (or without
// them, up to the next keyword), separated by spaces or commas; an argument
// may be an expression in braces. Every analysis reads the source through
// VoltageSource or CurrentSource (set_waveform): the operating point and the
// DC sweep take the DC value, or the waveform at time 0 when there is none;
// the transient takes the waveform; AC takes the AC input.
struct SourceSpecification {
    Real dc_value = 0.0;
    bool dc_given = false;
    util::AcInput ac;
    std::shared_ptr<const devices::Waveform> waveform;
};

static bool is_waveform_keyword(const std::string& token, std::string& keyword) {
    std::string upper = token.substr(0, token.find('('));
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    for (const char* name : {"PULSE", "SIN", "EXP", "PWL", "SFFM"}) {
        if (upper == name) {
            keyword = name;
            return true;
        }
    }
    return false;
}

// The arguments of a waveform: the text inside its parentheses split at
// spaces and commas, each a number or an expression in braces
static std::vector<std::string> split_waveform_arguments(const std::string& inside) {
    std::vector<std::string> arguments;
    std::size_t i = 0;
    while (i < inside.size()) {
        const char c = inside[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++i;
            continue;
        }
        std::size_t end = i;
        if (c == '{') {
            int depth = 0;
            for (; end < inside.size(); ++end) {
                if (inside[end] == '{') ++depth;
                if (inside[end] == '}' && --depth == 0) break;
            }
            ++end;
        } else {
            while (end < inside.size() && !std::isspace(static_cast<unsigned char>(inside[end])) &&
                   inside[end] != ',') {
                ++end;
            }
        }
        arguments.push_back(inside.substr(i, end - i));
        i = end;
    }
    return arguments;
}

static std::shared_ptr<const devices::Waveform> make_waveform(const std::string& keyword,
                                                              const std::vector<Real>& a,
                                                              ParseState& state) {
    const auto need = [&](std::size_t low, std::size_t high, const char* syntax) {
        if (a.size() < low || a.size() > high) {
            throw ParseError(fmt::format("{} takes {} ({} given)", keyword, syntax, a.size()),
                             state.line_number);
        }
    };
    if (keyword == "PULSE") {
        need(7, 7, "7 arguments: V1 V2 TD TR TF PW PER");
        return std::make_shared<devices::PulseWaveform>(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    }
    if (keyword == "SIN") {
        need(3, 6, "3 to 6 arguments: VO VA FREQ [TD [THETA [PHASE]]]");
        return std::make_shared<devices::SinWaveform>(a[0], a[1], a[2], a.size() > 3 ? a[3] : 0.0,
                                                      a.size() > 4 ? a[4] : 0.0,
                                                      a.size() > 5 ? a[5] : 0.0);
    }
    if (keyword == "EXP") {
        need(6, 6, "6 arguments: V1 V2 TD1 TAU1 TD2 TAU2");
        if (a[3] <= 0.0 || a[5] <= 0.0) {
            throw ParseError("EXP needs positive TAU1 and TAU2", state.line_number);
        }
        return std::make_shared<devices::ExpWaveform>(a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    if (keyword == "SFFM") {
        need(3, 5, "3 to 5 arguments: VO VA FC [MDI FS]");
        if (a.size() == 4 && a[3] != 0.0) {
            throw ParseError("SFFM with a modulation index needs FS", state.line_number);
        }
        return std::make_shared<devices::SffmWaveform>(a[0], a[1], a[2], a.size() > 3 ? a[3] : 0.0,
                                                       a.size() > 4 ? a[4] : 0.0);
    }
    // PWL
    if (a.size() < 2 || a.size() % 2 != 0) {
        throw ParseError(fmt::format("PWL takes pairs of time and value ({} numbers given)",
                                     a.size()),
                         state.line_number);
    }
    std::vector<std::pair<Real, Real>> points;
    for (std::size_t k = 0; k < a.size(); k += 2) {
        if (!points.empty() && a[k] < points.back().first) {
            throw ParseError("PWL times must not decrease", state.line_number);
        }
        points.emplace_back(a[k], a[k + 1]);
    }
    return std::make_shared<devices::PWLWaveform>(std::move(points));
}

static SourceSpecification parse_source_specification(const std::vector<std::string>& tokens,
                                                      std::size_t idx, ParseState& state) {
    auto read_value = [&](const std::string& token) { return read_element_value(token, state); };
    SourceSpecification spec;
    while (idx < tokens.size()) {
        const std::string& token = tokens[idx];
        std::string keyword;
        if (util::equals_icase(token, "DC")) {
            ++idx;
            spec.dc_given = true;
            spec.dc_value = 0.0;  // DC without a number is 0
            if (idx < tokens.size() && util::is_value_token(tokens[idx])) {
                spec.dc_value = read_value(tokens[idx++]);
            }
        } else if (util::equals_icase(token, "AC")) {
            idx = util::read_ac_input(tokens, idx, spec.ac, read_value);
        } else if (is_waveform_keyword(token, keyword)) {
            if (spec.waveform) {
                throw ParseError("A source takes one transient waveform", state.line_number);
            }
            // The text from the keyword to its closing parenthesis, or to
            // the next DC or AC keyword when there are no parentheses
            std::string text;
            int depth = 0;
            bool parenthesized = false;
            for (; idx < tokens.size(); ++idx) {
                if (!text.empty() && !parenthesized &&
                    (util::equals_icase(tokens[idx], "DC") || util::equals_icase(tokens[idx], "AC"))) {
                    break;
                }
                text += (text.empty() ? "" : " ") + tokens[idx];
                for (char c : tokens[idx]) {
                    if (c == '(') { ++depth; parenthesized = true; }
                    if (c == ')') --depth;
                }
                if (parenthesized && depth == 0) {
                    ++idx;
                    break;
                }
            }
            if (parenthesized && depth != 0) {
                throw ParseError("Malformed " + keyword + " waveform (missing parentheses)",
                                 state.line_number);
            }
            std::string inside;
            if (parenthesized) {
                const auto open = text.find('(');
                inside = text.substr(open + 1, text.rfind(')') - open - 1);
            } else {
                inside = text.substr(keyword.size());
            }
            std::vector<Real> arguments;
            for (const auto& argument : split_waveform_arguments(inside)) {
                arguments.push_back(read_value(argument));
            }
            spec.waveform = make_waveform(keyword, arguments, state);
        } else if (!spec.dc_given && util::is_value_token(token)) {
            spec.dc_value = read_value(token);
            spec.dc_given = true;
            ++idx;
        } else {
            throw ParseError("Unexpected '" + token + "' in the source", state.line_number);
        }
    }
    return spec;
}

static void parse_voltage_source_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    if (tokens.size() < 4) {
        throw ParseError("Invalid voltage source syntax", state.line_number);
    }
    const SourceSpecification spec = parse_source_specification(tokens, 3, state);
    if (spec.waveform) {
        state.circuit->add_voltage_source_with_waveform(dev_name, tokens[1], tokens[2],
                                                        spec.dc_value, spec.dc_given,
                                                        spec.ac.magnitude, spec.ac.phase,
                                                        spec.waveform);
    } else {
        state.circuit->add_voltage_source(dev_name, tokens[1], tokens[2], spec.dc_value,
                                          spec.ac.magnitude, spec.ac.phase);
    }
    spdlog::debug("Added voltage source: {} {} {} DC={}V AC={}V∠{}°{}", dev_name, tokens[1],
                  tokens[2], spec.dc_value, spec.ac.magnitude, spec.ac.phase,
                  spec.waveform ? " with a waveform" : "");
}

static void parse_current_source_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    if (tokens.size() < 4) {
        throw ParseError("Invalid current source syntax", state.line_number);
    }
    const SourceSpecification spec = parse_source_specification(tokens, 3, state);
    if (spec.waveform) {
        state.circuit->add_current_source_with_waveform(dev_name, tokens[1], tokens[2],
                                                        spec.dc_value, spec.dc_given,
                                                        spec.ac.magnitude, spec.ac.phase,
                                                        spec.waveform);
    } else {
        state.circuit->add_current_source(dev_name, tokens[1], tokens[2], spec.dc_value,
                                          spec.ac.magnitude, spec.ac.phase);
    }
    spdlog::debug("Added current source: {} {} {} IDC={}A AC={}A∠{}°{}", dev_name, tokens[1],
                  tokens[2], spec.dc_value, spec.ac.magnitude, spec.ac.phase,
                  spec.waveform ? " with a waveform" : "");
}

// The names a card gives and its device does not use, each named once per
// card: a card read for many elements warns once
static void warn_unused_card_names(ParseState& state, const std::string& what,
                                   const std::vector<std::string>& unused) {
    for (const auto& name : unused) {
        if (state.warned_card_names.insert(what + ": " + name).second) {
            spdlog::warn("{}: {} is not used", what, name);
        }
    }
}

static void parse_diode_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // Diode: D<name> <anode> <cathode> [model_name] [is=<value> n=<value>]
    // The card gives IS, N, CJO (or CJ0), VJ (or PB) and M (or MJ), with the
    // defaults of ngspice-47 (1e-14 A, 1, 0, 1 V, 0.5); the element line may
    // give IS and N. Every other name is not used and is named in a warning.
    if (tokens.size() < 3) {
        throw ParseError("Invalid diode syntax: D<name> <anode> <cathode> [model | parameters]", state.line_number);
    }
    std::string anode = tokens[1];
    std::string cathode = tokens[2];

    Real is = 1e-14;
    Real n = 1.0;
    Real cj0 = 0.0;
    Real vj = 1.0;
    Real m = 0.5;

    // Check if tokens[3] is a model reference
    size_t param_start = 3;
    if (tokens.size() > 3) {
        std::string potential_model = tokens[3];
        std::transform(potential_model.begin(), potential_model.end(),
                     potential_model.begin(), ::toupper);

        // Check if it's a parameter (contains '=')
        if (potential_model.find('=') == std::string::npos) {
            // It's a model reference
            const ModelDefinition* found_model = state.config->find_model(potential_model);
            if (found_model != nullptr) {
                const ModelDefinition& model = *found_model;

                // Verify model type
                if (model.device_type != DeviceType::DIODE) {
                    throw ParseError(fmt::format("Model {} is not a DIODE model", potential_model),
                                   state.line_number);
                }

                std::vector<std::string> unused;
                for (const auto& [key, value] : model.params) {
                    if (key == "is") is = value;
                    else if (key == "n") n = value;
                    else if (key == "cjo" || key == "cj0") cj0 = value;
                    else if (key == "vj" || key == "pb") vj = value;
                    else if (key == "m" || key == "mj") m = value;
                    else unused.push_back(key);
                }
                warn_unused_card_names(state, "Diode model " + potential_model, unused);

                param_start = 4;  // Skip model name in parameter parsing
            }
            else {
                throw UnresolvedDefinition(fmt::format("Undefined diode model: {}", potential_model),
                                      state.line_number);
            }
        }
    }

    // Parse optional parameters (can override model parameters)
    for (size_t i = param_start; i < tokens.size(); ++i) {
        std::string param = tokens[i];
        std::transform(param.begin(), param.end(), param.begin(), ::tolower);

        size_t eq_pos = param.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value_str = param.substr(eq_pos + 1);

            const Real value = read_element_value(value_str, state);

            if (key == "is") {
                is = value;
            } else if (key == "n") {
                n = value;
            } else {
                spdlog::warn("Line {}: {}: '{}' is not used", state.line_number, dev_name, tokens[i]);
            }
        } else {
            spdlog::warn("Line {}: {}: '{}' is not used", state.line_number, dev_name, tokens[i]);
        }
    }

    state.circuit->add_diode(dev_name, anode, cathode, is, n, cj0, vj, m);
    spdlog::debug("Added diode: {} {} {} Is={:.3e}A N={:.3f} Cj0={:.3e}F Vj={:.3f}V M={:.3f}",
                  dev_name, anode, cathode, is, n, cj0, vj, m);
}

static void parse_scr_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // SCR (Silicon Controlled Rectifier / Thyristor): S<name> <anode> <cathode> <gate> [model_name | parameters]
    // SCR implementation
    if (tokens.size() < 4) {
        throw ParseError("Invalid SCR syntax: S<name> <anode> <cathode> <gate> [model | parameters]", state.line_number);
    }
    std::string anode = tokens[1];
    std::string cathode = tokens[2];
    std::string gate = tokens[3];

    // Default SCR_N parameters
    Real vbf = 400.0;   // Forward breakover voltage [V] (100-1200V typical)
    Real igt = 20e-3;   // Gate trigger current [A] (5-50mA typical)
    Real ih = 10e-3;    // Holding current [A] (5-50mA typical)
    Real il = 30e-3;    // Latching current [A] (typically 2-3× IH)
    Real vgt = 1.0;     // Gate trigger voltage [V] (0.7-1.5V typical)
    Real ron = 0.1;     // On-state resistance [Ω] (0.01-1Ω typical)
    Real vtm = 1.5;     // On-state voltage drop [V] (1-2V typical)
    Real dv_dt = 10.0;  // dv/dt rating [V/μs] (optional, future use)
    Real tnom = 300.15; // Nominal temperature [K] (27°C)

    // Check if tokens[4] is a model reference
    size_t param_start = 4;
    if (tokens.size() > 4) {
        std::string potential_model = tokens[4];
        std::transform(potential_model.begin(), potential_model.end(),
                     potential_model.begin(), ::toupper);

        // Check if it's a parameter (contains '=')
        if (potential_model.find('=') == std::string::npos) {
            // It's a model reference
            const ModelDefinition* found_model = state.config->find_model(potential_model);
            if (found_model != nullptr) {
                const ModelDefinition& model = *found_model;

                // Verify model type
                if (model.device_type != DeviceType::SCR_N && model.device_type != DeviceType::SCR_P) {
                    throw ParseError(fmt::format("Model {} is not an SCR model", potential_model),
                                   state.line_number);
                }

                // Apply model parameters
                if (model.params.count("vbf")) vbf = model.params.at("vbf");
                if (model.params.count("igt")) igt = model.params.at("igt");
                if (model.params.count("ih")) ih = model.params.at("ih");
                if (model.params.count("il")) il = model.params.at("il");
                if (model.params.count("vgt")) vgt = model.params.at("vgt");
                if (model.params.count("ron")) ron = model.params.at("ron");
                if (model.params.count("vtm")) vtm = model.params.at("vtm");
                if (model.params.count("dv_dt")) dv_dt = model.params.at("dv_dt");
                if (model.params.count("tnom")) tnom = core::celsius_to_kelvin(model.params.at("tnom"));

                param_start = 5;  // Skip model name in parameter parsing
            }
            // If model not found, use default parameters
        }
    }

    // Parse optional parameters (can override model parameters)
    for (size_t i = param_start; i < tokens.size(); ++i) {
        std::string param = tokens[i];
        std::transform(param.begin(), param.end(), param.begin(), ::tolower);

        size_t eq_pos = param.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value_str = param.substr(eq_pos + 1);

            const Real value = read_element_value(value_str, state);

            if (key == "vbf") {
                vbf = value;
            } else if (key == "igt") {
                igt = value;
            } else if (key == "ih") {
                ih = value;
            } else if (key == "il") {
                il = value;
            } else if (key == "vgt") {
                vgt = value;
            } else if (key == "ron") {
                ron = value;
            } else if (key == "vtm") {
                vtm = value;
            } else if (key == "dv_dt") {
                dv_dt = value;
            } else if (key == "tnom") {
                tnom = core::celsius_to_kelvin(value);
            }
        }
    }

    state.circuit->add_scr_n(dev_name, anode, cathode, gate, vbf, igt, ih, il, vgt, ron, vtm, dv_dt, tnom);
    spdlog::debug("Added SCR_N: {} A={} K={} G={} VBF={:.1f}V IGT={:.3e}A IH={:.3e}A IL={:.3e}A "
                 "VGT={:.3f}V RON={:.3f}Ω VTM={:.3f}V DV_DT={:.1f}V/μs",
                 dev_name, anode, cathode, gate, vbf, igt, ih, il, vgt, ron, vtm, dv_dt);
}

static void parse_triac_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // TRIAC (Triode for Alternating Current): Y<name> <mt2> <mt1> <gate> [model_name | parameters]
    // TRIAC implementation - bidirectional thyristor
    if (tokens.size() < 4) {
        throw ParseError("Invalid TRIAC syntax: Y<name> <mt2> <mt1> <gate> [model | parameters]", state.line_number);
    }
    std::string mt2 = tokens[1];
    std::string mt1 = tokens[2];
    std::string gate = tokens[3];

    // Default TRIAC parameters
    Real vbo = 400.0;    // Breakover voltage [V] (100-800V typical, both directions)
    Real igt = 20e-3;    // Gate trigger current [A] (5-50mA typical, both directions)
    Real ih = 10e-3;     // Holding current [A] (5-50mA typical, both directions)
    Real vgt = 1.0;      // Gate trigger voltage [V] (0.7-1.5V typical)
    Real ron = 0.1;      // On-state resistance [Ω] (0.01-1Ω typical)
    Real vtm = 1.5;      // On-state voltage drop [V] (1-2V typical)
    Real dv_dt = 10.0;   // dv/dt rating [V/μs] (optional, for snubber circuit design)
    Real tnom = 300.15;  // Nominal temperature [K] (27°C)

    // Check if tokens[4] is a model reference
    size_t param_start = 4;
    if (tokens.size() > 4) {
        std::string potential_model = tokens[4];
        std::transform(potential_model.begin(), potential_model.end(),
                     potential_model.begin(), ::toupper);

        // Check if it's a parameter (contains '=')
        if (potential_model.find('=') == std::string::npos) {
            // It's a model reference
            const ModelDefinition* found_model = state.config->find_model(potential_model);
            if (found_model != nullptr) {
                const ModelDefinition& model = *found_model;

                // Verify model type
                if (model.device_type != DeviceType::TRIAC) {
                    throw ParseError(fmt::format("Model {} is not a TRIAC model", potential_model),
                                   state.line_number);
                }

                // Apply model parameters
                if (model.params.count("vbo")) vbo = model.params.at("vbo");
                if (model.params.count("igt")) igt = model.params.at("igt");
                if (model.params.count("ih")) ih = model.params.at("ih");
                if (model.params.count("vgt")) vgt = model.params.at("vgt");
                if (model.params.count("ron")) ron = model.params.at("ron");
                if (model.params.count("vtm")) vtm = model.params.at("vtm");
                if (model.params.count("dv_dt") || model.params.count("dvdt")) {
                    dv_dt = model.params.count("dv_dt") ? model.params.at("dv_dt") : model.params.at("dvdt");
                }
                if (model.params.count("tnom")) tnom = core::celsius_to_kelvin(model.params.at("tnom"));

                param_start = 5;  // Skip model name in parameter parsing
            }
            // If model not found, use default parameters
        }
    }

    // Parse optional parameters (can override model parameters)
    for (size_t i = param_start; i < tokens.size(); ++i) {
        std::string param = tokens[i];
        std::transform(param.begin(), param.end(), param.begin(), ::tolower);

        size_t eq_pos = param.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value_str = param.substr(eq_pos + 1);

            const Real value = read_element_value(value_str, state);

            if (key == "vbo") {
                vbo = value;
            } else if (key == "igt") {
                igt = value;
            } else if (key == "ih") {
                ih = value;
            } else if (key == "vgt") {
                vgt = value;
            } else if (key == "ron") {
                ron = value;
            } else if (key == "vtm") {
                vtm = value;
            } else if (key == "dv_dt" || key == "dvdt") {
                dv_dt = value;
            } else if (key == "tnom") {
                tnom = core::celsius_to_kelvin(value);
            }
        }
    }

    state.circuit->add_triac(dev_name, mt2, mt1, gate, vbo, igt, ih, vgt, ron, vtm, dv_dt, tnom);
    spdlog::debug("Added TRIAC: {} MT2={} MT1={} G={} VBO={:.1f}V IGT={:.3e}A IH={:.3e}A "
                 "VGT={:.3f}V RON={:.3f}Ω VTM={:.3f}V DV_DT={:.1f}V/μs",
                 dev_name, mt2, mt1, gate, vbo, igt, ih, vgt, ron, vtm, dv_dt);
}

static void parse_mesfet_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // MESFET: Z<name> <drain> <gate> <source> [model_name | NMF|PMF [parameters]]
    // MESFET (Metal-Semiconductor FET) implementation
    if (tokens.size() < 4) {
        throw ParseError("Invalid MESFET syntax: Z<name> <drain> <gate> <source> [model | NMF|PMF [parameters]]", state.line_number);
    }
    std::string drain = tokens[1];
    std::string gate = tokens[2];
    std::string source = tokens[3];

    // Default MESFET parameters
    int model = 1;           // 1=Curtice, 2=Statz
    Real beta = 0.1;         // Transconductance parameter [A/V²]
    Real vto = -2.0;         // Threshold voltage [V] (negative for n-channel depletion)
    Real lambda = 0.05;      // Channel length modulation [1/V]
    Real alpha = 2.0;        // Saturation voltage parameter [1/V] (Curtice)
    Real b = 0.3;            // Doping tail parameter (Statz)
    Real vp = 3.0;           // Pinch-off voltage [V] (Statz)
    Real cgs = 1e-12;        // Gate-source capacitance [F]
    Real cgd = 0.5e-12;      // Gate-drain capacitance [F]
    Real cds = 0.1e-12;      // Drain-source capacitance [F]
    Real kf = 0.0;           // Flicker noise coefficient
    Real af = 1.0;           // Flicker noise current exponent
    Real ef = 1.0;           // Flicker noise frequency exponent
    Real tnom = 300.15;      // Nominal temperature [K] (27°C)

    // Determine channel type: NMF (n-channel) or PMF (p-channel)
    std::string mesfet_type;  // Will be set to "NMF" or "PMF"
    bool is_p_channel = false;

    // Check if tokens[4] is a model reference or type specifier
    size_t param_start = 4;
    if (tokens.size() > 4) {
        std::string potential_model = tokens[4];
        std::transform(potential_model.begin(), potential_model.end(),
                     potential_model.begin(), ::toupper);

        // Check if it's a type specifier (NMF or PMF)
        if (potential_model == "NMF" || potential_model == "PMF") {
            mesfet_type = potential_model;
            is_p_channel = (potential_model == "PMF");
            if (is_p_channel) {
                vto = 2.0;  // Positive for p-channel depletion
            }
            param_start = 5;  // Parameters start after type
        }
        // Check if it's a parameter (contains '=')
        else if (potential_model.find('=') == std::string::npos) {
            // It's a model reference
            const ModelDefinition* found_model = state.config->find_model(potential_model);
            if (found_model != nullptr) {
                const ModelDefinition& model_def = *found_model;

                // Verify model type
                if (model_def.device_type != DeviceType::MESFET_N &&
                    model_def.device_type != DeviceType::MESFET_P) {
                    throw ParseError(fmt::format("Model {} is not a MESFET model", potential_model),
                                   state.line_number);
                }

                // Determine channel type from model
                is_p_channel = (model_def.device_type == DeviceType::MESFET_P);
                mesfet_type = is_p_channel ? "PMF" : "NMF";

                // Apply model parameters
                if (model_def.params.count("level")) model = static_cast<int>(model_def.params.at("level"));
                if (model_def.params.count("beta")) beta = model_def.params.at("beta");
                if (model_def.params.count("vto")) vto = model_def.params.at("vto");
                if (model_def.params.count("lambda")) lambda = model_def.params.at("lambda");
                if (model_def.params.count("alpha")) alpha = model_def.params.at("alpha");
                if (model_def.params.count("b")) b = model_def.params.at("b");
                if (model_def.params.count("vp")) vp = model_def.params.at("vp");
                if (model_def.params.count("cgs")) cgs = model_def.params.at("cgs");
                if (model_def.params.count("cgd")) cgd = model_def.params.at("cgd");
                if (model_def.params.count("cds")) cds = model_def.params.at("cds");
                if (model_def.params.count("kf")) kf = model_def.params.at("kf");
                if (model_def.params.count("af")) af = model_def.params.at("af");
                if (model_def.params.count("ef")) ef = model_def.params.at("ef");
                if (model_def.params.count("tnom")) tnom = core::celsius_to_kelvin(model_def.params.at("tnom"));
                std::vector<std::string> unused;
                for (const auto& [key, value] : model_def.params) {
                    static const std::set<std::string> kMesfet = {
                        "level", "beta", "vto", "lambda", "alpha", "b", "vp", "cgs", "cgd",
                        "cds", "kf", "af", "ef", "tnom"};
                    if (!kMesfet.count(key)) unused.push_back(key);
                }
                warn_unused_card_names(state, "MESFET model " + potential_model, unused);

                param_start = 5;  // Skip model name in parameter parsing
            } else {
                throw UnresolvedDefinition(fmt::format("Undefined MESFET model: {}", potential_model),
                                      state.line_number);
            }
        } else {
            // It's a parameter, assume default NMF
            mesfet_type = "NMF";
        }
    } else {
        // No model/type specified, assume default NMF
        mesfet_type = "NMF";
    }

    // Parse optional parameters (can override model parameters)
    for (size_t i = param_start; i < tokens.size(); ++i) {
        std::string param = tokens[i];
        std::transform(param.begin(), param.end(), param.begin(), ::tolower);

        size_t eq_pos = param.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value_str = param.substr(eq_pos + 1);

            const Real value = read_element_value(value_str, state);

            if (key == "level" || key == "model") {
                model = static_cast<int>(value);
            } else if (key == "beta") {
                beta = value;
            } else if (key == "vto") {
                vto = value;
            } else if (key == "lambda") {
                lambda = value;
            } else if (key == "alpha") {
                alpha = value;
            } else if (key == "b") {
                b = value;
            } else if (key == "vp") {
                vp = value;
            } else if (key == "cgs") {
                cgs = value;
            } else if (key == "cgd") {
                cgd = value;
            } else if (key == "cds") {
                cds = value;
            } else if (key == "kf") {
                kf = value;
            } else if (key == "af") {
                af = value;
            } else if (key == "ef") {
                ef = value;
            } else if (key == "tnom") {
                tnom = core::celsius_to_kelvin(value);
            } else {
                spdlog::warn("Line {}: {}: '{}' is not used", state.line_number, dev_name, tokens[i]);
            }
        } else {
            spdlog::warn("Line {}: {}: '{}' is not used", state.line_number, dev_name, tokens[i]);
        }
    }

    // Add MESFET to circuit
    std::string model_str = (model == 1) ? "Curtice" : "Statz";
    if (is_p_channel) {
        state.circuit->add_mesfet_p(dev_name, drain, gate, source, model, beta, vto, lambda, alpha, b, vp,
                                    cgs, cgd, cds, kf, af, ef, tnom);
        spdlog::debug("Added MESFET_P: {} D={} G={} S={} model={} BETA={:.3e}A/V² VTO={:.3f}V LAMBDA={:.3f}/V",
                     dev_name, drain, gate, source, model_str, beta, vto, lambda);
    } else {
        state.circuit->add_mesfet_n(dev_name, drain, gate, source, model, beta, vto, lambda, alpha, b, vp,
                                    cgs, cgd, cds, kf, af, ef, tnom);
        spdlog::debug("Added MESFET_N: {} D={} G={} S={} model={} BETA={:.3e}A/V² VTO={:.3f}V LAMBDA={:.3f}/V",
                     dev_name, drain, gate, source, model_str, beta, vto, lambda);
    }
}

static void parse_bjt_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // BJT: Q<name> <collector> <base> <emitter> <type_or_model> [is=<value>] [bf=<value>] [n=<value>]
    // type: NPN or PNP, or model_name (e.g., 2N3904)
    if (tokens.size() < 5) {
        throw ParseError("Invalid BJT syntax: Q<name> <collector> <base> <emitter> <NPN|PNP|model> [parameters]", state.line_number);
    }
    std::string collector = tokens[1];
    std::string base = tokens[2];
    std::string emitter = tokens[3];
    std::string type_or_model = tokens[4];
    std::transform(type_or_model.begin(), type_or_model.end(), type_or_model.begin(), ::toupper);

    // Check if it's a model reference
    // The card's parameters, then the element line's (set_bjt_parameter)
    bool pnp = false;
    devices::BJTParams params;
    if (type_or_model == "NPN" || type_or_model == "PNP") {
        pnp = type_or_model == "PNP";
    } else {
        const ModelDefinition* found_model = state.config->find_model(type_or_model);
        if (found_model == nullptr) {
            throw UnresolvedDefinition(fmt::format("Undefined BJT model: {}", type_or_model), state.line_number);
        }
        const ModelDefinition& model = *found_model;
        if (model.device_type == DeviceType::BJT_NPN) {
            pnp = false;
        } else if (model.device_type == DeviceType::BJT_PNP) {
            pnp = true;
        } else {
            throw ParseError(fmt::format("Model {} is not a BJT model", type_or_model), state.line_number);
        }
        std::vector<std::string> unused;
        params = devices::bjt_params_from_model(model.params, &unused);
        warn_unused_card_names(state, "BJT model " + type_or_model, unused);
    }

    for (size_t i = 5; i < tokens.size(); ++i) {
        std::string param = tokens[i];
        std::transform(param.begin(), param.end(), param.begin(), ::tolower);
        const size_t eq_pos = param.find('=');
        if (eq_pos == std::string::npos ||
            !devices::set_bjt_parameter(params, param.substr(0, eq_pos),
                                        read_element_value(param.substr(eq_pos + 1), state))) {
            spdlog::warn("Line {}: {}: '{}' is not used", state.line_number, dev_name, tokens[i]);
        }
    }

    state.circuit->add_bjt(dev_name, collector, base, emitter, pnp, params);
    spdlog::debug("Added BJT_{}: {} C={} B={} E={} Is={:.3e}A BF={:.1f} VAF={:.3g} IKF={:.3g}",
                  pnp ? "PNP" : "NPN", dev_name, collector, base, emitter, params.is,
                  params.beta_f, params.vaf, params.ikf);
}

// Gives a device the parameters of its card or element line by name
// (Device::set_parameter), so that what they do not give keeps the device's
// own default. TNOM is in degrees Celsius in the netlist and in kelvin in the
// device. A parameter the device does not have, or a value it rejects, gets a
// warning.
static void set_parameters_by_name(devices::Device& device, const std::map<std::string, Real>& params,
                                   const std::string& what) {
    for (const auto& [key, written] : params) {
        if (key == "level") {
            continue;
        }
        const Real value = (key == "tnom") ? core::celsius_to_kelvin(written) : written;
        if (!device.set_parameter(key, value)) {
            spdlog::warn("{}: {}={} is not a parameter of this device or is out of range; ignored",
                         what, key, written);
        }
    }
}

// The index of the model token of an M line: 5 when the line has four
// terminals, M<name> <d> <g> <s> <b> <model>, which it has when the sixth token
// names a model or starts with NMOS or PMOS; 4 otherwise, the bulk then tied to
// the source. parse_mosfet_device and the renaming of a subcircuit body both
// read an M line's nodes through this.
static std::size_t mosfet_model_index(const std::vector<std::string>& tokens,
                                      const SimulationConfig& config) {
    if (tokens.size() < 6) {
        return 4;
    }
    std::string candidate = tokens[5];
    std::transform(candidate.begin(), candidate.end(), candidate.begin(), ::toupper);
    const bool nmos_or_pmos = candidate.rfind("NMOS", 0) == 0 || candidate.rfind("PMOS", 0) == 0;
    return (nmos_or_pmos || config.find_model(candidate) != nullptr) ? 5 : 4;
}

// The bulk, GAMMA, PHI and LAMBDA of a level-1 element (MOSFET_Level1::set_body)
static void set_level1_body(circuit::CircuitGraph& circuit, const std::string& name,
                            const std::string& bulk, Real gamma, Real phi, Real lambda) {
    const NodeId bulk_id = circuit.add_node(bulk);
    const auto device = circuit.get_device(name);
    if (auto* n = dynamic_cast<devices::MOSFET_N*>(device.get())) {
        n->set_body(bulk_id, gamma, phi, lambda);
    } else if (auto* p = dynamic_cast<devices::MOSFET_P*>(device.get())) {
        p->set_body(bulk_id, gamma, phi, lambda);
    }
}

static void parse_mosfet_device(const std::vector<std::string>& tokens, const std::string& dev_name, const std::string& line, ParseState& state) {
    // MOSFET: M<name> <drain> <gate> <source> [<bulk>] <type_or_model> [parameters]
    // 3-terminal: M<name> <drain> <gate> <source> <NMOS|PMOS|model> [params]
    // 4-terminal: M<name> <drain> <gate> <source> <bulk> <model> [params]

    // DEBUG: Entry point detection
    thread_local static int mosfet_parse_count = 0;
    if (mosfet_parse_count < 3) {
        spdlog::debug("=== MOSFET parser ENTRY [call {}] ===", mosfet_parse_count);
        spdlog::debug("  Line: {}", line);
        spdlog::debug("  Tokens: [{}]", fmt::join(tokens, ", "));
        mosfet_parse_count++;
    }

    if (tokens.size() < 5) {
        throw ParseError("Invalid MOSFET syntax: M<name> <drain> <gate> <source> [<bulk>] <NMOS|PMOS|model> [parameters]", state.line_number);
    }

    std::string drain = tokens[1];
    std::string gate = tokens[2];
    std::string source = tokens[3];
    std::string bulk;  // Optional (for 4-terminal devices)
    std::string type_or_model;
    size_t param_start_idx;  // Index where optional parameters start

    // Determine if 3-terminal or 4-terminal syntax (mosfet_model_index)
    std::string candidate_model_4 = tokens[4];
    std::transform(candidate_model_4.begin(), candidate_model_4.end(), candidate_model_4.begin(), ::toupper);

    const bool is_4_terminal = mosfet_model_index(tokens, *state.config) == 5;
    if (is_4_terminal) {
        bulk = tokens[4];
        type_or_model = tokens[5];
        std::transform(type_or_model.begin(), type_or_model.end(), type_or_model.begin(), ::toupper);
        param_start_idx = 6;
    }

    if (!is_4_terminal) {
        // 3-terminal syntax (or fallback)
        bulk = source;  // Bulk tied to source by default
        type_or_model = candidate_model_4;
        param_start_idx = 5;
    }

    // Check if it's a model reference
    std::string mosfet_type;
    Real kp = 2e-5;      // Default
    Real w = 10e-6;      // Default
    Real l = 1e-6;       // Default
    Real vth = 0.7;      // Will be set based on type
    Real kf = 0.0;       // Flicker noise coefficient
    Real af = 1.0;       // Flicker noise current exponent
    Real ef = 1.0;       // Flicker noise frequency exponent
    Real gamma = 2.0 / 3.0;  // Channel thermal noise coefficient (SPICE's level 1)
    Real gamma_body = 0.0;   // Level 1: GAMMA of the card, the body effect
    Real phi = 0.6;          // Level 1: PHI
    Real lambda = 0.0;       // Level 1: LAMBDA
    Real mult_i = 1.0;   // BSIM4 variability multiplier for current
    Real mult_q = 1.0;   // BSIM4 variability multiplier for charge
    Real mult_fn = 1.0;  // BSIM4 variability multiplier for flicker noise

    // The SiC MOSFET, GaN HEMT, FinFET, VDMOS and IGBT take their parameters
    // by name with their own defaults (set_parameters_by_name)

    // Check if it's a model reference first (higher priority)
    const ModelDefinition* found_model = state.config->find_model(type_or_model);

    if (found_model != nullptr) {
        // Model reference found
        spdlog::debug("[Parser] Found model definition for '{}'", type_or_model);

        const ModelDefinition& model = *found_model;

        // Verify model type
        if (model.device_type == DeviceType::MOSFET_N) {
            // A level-1 card takes the defaults of ngspice-47: VTO 0, KP
            // 2e-5, GAMMA 0, PHI 0.6, LAMBDA 0, W = L = 100 um
            mosfet_type = "NMOS";
            vth = 0.0;
            w = 100e-6;
            l = 100e-6;
        } else if (model.device_type == DeviceType::MOSFET_P) {
            mosfet_type = "PMOS";
            vth = 0.0;
            w = 100e-6;
            l = 100e-6;
        } else if (model.device_type == DeviceType::MOSFET_LEVEL3_N) {
            mosfet_type = "LEVEL3_N";
        } else if (model.device_type == DeviceType::MOSFET_LEVEL3_P) {
            mosfet_type = "LEVEL3_P";
        } else if (model.device_type == DeviceType::MOSFET_BSIM1_N) {
            mosfet_type = "BSIM1_N";
        } else if (model.device_type == DeviceType::MOSFET_BSIM1_P) {
            mosfet_type = "BSIM1_P";
        } else if (model.device_type == DeviceType::MOSFET_BSIM3_N) {
            mosfet_type = "BSIM3_N";
        } else if (model.device_type == DeviceType::MOSFET_BSIM3_P) {
            mosfet_type = "BSIM3_P";
        } else if (model.device_type == DeviceType::MOSFET_BSIM4_N) {
            mosfet_type = "BSIM4_N";
        } else if (model.device_type == DeviceType::MOSFET_BSIM4_P) {
            mosfet_type = "BSIM4_P";
        } else if (model.device_type == DeviceType::MOSFET_SIC_N) {
            mosfet_type = "SIC_N";
        } else if (model.device_type == DeviceType::MOSFET_SIC_P) {
            mosfet_type = "SIC_P";
        } else if (model.device_type == DeviceType::GAN_HEMT_N) {
            mosfet_type = "GAN_HEMT_N";
        } else if (model.device_type == DeviceType::GAN_HEMT_P) {
            mosfet_type = "GAN_HEMT_P";
        } else if (model.device_type == DeviceType::FINFET_N) {
            mosfet_type = "FINFET_N";
        } else if (model.device_type == DeviceType::FINFET_P) {
            mosfet_type = "FINFET_P";
        } else if (model.device_type == DeviceType::VDMOS_N) {
            mosfet_type = "VDMOS_N";
        } else if (model.device_type == DeviceType::VDMOS_P) {
            mosfet_type = "VDMOS_P";
        } else if (model.device_type == DeviceType::IGBT_N) {
            mosfet_type = "IGBT_N";
        } else if (model.device_type == DeviceType::IGBT_P) {
            mosfet_type = "IGBT_P";
        } else {
            throw ParseError(fmt::format("Model {} is not a MOSFET model", type_or_model), state.line_number);
        }

        // Apply model parameters
        spdlog::debug("[Parser] Applying model '{}' parameters (device_type={})", type_or_model, static_cast<int>(model.device_type));
        spdlog::debug("[Parser] Model has {} parameters", model.params.size());

        if (model.params.count("kp")) {
            kp = model.params.at("kp");
            spdlog::debug("[Parser] Found KP in model: {:.3e}", kp);
        } else {
            spdlog::debug("[Parser] KP not found in model, using default: {:.3e}", kp);
        }

        if (model.params.count("w")) w = model.params.at("w");
        if (model.params.count("l")) l = model.params.at("l");
        if (model.params.count("vth")) vth = model.params.at("vth");
        if (model.params.count("vto")) {
            vth = model.params.at("vto");
            spdlog::debug("[Parser] Found VTO in model: {:.3f}", vth);
        }
        if (model.params.count("kf")) kf = model.params.at("kf");
        if (model.params.count("af")) af = model.params.at("af");
        if (model.params.count("ef")) ef = model.params.at("ef");
        if (mosfet_type == "NMOS" || mosfet_type == "PMOS") {
            // SPICE's level 1: GAMMA is the body effect; the names it does
            // not use are named
            static const std::set<std::string> kLevel1 = {"level", "kp", "w", "l", "vth", "vto",
                                                          "kf", "af", "ef", "gamma", "phi",
                                                          "lambda"};
            if (model.params.count("gamma")) gamma_body = model.params.at("gamma");
            if (model.params.count("phi")) phi = model.params.at("phi");
            if (model.params.count("lambda")) lambda = model.params.at("lambda");
            std::vector<std::string> unused;
            for (const auto& [key, value] : model.params) {
                if (!kLevel1.count(key)) {
                    unused.push_back(key);
                }
            }
            warn_unused_card_names(state, "MOSFET model " + type_or_model + " (level 1)", unused);
        }
    } else if (type_or_model == "NMOS" || type_or_model == "PMOS") {
        // Direct type specification (no model defined)
        spdlog::info("[Parser] Using direct type specification for '{}'", type_or_model);
        mosfet_type = type_or_model;
        vth = (mosfet_type == "NMOS") ? 0.7 : -0.7;
    } else {
        // Neither model nor direct type found
        throw UnresolvedDefinition(fmt::format("Undefined MOSFET model: {}", type_or_model), state.line_number);
    }

    // Parse optional parameters (can override model parameters)
    std::map<std::string, Real> instance_params;  // as written, for devices set by name
    for (size_t i = param_start_idx; i < tokens.size(); ++i) {
        std::string param = tokens[i];
        std::transform(param.begin(), param.end(), param.begin(), ::tolower);

        size_t eq_pos = param.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = param.substr(0, eq_pos);
            std::string value_str = param.substr(eq_pos + 1);

            const Real value = read_element_value(value_str, state);
            instance_params[key] = value;

            if (key == "kp") {
                kp = value;
            } else if (key == "w") {
                w = value;
            } else if (key == "l") {
                l = value;
            } else if (key == "vth") {
                vth = value;
            } else if (key == "kf") {
                kf = value;
            } else if (key == "af") {
                af = value;
            } else if (key == "ef") {
                ef = value;
            } else if (key == "mult_i") {
                mult_i = value;
            } else if (key == "mult_q") {
                mult_q = value;
            } else if (key == "mult_fn") {
                mult_fn = value;
            }
        }
    }

    if (mosfet_type == "LEVEL3_N" || mosfet_type == "LEVEL3_P") {
        // MOSFET Level 3 model: create MOSFETLevel3Params from model parameters
        const ModelDefinition* found_model = state.config->find_model(type_or_model);
        const ModelDefinition& model = *found_model;

        devices::MOSFETLevel3Params level3_params;
        // VTO, PHI and GAMMA that the card does not give follow from NSUB
        level3_params.vto_given = model.params.count("vto") > 0 || model.params.count("vth0") > 0;
        level3_params.phi_given = model.params.count("phi") > 0;
        level3_params.gamma_given = model.params.count("gamma") > 0;

        // Copy all model parameters to MOSFETLevel3Params
        for (const auto& [key, value] : model.params) {
            // Process parameters
            if (key == "tox") level3_params.tox = value;
            else if (key == "nsub") level3_params.nsub = value;
            else if (key == "nss") level3_params.nss = value;
            else if (key == "tpg") level3_params.tpg = static_cast<int>(value);
            else if (key == "xj") level3_params.xj = value;
            else if (key == "nfs") level3_params.nfs = value;
            // Threshold voltage
            else if (key == "vto" || key == "vth0") level3_params.vto = value;
            else if (key == "phi") level3_params.phi = value;
            else if (key == "gamma") level3_params.gamma = value;
            // Mobility
            else if (key == "uo" || key == "u0") level3_params.uo = value;
            else if (key == "kp") level3_params.kp = value;
            else if (key == "theta") level3_params.theta = value;
            else if (key == "vmax") level3_params.vmax = value;
            // Short-channel effects
            else if (key == "eta") level3_params.eta = value;
            else if (key == "kappa") level3_params.kappa = value;
            else if (key == "delta") level3_params.delta = value;
            // Channel length modulation
            else if (key == "lambda") level3_params.lambda = value;
            // Geometry
            else if (key == "w") level3_params.w = value;
            else if (key == "l") level3_params.l = value;
            else if (key == "ld") level3_params.ld = value;
            else if (key == "wd") level3_params.wd = value;
            // Capacitances
            else if (key == "cgso") level3_params.cgso = value;
            else if (key == "cgdo") level3_params.cgdo = value;
            else if (key == "cgbo") level3_params.cgbo = value;
            else if (key == "cbd") level3_params.cbd = value;
            else if (key == "cbs") level3_params.cbs = value;
            else if (key == "cj") level3_params.cj = value;
            else if (key == "mj") level3_params.mj = value;
            else if (key == "pb") level3_params.pb = value;
            else if (key == "cjsw") level3_params.cjsw = value;
            else if (key == "mjsw") level3_params.mjsw = value;
            // Temperature
            else if (key == "tnom") level3_params.tnom = core::celsius_to_kelvin(value);
            // Series resistance
            else if (key == "rs") level3_params.rs = value;
            else if (key == "rd") level3_params.rd = value;
            // Level parameter (already processed)
            else if (key == "level") {
                // Already handled, skip
            }
            // Warn about unrecognized parameters
            else {
                spdlog::warn("Unrecognized Level 3 parameter '{}' in model {}", key, type_or_model);
            }
        }

        // Override with instance parameters (W, L from tokens)
        level3_params.w = w;
        level3_params.l = l;

        // Level 3: use bulk node determined earlier (4-terminal or 3-terminal)
        if (mosfet_type == "LEVEL3_N") {
            state.circuit->add_mosfet_level3_n(dev_name, drain, gate, source, bulk, level3_params);
            spdlog::debug("Added MOSFET_Level3_N: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m Vto={:.3f}V",
                         dev_name, drain, gate, source, bulk, level3_params.w, level3_params.l, level3_params.vto);
        } else {
            state.circuit->add_mosfet_level3_p(dev_name, drain, gate, source, bulk, level3_params);
            spdlog::debug("Added MOSFET_Level3_P: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m Vto={:.3f}V",
                         dev_name, drain, gate, source, bulk, level3_params.w, level3_params.l, level3_params.vto);
        }
    } else if (mosfet_type == "BSIM1_N" || mosfet_type == "BSIM1_P") {
        // BSIM1 model (Level 9): create MOSFETBsim1Params from model parameters
        const ModelDefinition* found_model = state.config->find_model(type_or_model);
        const ModelDefinition& model = *found_model;

        devices::MOSFETBsim1Params bsim1_params;

        auto normalize_tox_value = [](Real raw) {
            if (raw > 1e-5) {
                // Legacy BSIM1 decks often specify tox in µm (e.g., 0.02 = 20nm).
                return raw * 1e-6;
            }
            return raw;
        };

        // Copy all model parameters to MOSFETBsim1Params
        for (const auto& [key, value] : model.params) {
            // Basic process parameters
            if (key == "vfb") bsim1_params.vfb = value;
            else if (key == "phi") bsim1_params.phi = value;
            else if (key == "k1") bsim1_params.k1 = value;
            else if (key == "k2") bsim1_params.k2 = value;
            // Mobility parameters
            else if (key == "muz") bsim1_params.muz = value;
            else if (key == "u0") bsim1_params.u0 = value;
            else if (key == "u1") bsim1_params.u1 = value;
            // Short-channel effects
            else if (key == "x2mz") bsim1_params.x2mz = value;
            else if (key == "x2ms") bsim1_params.x2ms = value;
            else if (key == "x2m") bsim1_params.x2m = value;
            else if (key == "x2u0") bsim1_params.x2u0 = value;
            else if (key == "x2u1") bsim1_params.x2u1 = value;
            // DIBL
            else if (key == "eta") bsim1_params.eta = value;
            else if (key == "x2e") bsim1_params.x2e = value;
            else if (key == "x3e") bsim1_params.x3e = value;
            else if (key == "x3ms") bsim1_params.x3ms = value;
            // Subthreshold
            else if (key == "n0") bsim1_params.n0 = value;
            else if (key == "nb") bsim1_params.nb = value;
            else if (key == "nd") bsim1_params.nd = value;
            // Saturation velocity
            else if (key == "vsat") bsim1_params.vsat = value;
            // Channel length/width corrections
            else if (key == "dl") bsim1_params.dl = value;
            else if (key == "dw") bsim1_params.dw = value;
            // Junction parameters
            else if (key == "cbd") bsim1_params.cbd = value;
            else if (key == "cbs") bsim1_params.cbs = value;
            else if (key == "is") bsim1_params.is = value;
            else if (key == "pb") bsim1_params.pb = value;
            else if (key == "cj") bsim1_params.cj = value;
            else if (key == "mj") bsim1_params.mj = value;
            else if (key == "cjsw") bsim1_params.cjsw = value;
            else if (key == "mjsw") bsim1_params.mjsw = value;
            // Gate capacitances
            else if (key == "cgso") bsim1_params.cgso = value;
            else if (key == "cgdo") bsim1_params.cgdo = value;
            else if (key == "cgbo") bsim1_params.cgbo = value;
            // Oxide parameters
            else if (key == "tox") bsim1_params.tox = normalize_tox_value(value);
            // Temperature
            else if (key == "tnom") bsim1_params.tnom = core::celsius_to_kelvin(value);
            // Noise
            else if (key == "kf") bsim1_params.kf = value;
            else if (key == "af") bsim1_params.af = value;
            // Geometry (instance parameters, may be overridden)
            else if (key == "w") bsim1_params.w = value;
            else if (key == "l") bsim1_params.l = value;
            else if (key == "ad") bsim1_params.ad = value;
            else if (key == "as") bsim1_params.as = value;
            else if (key == "pd") bsim1_params.pd = value;
            else if (key == "ps") bsim1_params.ps = value;
            // Level parameter (already processed)
            else if (key == "level") {
                // Already handled, skip
            }
            // Warn about unrecognized parameters
            else {
                spdlog::warn("Unrecognized BSIM1 parameter '{}' in model {}", key, type_or_model);
            }
        }

        // Override with instance parameters (W, L from tokens)
        bsim1_params.w = w;
        bsim1_params.l = l;

        // BSIM1: use bulk node determined earlier (4-terminal or 3-terminal)
        if (mosfet_type == "BSIM1_N") {
            state.circuit->add_mosfet_bsim1_n(dev_name, drain, gate, source, bulk, bsim1_params);
            spdlog::debug("Added MOSFET_BSIM1_N: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m",
                         dev_name, drain, gate, source, bulk, bsim1_params.w, bsim1_params.l);
        } else {
            state.circuit->add_mosfet_bsim1_p(dev_name, drain, gate, source, bulk, bsim1_params);
            spdlog::debug("Added MOSFET_BSIM1_P: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m",
                         dev_name, drain, gate, source, bulk, bsim1_params.w, bsim1_params.l);
        }
    } else if (mosfet_type == "BSIM3_N" || mosfet_type == "BSIM3_P") {
        // BSIM3 model: create BSIM3Params from model parameters
        const ModelDefinition* found_model = state.config->find_model(type_or_model);
        const ModelDefinition& model = *found_model;

        devices::BSIM3Params bsim3_params;

        // Copy all model parameters to BSIM3Params (100+ parameters)
        for (const auto& [key, value] : model.params) {
            // Process parameters
            if (key == "tox") bsim3_params.tox = value;
            else if (key == "toxm") bsim3_params.toxm = value;
            else if (key == "cdsc") bsim3_params.cdsc = value;
            else if (key == "cdscb") bsim3_params.cdscb = value;
            else if (key == "cdscd") bsim3_params.cdscd = value;
            else if (key == "cit") bsim3_params.cit = value;
            else if (key == "nfactor") bsim3_params.nfactor = value;
            else if (key == "xj") bsim3_params.xj = value;
            else if (key == "vsat") bsim3_params.vsat = value;
            else if (key == "at") bsim3_params.at = value;
            else if (key == "a0") bsim3_params.a0 = value;
            else if (key == "ags") bsim3_params.ags = value;
            else if (key == "a1") bsim3_params.a1 = value;
            else if (key == "a2") bsim3_params.a2 = value;
            else if (key == "keta") bsim3_params.keta = value;
            else if (key == "nsub") bsim3_params.nsub = value;
            else if (key == "nch") bsim3_params.nch = value;
            else if (key == "ngate") bsim3_params.ngate = value;
            else if (key == "gamma1") bsim3_params.gamma1 = value;
            else if (key == "gamma2") bsim3_params.gamma2 = value;
            else if (key == "vbx") bsim3_params.vbx = value;
            else if (key == "vbm") bsim3_params.vbm = value;
            // Threshold voltage parameters
            else if (key == "vth0" || key == "vto") bsim3_params.vth0 = value;
            else if (key == "k1") bsim3_params.k1 = value;
            else if (key == "k2") bsim3_params.k2 = value;
            else if (key == "k3") bsim3_params.k3 = value;
            else if (key == "k3b") bsim3_params.k3b = value;
            else if (key == "w0") bsim3_params.w0 = value;
            else if (key == "nlx") bsim3_params.nlx = value;
            else if (key == "dvt0") bsim3_params.dvt0 = value;
            else if (key == "dvt1") bsim3_params.dvt1 = value;
            else if (key == "dvt2") bsim3_params.dvt2 = value;
            else if (key == "dvt0w") bsim3_params.dvt0w = value;
            else if (key == "dvt1w") bsim3_params.dvt1w = value;
            else if (key == "dvt2w") bsim3_params.dvt2w = value;
            else if (key == "drout") bsim3_params.drout = value;
            else if (key == "dsub") bsim3_params.dsub = value;
            // Mobility parameters
            else if (key == "u0") bsim3_params.u0 = value;
            else if (key == "ua") bsim3_params.ua = value;
            else if (key == "ub") bsim3_params.ub = value;
            else if (key == "uc") bsim3_params.uc = value;
            else if (key == "voff") bsim3_params.voff = value;
            else if (key == "delta") bsim3_params.delta = value;
            else if (key == "rdsw") bsim3_params.rdsw = value;
            else if (key == "prwg") bsim3_params.prwg = value;
            else if (key == "prwb") bsim3_params.prwb = value;
            else if (key == "prt") bsim3_params.prt = value;
            else if (key == "eta0") bsim3_params.eta0 = value;
            else if (key == "etab") bsim3_params.etab = value;
            // Channel length modulation
            else if (key == "pclm") bsim3_params.pclm = value;
            else if (key == "pdiblc1") bsim3_params.pdiblc1 = value;
            else if (key == "pdiblc2") bsim3_params.pdiblc2 = value;
            else if (key == "pdiblcb") bsim3_params.pdiblcb = value;
            else if (key == "pscbe1") bsim3_params.pscbe1 = value;
            else if (key == "pscbe2") bsim3_params.pscbe2 = value;
            else if (key == "pvag") bsim3_params.pvag = value;
            else if (key == "wr") bsim3_params.wr = value;
            else if (key == "dwg") bsim3_params.dwg = value;
            else if (key == "dwb") bsim3_params.dwb = value;
            else if (key == "b0") bsim3_params.b0 = value;
            else if (key == "b1") bsim3_params.b1 = value;
            // Impact ionization
            else if (key == "alpha0") bsim3_params.alpha0 = value;
            else if (key == "alpha1") bsim3_params.alpha1 = value;
            else if (key == "beta0") bsim3_params.beta0 = value;
            // Gate overlap capacitance
            else if (key == "cgso") bsim3_params.cgso = value;
            else if (key == "cgdo") bsim3_params.cgdo = value;
            else if (key == "cgbo") bsim3_params.cgbo = value;
            else if (key == "xpart") bsim3_params.xpart = value;
            // Intrinsic capacitance
            else if (key == "cf") bsim3_params.cf = value;
            else if (key == "clc") bsim3_params.clc = value;
            else if (key == "cle") bsim3_params.cle = value;
            else if (key == "dwc") bsim3_params.dwc = value;
            else if (key == "dlc") bsim3_params.dlc = value;
            else if (key == "vfbcv") bsim3_params.vfbcv = value;
            else if (key == "acde") bsim3_params.acde = value;
            else if (key == "moin") bsim3_params.moin = value;
            else if (key == "noff") bsim3_params.noff = value;
            else if (key == "voffcv") bsim3_params.voffcv = value;
            // Junction diode
            else if (key == "cj") bsim3_params.cj = value;
            else if (key == "mj") bsim3_params.mj = value;
            else if (key == "pb") bsim3_params.pb = value;
            else if (key == "cjsw") bsim3_params.cjsw = value;
            else if (key == "mjsw") bsim3_params.mjsw = value;
            else if (key == "pbsw") bsim3_params.pbsw = value;
            else if (key == "cjswg") bsim3_params.cjswg = value;
            else if (key == "mjswg") bsim3_params.mjswg = value;
            else if (key == "pbswg") bsim3_params.pbswg = value;
            else if (key == "js") bsim3_params.js = value;
            else if (key == "jsw") bsim3_params.jsw = value;
            else if (key == "ijth") bsim3_params.ijth = value;
            else if (key == "xti") bsim3_params.xti = value;
            // Temperature parameters
            else if (key == "tnom") bsim3_params.tnom = core::celsius_to_kelvin(value);
            else if (key == "ute") bsim3_params.ute = value;
            else if (key == "kt1") bsim3_params.kt1 = value;
            else if (key == "kt1l") bsim3_params.kt1l = value;
            else if (key == "kt2") bsim3_params.kt2 = value;
            else if (key == "ua1") bsim3_params.ua1 = value;
            else if (key == "ub1") bsim3_params.ub1 = value;
            else if (key == "uc1") bsim3_params.uc1 = value;
            // Noise parameters
            else if (key == "kf") bsim3_params.kf = value;
            else if (key == "af") bsim3_params.af = value;
            else if (key == "ef") bsim3_params.ef = value;
            else if (key == "noia") bsim3_params.noia = value;
            else if (key == "noib") bsim3_params.noib = value;
            else if (key == "noic") bsim3_params.noic = value;
            else if (key == "em") bsim3_params.em = value;
            // Geometry scaling
            else if (key == "lint") bsim3_params.lint = value;
            else if (key == "ll") bsim3_params.ll = value;
            else if (key == "llc") bsim3_params.llc = value;
            else if (key == "lln") bsim3_params.lln = value;
            else if (key == "lw") bsim3_params.lw = value;
            else if (key == "lwc") bsim3_params.lwc = value;
            else if (key == "lwn") bsim3_params.lwn = value;
            else if (key == "lwl") bsim3_params.lwl = value;
            else if (key == "lwlc") bsim3_params.lwlc = value;
            else if (key == "wint") bsim3_params.wint = value;
            else if (key == "wl") bsim3_params.wl = value;
            else if (key == "wlc") bsim3_params.wlc = value;
            else if (key == "wln") bsim3_params.wln = value;
            else if (key == "ww") bsim3_params.ww = value;
            else if (key == "wwc") bsim3_params.wwc = value;
            else if (key == "wwn") bsim3_params.wwn = value;
            else if (key == "wwl") bsim3_params.wwl = value;
            else if (key == "wwlc") bsim3_params.wwlc = value;
            // Instance parameters (will be overridden by M-line parameters)
            else if (key == "w") bsim3_params.w = value;
            else if (key == "l") bsim3_params.l = value;
            else if (key == "as") bsim3_params.as = value;
            else if (key == "ad") bsim3_params.ad = value;
            else if (key == "ps") bsim3_params.ps = value;
            else if (key == "pd") bsim3_params.pd = value;
            else if (key == "nrd") bsim3_params.nrd = static_cast<int>(value);
            else if (key == "nrs") bsim3_params.nrs = static_cast<int>(value);
            // Level parameter (already processed)
            else if (key == "level") {
                // Already handled, skip
            }
            // Warn about unrecognized parameters
            else {
                spdlog::warn("Unrecognized BSIM3 parameter '{}' in model {}", key, type_or_model);
            }
        }

        // Override with instance parameters (W, L from tokens)
        bsim3_params.w = w;
        bsim3_params.l = l;

        // BSIM3: use bulk node determined earlier (4-terminal or 3-terminal)
        if (mosfet_type == "BSIM3_N") {
            state.circuit->add_mosfet_bsim3_n(dev_name, drain, gate, source, bulk, bsim3_params);
        } else {
            state.circuit->add_mosfet_bsim3_p(dev_name, drain, gate, source, bulk, bsim3_params);
        }
    } else if (mosfet_type == "BSIM4_N" || mosfet_type == "BSIM4_P") {
        // BSIM4 model: create BSIM4Params from model parameters
        const ModelDefinition* found_model = state.config->find_model(type_or_model);
        const ModelDefinition& model = *found_model;

        std::vector<std::string> bsim4_unused;
        devices::BSIM4Params bsim4_params =
            devices::bsim4_params_from_model(model.params, type_or_model, &bsim4_unused);
        warn_unused_card_names(state, "BSIM4 model " + type_or_model, bsim4_unused);

        bsim4_params.w = w;
        bsim4_params.l = l;
        bsim4_params.mult_i = mult_i;
        bsim4_params.mult_q = mult_q;
        bsim4_params.mult_fn = mult_fn;
        // NF, M, AS, AD, PS, PD, NRD and NRS of the element line
        for (const auto& [key, value] : instance_params) {
            devices::set_bsim4_instance_parameter(bsim4_params, key, value);
        }

        bsim4_params.compute_derived_params(bsim4_params.tnom);

        // BSIM4: use bulk node determined earlier (4-terminal or 3-terminal)
        if (mosfet_type == "BSIM4_N") {
            state.circuit->add_mosfet_bsim4_n(dev_name, drain, gate, source, bulk, bsim4_params);
        } else {
            state.circuit->add_mosfet_bsim4_p(dev_name, drain, gate, source, bulk, bsim4_params);
        }
    } else if (mosfet_type == "SIC_N" || mosfet_type == "SIC_P") {
        // SiC MOSFET (LEVEL=10): the device with its own defaults (those of
        // its polarity), then the card's parameters and the element line's
        if (mosfet_type == "SIC_N") {
            state.circuit->add_mosfet_sic_n(dev_name, drain, gate, source);
        } else {
            state.circuit->add_mosfet_sic_p(dev_name, drain, gate, source);
        }
        auto sic = state.circuit->get_device(dev_name);
        if (const ModelDefinition* sic_model = state.config->find_model(type_or_model)) {
            set_parameters_by_name(*sic, sic_model->params, "SiC MOSFET model " + type_or_model);
        }
        set_parameters_by_name(*sic, instance_params, "SiC MOSFET " + dev_name);
        spdlog::debug("Added {}: {} D={} G={} S={}", mosfet_type, dev_name, drain, gate, source);
    } else if (mosfet_type == "GAN_HEMT_N" || mosfet_type == "GAN_HEMT_P") {
        // GaN HEMT (LEVEL=11): the device with its own defaults (those of
        // its polarity), then the card's parameters and the element line's
        if (mosfet_type == "GAN_HEMT_N") {
            state.circuit->add_gan_hemt_n(dev_name, drain, gate, source);
        } else {
            state.circuit->add_gan_hemt_p(dev_name, drain, gate, source);
        }
        auto hemt = state.circuit->get_device(dev_name);
        if (const ModelDefinition* hemt_model = state.config->find_model(type_or_model)) {
            set_parameters_by_name(*hemt, hemt_model->params, "GaN HEMT model " + type_or_model);
        }
        set_parameters_by_name(*hemt, instance_params, "GaN HEMT " + dev_name);
        spdlog::debug("Added {}: {} D={} G={} S={}", mosfet_type, dev_name, drain, gate, source);
    } else if (mosfet_type == "FINFET_N" || mosfet_type == "FINFET_P") {
        // FinFET (LEVEL=12): the device with its own defaults, then the
        // card's parameters and the gate length through
        // FinFET::set_parameter, so that the defaults are those of the
        // device and of its polarity
        if (mosfet_type == "FINFET_N") {
            state.circuit->add_finfet_n(dev_name, drain, gate, source);
        } else {
            state.circuit->add_finfet_p(dev_name, drain, gate, source);
        }
        auto finfet = state.circuit->get_device(dev_name);
        const ModelDefinition* finfet_model = state.config->find_model(type_or_model);
        if (finfet_model != nullptr) {
            set_parameters_by_name(*finfet, finfet_model->params, "FinFET model " + type_or_model);
        }
        finfet->set_parameter("l", l);
        spdlog::debug("Added {}: {} D={} G={} S={} L={:.3e}m", mosfet_type, dev_name, drain, gate,
                      source, l);
    } else if (mosfet_type == "VDMOS_N" || mosfet_type == "VDMOS_P") {
        // VDMOS (LEVEL=20): the device with its own defaults (those of its
        // polarity), then the card's parameters and the element line's
        if (mosfet_type == "VDMOS_N") {
            state.circuit->add_vdmos_n(dev_name, drain, gate, source);
        } else {
            state.circuit->add_vdmos_p(dev_name, drain, gate, source);
        }
        auto vdmos = state.circuit->get_device(dev_name);
        if (const ModelDefinition* vdmos_model = state.config->find_model(type_or_model)) {
            set_parameters_by_name(*vdmos, vdmos_model->params, "VDMOS model " + type_or_model);
        }
        set_parameters_by_name(*vdmos, instance_params, "VDMOS " + dev_name);
        spdlog::debug("Added {}: {} D={} G={} S={}", mosfet_type, dev_name, drain, gate, source);
    } else if (mosfet_type == "IGBT_N" || mosfet_type == "IGBT_P") {
        // IGBT (LEVEL=21; drain is the collector, source the emitter): the
        // device with its own defaults, then the card's parameters and the
        // element line's
        if (mosfet_type == "IGBT_N") {
            state.circuit->add_igbt_n(dev_name, drain, gate, source);
        } else {
            state.circuit->add_igbt_p(dev_name, drain, gate, source);
        }
        auto igbt = state.circuit->get_device(dev_name);
        if (const ModelDefinition* igbt_model = state.config->find_model(type_or_model)) {
            set_parameters_by_name(*igbt, igbt_model->params, "IGBT model " + type_or_model);
        }
        set_parameters_by_name(*igbt, instance_params, "IGBT " + dev_name);
        spdlog::debug("Added {}: {} C={} G={} E={}", mosfet_type, dev_name, drain, gate, source);
    } else if (mosfet_type == "NMOS" || mosfet_type == "PMOS") {
        // The element line of level 1 gives KP, W, L, VTH and the noise
        // parameters; other tokens are not used
        static const std::set<std::string> kLine = {"kp", "w", "l", "vth", "kf", "af", "ef"};
        for (size_t i = param_start_idx; i < tokens.size(); ++i) {
            std::string key = tokens[i].substr(0, tokens[i].find('='));
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            if (tokens[i].find('=') == std::string::npos || !kLine.count(key)) {
                spdlog::warn("Line {}: {}: '{}' is not used", state.line_number, dev_name, tokens[i]);
            }
        }
        // A level-1 card has no overlap capacitances (CGSO, CGDO, CGBO = 0)
        if (mosfet_type == "NMOS") {
            state.circuit->add_mosfet_n(dev_name, drain, gate, source, kp, w, l, vth, 0.0, 0.0, 0.0,
                                        kf, af, ef, gamma);
        } else {
            state.circuit->add_mosfet_p(dev_name, drain, gate, source, kp, w, l, vth, 0.0, 0.0, 0.0,
                                        kf, af, ef, gamma);
        }
        set_level1_body(*state.circuit, dev_name, bulk, gamma_body, phi, lambda);
        spdlog::debug("Added MOSFET_{}: {} D={} G={} S={} B={} Kp={:.3e}A/V² W={:.3e}m L={:.3e}m Vth={:.3f}V",
                      mosfet_type == "NMOS" ? "N" : "P", dev_name, drain, gate, source, bulk, kp, w,
                      l, vth);
    }
}

// The POLY form of a controlled source, E/F/G/H <n+> <n-> POLY(<n>) ..., is
// not implemented. Read as the linear form, its POLY(<n>) would be a node or
// a source name and a coefficient the gain, so it is an error instead.
static void reject_poly_form(const std::vector<std::string>& tokens, ParseState& state) {
    if (tokens.size() > 3 &&
        (util::starts_with_icase(tokens[3], "POLY(") || util::equals_icase(tokens[3], "POLY"))) {
        throw ParseError(fmt::format("{}: the POLY form of a controlled source is not implemented",
                                     tokens[0]),
                         state.line_number);
    }
}

static void parse_vcvs_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // VCVS: E<name> <n+> <n-> <nc+> <nc-> <gain>
    reject_poly_form(tokens, state);
    if (tokens.size() < 6) {
        throw ParseError("Invalid VCVS syntax (E<name> <n+> <n-> <nc+> <nc-> <gain>)", state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];
    std::string ncp = tokens[3];
    std::string ncn = tokens[4];
    std::string gain_str = tokens[5];

    const Real gain = read_element_value(gain_str, state);

    state.circuit->add_vcvs(dev_name, np, nn, ncp, ncn, gain);
    spdlog::debug("Added VCVS: {} np={} nn={} ncp={} ncn={} gain={}",
                 dev_name, np, nn, ncp, ncn, gain);
}

static void parse_cccs_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // CCCS: F<name> <n+> <n-> V<control> <gain>
    reject_poly_form(tokens, state);
    if (tokens.size() < 5) {
        throw ParseError("Invalid CCCS syntax (F<name> <n+> <n-> V<control> <gain>)", state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];
    std::string control_vsource = tokens[3];
    std::string gain_str = tokens[4];

    // The control is a voltage source, whose name starts with V (in a
    // subcircuit its last part does, X1.Vs); resolve_controlled_source_references
    // checks the device too
    if (element_letter(control_vsource) != 'V') {
        throw ParseError("CCCS control must reference a voltage source (V<name>)", state.line_number);
    }

    const Real gain = read_element_value(gain_str, state);

    state.circuit->add_cccs(dev_name, np, nn, control_vsource, gain);
    spdlog::debug("Added CCCS: {} np={} nn={} control={} gain={}",
                 dev_name, np, nn, control_vsource, gain);
}

static void parse_vccs_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // VCCS: G<name> <n+> <n-> <nc+> <nc-> <transconductance>
    reject_poly_form(tokens, state);
    if (tokens.size() < 6) {
        throw ParseError("Invalid VCCS syntax (G<name> <n+> <n-> <nc+> <nc-> <Gm>)", state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];
    std::string ncp = tokens[3];
    std::string ncn = tokens[4];
    std::string gm_str = tokens[5];

    const Real transconductance = read_element_value(gm_str, state);

    state.circuit->add_vccs(dev_name, np, nn, ncp, ncn, transconductance);
    spdlog::debug("Added VCCS: {} np={} nn={} ncp={} ncn={} Gm={}S",
                 dev_name, np, nn, ncp, ncn, transconductance);
}

static void parse_ccvs_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // CCVS: H<name> <n+> <n-> V<control> <transresistance>
    reject_poly_form(tokens, state);
    if (tokens.size() < 5) {
        throw ParseError("Invalid CCVS syntax (H<name> <n+> <n-> V<control> <H>)", state.line_number);
    }
    std::string np = tokens[1];
    std::string nn = tokens[2];
    std::string control_vsource = tokens[3];
    std::string h_str = tokens[4];

    // The control is a voltage source, whose name starts with V (in a
    // subcircuit its last part does, X1.Vs); resolve_controlled_source_references
    // checks the device too
    if (element_letter(control_vsource) != 'V') {
        throw ParseError("CCVS control must reference a voltage source (V<name>)", state.line_number);
    }

    const Real transresistance = read_element_value(h_str, state);

    state.circuit->add_ccvs(dev_name, np, nn, control_vsource, transresistance);
    spdlog::debug("Added CCVS: {} np={} nn={} control={} H={}Ω",
                 dev_name, np, nn, control_vsource, transresistance);
}

static void parse_mutual_inductance_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // Mutual Inductance: K<name> L<inductor1> L<inductor2> <coupling_coefficient>
    if (tokens.size() < 4) {
        throw ParseError("Invalid mutual inductance syntax (K<name> L<inductor1> L<inductor2> <k>)", state.line_number);
    }
    std::string inductor1_name = tokens[1];
    std::string inductor2_name = tokens[2];
    std::string k_str = tokens[3];

    // The two are inductors, whose names start with L (in a subcircuit their
    // last parts do, X1.L1); resolve_mutual_inductance_references checks the
    // devices too
    if (element_letter(inductor1_name) != 'L') {
        throw ParseError("Mutual inductance first inductor must reference an inductor (L<name>)", state.line_number);
    }
    if (element_letter(inductor2_name) != 'L') {
        throw ParseError("Mutual inductance second inductor must reference an inductor (L<name>)", state.line_number);
    }

    const Real coupling_coefficient = read_element_value(k_str, state);

    // Validate coupling coefficient range
    if (coupling_coefficient < 0.0 || coupling_coefficient > 1.0) {
        throw ParseError(fmt::format("Coupling coefficient k={} out of range [0, 1]", coupling_coefficient), state.line_number);
    }

    state.circuit->add_mutual_inductance(dev_name, inductor1_name, inductor2_name, coupling_coefficient);
    spdlog::debug("Added MutualInductance: {} (L1={}, L2={}, k={})",
                 dev_name, inductor1_name, inductor2_name, coupling_coefficient);
}

static void parse_tline_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // Lossless Transmission Line: T<name> <ap> <an> <bp> <bn> Z0=<value> TD=<value>
    // Example: T1 1 0 2 0 Z0=50 TD=10NS
    if (tokens.size() < 5) {
        throw ParseError("Invalid transmission line syntax (T<name> <ap> <an> <bp> <bn> Z0=<value> TD=<value>)", state.line_number);
    }

    std::string ap = tokens[1];
    std::string an = tokens[2];
    std::string bp = tokens[3];
    std::string bn = tokens[4];

    // Parse parameters (Z0=<value>, TD=<value>)
    Real z0 = 0.0;
    Real td = 0.0;
    bool has_z0 = false;
    bool has_td = false;

    for (std::size_t i = 5; i < tokens.size(); ++i) {
        std::string token = tokens[i];
        // Convert to uppercase for case-insensitive matching
        std::string token_upper = token;
        std::transform(token_upper.begin(), token_upper.end(), token_upper.begin(), ::toupper);

        // Find '=' separator
        auto eq_pos = token_upper.find('=');
        if (eq_pos == std::string::npos) {
            continue; // Skip tokens without '='
        }

        std::string param_name = token_upper.substr(0, eq_pos);
        std::string param_value = token.substr(eq_pos + 1); // Use original token for value (preserve case)

        if (param_name == "Z0" || param_name == "ZO") {
            z0 = read_element_value(param_value, state);
            has_z0 = true;
        } else if (param_name == "TD") {
            td = read_element_value(param_value, state);
            has_td = true;
        }
    }

    // Validate required parameters
    if (!has_z0) {
        throw ParseError("Transmission line missing required parameter Z0 (characteristic impedance)", state.line_number);
    }
    if (!has_td) {
        throw ParseError("Transmission line missing required parameter TD (time delay)", state.line_number);
    }
    if (z0 <= 0.0) {
        throw ParseError(fmt::format("Transmission line Z0={} must be positive", z0), state.line_number);
    }
    if (td <= 0.0) {
        throw ParseError(fmt::format("Transmission line TD={} must be positive", td), state.line_number);
    }

    state.circuit->add_tline_lossless(dev_name, ap, an, bp, bn, z0, td);
    spdlog::debug("Added TLineLossless: {} (A={},{}, B={},{}, Z0={} Ω, TD={} s)",
                 dev_name, ap, an, bp, bn, z0, td);
}

static void parse_tline_lumped_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // Lossy Transmission Line (O-element):
    // O<name> <ap> <an> <bp> <bn> R=<r> L=<l> C=<c> G=<g> TD=<td>
    if (tokens.size() < 5) {
        throw ParseError("Invalid O-line syntax (O<name> <ap> <an> <bp> <bn> R= L= C= G= TD=)", state.line_number);
    }

    std::string ap = tokens[1];
    std::string an = tokens[2];
    std::string bp = tokens[3];
    std::string bn = tokens[4];

    Real r = 0.0, l = 0.0, c = 0.0, g = 0.0, td = 0.0;
    bool has_td = false;

    for (std::size_t i = 5; i < tokens.size(); ++i) {
        std::string token = tokens[i];
        std::string token_upper = token;
        std::transform(token_upper.begin(), token_upper.end(), token_upper.begin(), ::toupper);

        auto eq_pos = token_upper.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string param = token_upper.substr(0, eq_pos);
        std::string value = token.substr(eq_pos + 1);

        if (param == "R") r = read_element_value(value, state);
        else if (param == "L") l = read_element_value(value, state);
        else if (param == "C") c = read_element_value(value, state);
        else if (param == "G") g = read_element_value(value, state);
        else if (param == "TD") { td = read_element_value(value, state); has_td = true; }
    }

    if (!has_td || td <= 0.0) {
        throw ParseError("O-line missing or invalid TD (>0 required)", state.line_number);
    }

    state.circuit->add_tline_lossy(dev_name, ap, an, bp, bn, r, l, c, g, td);
    spdlog::debug("Added TLineLossy: {} (A={},{}, B={},{}, R={} L={} C={} G={} TD={} s)",
                  dev_name, ap, an, bp, bn, r, l, c, g, td);
}

static void parse_subcircuit_instance_device(const std::vector<std::string>& tokens, const std::string& dev_name, ParseState& state) {
    // Subcircuit instance: X<name> <node1> <node2> ... <subcircuit_type> [PARAM=VALUE ...]
    // Syntax: X1 in out VCC GND OPAMP GAIN=10
    if (tokens.size() < 2) {
        throw ParseError("Invalid subcircuit instance syntax: X<name> <nodes...> <subcircuit_type>",
                        state.line_number);
    }

    std::string instance_name = dev_name;

    const circuit::InstanceLineParts parts = circuit::split_instance_line(tokens);
    if (parts.type.empty()) {
        throw ParseError("Missing subcircuit type in X device", state.line_number);
    }
    if (!state.circuit->subcircuits().has_subcircuit(parts.type)) {
        throw UnresolvedDefinition(
            fmt::format("Subcircuit instance '{}' references undefined subcircuit type '{}'",
                        instance_name, parts.type),
            state.line_number);
    }
    const std::string& subcircuit_type = parts.type;
    const std::vector<std::string>& connected_nodes = parts.nodes;

    std::map<std::string, Real> instance_params;
    for (const auto& [param_name, param_value_str] : parts.assignments) {
        const Real param_value = read_element_value(param_value_str, state);
        instance_params[param_name] = param_value;
    }

    // Create subcircuit instance
    circuit::SubcircuitInstance instance(instance_name, subcircuit_type, connected_nodes, state.line_number);
    instance.instance_params = instance_params;

    // Add to circuit
    state.circuit->subcircuits().add_instance(instance);

    spdlog::debug("Added subcircuit instance: {} type={}, nodes={}, params={}",
                 instance_name, subcircuit_type, connected_nodes.size(), instance_params.size());
}

// The letters of the elements the program simulates; an element of another
// letter is not dropped, since the circuit would then be another one
static bool is_element_letter(char letter) {
    return std::string_view("BCDEFGHIKLMOQRSTVXYZ").find(letter) != std::string_view::npos;
}

static ParseError unknown_element_letter(char letter, const std::string& name, std::size_t line) {
    return ParseError(fmt::format("Unknown element letter '{}' in '{}'; the elements are "
                                  "B C D E F G H I K L M O Q R S T V X Y Z",
                                  letter, name),
                      line);
}

// Parse an element line of the given type letter and name: the type is the
// first letter of the name on a line of the top level, and that of the
// element as written in a subcircuit body, whose name is then X1.R1
// (BodyLineReader). A line whose model or subcircuit comes later is deferred.
static void parse_element(const std::vector<std::string>& tokens, char dev_type,
                          const std::string& dev_name, const std::string& line,
                          ParseState& state) {
    if (!is_element_letter(dev_type)) {
        throw unknown_element_letter(dev_type, dev_name, state.line_number);
    }
    try {
        switch (dev_type) {
            case 'R': parse_resistor_device(tokens, dev_name, state); break;
            case 'B': parse_behavioral_source_device(tokens, dev_name, state); break;
            case 'C': parse_capacitor_device(tokens, dev_name, state); break;
            case 'L': parse_inductor_device(tokens, dev_name, state); break;
            case 'V': parse_voltage_source_device(tokens, dev_name, state); break;
            case 'I': parse_current_source_device(tokens, dev_name, state); break;
            case 'D': parse_diode_device(tokens, dev_name, state); break;
            case 'S': parse_scr_device(tokens, dev_name, state); break;
            case 'Y': parse_triac_device(tokens, dev_name, state); break;
            case 'Z': parse_mesfet_device(tokens, dev_name, state); break;
            case 'Q': parse_bjt_device(tokens, dev_name, state); break;
            case 'M': parse_mosfet_device(tokens, dev_name, line, state); break;
            case 'E': parse_vcvs_device(tokens, dev_name, state); break;
            case 'F': parse_cccs_device(tokens, dev_name, state); break;
            case 'G': parse_vccs_device(tokens, dev_name, state); break;
            case 'H': parse_ccvs_device(tokens, dev_name, state); break;
            case 'K': parse_mutual_inductance_device(tokens, dev_name, state); break;
            case 'T': parse_tline_device(tokens, dev_name, state); break;
            case 'O': parse_tline_lumped_device(tokens, dev_name, state); break;
            case 'X': parse_subcircuit_instance_device(tokens, dev_name, state); break;
            default:
                break;  // rejected above
        }
    } catch (const UnresolvedDefinition& e) {
        if (!state.resolving_deferred) {
            state.deferred_elements.emplace_back(line, state.line_number);
            return;
        }
        throw ParseError(fmt::format("Error parsing device '{}': {}", dev_name, e.what()),
                       state.line_number);
    } catch (const std::exception& e) {
        throw ParseError(fmt::format("Error parsing device '{}': {}", dev_name, e.what()),
                       state.line_number);
    }
}

// Helper function to process a single line
void process_line(const std::string& raw_line, ParseState& state) {
    std::string line = raw_line;

    // Remove inline comments
    size_t comment_pos = line.find(';');
    if (comment_pos != std::string::npos) {
        line = line.substr(0, comment_pos);
    }

    // Use trim_inplace utility
    util::trim_inplace(line);

    // Skip empty lines or comment lines
    if (line.empty() || line[0] == '*') {
        return;
    }

    // Check for control cards (directives)
    if (line[0] == '.') {
        auto tokens = util::split_netlist_line(line);
        if (tokens.empty()) {
            return;
        }

        // Use to_upper_inplace utility
        std::string directive = tokens[0];
        util::to_upper_inplace(directive);

        state.config->directives_read.insert(directive);
        if (directive == ".OP") { state.config->op_requested = true; return; }
        if (directive == ".AC") { parse_ac_directive(tokens, state); return; }
        if (directive == ".DC") { parse_dc_directive(tokens, state); return; }
        if (directive == ".TRAN") { parse_tran_directive(tokens, state); return; }
        if (directive == ".OPTION" || directive == ".OPTIONS") { parse_options_directive(tokens, state); return; }
        if (directive == ".IC") { parse_ic_directive(tokens, state); return; }
        if (directive == ".NODESET") { parse_nodeset_directive(tokens, state); return; }
        if (directive == ".PRINT") { parse_print_directive(tokens, state); return; }
        if (directive == ".SAVE") { parse_save_directive(tokens, state); return; }
        if (directive == ".GLOBAL") { parse_global_directive(tokens, state); return; }
        if (directive == ".PARAM") {
            // Inside a .SUBCKT, .PARAM is local: kept with the body and
            // evaluated per instance (SubcircuitDefinition::substitute_parameters)
            if (state.inside_subcircuit) {
                state.current_subcircuit->internal_netlist_lines.push_back(line);
                return;
            }
            parse_param_directive(tokens, state);
            return;
        }
        if (directive == ".STEP") { parse_step_directive(tokens, state); return; }
        if (directive == ".SENS" || directive == ".SENSITIVITY") { parse_sens_directive(tokens, state); return; }
        if (directive == ".TEMP") { parse_temp_directive(tokens, state); return; }
        if (directive == ".MC" || directive == ".MONTECARLO") { parse_mc_directive(tokens, state); return; }
        if (directive == ".TF") { parse_tf_directive(tokens, state); return; }
        if (directive == ".FOUR" || directive == ".FOURIER") { parse_four_directive(tokens, state); return; }
        if (directive == ".PZ") { parse_pz_directive(tokens, state); return; }
        if (directive == ".DISTO") { parse_disto_directive(tokens, state); return; }
        if (directive == ".PORT") { parse_port_directive(tokens, state); return; }
        if (directive == ".WCASE") { parse_wcase_directive(tokens, state); return; }
        if (directive == ".OPTIMIZE") { parse_optimize_directive(tokens, state); return; }
        if (directive == ".FUNC") { parse_func_directive(tokens, line, state); return; }
        if (directive == ".MODEL") { parse_model_directive(tokens, state); return; }
        if (directive == ".SUBCKT") { parse_subckt_directive(tokens, state); return; }
        if (directive == ".ENDS") { parse_ends_directive(tokens, state); return; }

        // .END ends nothing here (the lines after it are read); any other
        // directive is ignored, and named in one warning
        if (directive == ".END") {
            return;
        }
        if (state.warned_directives.insert(directive).second) {
            static const std::set<std::string> kNotSupported = {".NOISE", ".MEASURE", ".MEAS",
                                                                ".TITLE"};
            if (kNotSupported.count(directive)) {
                spdlog::warn("Line {}: {} is not supported and is ignored", state.line_number,
                             tokens[0]);
            } else {
                spdlog::warn("Line {}: unknown directive {} is ignored", state.line_number,
                             tokens[0]);
            }
        }
        return;
    }

    // If inside subcircuit, accumulate lines to internal netlist
    if (state.inside_subcircuit) {
        state.current_subcircuit->internal_netlist_lines.push_back(line);
        return;
    }

    // Parse device line
    auto tokens = util::split_netlist_line(line);
    if (tokens.empty()) {
        return;
    }

    const char dev_type = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[0][0])));
    parse_element(tokens, dev_type, tokens[0], line, state);
}


// Each V(node) of a B expression with its node renamed by map_node, V in
// either case (as CircuitGraph's rewrite_node_voltages reads it)
static std::string map_node_voltages(const std::string& expression,
                                     const std::function<std::string(const std::string&)>& map_node) {
    std::string out;
    std::size_t i = 0;
    while (i < expression.size()) {
        const char c = expression[i];
        const bool starts_name = i == 0 ||
            !(std::isalnum(static_cast<unsigned char>(expression[i - 1])) || expression[i - 1] == '_');
        if ((c == 'V' || c == 'v') && starts_name && i + 1 < expression.size() &&
            expression[i + 1] == '(') {
            const std::size_t close = expression.find(')', i + 2);
            if (close != std::string::npos) {
                std::string node = expression.substr(i + 2, close - i - 2);
                node.erase(0, node.find_first_not_of(" \t"));
                node.erase(node.find_last_not_of(" \t") + 1);
                if (!node.empty()) {
                    out += c;
                    out += '(' + map_node(node) + ')';
                    i = close + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

// A body line of the instance `instance` in the names of the circuit: the
// element's name, and those of the elements it names (the controlling source
// of F and H, the inductors of K), prefixed with the instance's name, X1.R1;
// each node mapped by map_node; the nodes in the V(node) of a B expression
// mapped too. Which tokens are nodes is decided as each element parser reads
// its line.
static std::vector<std::string> rename_body_line(
    std::vector<std::string> tokens, const std::string& instance,
    const std::function<std::string(const std::string&)>& map_node,
    const SimulationConfig& config, std::size_t line_number) {
    const std::string prefix = instance + ".";
    const char type = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[0][0])));
    auto nodes = [&](std::size_t first, std::size_t last) {
        for (std::size_t k = first; k <= last && k < tokens.size(); ++k) {
            tokens[k] = map_node(tokens[k]);
        }
    };
    auto names = [&](std::size_t first, std::size_t last) {
        for (std::size_t k = first; k <= last && k < tokens.size(); ++k) {
            tokens[k] = prefix + tokens[k];
        }
    };
    switch (type) {
        case 'R': case 'C': case 'L': case 'V': case 'I': case 'D':
            nodes(1, 2);
            break;
        case 'B':
            nodes(1, 2);
            for (std::size_t k = 3; k < tokens.size(); ++k) {
                tokens[k] = map_node_voltages(tokens[k], map_node);
            }
            break;
        case 'E': case 'G': case 'T': case 'O':
            nodes(1, 4);
            break;
        case 'F': case 'H':
            nodes(1, 2);
            names(3, 3);
            break;
        case 'K':
            names(1, 2);
            break;
        case 'Q': case 'Z': case 'S': case 'Y':
            nodes(1, 3);
            break;
        case 'M':
            if (tokens.size() < 5) {
                throw ParseError(fmt::format("Invalid MOSFET line in a subcircuit: '{}'",
                                             fmt::join(tokens, " ")), line_number);
            }
            nodes(1, mosfet_model_index(tokens, config) - 1);
            break;
        case 'X': {
            const circuit::InstanceLineParts parts = circuit::split_instance_line(tokens);
            nodes(1, parts.nodes.size());
            break;
        }
        default:
            throw unknown_element_letter(type, tokens[0], line_number);
    }
    tokens[0] = prefix + tokens[0];
    return tokens;
}

// The SubcircuitLineReader of the parser: a body line, renamed into its
// instance (rename_body_line), goes through process_line as a line of the
// top level does, with the models and parameters of the whole netlist.
class BodyLineReader : public circuit::SubcircuitLineReader {
public:
    explicit BodyLineReader(const SimulationConfig& config) : config_(config) {
        for (const auto& [name, value] : config_.global_params) {
            evaluator_.set_variable(name, value);
        }
        evaluator_.set_functions(config_.global_funcs);
    }

    const std::map<std::string, Real>& global_params() const override {
        return config_.global_params;
    }

    void read(circuit::CircuitGraph& circuit, const std::string& line,
              const std::string& instance_name,
              const std::function<std::string(const std::string&)>& map_node,
              std::size_t line_number) override {
        std::vector<std::string> tokens = util::split_netlist_line(line);
        if (tokens.empty()) {
            return;
        }
        if (tokens[0][0] == '.') {
            // .PARAM lines are the parameters' (substitute_parameters); the
            // other directives of a body are read with the netlist
            return;
        }
        const char type = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[0][0])));
        tokens = rename_body_line(std::move(tokens), instance_name, map_node, config_, line_number);
        ParseState state;
        state.circuit = &circuit;
        state.evaluator = &evaluator_;
        state.config = &config_;
        state.line_number = line_number;
        state.resolving_deferred = true;  // every model is known by now
        parse_element(tokens, type, tokens[0], fmt::format("{}", fmt::join(tokens, " ")), state);
    }

private:
    SimulationConfig config_;
    ExpressionEvaluator evaluator_;
};

// Process each line
template<>
struct action<grammar::content_line> {
    template<typename Input>
    static void apply(const Input& in, ParseState& state) {
        std::string line = in.string();

        // Remove trailing newline
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }

        if (state.line_number == 1) {
            // First line is title
            // Use trim_inplace utility
            util::trim_inplace(line);

            state.circuit->set_title(line);
            spdlog::debug("Parsed title: {}", line);
        } else {
            // Process other lines
            process_line(line, state);
        }

        state.line_number++;
    }
};

//==============================================================================
// NetlistParser::Impl
//==============================================================================
class NetlistParser::Impl {
public:
    NetlistParseResult parse(
        const std::string& content,
        const std::unordered_map<std::string, Real>& initial_params = {}
    ) {
        // Check for empty netlist
        if (content.empty()) {
            throw ParseError("Empty netlist", 1);
        }

        auto circuit = std::make_unique<circuit::CircuitGraph>();
        ExpressionEvaluator eval;
        SimulationConfig config;  // NEW: Create simulation config

        // Apply initial_params to evaluator BEFORE parsing
        // This ensures that sweep parameters override .PARAM definitions in netlist
        for (const auto& [name, value] : initial_params) {
            eval.set_variable(name, value);
            config.global_params[name] = value;  // Also store in config
            spdlog::debug("Initial parameter override: {}={}", name, value);
        }

        // Preprocess conditional directives (.IF/.ELIF/.ELSE/.ENDIF)
        // Note: This is a simplified integration. Full integration requires parsing .PARAM first.
        // For now, conditionals can use initial_params but not .PARAM from netlist.
        std::string preprocessed_content = content;
        try {
            preprocessed_content = preprocess_conditionals(content, eval);
            spdlog::debug("Conditional preprocessing completed");
        } catch (const std::exception& e) {
            spdlog::warn("Conditional preprocessing failed: {}", e.what());
            // Continue with original content if preprocessing fails
            preprocessed_content = content;
        }

        // Preprocess .CONTROL/.ENDC blocks
        ControlBlock control_block;
        control_block.enabled = false;  // Default: no control block
        try {
            preprocessed_content = preprocess_control_blocks(preprocessed_content, control_block);
            if (control_block.enabled) {
                spdlog::debug(".CONTROL/.ENDC block found with {} commands", control_block.commands.size());
                config.control_block = control_block;
            }
        } catch (const ParseError& e) {
            throw;  // Re-throw ParseError as-is
        } catch (const std::exception& e) {
            throw ParseError(fmt::format(".CONTROL/.ENDC preprocessing failed: {}", e.what()), 0);
        }

        ParseState state;
        state.circuit = circuit.get();
        state.evaluator = &eval;
        state.config = &config;  // NEW: Set config pointer

        pegtl::memory_input input(preprocessed_content, "netlist");

        // The grammar reads each line up to its line feed. A line it cannot
        // read ends the parse, so it is an error, not the end of the netlist:
        // the line and every line after it would be dropped. Only a carriage
        // return that does not end a line stops it (without_carriage_returns
        // has removed those that do).
        bool read_to_end = false;
        try {
            read_to_end = pegtl::parse<grammar::netlist, action>(input, state);
        } catch (const pegtl::parse_error& e) {
            throw ParseError(e.what(), state.line_number);
        }
        if (!read_to_end) {
            throw ParseError(
                fmt::format("Line {}: the line cannot be read: it holds a carriage return "
                            "that does not end it",
                            state.line_number),
                state.line_number);
        }

        // A .SUBCKT must end with .ENDS, as ngspice requires ("Mismatch of
        // .subckt ... .ends"); otherwise every later line would be its body
        if (state.inside_subcircuit && state.current_subcircuit) {
            throw ParseError(fmt::format(".SUBCKT {} has no .ENDS",
                                         state.current_subcircuit->name),
                             state.current_subcircuit->definition_line);
        }

        // Parse the elements whose model or subcircuit is defined further
        // down the netlist (UnresolvedDefinition). They are top-level lines,
        // so they are parsed outside any .SUBCKT.
        state.resolving_deferred = true;
        const bool was_inside_subcircuit = state.inside_subcircuit;
        state.inside_subcircuit = false;
        for (const auto& [line, line_number] : state.deferred_elements) {
            state.line_number = line_number;
            process_line(line, state);
        }
        state.inside_subcircuit = was_inside_subcircuit;

        // Register .FUNC definitions in ExpressionEvaluator
        // This enables function calls in device parameter expressions
        eval.set_functions(config.global_funcs);

        // Resolve control voltage source references for CCCS and CCVS
        circuit->resolve_controlled_source_references();

        // Resolve inductor references for MutualInductance devices
        circuit->resolve_mutual_inductance_references();

        // Execute control block commands (if present)
        if (config.control_block.has_value()) {
            execute_control_commands(config.control_block.value(), eval, config, *circuit);
        }

        // Subcircuit bodies are read as the top level is, at expansion
        circuit->set_subcircuit_line_reader(std::make_shared<BodyLineReader>(config));

        return NetlistParseResult(std::move(circuit), std::move(config));
    }

    // Legacy: parse and return circuit only (for backward compatibility)
    std::unique_ptr<circuit::CircuitGraph> parse_legacy(const std::string& content) {
        NetlistParseResult result = parse(content);
        return std::move(result.circuit);
    }
};

//==============================================================================
// ExpressionEvaluator::Impl
//==============================================================================
class ExpressionEvaluator::Impl {
public:
    Impl() {
        // IMPORTANT: Do NOT call add_constants() or add_package() with default functions
        // This causes variable name conflicts (e.g., "sum", "min", "max" are built-in functions)
        // Only add explicitly needed constants
        // Check ExprTk return values
        for (const auto& constant : util::kExpressionConstants) {
            if (!symbol_table_.add_constant(constant.name, constant.value)) {
                spdlog::warn("ExpressionEvaluator: Failed to add constant '{}'", constant.name);
            }
        }
    }

    void set_variable(const std::string& name, Real value) {
        // Check if variable already exists
        auto it = variables_.find(name);
        if (it != variables_.end()) {
            // Variable exists: update value directly (reference remains valid)
            it->second = value;
        } else {
            // New variable: insert and register with symbol_table
            // Insert first to get stable reference
            auto [new_it, inserted] = variables_.insert({name, value});
            // Register reference with ExprTk (requires persistent storage)
            // Check ExprTk return value
            if (!symbol_table_.add_variable(name, new_it->second)) {
                spdlog::warn("ExpressionEvaluator: Failed to add variable '{}'", name);
            }
        }
    }

    // Register user-defined functions from .FUNC directives
    void set_functions(const std::map<std::string, std::string>& global_funcs) {
        // Clear existing functions
        funcs_1arg_.clear();
        funcs_2arg_.clear();
        funcs_3arg_.clear();

        // Register each function
        for (const auto& [func_name, func_data] : global_funcs) {
            // Parse function data to determine number of arguments
            size_t pipe_pos = func_data.find('|');
            if (pipe_pos == std::string::npos) {
                spdlog::warn("Skipping invalid .FUNC '{}': missing pipe separator", func_name);
                continue;
            }

            std::string args_str = func_data.substr(0, pipe_pos);
            std::vector<std::string> arg_names = split_args(args_str);

            try {
                // Check ExprTk return values for add_function
                if (arg_names.size() == 1) {
                    funcs_1arg_.push_back(std::make_unique<UserDefinedFunction<1>>(func_data));
                    if (!symbol_table_.add_function(func_name, *funcs_1arg_.back())) {
                        spdlog::warn("ExpressionEvaluator: Failed to add function '{}'", func_name);
                    } else {
                        spdlog::debug("ExpressionEvaluator: Registered 1-arg .FUNC: {}", func_name);
                    }
                } else if (arg_names.size() == 2) {
                    funcs_2arg_.push_back(std::make_unique<UserDefinedFunction<2>>(func_data));
                    if (!symbol_table_.add_function(func_name, *funcs_2arg_.back())) {
                        spdlog::warn("ExpressionEvaluator: Failed to add function '{}'", func_name);
                    } else {
                        spdlog::debug("ExpressionEvaluator: Registered 2-arg .FUNC: {}", func_name);
                    }
                } else if (arg_names.size() == 3) {
                    funcs_3arg_.push_back(std::make_unique<UserDefinedFunction<3>>(func_data));
                    if (!symbol_table_.add_function(func_name, *funcs_3arg_.back())) {
                        spdlog::warn("ExpressionEvaluator: Failed to add function '{}'", func_name);
                    } else {
                        spdlog::debug("ExpressionEvaluator: Registered 3-arg .FUNC: {}", func_name);
                    }
                } else {
                    spdlog::warn("Skipping .FUNC '{}': unsupported argument count {} (max 3)",
                                func_name, arg_names.size());
                }
            } catch (const std::exception& e) {
                spdlog::warn("Failed to register .FUNC '{}' in ExpressionEvaluator: {}",
                            func_name, e.what());
            }
        }
    }

    Real evaluate(const std::string& expression) {
        exprtk::expression<Real> expr;
        expr.register_symbol_table(symbol_table_);

        exprtk::parser<Real> parser;
        if (!parser.compile(expression, expr)) {
            throw std::runtime_error("Expression parse error: " + expression);
        }

        return expr.value();
    }

private:
    exprtk::symbol_table<Real> symbol_table_;
    // IMPORTANT: Use std::map instead of std::unordered_map
    // std::map guarantees that references to elements remain valid when new elements are added
    // ExprTk's symbol_table stores references to variables, so reference stability is critical
    std::map<std::string, Real> variables_;  // Persistent storage for variables

    // User-defined functions (must persist while symbol_table is in use)
    std::vector<std::unique_ptr<UserDefinedFunction<1>>> funcs_1arg_;
    std::vector<std::unique_ptr<UserDefinedFunction<2>>> funcs_2arg_;
    std::vector<std::unique_ptr<UserDefinedFunction<3>>> funcs_3arg_;
};

//==============================================================================
// NetlistParser Implementation
//==============================================================================
NetlistParser::NetlistParser() : pimpl_(std::make_unique<Impl>()) {}
NetlistParser::~NetlistParser() = default;

NetlistParseResult
NetlistParser::parse_file(
    const std::string& filepath,
    const std::unordered_map<std::string, Real>& initial_params
) {
    std::ifstream file(filepath);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    const std::string text = read_netlist_file(file);

    // Preprocess .INCLUDE directives
    std::filesystem::path file_path(filepath);
    std::filesystem::path base_path = file_path.parent_path();
    if (base_path.empty()) {
        base_path = std::filesystem::current_path();
    }

    std::set<std::filesystem::path> included_files;
    std::string after_includes = preprocess_includes(text, base_path, included_files);

    // Preprocess .LIB directives
    LibraryChain library_chain;
    std::string after_lib = preprocess_lib_sections(after_includes, base_path, library_chain);

    // Preprocess continuation lines
    std::string preprocessed = preprocess_continuations(after_lib);

    spdlog::info("Parsing netlist: {}", filepath);
    return pimpl_->parse(preprocessed, initial_params);
}

NetlistParseResult
NetlistParser::parse_string(
    const std::string& netlist,
    const std::unordered_map<std::string, Real>& initial_params
) {
    // Preprocess .LIB directives
    std::filesystem::path base_path = std::filesystem::current_path();
    LibraryChain library_chain;
    std::string after_lib =
        preprocess_lib_sections(without_carriage_returns(netlist), base_path, library_chain);

    // Preprocess continuation lines
    std::string preprocessed = preprocess_continuations(after_lib);
    return pimpl_->parse(preprocessed, initial_params);
}

// Legacy methods (for backward compatibility)
std::unique_ptr<circuit::CircuitGraph>
NetlistParser::parse_file_legacy(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    const std::string text = read_netlist_file(file);

    // Preprocess .INCLUDE directives
    std::filesystem::path file_path(filepath);
    std::filesystem::path base_path = file_path.parent_path();
    if (base_path.empty()) {
        base_path = std::filesystem::current_path();
    }

    std::set<std::filesystem::path> included_files;
    std::string after_includes = preprocess_includes(text, base_path, included_files);

    // Preprocess .LIB directives
    LibraryChain library_chain;
    std::string after_lib = preprocess_lib_sections(after_includes, base_path, library_chain);

    // Preprocess continuation lines
    std::string preprocessed = preprocess_continuations(after_lib);

    spdlog::info("Parsing netlist (legacy): {}", filepath);
    return pimpl_->parse_legacy(preprocessed);
}

std::unique_ptr<circuit::CircuitGraph>
NetlistParser::parse_string_legacy(const std::string& netlist) {
    // Preprocess .LIB directives
    std::filesystem::path base_path = std::filesystem::current_path();
    LibraryChain library_chain;
    std::string after_lib =
        preprocess_lib_sections(without_carriage_returns(netlist), base_path, library_chain);

    // Preprocess continuation lines
    std::string preprocessed = preprocess_continuations(after_lib);
    return pimpl_->parse_legacy(preprocessed);
}

//==============================================================================
// ExpressionEvaluator Implementation
//==============================================================================
ExpressionEvaluator::ExpressionEvaluator()
    : pimpl_(std::make_unique<Impl>()) {}
ExpressionEvaluator::~ExpressionEvaluator() = default;

void ExpressionEvaluator::set_variable(const std::string& name, Real value) {
    pimpl_->set_variable(name, value);
}

void ExpressionEvaluator::set_functions(const std::map<std::string, std::string>& global_funcs) {
    pimpl_->set_functions(global_funcs);
}

Real ExpressionEvaluator::evaluate(const std::string& expression) {
    return pimpl_->evaluate(expression);
}

} // namespace kirimaspice::parser
