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
#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <cstdlib>
#include <algorithm>
#include <charconv>
#include "kirimaspice/solvers/solver_factory.hpp"

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
            // The times of the .TRAN line (parse_tran_directive)
            const Real t_start = config.transient.start_time;
            const Real t_stop = config.transient.end_time;
            const Real t_step = config.transient.initial_step;

            spdlog::info("Running transient analysis: {} to {} s (step: {} s)",
                         t_start, t_stop, t_step);

            solvers::TransientSolver tran_solver(*circuit, config);

            {
                auto result = tran_solver.solve(t_start, t_stop, t_step);

                if (result.status == ConvergenceStatus::SUCCESS) {
                    spdlog::info("Transient analysis completed: {} timepoints",
                                 result.timepoints.size());
                    spdlog::info("Total iterations: {}", result.stats.total_iterations);

                    // Print results
                    std::cout << "\n=== Transient Analysis Results ===\n";
                    std::cout << "Time (s)\t";
                    auto node_names = circuit->get_node_names();
                    auto voltage_sources = circuit->get_voltage_sources();
                    std::vector<std::string> branch_names;
                    branch_names.reserve(voltage_sources.size());
                    for (const auto& vsrc : voltage_sources) {
                        branch_names.push_back(vsrc->name());
                    }
                    for (const auto& name : node_names) {
                        std::cout << "V(" << name << ")\t";
                    }
                    std::cout << "\n";

                    // Print all timepoints
                    for (const auto& tp : result.timepoints) {
                        std::cout << tp.time << "\t";
                        // Print node voltages, inserting 0 for ground node
                        for (size_t i = 0; i < node_names.size(); ++i) {
                            if (node_names[i] == NODE_GND_NAME) {
                                std::cout << "0\t";  // Ground is always 0
                            } else {
                                // Find index in node_voltages (excluding ground)
                                // node_voltages[0] = first non-ground node, etc.
                                size_t voltage_idx = i - 1;  // Offset by 1 if ground is first
                                if (voltage_idx < tp.node_voltages.size()) {
                                    std::cout << tp.node_voltages[voltage_idx] << "\t";
                                }
                            }
                        }
                        std::cout << "\n";
                    }

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
    os << "\nUsage: " << program << " <netlist.cir> [-o <output.ssf>] [options]\n\n";
    os << "Options:\n";
    os << "  -h, --help                  Show this help message and exit\n";
    os << "  -o <file>                   Write SSF/CSV outputs to <file> base name\n";
    os << "  --raw-out <file>            Write Nutmeg ASCII .raw output (DC/TRAN)\n";
    os << "  --raw-binary-out <file>     Write SPICE3 binary .raw output (DC/TRAN)\n";
    os << "  --wav-out <file>            Write WAV audio output (TRAN only, default: 44.1kHz 16-bit)\n";
    os << "  --wav-sample-rate <Hz>      Set WAV sample rate in Hz (default: 44100)\n";
    os << "  --wav-bits <N>              Set WAV bit depth: 8, 16, 24, or 32 (default: 16)\n";
    os << "  --solver-backend=<name>     Choose backend: KLU (default), SUPERLU_DIST\n";
    os << "  --solver-fallback-order=A,B Fallback order when requested backend unavailable\n";
    os << "  --strict-backend            Do not fallback; error out if unavailable\n";
    os << "  --superlu-dist-topology=<t>  SUPERLU_DIST topology hint (e.g., 2D/3D)\n";
    os << "  --superlu-dist-grid=<g>      SUPERLU_DIST grid hint (e.g., 2x2 or 2x2x1)\n\n";
    os << "Environment overrides:\n";
    os << "  KIRIMASPICE_SOLVER_BACKEND=KLU|SUPERLU_DIST\n";
    os << "  KIRIMASPICE_SOLVER_FALLBACK_ORDER=klu\n";
    os << "  KIRIMASPICE_SOLVER_STRICT=1\n\n";
}

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);

    for (int i = 1; i < argc; ++i) {
        if (is_help_argument(argv[i])) {
            print_usage(std::cout, argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        spdlog::error("Usage: {} <netlist.cir> [-o <output.ssf>] [options]", argv[0]);
        spdlog::info("Use --help for details.");
        return 1;
    }

    spdlog::info("kirimaspice v{} - SPICE circuit simulator", KIRIMASPICE_VERSION);

    std::string netlist_file = argv[1];
    std::optional<std::string> output_file;
    std::optional<std::string> raw_output_file;
    std::optional<std::string> raw_binary_output_file;
    std::optional<std::string> wav_output_file;
    std::uint32_t wav_sample_rate = 44100;
    std::uint16_t wav_bits_per_sample = 16;
    bool raw_append_mode = false;
    bool raw_binary_append_mode = false;

    // Parse command-line arguments
    std::optional<std::string> cli_backend;
    std::vector<std::string> cli_fb_order;
    bool strict_backend = false;
    std::optional<std::string> sld_topology; // SUPERLU_DIST topology hint
    std::optional<std::string> sld_grid;     // SUPERLU_DIST grid hint
    try {
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-o" && i + 1 < argc) {
                output_file = argv[i + 1];
                ++i;  // Skip next argument
            } else if (arg == "--raw-out" && i + 1 < argc) {
                raw_output_file = argv[i + 1];
                ++i;
            } else if (arg.rfind("--raw-out=", 0) == 0) {
                raw_output_file = arg.substr(std::string("--raw-out=").size());
            } else if (arg == "--raw-binary-out" && i + 1 < argc) {
                raw_binary_output_file = argv[i + 1];
                ++i;
            } else if (arg.rfind("--raw-binary-out=", 0) == 0) {
                raw_binary_output_file = arg.substr(std::string("--raw-binary-out=").size());
            } else if (arg == "--wav-out" && i + 1 < argc) {
                wav_output_file = argv[i + 1];
                ++i;
            } else if (arg.rfind("--wav-out=", 0) == 0) {
                wav_output_file = arg.substr(std::string("--wav-out=").size());
            } else if (arg == "--wav-sample-rate" && i + 1 < argc) {
                wav_sample_rate = read_positive_integer_option(argv[i + 1], arg);
                ++i;
            } else if (arg.rfind("--wav-sample-rate=", 0) == 0) {
                wav_sample_rate = read_positive_integer_option(arg.substr(std::string("--wav-sample-rate=").size()), "--wav-sample-rate");
            } else if (arg == "--wav-bits" && i + 1 < argc) {
                const auto bits = read_positive_integer_option(argv[i + 1], arg);
                if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
                    throw std::invalid_argument("--wav-bits must be 8, 16, 24, or 32");
                }
                wav_bits_per_sample = static_cast<std::uint16_t>(bits);
                ++i;
            } else if (arg.rfind("--wav-bits=", 0) == 0) {
                const auto bits = read_positive_integer_option(arg.substr(std::string("--wav-bits=").size()), "--wav-bits");
                if (bits != 8 && bits != 16 && bits != 24 && bits != 32) {
                    throw std::invalid_argument("--wav-bits must be 8, 16, 24, or 32");
                }
                wav_bits_per_sample = static_cast<std::uint16_t>(bits);
            } else if (arg == "--wav-sample-rate" || arg == "--wav-bits") {
                throw std::invalid_argument(arg + " requires a value");
            } else if (arg.rfind("--solver-backend=", 0) == 0) {
                cli_backend = arg.substr(std::string("--solver-backend=").size());
            } else if (arg.rfind("--solver-fallback-order=", 0) == 0) {
                std::string list = arg.substr(std::string("--solver-fallback-order=").size());
                std::stringstream ss(list);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    if (!item.empty()) cli_fb_order.push_back(item);
                }
            } else if (arg == "--strict-backend") {
                strict_backend = true;
            } else if (arg.rfind("--superlu-dist-topology=", 0) == 0) {
                sld_topology = arg.substr(std::string("--superlu-dist-topology=").size());
            } else if (arg.rfind("--superlu-dist-grid=", 0) == 0) {
                sld_grid = arg.substr(std::string("--superlu-dist-grid=").size());
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Invalid command-line option: {}", e.what());
        return 1;
    }

    // Apply CLI overrides to solver factory
    solvers::set_cli_backend_override(cli_backend);
    if (!cli_fb_order.empty()) solvers::set_cli_fallback_order(cli_fb_order);
    solvers::set_cli_strict_backend(strict_backend);
    if (sld_topology) solvers::set_cli_superludist_topology(*sld_topology);
    if (sld_grid)     solvers::set_cli_superludist_grid(*sld_grid);

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
                                      ".PRINT", ".SAVE"}) {
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
