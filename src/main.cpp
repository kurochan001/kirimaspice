// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/main.cpp
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/output/ssf_writer.hpp"
#include "kirimaspice/output/ac_csv_writer.hpp"
#include "kirimaspice/output/raw_writer.hpp"  // (Nutmeg ASCII)
#include "kirimaspice/output/raw_binary_writer.hpp"  // (Raw Binary)
#include "kirimaspice/output/wav_writer.hpp"  // (WAV)
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/util/units.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <cstdlib>
#include <algorithm>
#include <charconv>

using namespace kirimaspice;

// The files a run writes; the SSF, WAV and AC CSV names carry the
// temperature suffix of a run under a .TEMP list (with_suffix)
struct OutputFiles {
    std::optional<std::string> ssf;
    std::string ac_csv;
    std::optional<std::string> raw;
    std::optional<std::string> raw_binary;
    std::optional<std::string> wav;
    std::uint32_t wav_sample_rate = 44100;
    std::uint16_t wav_bits_per_sample = 16;
};

// The path without the extension of its last component
static std::string path_stem(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path;
    }
    return path.substr(0, dot);
}

// The path with suffix inserted before the extension of its last component
static std::optional<std::string> with_suffix(const std::optional<std::string>& path,
                                              const std::string& suffix) {
    if (!path || suffix.empty()) {
        return path;
    }
    const std::string stem = path_stem(*path);
    return stem + suffix + path->substr(stem.size());
}

// The table of .PRINT TRAN after a transient: a column of time and one for
// each signal, v(<node>) or i(<source>), at every time point, as SPICE
// prints .PRINT; nothing without .PRINT TRAN (the raw, SSF and WAV files
// hold the whole result). A signal that names no node or source is left
// out with a warning.
static void print_transient_table(const std::vector<std::string>& signals,
                                  const std::vector<std::string>& node_names,
                                  const std::vector<std::string>& branch_names,
                                  const solvers::TransientResult& result) {
    if (signals.empty()) {
        return;
    }
    struct Column {
        std::string title;
        bool voltage;
        std::size_t index;  // node id, or branch index
    };
    std::vector<Column> columns;
    for (const auto& signal : signals) {
        std::string kind = signal.substr(0, signal.find('('));
        util::to_upper_inplace(kind);
        const auto open = signal.find('(');
        const auto close = signal.rfind(')');
        std::optional<Column> column;
        if (open != std::string::npos && close != std::string::npos && close > open + 1) {
            const std::string name = signal.substr(open + 1, close - open - 1);
            if (kind == "V") {
                for (std::size_t id = 0; id < node_names.size(); ++id) {
                    if (util::equals_icase(node_names[id], name)) {
                        column = Column{signal, true, id};
                    }
                }
            } else if (kind == "I") {
                for (std::size_t k = 0; k < branch_names.size(); ++k) {
                    if (util::equals_icase(branch_names[k], name)) {
                        column = Column{signal, false, k};
                    }
                }
            }
        }
        if (!column) {
            spdlog::warn(".PRINT TRAN {}: no such node or voltage source; left out", signal);
            continue;
        }
        columns.push_back(*column);
    }
    if (columns.empty()) {
        return;
    }
    std::cout << "\n=== Transient Analysis Results ===\n";
    std::cout << "Time (s)";
    for (const auto& c : columns) {
        std::cout << "\t" << c.title;
    }
    std::cout << "\n";
    for (const auto& tp : result.timepoints) {
        std::cout << tp.time;
        for (const auto& c : columns) {
            Real value = 0.0;  // ground
            if (c.voltage && c.index > 0 && c.index - 1 < tp.node_voltages.size()) {
                value = tp.node_voltages[c.index - 1];
            } else if (!c.voltage && c.index < tp.branch_currents.size()) {
                value = tp.branch_currents[c.index];
            }
            std::cout << "\t" << value;
        }
        std::cout << "\n";
    }
}

// Run the analyses of the netlist once, at the temperature of config: the DC
// sweep, the operating point, the transient (with .FOUR) and AC, in this
// order, or the operating point when the netlist asks for none. Which ones
// is what the parser recorded (SimulationConfig's op_requested,
// dc_sweep_enabled, tran_requested and ac_requested), so an analysis line
// counts as the parser reads it: joined to its continuation lines, from
// .INCLUDE and .LIB text, in the branch of .IF taken, never in a comment.
// Returns the exit code. raw_append and raw_binary_append carry over from
// run to run, so the raw files hold the plots of every run.
static int run_analyses(circuit::CircuitGraph& circuit_graph, const SimulationConfig& config,
                        const OutputFiles& files, bool& raw_append_mode,
                        bool& raw_binary_append_mode) {
    auto* circuit = &circuit_graph;
    const auto& output_file = files.ssf;
    const auto& raw_output_file = files.raw;
    const auto& raw_binary_output_file = files.raw_binary;
    const auto& wav_output_file = files.wav;
    const auto wav_sample_rate = files.wav_sample_rate;
    const auto wav_bits_per_sample = files.wav_bits_per_sample;

    auto node_names = circuit->get_node_names();
    auto voltage_sources = circuit->get_voltage_sources();
    std::vector<std::string> branch_names;
    branch_names.reserve(voltage_sources.size());
    for (const auto& vsrc : voltage_sources) {
        branch_names.push_back(vsrc->name());
    }

    const bool run_dc_sweep = config.dc_sweep_enabled;
    const bool asks_for_none = !config.op_requested && !config.dc_sweep_enabled &&
                               !config.tran_requested && !config.ac_requested;
    // A netlist without an analysis line runs the operating point as if it had
    // .OP; with .DC as well, .OP gives the sweep alone
    const bool run_dc_point = (config.op_requested || asks_for_none) && !run_dc_sweep;
    if (asks_for_none) {
        spdlog::info("No analysis specified, running the operating point as for .OP");
    }

    // Run DC sweep analysis if requested via .DC
    if (run_dc_sweep) {
        spdlog::info("Running DC sweep analysis...");
        solvers::DCSolver dc_solver(*circuit, config);
        DCSweepResult sweep_result;
        auto sweep_status = dc_solver.solve_sweep(config.dc_sweep, sweep_result);

        if (sweep_status != ConvergenceStatus::SUCCESS) {
            spdlog::error("DC sweep failed; no sweep output written");
            return 1;
        }

        std::cout << "\n=== DC Sweep Results ===\n";
        std::cout << "Source " << config.dc_sweep.source_name
                  << " swept from " << config.dc_sweep.start_value
                  << " to " << config.dc_sweep.stop_value
                  << " (step=" << config.dc_sweep.step_value << ")\n";
        if (config.dc_sweep.nested_sweep) {
            const auto& nested = *config.dc_sweep.nested_sweep;
            std::cout << "Nested source " << nested.source_name
                      << " swept from " << nested.start_value
                      << " to " << nested.stop_value
                      << " (step=" << nested.step_value << ")\n";
        }
        std::cout << "Points solved: " << sweep_result.points.size()
                  << " (status=" << (sweep_result.all_converged() ? "SUCCESS" : "PARTIAL") << ")\n";
        if (!sweep_result.points.empty()) {
            const auto& first = sweep_result.points.front();
            const auto& last = sweep_result.points.back();
            std::cout << "First sweep value: " << first.sweep_value
                      << ", last sweep value: " << last.sweep_value << "\n";
        }
        std::cout << "Detailed waveforms are available via --raw-out / SSF outputs.\n";

        if (raw_output_file) {
            output::RawWriter raw_writer(*raw_output_file, raw_append_mode);
            std::optional<std::string> nested_name;
            if (config.dc_sweep.nested_sweep) {
                nested_name = config.dc_sweep.nested_sweep->source_name;
            }
            raw_writer.write_dc_sweep(config.dc_sweep.source_name,
                                      nested_name,
                                      node_names,
                                      branch_names,
                                      sweep_result);
            raw_append_mode = true;
        }

        if (raw_binary_output_file) {
            output::RawBinaryWriter raw_binary_writer(*raw_binary_output_file, raw_binary_append_mode);
            std::optional<std::string> nested_name;
            if (config.dc_sweep.nested_sweep) {
                nested_name = config.dc_sweep.nested_sweep->source_name;
            }
            raw_binary_writer.write_dc_sweep(config.dc_sweep.source_name,
                                              nested_name,
                                              node_names,
                                              branch_names,
                                              sweep_result);
            raw_binary_append_mode = true;
        }
    }

    // Run single-point DC analysis (legacy .DC/.OP)
    if (run_dc_point) {
        spdlog::info("Running DC analysis...");
        solvers::DCSolver dc_solver(*circuit, config);

        {
            DenseVector solution;
            SolverStats stats;

            auto status = dc_solver.solve(solution, stats);

            if (status == ConvergenceStatus::SUCCESS) {
                spdlog::info("DC analysis converged in {} iterations", stats.total_iterations);

                // Print node voltages
                std::cout << "\n=== DC Operating Point ===\n";

                // Print ground node explicitly (always 0V)
                for (const auto& name : node_names) {
                    if (name == NODE_GND_NAME) {
                        std::cout << "V(0) = 0 V\n";
                        break;
                    }
                }

                // Print non-ground nodes from solution vector
                size_t solution_idx = 0;
                for (const auto& name : node_names) {
                    if (name == NODE_GND_NAME) {
                        continue; // Skip ground node
                    }
                    if (solution_idx < solution.size()) {
                        std::cout << "V(" << name << ") = "
                                  << solution[solution_idx] << " V\n";
                        solution_idx++;
                    }
                }

                // Print branch currents if available
                size_t remaining = solution.size() - solution_idx;
                if (remaining > 0 && !branch_names.empty()) {
                    for (size_t i = 0; i < remaining && i < branch_names.size(); ++i) {
                        std::cout << "I(" << branch_names[i] << ") = "
                                  << solution[solution_idx + i] << " A\n";
                    }
                }

                // Reconstruct node_voltages and branch_currents for file writers
                size_t num_non_ground_nodes = 0;
                for (const auto& name : node_names) {
                    if (name != NODE_GND_NAME) {
                        num_non_ground_nodes++;
                    }
                }

                size_t available_nodes = std::min(num_non_ground_nodes, solution.size());
                DenseVector node_voltages(solution.begin(), solution.begin() + available_nodes);

                remaining = solution.size() - available_nodes;
                size_t branch_count = std::min(branch_names.size(), remaining);
                DenseVector branch_currents;
                if (branch_count > 0) {
                    branch_currents.assign(solution.begin() + available_nodes,
                                           solution.begin() + available_nodes + branch_count);
                }

                // Write SSF/RAW outputs
                if (output_file) {
                    spdlog::info("Writing DC results to SSF: {}", *output_file);
                    output::SSFWriter writer(*output_file);
                    writer.write_dc_analysis(node_voltages, node_names, branch_currents, branch_names);
                    spdlog::info("SSF file written successfully");
                }

                if (raw_output_file) {
                    output::RawWriter raw_writer(*raw_output_file, raw_append_mode);
                    raw_writer.write_dc(node_names, node_voltages, branch_names, branch_currents);
                    raw_append_mode = true;
                }

                if (raw_binary_output_file) {
                    output::RawBinaryWriter raw_binary_writer(*raw_binary_output_file, raw_binary_append_mode);
                    raw_binary_writer.write_dc(node_names, node_voltages, branch_names, branch_currents);
                    raw_binary_append_mode = true;
                }
            } else {
                spdlog::error("DC analysis failed to converge");
                return 1;
            }
        }
    }

        // Run transient analysis if requested
        if (config.tran_requested) {
            // The times of the .TRAN line (parse_tran_directive): the step is
            // TSTEP, or TMAX when that is shorter (the parser sets TMAX to
            // TSTEP when the line has none), so no step is longer than TMAX
            const Real t_start = config.transient.start_time;
            const Real t_stop = config.transient.end_time;
            const Real t_step = std::min(config.transient.initial_step, config.transient.max_step);

            spdlog::info("Running transient analysis: {} to {} s (step: {} s)",
                         t_start, t_stop, t_step);

            solvers::TransientSolver tran_solver(*circuit, config);

            {
                auto result = tran_solver.solve(t_start, t_stop, t_step);

                if (result.status == ConvergenceStatus::SUCCESS) {
                    spdlog::info("Transient analysis completed: {} timepoints",
                                 result.timepoints.size());
                    spdlog::info("Total iterations: {}", result.stats.total_iterations);

                    auto node_names = circuit->get_node_names();
                    auto voltage_sources = circuit->get_voltage_sources();
                    std::vector<std::string> branch_names;
                    branch_names.reserve(voltage_sources.size());
                    for (const auto& vsrc : voltage_sources) {
                        branch_names.push_back(vsrc->name());
                    }
                    print_transient_table(config.output.print_tran_signals, node_names, branch_names,
                                          result);

                    // Write SSF output if requested
                    if (output_file) {
                        spdlog::info("Writing transient results to SSF: {}", *output_file);
                        output::SSFWriter writer(*output_file);
                        writer.write_transient_analysis(result, node_names, branch_names);
                        spdlog::info("SSF file written successfully");
                    }

                    if (raw_output_file) {
                        output::RawWriter raw_writer(*raw_output_file, raw_append_mode);
                        raw_writer.write_transient(node_names, branch_names, result);
                        raw_append_mode = true;
                    }

                    if (raw_binary_output_file) {
                        output::RawBinaryWriter raw_binary_writer(*raw_binary_output_file, raw_binary_append_mode);
                        raw_binary_writer.write_transient(node_names, branch_names, result);
                        raw_binary_append_mode = true;
                    }

                    if (wav_output_file) {
                        output::WavWriter wav_writer(*wav_output_file, wav_sample_rate, wav_bits_per_sample);
                        wav_writer.write_transient(node_names, result);
                    }
                } else {
                    spdlog::error("Transient analysis failed");
                    return 1;
                }
            }
        }

        // Run AC analysis if specified
        if (config.ac_requested) {
            spdlog::info("Running AC analysis...");
            solvers::ACSolver ac_solver(*circuit, config);
            auto ac_result = ac_solver.solve();

            if (ac_result.status == ConvergenceStatus::SUCCESS) {
                spdlog::info("AC analysis completed: {} frequency points",
                            ac_result.frequency_points.size());

                const std::string& ac_output_file = files.ac_csv;

                // Write AC results to CSV
                spdlog::info("Writing AC results to CSV: {}", ac_output_file);
                output::ACCSVWriter ac_writer(ac_output_file, *circuit);
                ac_writer.write(ac_result, circuit->title());
                spdlog::info("AC CSV file written successfully");

            } else {
                spdlog::error("AC analysis failed");
                return 1;
            }
        }

    return 0;
}

// The program's own messages go to standard error as "kirimaspice: <level>:
// <text>", and only results to standard output (kirimasim docs/spec/CLI.md,
// sections 3 and 4); standard output is flushed first, so that the two
// keep their order where both go to one terminal
class StderrAfterStdout final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        std::cout.flush();
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        std::cerr.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
    }
    void flush_() override { std::cerr.flush(); }
};

static bool is_help_argument(const std::string& arg) {
    return arg == "-h" || arg == "--help";
}

static std::uint32_t read_positive_integer_option(const std::string& value,
                                                 const std::string& option) {
    std::uint32_t parsed = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size() || parsed == 0) {
        throw std::invalid_argument(option + " requires a positive 32-bit integer");
    }
    return parsed;
}

static void print_usage(std::ostream& os, const char* program) {
    os << "\nUsage: " << program << " [options] <netlist.cir>\n\n";
    os << "Options:\n";
    os << "  (an option with a value also takes it as <option>=<value>)\n";
    os << "  -h, --help                  Show this help message and exit\n";
    os << "  --version                   Show the version and exit\n";
    os << "  --log-level <level>         Messages to show on standard error: error, warning,\n";
    os << "                              info (default) or debug\n";
    os << "  -o <file>                   Write SSF/CSV outputs to <file> base name\n";
    os << "  --raw-out <file>            Write Nutmeg ASCII .raw output (DC/TRAN)\n";
    os << "  --raw-binary-out <file>     Write SPICE3 binary .raw output (DC/TRAN)\n";
    os << "  --wav-out <file>            Write WAV audio output (TRAN only, default: 44.1kHz 16-bit)\n";
    os << "  --wav-sample-rate <Hz>      Set WAV sample rate in Hz (default: 44100)\n";
    os << "  --wav-bits <N>              Set WAV bit depth: 8, 16, 24, or 32 (default: 16)\n\n";
}

int main(int argc, char* argv[]) {
    {
        auto logger = std::make_shared<spdlog::logger>("kirimaspice",
                                                       std::make_shared<StderrAfterStdout>());
        logger->set_pattern("kirimaspice: %l: %v");
        spdlog::set_default_logger(logger);
    }
    spdlog::set_level(spdlog::level::info);

    for (int i = 1; i < argc; ++i) {
        if (is_help_argument(argv[i])) {
            print_usage(std::cout, argv[0]);
            return 0;
        }
    }

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--version") {
            std::cout << "kirimaspice " << KIRIMASPICE_VERSION << "\n";
            return 0;
        }
    }

    std::string netlist_file;
    std::optional<std::string> output_file;
    std::optional<std::string> raw_output_file;
    std::optional<std::string> raw_binary_output_file;
    std::optional<std::string> wav_output_file;
    std::uint32_t wav_sample_rate = 44100;
    std::uint16_t wav_bits_per_sample = 16;
    bool raw_append_mode = false;
    bool raw_binary_append_mode = false;

    // Parse command-line arguments: options anywhere, each taking its value
    // as "<option> <value>" or "<option>=<value>", and one netlist. An
    // option the program does not know, or one without its value, is an
    // error, not ignored
    static const std::set<std::string> kValueOptions = {
        "-o", "--raw-out", "--raw-binary-out", "--wav-out", "--wav-sample-rate", "--wav-bits",
        "--log-level"};
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg.empty() || arg[0] != '-' || arg == "-") {
                if (!netlist_file.empty()) {
                    throw std::invalid_argument("more than one netlist: " + netlist_file + " and " + arg);
                }
                netlist_file = arg;
                continue;
            }
            std::string name = arg;
            std::optional<std::string> value;
            if (const auto eq = arg.find('='); arg.rfind("--", 0) == 0 && eq != std::string::npos) {
                name = arg.substr(0, eq);
                value = arg.substr(eq + 1);
            }
            if (!kValueOptions.count(name)) {
                throw std::invalid_argument("unknown option " + arg + " (see --help)");
            }
            if (!value) {
                if (i + 1 >= argc) {
                    throw std::invalid_argument(name + " requires a value");
                }
                value = argv[++i];
            }
            if (value->empty()) {
                throw std::invalid_argument(name + " requires a value");
            }
            if (name == "--log-level") {
                static const std::map<std::string, spdlog::level::level_enum> kLevels = {
                    {"error", spdlog::level::err}, {"warning", spdlog::level::warn},
                    {"info", spdlog::level::info}, {"debug", spdlog::level::debug}};
                const auto level = kLevels.find(*value);
                if (level == kLevels.end()) {
                    throw std::invalid_argument("--log-level must be error, warning, info or debug");
                }
                spdlog::set_level(level->second);
            } else if (name == "-o") {
                output_file = *value;
            } else if (name == "--raw-out") {
                raw_output_file = *value;
            } else if (name == "--raw-binary-out") {
                raw_binary_output_file = *value;
            } else if (name == "--wav-out") {
                wav_output_file = *value;
            } else if (name == "--wav-sample-rate") {
                wav_sample_rate = read_positive_integer_option(*value, name);
            } else if (name == "--wav-bits") {
                const auto bits = read_positive_integer_option(*value, name);
                if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
                    throw std::invalid_argument("--wav-bits must be 8, 16, 24, or 32");
                }
                wav_bits_per_sample = static_cast<std::uint16_t>(bits);
            }
        }
        if (netlist_file.empty()) {
            throw std::invalid_argument("no netlist given");
        }
    } catch (const std::exception& e) {
        spdlog::error("Invalid command-line option: {}", e.what());
        spdlog::info("Usage: {} [options] <netlist.cir>; see --help", argv[0]);
        return 1;
    }

    spdlog::info("kirimaspice v{} - SPICE circuit simulator", KIRIMASPICE_VERSION);

    spdlog::info("Loading netlist: {}", netlist_file);
    if (output_file) {
        spdlog::info("SSF output file: {}", *output_file);
    }
    if (raw_output_file) {
        spdlog::info("RAW ASCII output file: {}", *raw_output_file);
    }
    if (raw_binary_output_file) {
        spdlog::info("RAW Binary output file: {}", *raw_binary_output_file);
    }
    if (wav_output_file) {
        spdlog::info("WAV output file: {} ({} Hz, {} bits)", *wav_output_file,
                     wav_sample_rate, wav_bits_per_sample);
    }

    try {
        // Parse netlist (returns circuit + simulation config)
        parser::NetlistParser parser;
        auto parse_result = parser.parse_file(netlist_file);

        auto& circuit = parse_result.circuit;
        auto& config = parse_result.config;

        // If .TEMP is specified, use the first temperature as the base temperature
        bool has_temp_sweep = config.temp_sweep.enabled && !config.temp_sweep.temperatures.empty();
        if (has_temp_sweep) {
            config.temperature = config.temp_sweep.temperatures.front();
        }

        // Set simulation config for model parameter access during subcircuit expansion
        circuit->set_simulation_config(&config);

        // Expand subcircuits if any
        circuit->expand_all_subcircuits();

        spdlog::info("Circuit: {}", circuit->title());
        spdlog::info("Nodes: {}", circuit->num_nodes());
        spdlog::info("Devices: {}", circuit->num_devices());

        // The directives the parser reads for the library that this program
        // does not run: each is named once, so that no result is taken for
        // one of them
        for (const char* directive : {".SENS", ".SENSITIVITY", ".TF", ".PZ", ".DISTO", ".STEP",
                                      ".MC", ".MONTECARLO", ".WCASE", ".OPTIMIZE", ".PORT",
                                      ".PRINT DC", ".PRINT AC", ".SAVE"}) {
            if (config.directives_read.count(directive)) {
                spdlog::warn("{} is read, but this program does not run it", directive);
            }
        }

        // .TEMP with one value is .OPTIONS TEMP (as in ngspice-47, which takes
        // one value only); with several, every analysis runs at each of them,
        // and the SSF, WAV and AC CSV files of a run carry its temperature in
        // their names (out_27C.ssf, out_ac_27C.csv)
        std::vector<std::optional<Real>> run_temperatures;
        if (has_temp_sweep) {
            run_temperatures.assign(config.temp_sweep.temperatures.begin(),
                                    config.temp_sweep.temperatures.end());
        } else {
            run_temperatures.push_back(std::nullopt);
        }
        const bool several_temperatures = run_temperatures.size() > 1;
        for (const auto& temperature : run_temperatures) {
            SimulationConfig run_config = config;
            std::string suffix;
            if (temperature) {
                run_config.temperature = *temperature;
                run_config.temperature_explicitly_set = true;
                run_config.temp_sweep.enabled = false;
            }
            if (several_temperatures) {
                const std::string celsius = fmt::format("{:g}", *temperature - 273.15);
                suffix = "_" + celsius + "C";
                spdlog::info("Running the analyses at {} °C", celsius);
                std::cout << "\n=== Temperature " << celsius << " °C ===\n";
            }
            // The AC CSV is <base>_ac.csv, <base> being the -o path or the
            // netlist path without the extension of its last component
            const std::string ac_csv =
                *with_suffix(path_stem(output_file ? *output_file : netlist_file) + "_ac.csv",
                             suffix);
            const OutputFiles files{with_suffix(output_file, suffix), ac_csv, raw_output_file,
                                    raw_binary_output_file, with_suffix(wav_output_file, suffix),
                                    wav_sample_rate, wav_bits_per_sample};
            const int code = run_analyses(*circuit, run_config, files, raw_append_mode,
                                          raw_binary_append_mode);
            if (code != 0) {
                return code;
            }
        }
        return 0;

    } catch (const parser::ParseError& e) {
        spdlog::error("Parse error at line {}: {}", e.line(), e.what());
        return 1;
    } catch (const std::exception& e) {
        spdlog::error("Error: {}", e.what());
        return 1;
    }
}
