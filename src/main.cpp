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
#include <regex>
#include <optional>
#include <cstdlib>
#include <algorithm>
#include "kirimaspice/solvers/solver_factory.hpp"

using namespace kirimaspice;

// Parse analysis commands from netlist file
struct AnalysisCommand {
    bool has_dc = false;
    bool has_tran = false;
    Real tran_tstep = 1e-6;
    Real tran_tstop = 1e-3;
};

AnalysisCommand parse_analysis_commands(const std::string& filepath) {
    AnalysisCommand cmd;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return cmd;
    }

    std::string line;
    std::regex tran_regex(R"(\.tran\s+(\S+)\s+(\S+))", std::regex::icase);
    std::regex dc_regex(R"(\.dc)", std::regex::icase);
    std::regex op_regex(R"(\.op)", std::regex::icase);
    std::smatch match;

    while (std::getline(file, line)) {
        // Check for .tran command
        if (std::regex_search(line, match, tran_regex)) {
            cmd.has_tran = true;
            cmd.tran_tstep = kirimaspice::util::parse_value_with_unit(match[1].str());
            cmd.tran_tstop = kirimaspice::util::parse_value_with_unit(match[2].str());
        }
        // Check for .dc command
        else if (std::regex_search(line, dc_regex)) {
            cmd.has_dc = true;
        }
        // Check for .op command (DC operating point analysis)
        else if (std::regex_search(line, op_regex)) {
            cmd.has_dc = true;
        }
    }
    return cmd;
}

static bool is_help_argument(const std::string& arg) {
    return arg == "-h" || arg == "--help";
}

static void print_usage(std::ostream& os, const char* program) {
    os << "\nUsage: " << program << " <netlist.cir> [-o <output.ssf>] [options]\n\n";
    os << "Options:\n";
    os << "  -h, --help                  Show this help message and exit\n";
    os << "  -o <file>                   Write SSF/CSV outputs to <file> base name\n";
    os << "  --raw-out <file>            Write Nutmeg ASCII .raw output (DC/TRAN)\n";
    os << "  --raw-binary-out <file>     Write Raw Binary .raw output (DC/TRAN, LTspice compatible)\n";
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
            wav_sample_rate = std::stoul(argv[i + 1]);
            ++i;
        } else if (arg.rfind("--wav-sample-rate=", 0) == 0) {
            wav_sample_rate = std::stoul(arg.substr(std::string("--wav-sample-rate=").size()));
        } else if (arg == "--wav-bits" && i + 1 < argc) {
            wav_bits_per_sample = std::stoul(argv[i + 1]);
            ++i;
        } else if (arg.rfind("--wav-bits=", 0) == 0) {
            wav_bits_per_sample = std::stoul(arg.substr(std::string("--wav-bits=").size()));
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

        // Parse analysis commands
        auto analysis = parse_analysis_commands(netlist_file);

    auto node_names = circuit->get_node_names();
    auto voltage_sources = circuit->get_voltage_sources();
    std::vector<std::string> branch_names;
    branch_names.reserve(voltage_sources.size());
    for (const auto& vsrc : voltage_sources) {
        branch_names.push_back(vsrc->name());
    }

    bool run_dc_sweep = config.dc_sweep_enabled;
    bool run_dc_point = analysis.has_dc && !run_dc_sweep;

    // Run DC sweep analysis if requested via .DC
    if (run_dc_sweep) {
        if (has_temp_sweep) {
            spdlog::warn(".TEMP + .DC sweep is not fully supported; running sweep at first temperature {:.2f}K",
                         config.temperature);
        }
        spdlog::info("Running DC sweep analysis...");
        solvers::DCSolver dc_solver(*circuit, config);
        DCSweepResult sweep_result;
        auto sweep_status = dc_solver.solve_sweep(config.dc_sweep, sweep_result);

        if (sweep_result.points.empty() && sweep_status != ConvergenceStatus::SUCCESS) {
            spdlog::error("DC sweep failed to produce any valid points");
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

        if (has_temp_sweep) {
            TemperatureSweepResult temp_result;
            auto status = dc_solver.solve_temp_sweep(temp_result);
            if (status != ConvergenceStatus::SUCCESS && !temp_result.all_converged()) {
                spdlog::error("DC temperature sweep failed to converge");
                return 1;
            }

            for (const auto& point : temp_result.points) {
                std::cout << "\n=== DC Operating Point @ " << (point.temperature - 273.15) << " °C ===\n";

                // Print ground node explicitly (always 0V)
                for (const auto& name : node_names) {
                    if (name == "0" || name == "GND" || name.empty()) {
                        std::cout << "V(0) = 0 V\n";
                        break;
                    }
                }

                size_t solution_idx = 0;
                for (const auto& name : node_names) {
                    if (name == "0" || name == "GND" || name.empty()) continue;
                    if (solution_idx < point.node_voltages.size()) {
                        std::cout << "V(" << name << ") = "
                                  << point.node_voltages[solution_idx] << " V\n";
                        solution_idx++;
                    }
                }

                if (!branch_names.empty() && !point.branch_currents.empty()) {
                    size_t count = std::min(branch_names.size(), point.branch_currents.size());
                    for (size_t i = 0; i < count; ++i) {
                        std::cout << "I(" << branch_names[i] << ") = "
                                  << point.branch_currents[i] << " A\n";
                    }
                }
            }
        } else {
            DenseVector solution;
            SolverStats stats;

            auto status = dc_solver.solve(solution, stats);

            if (status == ConvergenceStatus::SUCCESS) {
                spdlog::info("DC analysis converged in {} iterations", stats.total_iterations);

                // Print node voltages
                std::cout << "\n=== DC Operating Point ===\n";

                // Print ground node explicitly (always 0V)
                for (const auto& name : node_names) {
                    if (name == "0" || name == "GND" || name.empty()) {
                        std::cout << "V(0) = 0 V\n";
                        break;
                    }
                }

                // Print non-ground nodes from solution vector
                size_t solution_idx = 0;
                for (const auto& name : node_names) {
                    if (name == "0" || name == "GND" || name.empty()) {
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
                    if (name != "0" && name != "GND" && !name.empty()) {
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
        if (analysis.has_tran) {
            // Prefer parser-derived config (.tran/.options). Regex fallback values are only for legacy decks.
            Real t_start = config.transient.start_time;
            Real t_stop  = config.transient.end_time > 0 ? config.transient.end_time : analysis.tran_tstop;
            Real t_step  = config.transient.initial_step > 0 ? config.transient.initial_step : analysis.tran_tstep;

            spdlog::info("Running transient analysis: {} to {} s (step: {} s)",
                         t_start, t_stop, t_step);

            solvers::TransientSolver tran_solver(*circuit, config);

            if (has_temp_sweep) {
                solvers::TransientTemperatureSweepResult sweep_result;
                auto status = tran_solver.solve_temp_sweep(t_start, t_stop, t_step, sweep_result);
                if (status != ConvergenceStatus::SUCCESS && !sweep_result.all_converged()) {
                    spdlog::error("Transient temperature sweep failed");
                    return 1;
                }

                spdlog::info("Transient temperature sweep completed ({} points)", sweep_result.points.size());
                for (const auto& point : sweep_result.points) {
                    spdlog::info("  T = {:.2f}°C : {} timepoints, total iterations {} (status={})",
                                 point.temperature - 273.15,
                                 point.result.timepoints.size(),
                                 point.result.stats.total_iterations,
                                 point.result.status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAIL");
                }
            } else {
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
                            if (node_names[i] == "0" || node_names[i] == "GND") {
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
        if (config.ac.start_freq > 0.0 && config.ac.stop_freq > 0.0) {
            spdlog::info("Running AC analysis...");
            solvers::ACSolver ac_solver(*circuit, config);
            auto ac_result = ac_solver.solve();

            if (ac_result.status == ConvergenceStatus::SUCCESS) {
                spdlog::info("AC analysis completed: {} frequency points",
                            ac_result.frequency_points.size());

                // Determine output filename
                std::string ac_output_file;
                if (output_file) {
                    // Replace .ssf with .csv
                    ac_output_file = *output_file;
                    size_t pos = ac_output_file.find_last_of('.');
                    if (pos != std::string::npos) {
                        ac_output_file = ac_output_file.substr(0, pos) + "_ac.csv";
                    } else {
                        ac_output_file += "_ac.csv";
                    }
                } else {
                    // Use netlist filename base
                    ac_output_file = netlist_file;
                    size_t pos = ac_output_file.find_last_of('.');
                    if (pos != std::string::npos) {
                        ac_output_file = ac_output_file.substr(0, pos) + "_ac.csv";
                    } else {
                        ac_output_file += "_ac.csv";
                    }
                }

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

        // If no analysis specified, run DC by default
        if (!analysis.has_dc && !analysis.has_tran && config.ac.start_freq == 0.0) {
            spdlog::info("No analysis specified, running DC analysis...");
            solvers::DCSolver dc_solver(*circuit);
            DenseVector solution;
            SolverStats stats;

            auto status = dc_solver.solve(solution, stats);

            if (status == ConvergenceStatus::SUCCESS) {
                spdlog::info("DC analysis converged");
                std::cout << "\n=== DC Operating Point ===\n";
                auto node_names = circuit->get_node_names();

                // Print ground node explicitly (always 0V)
                for (const auto& name : node_names) {
                    if (name == "0" || name == "GND" || name.empty()) {
                        std::cout << "V(0) = 0 V\n";
                        break;
                    }
                }

                // Print non-ground nodes from solution vector
                size_t solution_idx = 0;
                for (const auto& name : node_names) {
                    if (name == "0" || name == "GND" || name.empty()) {
                        continue; // Skip ground node
                    }
                    if (solution_idx < solution.size()) {
                        std::cout << "V(" << name << ") = "
                                  << solution[solution_idx] << " V\n";
                        solution_idx++;
                    }
                }
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
