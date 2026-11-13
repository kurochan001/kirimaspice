// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/circuit/circuit_graph.cpp
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/circuit/subcircuit.hpp"
#include "kirimaspice/core/types.hpp"  // For SimulationConfig and ModelDefinition
#include "kirimaspice/core/constants.hpp"  // For MAX_SUBCIRCUIT_ITERATIONS
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/devices/capacitor.hpp"
#include "kirimaspice/devices/inductor.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/devices/time_varying_voltage_source.hpp"
#include "kirimaspice/devices/waveform.hpp"
#include "kirimaspice/devices/current_source.hpp"
#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/bjt_pnp.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"  // Step 5.1: Unified MOSFET Level 1
#include "kirimaspice/devices/mosfet_level3.hpp"  // Step 5.4: Unified MOSFET Level 3
#include "kirimaspice/devices/mosfet_bsim3.hpp"  // Step 5.6: Unified BSIM3 template
#include "kirimaspice/devices/mosfet_bsim1.hpp"    // Step 5.5: Unified BSIM1 (Level 9)
#include "kirimaspice/devices/mosfet_bsim4.hpp"  // Step 5.7: Unified BSIM4 template
#include "kirimaspice/devices/mosfet_sic.hpp"    // Step 5.3: Unified SiC MOSFET
#include "kirimaspice/devices/gan_hemt.hpp"      // Step 3.1: Unified GaN HEMT
#include "kirimaspice/devices/finfet.hpp"        // Step 5.2: Unified FinFET
#include "kirimaspice/devices/vdmos.hpp"         // Step 3.2: Unified VDMOS
#include "kirimaspice/devices/igbt.hpp"          // Step 3.3: Unified IGBT
#include "kirimaspice/devices/scr.hpp"           // Step 4.3: Unified SCR
#include "kirimaspice/devices/triac.hpp"
#include "kirimaspice/devices/mesfet.hpp"        // Step 4.2: Unified MESFET
#include "kirimaspice/devices/vcvs.hpp"
#include "kirimaspice/devices/cccs.hpp"
#include "kirimaspice/devices/vccs.hpp"
#include "kirimaspice/devices/ccvs.hpp"
#include "kirimaspice/devices/mutual_inductance.hpp"
#include "kirimaspice/devices/tline_lossless.hpp"
#include "kirimaspice/devices/tline_lossy.hpp"
#include "kirimaspice/devices/behavioral_source.hpp"
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <exprtk.hpp>
#include <stdexcept>
#include <algorithm>

#include "kirimaspice/util/units.hpp"

namespace kirimaspice::circuit {

//==============================================================================
// Helper Functions
//==============================================================================

namespace {

using kirimaspice::util::parse_value_with_unit;

} // anonymous namespace

//==============================================================================
// CircuitGraph Implementation
//==============================================================================

CircuitGraph::CircuitGraph()
    : next_node_id_(0)
    , next_device_id_(0)
    , subcircuits_(std::make_unique<SubcircuitRegistry>())
    , config_(nullptr) {

    // Add ground node (GND = node 0)
    add_node("0");

    spdlog::debug("CircuitGraph created");
}

CircuitGraph::~CircuitGraph() = default;

// Move constructor and assignment (defined here to allow incomplete SubcircuitRegistry type)
CircuitGraph::CircuitGraph(CircuitGraph&&) noexcept = default;
CircuitGraph& CircuitGraph::operator=(CircuitGraph&&) noexcept = default;

void CircuitGraph::set_simulation_config(const SimulationConfig* config) {
    config_ = config;
    spdlog::info("SimulationConfig set for model parameter access during subcircuit expansion");
}

//==============================================================================
// Node Management
//==============================================================================
NodeId CircuitGraph::add_node(const std::string& name) {
    // Check if node already exists
    auto it = node_map_.find(name);
    if (it != node_map_.end()) {
        return it->second;  // Return existing node ID
    }

    // Create new node
    NodeId id = next_node_id_++;

    // Add to maps
    node_map_[name] = id;

    // Expand node_names_ vector if needed
    if (id >= node_names_.size()) {
        node_names_.resize(id + 1);
    }
    node_names_[id] = name;

    // Add vertex to Boost.Graph
    auto vertex = boost::add_vertex(graph_);
    graph_[vertex].id = id;
    graph_[vertex].name = name;

    spdlog::debug("Added node: {} (ID={})", name, id);

    return id;
}

NodeId CircuitGraph::get_node_id(const std::string& name) const {
    auto it = node_map_.find(name);
    if (it == node_map_.end()) {
        throw std::runtime_error(fmt::format("Node not found: {}", name));
    }
    return it->second;
}

bool CircuitGraph::has_node(const std::string& name) const {
    return node_map_.find(name) != node_map_.end();
}

std::uint32_t CircuitGraph::num_nodes() const {
    return static_cast<std::uint32_t>(node_map_.size());
}

std::vector<std::string> CircuitGraph::get_node_names() const {
    std::vector<std::string> names;
    names.reserve(node_names_.size());

    for (const auto& name : node_names_) {
        if (!name.empty()) {
            names.push_back(name);
        }
    }

    return names;
}

//==============================================================================
// Device Management
//==============================================================================

void CircuitGraph::add_resistor(const std::string& name,
                                const std::string& np,
                                const std::string& nn,
                                Real resistance) {
    // Add nodes if they don't exist
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    // Create Resistor device
    DeviceId id = next_device_id_++;
    auto resistor = std::make_shared<devices::Resistor>(id, name, np_id, nn_id, resistance);

    // Add to device list
    devices_.push_back(resistor);
    device_map_[name] = resistor;

    // Add edge to graph
    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added resistor: {} {} {} {}Ω", name, np, nn, resistance);
}

void CircuitGraph::add_capacitor(const std::string& name,
                                 const std::string& np,
                                 const std::string& nn,
                                 Real capacitance) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto capacitor = std::make_shared<devices::Capacitor>(id, name, np_id, nn_id, capacitance);

    devices_.push_back(capacitor);
    device_map_[name] = capacitor;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added capacitor: {} {} {} {}F", name, np, nn, capacitance);
}

void CircuitGraph::add_inductor(const std::string& name,
                                const std::string& np,
                                const std::string& nn,
                                Real inductance) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto inductor = std::make_shared<devices::Inductor>(id, name, np_id, nn_id, inductance);

    devices_.push_back(inductor);
    device_map_[name] = inductor;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added inductor: {} {} {} {}H", name, np, nn, inductance);
}

void CircuitGraph::add_voltage_source(const std::string& name,
                                      const std::string& np,
                                      const std::string& nn,
                                      Real dc_value,
                                      Real ac_magnitude,
                                      Real ac_phase) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto vsource = std::make_shared<devices::VoltageSource>(id, name, np_id, nn_id, dc_value,
                                                            ac_magnitude, ac_phase);

    // Assign branch ID for MNA (voltage sources need extra current variable)
    BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
    vsource->set_branch_id(branch_id);
    voltage_sources_.push_back(vsource);

    devices_.push_back(vsource);
    device_map_[name] = vsource;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added voltage source: {} {} {} {}V (branch={})", name, np, nn, dc_value, branch_id);
}

void CircuitGraph::add_current_source(const std::string& name,
                                      const std::string& np,
                                      const std::string& nn,
                                      Real dc_value,
                                      Real ac_magnitude,
                                      Real ac_phase) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto isource = std::make_shared<devices::CurrentSource>(id, name, np_id, nn_id, dc_value,
                                                            ac_magnitude, ac_phase);

    devices_.push_back(isource);
    device_map_[name] = isource;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added current source: {} {} {} IDC={}A AC={}A∠{}°",
                 name, np, nn, dc_value, ac_magnitude, ac_phase);
}

void CircuitGraph::add_behavioral_source(const std::string& name,
                                         const std::string& np,
                                         const std::string& nn,
                                         BehavioralSourceType type,
                                         const std::string& expression) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;

    // Parse expression to find referenced nodes in V(...) syntax
    std::vector<std::string> referenced_nodes;
    {
        // Simple regex-like parsing for V(node_name)
        std::string expr_upper = expression;
        std::transform(expr_upper.begin(), expr_upper.end(), expr_upper.begin(), ::toupper);

        size_t pos = 0;
        while ((pos = expr_upper.find("V(", pos)) != std::string::npos) {
            size_t close = expression.find(')', pos + 2);
            if (close != std::string::npos) {
                std::string node_name = expression.substr(pos + 2, close - pos - 2);
                // Trim whitespace
                node_name.erase(0, node_name.find_first_not_of(" \t"));
                node_name.erase(node_name.find_last_not_of(" \t") + 1);

                if (!node_name.empty()) {
                    referenced_nodes.push_back(node_name);
                }
            }
            pos += 2;
        }
    }

    // Create evaluator with V(node) support
    auto evaluator = [this, expression, name, referenced_nodes](Real time, const DenseVector& node_voltages) -> Real {
        // Create symbol table with TIME variable
        exprtk::symbol_table<Real> symbol_table;
        symbol_table.add_variable("TIME", const_cast<Real&>(time));
        symbol_table.add_constants();

        // Add V(node) variables to symbol table
        // We create mutable copies to satisfy ExprTk's API
        std::vector<Real> node_voltage_values(referenced_nodes.size());
        for (size_t i = 0; i < referenced_nodes.size(); ++i) {
            const std::string& node_name = referenced_nodes[i];

            // Get voltage value for this node
            Real voltage = 0.0;
            try {
                NodeId node_id = get_node_id(node_name);
                if (node_id == NODE_GND) {
                    voltage = 0.0;
                } else {
                    size_t idx = static_cast<size_t>(node_id - 1);  // 0-indexed
                    if (idx < node_voltages.size()) {
                        voltage = node_voltages[idx];
                    }
                }
            } catch (const std::exception& e) {
                spdlog::warn("BehavioralSource '{}': Unknown node '{}' in expression: {}",
                           name, node_name, e.what());
                voltage = 0.0;
            } catch (...) {
                spdlog::warn("BehavioralSource '{}': Unknown node '{}' in expression (unknown exception)",
                           name, node_name);
                voltage = 0.0;
            }

            node_voltage_values[i] = voltage;

            // Register as V_<node_name> variable (e.g., V_in, V_out)
            std::string var_name = "V_" + node_name;
            symbol_table.add_variable(var_name, node_voltage_values[i]);
        }

        // Preprocess expression: replace V(node) with V_node
        std::string processed_expr = expression;
        size_t pos = 0;
        while ((pos = processed_expr.find("V(", pos)) != std::string::npos) {
            size_t close = processed_expr.find(')', pos + 2);
            if (close != std::string::npos) {
                std::string node_name = processed_expr.substr(pos + 2, close - pos - 2);
                node_name.erase(0, node_name.find_first_not_of(" \t"));
                node_name.erase(node_name.find_last_not_of(" \t") + 1);

                std::string replacement = "V_" + node_name;
                processed_expr.replace(pos, close - pos + 1, replacement);
                pos += replacement.length();
            } else {
                pos += 2;
            }
        }

        // TODO +: Add I(source) support (requires accessing branch currents during evaluation)
        // Placeholder implementation - I(source) currently evaluates to 0
        // Full implementation requires:
        //   1. Storing previous solution vector in CircuitGraph
        //   2. Extracting branch current by device name
        //   3. Preprocessing I(source) -> I_source in expression
        // Example: I(Vin) would be replaced with I_Vin variable

        exprtk::expression<Real> expr;
        expr.register_symbol_table(symbol_table);

        exprtk::parser<Real> parser;
        if (!parser.compile(processed_expr, expr)) {
            spdlog::error("BehavioralSource '{}': Failed to compile expression '{}'",
                         name, processed_expr);
            return 0.0;
        }

        return expr.value();
    };

    auto bsource = std::make_shared<devices::BehavioralSource>(
        id, name, np_id, nn_id, type, expression, evaluator);

    // If voltage source, add to voltage_sources_ and assign branch_id
    if (type == BehavioralSourceType::VOLTAGE) {
        BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
        bsource->set_branch_id(branch_id);
        voltage_sources_.push_back(bsource);
    }

    devices_.push_back(bsource);
    device_map_[name] = bsource;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added behavioral source: {} {} {} {}={}",
                 name, np, nn,
                 (type == BehavioralSourceType::VOLTAGE ? "V" : "I"),
                 expression);
}

void CircuitGraph::add_sin_voltage_source(const std::string& name,
                                          const std::string& np,
                                          const std::string& nn,
                                          Real offset,
                                          Real amplitude,
                                          Real frequency,
                                          Real delay,
                                          Real damping,
                                          Real phase) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto waveform = std::make_unique<devices::SinWaveform>(
        offset, amplitude, frequency, delay, damping, phase);
    auto vsource = std::make_shared<devices::TimeVaryingVoltageSource>(
        id, name, np_id, nn_id, std::move(waveform));

    // Assign branch ID for MNA (voltage sources need extra current variable)
    BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
    vsource->set_branch_id(branch_id);
    voltage_sources_.push_back(vsource);

    devices_.push_back(vsource);
    device_map_[name] = vsource;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added SIN voltage source: {} {} {} offset={}V amp={}V freq={}Hz",
                  name, np, nn, offset, amplitude, frequency);
}

void CircuitGraph::add_pulse_voltage_source(const std::string& name,
                                            const std::string& np,
                                            const std::string& nn,
                                            Real v1,
                                            Real v2,
                                            Real delay,
                                            Real rise_time,
                                            Real fall_time,
                                            Real pulse_width,
                                            Real period) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto waveform = std::make_unique<devices::PulseWaveform>(
        v1, v2, delay, rise_time, fall_time, pulse_width, period);
    auto vsource = std::make_shared<devices::TimeVaryingVoltageSource>(
        id, name, np_id, nn_id, std::move(waveform));

    // Assign branch ID for MNA
    BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
    vsource->set_branch_id(branch_id);
    voltage_sources_.push_back(vsource);

    devices_.push_back(vsource);
    device_map_[name] = vsource;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added PULSE voltage source: {} {} {} v1={}V v2={}V period={}s",
                  name, np, nn, v1, v2, period);
}

void CircuitGraph::add_pwl_voltage_source(const std::string& name,
                                          const std::string& np,
                                          const std::string& nn,
                                          const std::vector<std::pair<Real, Real>>& points) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto waveform = std::make_unique<devices::PWLWaveform>(points);
    auto vsource = std::make_shared<devices::TimeVaryingVoltageSource>(
        id, name, np_id, nn_id, std::move(waveform));

    // Assign branch ID for MNA
    BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
    vsource->set_branch_id(branch_id);
    voltage_sources_.push_back(vsource);

    devices_.push_back(vsource);
    device_map_[name] = vsource;

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added PWL voltage source: {} {} {} points={}",
                  name, np, nn, points.size());
}

void CircuitGraph::add_diode(const std::string& name,
                             const std::string& anode,
                             const std::string& cathode,
                             Real is,
                             Real n) {
    NodeId anode_id = add_node(anode);
    NodeId cathode_id = add_node(cathode);

    DeviceId id = next_device_id_++;
    auto diode = std::make_shared<devices::Diode>(id, name, anode_id, cathode_id, is, n);

    devices_.push_back(diode);
    device_map_[name] = diode;

    auto [edge, success] = boost::add_edge(anode_id, cathode_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added diode: {} {} {} Is={:.3e}A N={:.3f}", name, anode, cathode, is, n);
}

void CircuitGraph::add_bjt_npn(const std::string& name,
                               const std::string& collector,
                               const std::string& base,
                               const std::string& emitter,
                               Real is,
                               Real beta_f,
                               Real n,
                               Real kf,
                               Real af,
                               Real ff,
                               Real ise,
                               Real ne,
                               Real isc_leak,
                               Real nc) {
    NodeId collector_id = add_node(collector);
    NodeId base_id = add_node(base);
    NodeId emitter_id = add_node(emitter);

    DeviceId id = next_device_id_++;
    auto bjt = std::make_shared<devices::BJT_NPN>(id, name, collector_id, base_id, emitter_id,
                                                    is, beta_f, n,
                                                    -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                                                    0.0, 0.0,        // vaf, var (Early effect, defaults to no effect)
                                                    0.0, 0.0,        // ikf, ikr (high-current effect, defaults to no effect)
                                                    0.0, 0.0, 0.0,   // rb, rbm, irb (base resistance, defaults to no resistance)
                                                    ise, ne, isc_leak, nc,  // ise, ne, isc_leak, nc (non-ideal currents)
                                                    0.0, 0.75, 0.33,  // cje, vje, mje
                                                    0.0, 0.75, 0.33,  // cjc, vjc, mjc
                                                    0.0, 0.0,         // tf, tr
                                                    kf, af, ff);      // Noise parameters

    devices_.push_back(bjt);
    device_map_[name] = bjt;

    // Add edges: C-B and B-E (BJT is a 3-terminal device)
    auto [edge_cb, success_cb] = boost::add_edge(collector_id, base_id, graph_);
    if (success_cb) {
        graph_[edge_cb].device_id = id;
    }

    auto [edge_be, success_be] = boost::add_edge(base_id, emitter_id, graph_);
    if (success_be) {
        graph_[edge_be].device_id = id;
    }

    spdlog::debug("Added BJT_NPN: {} C={} B={} E={} Is={:.3e}A βF={:.1f} N={:.3f} Kf={:.3e} AF={:.2f} FF={:.2f}",
                  name, collector, base, emitter, is, beta_f, n, kf, af, ff);
}

void CircuitGraph::add_bjt_pnp(const std::string& name,
                               const std::string& collector,
                               const std::string& base,
                               const std::string& emitter,
                               Real is,
                               Real beta_f,
                               Real n,
                               Real kf,
                               Real af,
                               Real ff,
                               Real ise,
                               Real ne,
                               Real isc_leak,
                               Real nc) {
    NodeId collector_id = add_node(collector);
    NodeId base_id = add_node(base);
    NodeId emitter_id = add_node(emitter);

    DeviceId id = next_device_id_++;
    auto bjt = std::make_shared<devices::BJT_PNP>(id, name, collector_id, base_id, emitter_id,
                                                    is, beta_f, n,
                                                    -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                                                    0.0, 0.0,        // vaf, var (Early effect, defaults to no effect)
                                                    0.0, 0.0,        // ikf, ikr (high-current effect, defaults to no effect)
                                                    0.0, 0.0, 0.0,   // rb, rbm, irb (base resistance, defaults to no resistance)
                                                    ise, ne, isc_leak, nc,  // ise, ne, isc_leak, nc (non-ideal currents)
                                                    0.0, 0.75, 0.33,  // cje, vje, mje
                                                    0.0, 0.75, 0.33,  // cjc, vjc, mjc
                                                    0.0, 0.0,         // tf, tr
                                                    kf, af, ff);      // Noise parameters

    devices_.push_back(bjt);
    device_map_[name] = bjt;

    // Add edges: C-B and B-E (BJT is a 3-terminal device)
    auto [edge_cb, success_cb] = boost::add_edge(collector_id, base_id, graph_);
    if (success_cb) {
        graph_[edge_cb].device_id = id;
    }

    auto [edge_be, success_be] = boost::add_edge(base_id, emitter_id, graph_);
    if (success_be) {
        graph_[edge_be].device_id = id;
    }

    spdlog::debug("Added BJT_PNP: {} C={} B={} E={} Is={:.3e}A βF={:.1f} N={:.3f} Kf={:.3e} AF={:.2f} FF={:.2f}",
                  name, collector, base, emitter, is, beta_f, n, kf, af, ff);
}

void CircuitGraph::add_mosfet_n(const std::string& name,
                                const std::string& drain,
                                const std::string& gate,
                                const std::string& source,
                                Real kp,
                                Real w,
                                Real l,
                                Real vth,
                                Real cgso,
                                Real cgdo,
                                Real cgbo,
                                Real kf,
                                Real af,
                                Real ef,
                                Real gamma,
                                Real kt1,
                                Real kt2,
                                Real ute,
                                Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_N>(id, name, drain_id, gate_id, source_id,
                                                        kp, w, l, vth,
                                                        cgso, cgdo, cgbo,  // Overlap capacitances
                                                        kf, af, ef, gamma, // Noise parameters
                                                        kt1, kt2, ute, tnom); // Temperature parameters

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S (MOSFET is a 3-terminal device)
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    Real beta = kp * w / l;
    spdlog::info("Added MOSFET_N: {} DevID={} D={}({}) G={}({}) S={}({}) Kp={:.3e} W={:.3e} L={:.3e} Vth={:.3f} β={:.3e}",
                  name, id, drain, drain_id, gate, gate_id, source, source_id, kp, w, l, vth, beta);
}

void CircuitGraph::add_mosfet_p(const std::string& name,
                                const std::string& drain,
                                const std::string& gate,
                                const std::string& source,
                                Real kp,
                                Real w,
                                Real l,
                                Real vth,
                                Real cgso,
                                Real cgdo,
                                Real cgbo,
                                Real kf,
                                Real af,
                                Real ef,
                                Real gamma,
                                Real kt1,
                                Real kt2,
                                Real ute,
                                Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_P>(id, name, drain_id, gate_id, source_id,
                                                        kp, w, l, vth,
                                                        cgso, cgdo, cgbo,  // Overlap capacitances
                                                        kf, af, ef, gamma, // Noise parameters
                                                        kt1, kt2, ute, tnom); // Temperature parameters

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S (MOSFET is a 3-terminal device)
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    Real beta = kp * w / l;
    spdlog::info("Added MOSFET_P: {} DevID={} D={}({}) G={}({}) S={}({}) Kp={:.3e} W={:.3e} L={:.3e} Vth={:.3f} β={:.3e}",
                  name, id, drain, drain_id, gate, gate_id, source, source_id, kp, w, l, vth, beta);
}

//==============================================================================
// MOSFET Level 3
//==============================================================================

void CircuitGraph::add_mosfet_level3_n(const std::string& name,
                                         const std::string& drain,
                                         const std::string& gate,
                                         const std::string& source,
                                         const std::string& bulk,
                                         const devices::MOSFETLevel3Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_Level3_N>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_Level3_N: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m VTO={:.3f}V",
                  name, drain, gate, source, bulk, params.w, params.l, params.vto);
}

void CircuitGraph::add_mosfet_level3_p(const std::string& name,
                                         const std::string& drain,
                                         const std::string& gate,
                                         const std::string& source,
                                         const std::string& bulk,
                                         const devices::MOSFETLevel3Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_Level3_P>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_Level3_P: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m VTO={:.3f}V",
                  name, drain, gate, source, bulk, params.w, params.l, params.vto);
}

//==============================================================================
// BSIM3 MOSFETs
//==============================================================================

void CircuitGraph::add_mosfet_bsim3_n(const std::string& name,
                                       const std::string& drain,
                                       const std::string& gate,
                                       const std::string& source,
                                       const std::string& bulk,
                                       const devices::BSIM3Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_BSIM3_N>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S (simplified)
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_BSIM3_N: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m VTH0={:.3f}V",
                  name, drain, gate, source, bulk, params.w, params.l, params.vth0);
}

void CircuitGraph::add_mosfet_bsim3_p(const std::string& name,
                                       const std::string& drain,
                                       const std::string& gate,
                                       const std::string& source,
                                       const std::string& bulk,
                                       const devices::BSIM3Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_BSIM3_P>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S (simplified)
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_BSIM3_P: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m VTH0={:.3f}V",
                  name, drain, gate, source, bulk, params.w, params.l, params.vth0);
}

void CircuitGraph::add_mosfet_bsim1_n(const std::string& name,
                                       const std::string& drain,
                                       const std::string& gate,
                                       const std::string& source,
                                       const std::string& bulk,
                                       const devices::MOSFETBsim1Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_BSIM1_N>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_BSIM1_N: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m",
                  name, drain, gate, source, bulk, params.w, params.l);
}

void CircuitGraph::add_mosfet_bsim1_p(const std::string& name,
                                       const std::string& drain,
                                       const std::string& gate,
                                       const std::string& source,
                                       const std::string& bulk,
                                       const devices::MOSFETBsim1Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_BSIM1_P>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_BSIM1_P: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m",
                  name, drain, gate, source, bulk, params.w, params.l);
}

void CircuitGraph::add_mosfet_bsim4_n(const std::string& name,
                                       const std::string& drain,
                                       const std::string& gate,
                                       const std::string& source,
                                       const std::string& bulk,
                                       const devices::BSIM4Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_BSIM4_N>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_BSIM4_N: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m VTH0={:.3f}V",
                  name, drain, gate, source, bulk, params.w, params.l, params.vth0);
}

void CircuitGraph::add_mosfet_bsim4_p(const std::string& name,
                                       const std::string& drain,
                                       const std::string& gate,
                                       const std::string& source,
                                       const std::string& bulk,
                                       const devices::BSIM4Params& params) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);
    NodeId bulk_id = add_node(bulk);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_BSIM4_P>(id, name, drain_id, gate_id, source_id, bulk_id, params);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_BSIM4_P: {} D={} G={} S={} B={} W={:.3e}m L={:.3e}m VTH0={:.3f}V",
                  name, drain, gate, source, bulk, params.w, params.l, params.vth0);
}

//==============================================================================
// SiC MOSFET
//==============================================================================

void CircuitGraph::add_mosfet_sic_n(const std::string& name,
                                     const std::string& drain,
                                     const std::string& gate,
                                     const std::string& source,
                                     Real kp, Real w, Real l, Real vth,
                                     Real theta, Real rdrift, Real nfs, Real vbd,
                                     Real cgso, Real cgdo, Real cgbo,
                                     Real cjo, Real pb, Real mj,
                                     Real kf, Real af, Real ef, Real gamma,
                                     Real kt1, Real kt2, Real ute, Real rdtemp, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_SiC_N>(
        id, name, drain_id, gate_id, source_id,
        kp, w, l, vth,
        theta, rdrift, nfs, vbd,
        cgso, cgdo, cgbo,
        cjo, pb, mj,
        kf, af, ef, gamma,
        kt1, kt2, ute, rdtemp, tnom);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_SiC_N: {} D={} G={} S={} Kp={:.3e}A/V² W={:.3e}m L={:.3e}m "
                  "Vth={:.3f}V β={:.3e}A/V² Rdrift={:.3e}Ω THETA={:.3e}V⁻¹ Vbd={:.1f}V",
                  name, drain, gate, source, kp, w, l, vth, kp * w / l, rdrift, theta, vbd);
}

void CircuitGraph::add_mosfet_sic_p(const std::string& name,
                                     const std::string& drain,
                                     const std::string& gate,
                                     const std::string& source,
                                     Real kp, Real w, Real l, Real vth,
                                     Real theta, Real rdrift, Real nfs, Real vbd,
                                     Real cgso, Real cgdo, Real cgbo,
                                     Real cjo, Real pb, Real mj,
                                     Real kf, Real af, Real ef, Real gamma,
                                     Real kt1, Real kt2, Real ute, Real rdtemp, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto mosfet = std::make_shared<devices::MOSFET_SiC_P>(
        id, name, drain_id, gate_id, source_id,
        kp, w, l, vth,
        theta, rdrift, nfs, vbd,
        cgso, cgdo, cgbo,
        cjo, pb, mj,
        kf, af, ef, gamma,
        kt1, kt2, ute, rdtemp, tnom);

    devices_.push_back(mosfet);
    device_map_[name] = mosfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added MOSFET_SiC_P: {} D={} G={} S={} Kp={:.3e}A/V² W={:.3e}m L={:.3e}m "
                  "Vth={:.3f}V β={:.3e}A/V² Rdrift={:.3e}Ω THETA={:.3e}V⁻¹ Vbd={:.1f}V",
                  name, drain, gate, source, kp, w, l, vth, kp * w / l, rdrift, theta, vbd);
}

//==============================================================================
// GaN HEMT
//==============================================================================

void CircuitGraph::add_gan_hemt_n(const std::string& name,
                                   const std::string& drain,
                                   const std::string& gate,
                                   const std::string& source,
                                   Real beta, Real vth, Real lambda,
                                   Real rs, Real rd, Real vbd, Real trap,
                                   Real cgs, Real cgd, Real cds,
                                   Real kf, Real af, Real ef,
                                   Real kt1, Real kt2, Real ute, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto hemt = std::make_shared<devices::GaN_HEMT_N>(
        id, name, drain_id, gate_id, source_id,
        beta, vth, lambda,
        rs, rd, vbd, trap,
        cgs, cgd, cds,
        kf, af, ef,
        kt1, kt2, ute, tnom);

    devices_.push_back(hemt);
    device_map_[name] = hemt;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added GaN_HEMT_N: {} D={} G={} S={} β={:.3e}A/V² Vth={:.3f}V λ={:.3e}V⁻¹ "
                  "RS={:.3e}Ω RD={:.3e}Ω Vbd={:.1f}V",
                  name, drain, gate, source, beta, vth, lambda, rs, rd, vbd);
}

void CircuitGraph::add_gan_hemt_p(const std::string& name,
                                   const std::string& drain,
                                   const std::string& gate,
                                   const std::string& source,
                                   Real beta, Real vth, Real lambda,
                                   Real rs, Real rd, Real vbd, Real trap,
                                   Real cgs, Real cgd, Real cds,
                                   Real kf, Real af, Real ef,
                                   Real kt1, Real kt2, Real ute, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto hemt = std::make_shared<devices::GaN_HEMT_P>(
        id, name, drain_id, gate_id, source_id,
        beta, vth, lambda,
        rs, rd, vbd, trap,
        cgs, cgd, cds,
        kf, af, ef,
        kt1, kt2, ute, tnom);

    devices_.push_back(hemt);
    device_map_[name] = hemt;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added GaN_HEMT_P: {} D={} G={} S={} β={:.3e}A/V² Vth={:.3f}V λ={:.3e}V⁻¹ "
                  "RS={:.3e}Ω RD={:.3e}Ω Vbd={:.1f}V",
                  name, drain, gate, source, beta, vth, lambda, rs, rd, vbd);
}

void CircuitGraph::add_finfet_n(const std::string& name,
                                 const std::string& drain,
                                 const std::string& gate,
                                 const std::string& source,
                                 int nfin, Real hfin, Real tfin,
                                 Real vth, Real u0, Real vsat, Real ss, Real dibl, Real tox,
                                 Real cgs, Real cgd,
                                 Real kf, Real af, Real kt1, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto finfet = std::make_shared<devices::FinFET_N>(
        id, name, drain_id, gate_id, source_id,
        nfin, hfin, tfin, vth, u0, vsat, ss, dibl, tox,
        cgs, cgd, kf, af, kt1, tnom);

    devices_.push_back(finfet);
    device_map_[name] = finfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added FinFET_N: {} D={} G={} S={} NFin={} Hfin={:.1f}nm Vth={:.3f}V "
                  "SS={:.1f}mV/dec DIBL={:.3f}V/V",
                  name, drain, gate, source, nfin, hfin * 1e9, vth, ss * 1e3, dibl);
}

void CircuitGraph::add_finfet_p(const std::string& name,
                                 const std::string& drain,
                                 const std::string& gate,
                                 const std::string& source,
                                 int nfin, Real hfin, Real tfin,
                                 Real vth, Real u0, Real vsat, Real ss, Real dibl, Real tox,
                                 Real cgs, Real cgd,
                                 Real kf, Real af, Real kt1, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto finfet = std::make_shared<devices::FinFET_P>(
        id, name, drain_id, gate_id, source_id,
        nfin, hfin, tfin, vth, u0, vsat, ss, dibl, tox,
        cgs, cgd, kf, af, kt1, tnom);

    devices_.push_back(finfet);
    device_map_[name] = finfet;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added FinFET_P: {} D={} G={} S={} NFin={} Hfin={:.1f}nm Vth={:.3f}V "
                  "SS={:.1f}mV/dec DIBL={:.3f}V/V",
                  name, drain, gate, source, nfin, hfin * 1e9, vth, ss * 1e3, dibl);
}

void CircuitGraph::add_vdmos_n(const std::string& name,
                                const std::string& drain,
                                const std::string& gate,
                                const std::string& source,
                                Real vto, Real kp, Real rds, Real vbd, Real lambda,
                                Real cgs, Real cgdmax, Real cgdmin, Real cds,
                                Real kf, Real af, Real ef, Real gamma,
                                Real kt1, Real kt2, Real ute, Real rtc, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto vdmos = std::make_shared<devices::VDMOS_N>(
        id, name, drain_id, gate_id, source_id,
        vto, kp, rds, vbd, lambda,
        cgs, cgdmax, cgdmin, cds,
        kf, af, ef, gamma,
        kt1, kt2, ute, rtc, tnom);

    devices_.push_back(vdmos);
    device_map_[name] = vdmos;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added VDMOS_N: {} D={} G={} S={} VTO={:.3f}V KP={:.3e}A/V² "
                  "RDS={:.3e}Ω VBD={:.1f}V λ={:.3e}V⁻¹",
                  name, drain, gate, source, vto, kp, rds, vbd, lambda);
}

void CircuitGraph::add_vdmos_p(const std::string& name,
                                const std::string& drain,
                                const std::string& gate,
                                const std::string& source,
                                Real vto, Real kp, Real rds, Real vbd, Real lambda,
                                Real cgs, Real cgdmax, Real cgdmin, Real cds,
                                Real kf, Real af, Real ef, Real gamma,
                                Real kt1, Real kt2, Real ute, Real rtc, Real tnom) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto vdmos = std::make_shared<devices::VDMOS_P>(
        id, name, drain_id, gate_id, source_id,
        vto, kp, rds, vbd, lambda,
        cgs, cgdmax, cgdmin, cds,
        kf, af, ef, gamma,
        kt1, kt2, ute, rtc, tnom);

    devices_.push_back(vdmos);
    device_map_[name] = vdmos;

    // Add edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    spdlog::debug("Added VDMOS_P: {} D={} G={} S={} VTO={:.3f}V KP={:.3e}A/V² "
                  "RDS={:.3e}Ω VBD={:.1f}V λ={:.3e}V⁻¹",
                  name, drain, gate, source, vto, kp, rds, vbd, lambda);
}

void CircuitGraph::add_igbt_n(const std::string& name,
                               const std::string& collector,
                               const std::string& gate,
                               const std::string& emitter,
                               Real vge_th, Real kp, Real vce_sat, Real tau, Real area,
                               Real cge, Real cce,
                               Real kf, Real af,
                               Real kt1, Real kt2, Real ute, Real tcv, Real tnom) {
    NodeId collector_id = add_node(collector);
    NodeId gate_id = add_node(gate);
    NodeId emitter_id = add_node(emitter);

    DeviceId id = next_device_id_++;
    auto igbt = std::make_shared<devices::IGBT_N>(
        id, name, collector_id, gate_id, emitter_id,
        vge_th, kp, vce_sat, tau, area,
        cge, cce,
        kf, af,
        kt1, kt2, ute, tcv, tnom);

    devices_.push_back(igbt);
    device_map_[name] = igbt;

    // Add edges: C-G and G-E
    auto [edge_cg, success_cg] = boost::add_edge(collector_id, gate_id, graph_);
    if (success_cg) {
        graph_[edge_cg].device_id = id;
    }

    auto [edge_ge, success_ge] = boost::add_edge(gate_id, emitter_id, graph_);
    if (success_ge) {
        graph_[edge_ge].device_id = id;
    }

    spdlog::debug("Added IGBT_N: {} C={} G={} E={} VGE_TH={:.3f}V KP={:.3e}A/V² "
                  "VCE_SAT={:.3f}V TAU={:.3e}s AREA={}",
                  name, collector, gate, emitter, vge_th, kp, vce_sat, tau, area);
}

void CircuitGraph::add_igbt_p(const std::string& name,
                               const std::string& collector,
                               const std::string& gate,
                               const std::string& emitter,
                               Real veg_th, Real kp, Real vce_sat, Real tau, Real area,
                               Real ceg, Real cec,
                               Real kf, Real af,
                               Real kt1, Real kt2, Real ute, Real tcv, Real tnom) {
    NodeId collector_id = add_node(collector);
    NodeId gate_id = add_node(gate);
    NodeId emitter_id = add_node(emitter);

    DeviceId id = next_device_id_++;
    auto igbt = std::make_shared<devices::IGBT_P>(
        id, name, collector_id, gate_id, emitter_id,
        veg_th, kp, vce_sat, tau, area,
        ceg, cec,
        kf, af,
        kt1, kt2, ute, tcv, tnom);

    devices_.push_back(igbt);
    device_map_[name] = igbt;

    // Add edges: C-G and G-E
    auto [edge_cg, success_cg] = boost::add_edge(collector_id, gate_id, graph_);
    if (success_cg) {
        graph_[edge_cg].device_id = id;
    }

    auto [edge_ge, success_ge] = boost::add_edge(gate_id, emitter_id, graph_);
    if (success_ge) {
        graph_[edge_ge].device_id = id;
    }

    spdlog::debug("Added IGBT_P: {} C={} G={} E={} VEG_TH={:.3f}V KP={:.3e}A/V² "
                  "VCE_SAT={:.3f}V TAU={:.3e}s AREA={}",
                  name, collector, gate, emitter, veg_th, kp, vce_sat, tau, area);
}

//==============================================================================
// SCR (Silicon Controlled Rectifier / Thyristor)
//==============================================================================

void CircuitGraph::add_scr_n(const std::string& name,
                              const std::string& anode,
                              const std::string& cathode,
                              const std::string& gate,
                              Real vbf, Real igt, Real ih, Real il, Real vgt,
                              Real ron, Real vtm, Real dv_dt, Real tnom) {
    NodeId anode_id = add_node(anode);
    NodeId cathode_id = add_node(cathode);
    NodeId gate_id = add_node(gate);

    DeviceId id = next_device_id_++;
    auto scr = std::make_shared<devices::SCR_N>(
        id, name, anode_id, cathode_id, gate_id,
        vbf, igt, ih, il, vgt, ron, vtm, dv_dt, tnom);

    devices_.push_back(scr);
    device_map_[name] = scr;

    // Add edges: A-K (main power path) and G-K (gate control)
    auto [edge_ak, success_ak] = boost::add_edge(anode_id, cathode_id, graph_);
    if (success_ak) {
        graph_[edge_ak].device_id = id;
    }

    auto [edge_gk, success_gk] = boost::add_edge(gate_id, cathode_id, graph_);
    if (success_gk) {
        graph_[edge_gk].device_id = id;
    }

    spdlog::debug("Added SCR_N: {} A={} K={} G={} VBF={:.1f}V IGT={:.3e}A "
                  "IH={:.3e}A IL={:.3e}A VGT={:.3f}V RON={:.3f}Ω VTM={:.3f}V",
                  name, anode, cathode, gate, vbf, igt, ih, il, vgt, ron, vtm);
}

void CircuitGraph::add_scr_p(const std::string& name,
                              const std::string& cathode,
                              const std::string& anode,
                              const std::string& gate,
                              Real vbf, Real igt, Real ih, Real il, Real vgt,
                              Real ron, Real vtm, Real dv_dt, Real tnom) {
    NodeId cathode_id = add_node(cathode);
    NodeId anode_id = add_node(anode);
    NodeId gate_id = add_node(gate);

    DeviceId id = next_device_id_++;
    auto scr = std::make_shared<devices::SCR_P>(
        id, name, cathode_id, anode_id, gate_id,
        vbf, igt, ih, il, vgt, ron, vtm, dv_dt, tnom);

    devices_.push_back(scr);
    device_map_[name] = scr;

    // Add edges: K-A (main power path, reverse polarity) and G-A (gate control)
    auto [edge_ka, success_ka] = boost::add_edge(cathode_id, anode_id, graph_);
    if (success_ka) {
        graph_[edge_ka].device_id = id;
    }

    auto [edge_ga, success_ga] = boost::add_edge(gate_id, anode_id, graph_);
    if (success_ga) {
        graph_[edge_ga].device_id = id;
    }

    spdlog::debug("Added SCR_P: {} K={} A={} G={} VBF={:.1f}V IGT={:.3e}A "
                  "IH={:.3e}A IL={:.3e}A VGT={:.3f}V RON={:.3f}Ω VTM={:.3f}V",
                  name, cathode, anode, gate, vbf, igt, ih, il, vgt, ron, vtm);
}

//==============================================================================
// TRIAC
//==============================================================================

void CircuitGraph::add_triac(const std::string& name,
                              const std::string& mt2,
                              const std::string& mt1,
                              const std::string& gate,
                              Real vbo, Real igt, Real ih, Real vgt,
                              Real ron, Real vtm, Real dv_dt, Real tnom) {
    NodeId mt2_id = add_node(mt2);
    NodeId mt1_id = add_node(mt1);
    NodeId gate_id = add_node(gate);

    DeviceId id = next_device_id_++;
    auto triac = std::make_shared<devices::TRIAC>(
        id, name, mt2_id, mt1_id, gate_id,
        vbo, igt, ih, vgt, ron, vtm, dv_dt, tnom);

    devices_.push_back(triac);
    device_map_[name] = triac;

    // Add edges: MT2-MT1 (main power path, bidirectional) and G-MT1 (gate control)
    auto [edge_mt, success_mt] = boost::add_edge(mt2_id, mt1_id, graph_);
    if (success_mt) {
        graph_[edge_mt].device_id = id;
    }

    auto [edge_g, success_g] = boost::add_edge(gate_id, mt1_id, graph_);
    if (success_g) {
        graph_[edge_g].device_id = id;
    }

    spdlog::debug("Added TRIAC: {} MT2={} MT1={} G={} VBO={:.1f}V IGT={:.3e}A "
                  "IH={:.3e}A VGT={:.3f}V RON={:.3f}Ω VTM={:.3f}V DV_DT={:.1f}V/μs",
                  name, mt2, mt1, gate, vbo, igt, ih, vgt, ron, vtm, dv_dt);
}

//==============================================================================
// MESFET
//==============================================================================

void CircuitGraph::add_mesfet_n(const std::string& name,
                                 const std::string& drain,
                                 const std::string& gate,
                                 const std::string& source,
                                 int model,
                                 Real beta, Real vto, Real lambda, Real alpha, Real b, Real vp,
                                 Real cgs, Real cgd, Real cds,
                                 Real kf, Real af, Real ef, Real tnom) {
    // Convert node names to IDs
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    // Generate device ID
    DeviceId id = next_device_id_++;

    // Create MESFET_N device with appropriate model
    devices::MESFETModel mesfet_model = (model == 1) ?
        devices::MESFETModel::CURTICE : devices::MESFETModel::STATZ;

    auto mesfet = std::make_shared<devices::MESFET_N>(
        id, name, drain_id, gate_id, source_id,
        mesfet_model, beta, vto, lambda, alpha, b, vp,
        cgs, cgd, cds, kf, af, ef, tnom);

    // Add to devices list
    devices_.push_back(mesfet);
    device_map_[name] = mesfet;

    // Add graph edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    // Log debug message
    std::string model_str = (model == 1) ? "Curtice" : "Statz";
    spdlog::debug("Added MESFET_N: {} D={} G={} S={} model={} BETA={:.3e}A/V² "
                  "VTO={:.3f}V LAMBDA={:.3f}/V CGS={:.3e}F CGD={:.3e}F",
                  name, drain, gate, source, model_str, beta, vto, lambda, cgs, cgd);
}

void CircuitGraph::add_mesfet_p(const std::string& name,
                                 const std::string& drain,
                                 const std::string& gate,
                                 const std::string& source,
                                 int model,
                                 Real beta, Real vto, Real lambda, Real alpha, Real b, Real vp,
                                 Real cgs, Real cgd, Real cds,
                                 Real kf, Real af, Real ef, Real tnom) {
    // Convert node names to IDs
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    // Generate device ID
    DeviceId id = next_device_id_++;

    // Create MESFET_P device with appropriate model
    devices::MESFETModel mesfet_model = (model == 1) ?
        devices::MESFETModel::CURTICE : devices::MESFETModel::STATZ;

    auto mesfet = std::make_shared<devices::MESFET_P>(
        id, name, drain_id, gate_id, source_id,
        mesfet_model, beta, vto, lambda, alpha, b, vp,
        cgs, cgd, cds, kf, af, ef, tnom);

    // Add to devices list
    devices_.push_back(mesfet);
    device_map_[name] = mesfet;

    // Add graph edges: D-G and G-S
    auto [edge_dg, success_dg] = boost::add_edge(drain_id, gate_id, graph_);
    if (success_dg) {
        graph_[edge_dg].device_id = id;
    }

    auto [edge_gs, success_gs] = boost::add_edge(gate_id, source_id, graph_);
    if (success_gs) {
        graph_[edge_gs].device_id = id;
    }

    // Log debug message
    std::string model_str = (model == 1) ? "Curtice" : "Statz";
    spdlog::debug("Added MESFET_P: {} D={} G={} S={} model={} BETA={:.3e}A/V² "
                  "VTO={:.3f}V LAMBDA={:.3f}/V CGS={:.3e}F CGD={:.3e}F",
                  name, drain, gate, source, model_str, beta, vto, lambda, cgs, cgd);
}

//==============================================================================
// Controlled Sources
//==============================================================================

void CircuitGraph::add_vcvs(const std::string& name,
                             const std::string& np,
                             const std::string& nn,
                             const std::string& ncp,
                             const std::string& ncn,
                             Real gain) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);
    NodeId ncp_id = add_node(ncp);
    NodeId ncn_id = add_node(ncn);

    DeviceId id = next_device_id_++;
    auto vcvs = std::make_shared<devices::VCVS>(id, name, np_id, nn_id, ncp_id, ncn_id, gain);

    // Assign branch ID for MNA (VCVS needs extra current variable like voltage source)
    BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
    vcvs->set_branch_id(branch_id);
    voltage_sources_.push_back(vcvs);

    devices_.push_back(vcvs);
    device_map_[name] = vcvs;

    // Add edges: output side (np-nn) and control side (ncp-ncn)
    auto [edge_out, success_out] = boost::add_edge(np_id, nn_id, graph_);
    if (success_out) {
        graph_[edge_out].device_id = id;
    }

    auto [edge_ctrl, success_ctrl] = boost::add_edge(ncp_id, ncn_id, graph_);
    if (success_ctrl) {
        graph_[edge_ctrl].device_id = id;
    }

    spdlog::debug("Added VCVS: {} np={} nn={} ncp={} ncn={} gain={} (branch={})",
                  name, np, nn, ncp, ncn, gain, branch_id);
}

void CircuitGraph::add_cccs(const std::string& name,
                             const std::string& np,
                             const std::string& nn,
                             const std::string& control_vsource_name,
                             Real gain) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto cccs = std::make_shared<devices::CCCS>(id, name, np_id, nn_id, control_vsource_name, gain);

    devices_.push_back(cccs);
    device_map_[name] = cccs;

    // Add edge: output side (np-nn)
    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added CCCS: {} np={} nn={} control={} gain={}",
                  name, np, nn, control_vsource_name, gain);

    // Note: control_branch_id will be resolved later by DC/Transient/AC solver
    // after all voltage sources have been assigned branch IDs
}

void CircuitGraph::add_vccs(const std::string& name,
                             const std::string& np,
                             const std::string& nn,
                             const std::string& ncp,
                             const std::string& ncn,
                             Real transconductance) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);
    NodeId ncp_id = add_node(ncp);
    NodeId ncn_id = add_node(ncn);

    DeviceId id = next_device_id_++;
    auto vccs = std::make_shared<devices::VCCS>(id, name, np_id, nn_id, ncp_id, ncn_id, transconductance);

    devices_.push_back(vccs);
    device_map_[name] = vccs;

    // Add edges: output side (np-nn) and control side (ncp-ncn)
    auto [edge_out, success_out] = boost::add_edge(np_id, nn_id, graph_);
    if (success_out) {
        graph_[edge_out].device_id = id;
    }

    auto [edge_ctrl, success_ctrl] = boost::add_edge(ncp_id, ncn_id, graph_);
    if (success_ctrl) {
        graph_[edge_ctrl].device_id = id;
    }

    spdlog::debug("Added VCCS: {} np={} nn={} ncp={} ncn={} Gm={}S",
                  name, np, nn, ncp, ncn, transconductance);
}

void CircuitGraph::add_ccvs(const std::string& name,
                             const std::string& np,
                             const std::string& nn,
                             const std::string& control_vsource_name,
                             Real transresistance) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto ccvs = std::make_shared<devices::CCVS>(id, name, np_id, nn_id, control_vsource_name, transresistance);

    // Assign branch ID for MNA (CCVS needs extra current variable like voltage source)
    BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
    ccvs->set_branch_id(branch_id);
    voltage_sources_.push_back(ccvs);

    devices_.push_back(ccvs);
    device_map_[name] = ccvs;

    // Add edge: output side (np-nn)
    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added CCVS: {} np={} nn={} control={} H={}Ω (branch={})",
                  name, np, nn, control_vsource_name, transresistance, branch_id);

    // Note: control_branch_id will be resolved later by DC/Transient/AC solver
    // after all voltage sources have been assigned branch IDs
}

//==============================================================================
// Mutual Inductance
//==============================================================================

void CircuitGraph::add_mutual_inductance(const std::string& name,
                                         const std::string& inductor1_name,
                                         const std::string& inductor2_name,
                                         Real coupling_coefficient) {
    DeviceId id = next_device_id_++;
    auto mutual = std::make_shared<devices::MutualInductance>(
        id, name, inductor1_name, inductor2_name, coupling_coefficient);

    devices_.push_back(mutual);
    device_map_[name] = mutual;

    spdlog::debug("Added MutualInductance: {} (L1={}, L2={}, k={})",
                  name, inductor1_name, inductor2_name, coupling_coefficient);

    // Note: Inductor references will be resolved later by resolve_mutual_inductance_references()
    // after all inductors have been added to the circuit
}

//==============================================================================
// Lossless Transmission Line
//==============================================================================

void CircuitGraph::add_tline_lossless(const std::string& name,
                                      const std::string& ap, const std::string& an,
                                      const std::string& bp, const std::string& bn,
                                      Real z0, Real td) {
    DeviceId id = next_device_id_++;
    NodeId ap_id = add_node(ap);
    NodeId an_id = add_node(an);
    NodeId bp_id = add_node(bp);
    NodeId bn_id = add_node(bn);

    auto tline = std::make_shared<devices::TLineLossless>(
        id, name, ap_id, an_id, bp_id, bn_id, z0, td);

    devices_.push_back(tline);
    device_map_[name] = tline;

    spdlog::debug("Added TLineLossless: {} (A={},{}, B={},{}, Z0={} Ω, TD={} s)",
                  name, ap, an, bp, bn, z0, td);
}

//==============================================================================
// Lossy Transmission Line (O-element)
//==============================================================================

void CircuitGraph::add_tline_lossy(const std::string& name,
                                   const std::string& ap, const std::string& an,
                                   const std::string& bp, const std::string& bn,
                                   Real r_per_m, Real l_per_m, Real c_per_m, Real g_per_m,
                                   Real td) {
    DeviceId id = next_device_id_++;
    NodeId ap_id = add_node(ap);
    NodeId an_id = add_node(an);
    NodeId bp_id = add_node(bp);
    NodeId bn_id = add_node(bn);

    auto tline = std::make_shared<devices::TLineLossy>(
        id, name, ap_id, an_id, bp_id, bn_id, r_per_m, l_per_m, c_per_m, g_per_m, td);

    devices_.push_back(tline);
    device_map_[name] = tline;

    spdlog::debug("Added TLineLossy: {} (A={},{}, B={},{}, R={} Ω/m, L={} H/m, C={} F/m, G={} S/m, TD={} s)",
                  name, ap, an, bp, bn, r_per_m, l_per_m, c_per_m, g_per_m, td);
}

//==============================================================================
// Lumped LC Ladder Transmission Line
//==============================================================================

void CircuitGraph::add_tline_lumped(const std::string& name,
                                    const std::string& ap, const std::string& an,
                                    const std::string& bp, const std::string& bn,
                                    Real z0, Real td,
                                    int num_sections) {
    // Validate parameters
    if (num_sections < 1) {
        throw std::runtime_error("T-line lumped model requires at least 1 section: " + name);
    }
    if (z0 <= 0.0) {
        throw std::runtime_error("T-line characteristic impedance must be positive: " + name);
    }
    if (td <= 0.0) {
        throw std::runtime_error("T-line propagation delay must be positive: " + name);
    }

    // Calculate LC values for π-type ladder
    // L_section = Z0 × TD / N
    // C_section = TD / Z0 / N
    Real L_section = z0 * td / static_cast<Real>(num_sections);
    Real C_section = td / z0 / static_cast<Real>(num_sections);

    spdlog::info("Expanding T-line {} into {} LC sections (L={:.6e} H, C={:.6e} F per section)",
                 name, num_sections, L_section, C_section);

    // Build π-type ladder: ap --L-- n1 --L-- n2 ... --L-- bp
    //                           |C     |C         |C
    //                           an     an         an

    std::string current_pos = ap;  // Current positive node
    std::string current_neg = an;  // Current negative node (reference)

    for (int i = 0; i < num_sections; ++i) {
        // Determine next positive node
        std::string next_pos;
        if (i == num_sections - 1) {
            // Last section connects to bp
            next_pos = bp;
        } else {
            // Internal node: {name}_n{i+1}
            next_pos = name + "_n" + std::to_string(i + 1);
        }

        // Add series inductor: current_pos -- L -- next_pos
        std::string L_name = name + "_L" + std::to_string(i + 1);
        add_inductor(L_name, current_pos, next_pos, L_section);

        // Add shunt capacitor at next_pos: next_pos -- C -- current_neg
        // Note: For differential ports, capacitor is between pos and neg terminals
        std::string C_name = name + "_C" + std::to_string(i + 1);
        add_capacitor(C_name, next_pos, current_neg, C_section);

        // Move to next section
        current_pos = next_pos;
    }

    spdlog::debug("Added T-line lumped model: {} ({} sections, Z0={} Ω, TD={} s)",
                  name, num_sections, z0, td);
}

std::uint32_t CircuitGraph::num_devices() const {
    return static_cast<std::uint32_t>(devices_.size());
}

const std::vector<std::shared_ptr<devices::Device>>& CircuitGraph::get_devices() const {
    return devices_;
}

std::shared_ptr<devices::Device> CircuitGraph::get_device(const std::string& name) const {
    auto it = device_map_.find(name);
    if (it == device_map_.end()) {
        throw std::runtime_error(fmt::format("Device not found: {}", name));
    }
    return it->second;
}

void CircuitGraph::resolve_controlled_source_references() {
    // Resolve control voltage source references for CCCS and CCVS
    for (auto& device : devices_) {
        if (device->type() == DeviceType::CCCS) {
            auto cccs = std::dynamic_pointer_cast<devices::CCCS>(device);
            if (!cccs) {
                throw std::runtime_error("Failed to cast CCCS device");
            }

            // Find controlling voltage source
            std::string control_name = cccs->control_vsource_name();
            auto control_device = get_device(control_name);

            // Try VoltageSource first
            auto vsource = std::dynamic_pointer_cast<devices::VoltageSource>(control_device);
            if (vsource) {
                cccs->set_control_branch_id(vsource->branch_id());
                spdlog::debug("CCCS {}: resolved control '{}' → branch_id={}",
                             cccs->name(), control_name, vsource->branch_id());
                continue;
            }

            // Try TimeVaryingVoltageSource
            auto time_vsource = std::dynamic_pointer_cast<devices::TimeVaryingVoltageSource>(control_device);
            if (time_vsource) {
                cccs->set_control_branch_id(time_vsource->branch_id());
                spdlog::debug("CCCS {}: resolved control '{}' → branch_id={}",
                             cccs->name(), control_name, time_vsource->branch_id());
                continue;
            }

            // Neither VoltageSource nor TimeVaryingVoltageSource
            throw std::runtime_error(
                fmt::format("CCCS {}: control device '{}' is not a voltage source",
                           cccs->name(), control_name));
        }

        if (device->type() == DeviceType::CCVS) {
            auto ccvs = std::dynamic_pointer_cast<devices::CCVS>(device);
            if (!ccvs) {
                throw std::runtime_error("Failed to cast CCVS device");
            }

            // Find controlling voltage source
            std::string control_name = ccvs->control_vsource_name();
            auto control_device = get_device(control_name);

            // Try VoltageSource first
            auto vsource = std::dynamic_pointer_cast<devices::VoltageSource>(control_device);
            if (vsource) {
                ccvs->set_control_branch_id(vsource->branch_id());
                spdlog::debug("CCVS {}: resolved control '{}' → branch_id={}",
                             ccvs->name(), control_name, vsource->branch_id());
                continue;
            }

            // Try TimeVaryingVoltageSource
            auto time_vsource = std::dynamic_pointer_cast<devices::TimeVaryingVoltageSource>(control_device);
            if (time_vsource) {
                ccvs->set_control_branch_id(time_vsource->branch_id());
                spdlog::debug("CCVS {}: resolved control '{}' → branch_id={}",
                             ccvs->name(), control_name, time_vsource->branch_id());
                continue;
            }

            // Neither VoltageSource nor TimeVaryingVoltageSource
            throw std::runtime_error(
                fmt::format("CCVS {}: control device '{}' is not a voltage source",
                           ccvs->name(), control_name));
        }
    }

    spdlog::debug("Resolved all controlled source references");
}

void CircuitGraph::resolve_mutual_inductance_references() {
    // Resolve inductor references for MutualInductance devices
    for (auto& device : devices_) {
        if (device->type() == DeviceType::MUTUAL_INDUCTANCE) {
            auto mutual = std::dynamic_pointer_cast<devices::MutualInductance>(device);
            if (!mutual) {
                throw std::runtime_error("Failed to cast MutualInductance device");
            }

            // Find L1 (first inductor)
            std::string l1_name = mutual->inductor1_name();
            auto l1_device = get_device(l1_name);
            if (l1_device->type() != DeviceType::INDUCTOR) {
                throw std::runtime_error(
                    fmt::format("MutualInductance {}: {} is not an inductor",
                               mutual->name(), l1_name));
            }

            // Find L2 (second inductor)
            std::string l2_name = mutual->inductor2_name();
            auto l2_device = get_device(l2_name);
            if (l2_device->type() != DeviceType::INDUCTOR) {
                throw std::runtime_error(
                    fmt::format("MutualInductance {}: {} is not an inductor",
                               mutual->name(), l2_name));
            }

            // Set inductor pointers
            mutual->set_inductors(l1_device.get(), l2_device.get());

            spdlog::debug("MutualInductance {}: resolved L1={}, L2={}, M={:.3e}H",
                         mutual->name(), l1_name, l2_name, mutual->mutual_inductance());
        }
    }

    spdlog::debug("Resolved all mutual inductance references");
}

//==============================================================================
// Subcircuit Management
//==============================================================================

SubcircuitRegistry& CircuitGraph::subcircuits() {
    return *subcircuits_;
}

const SubcircuitRegistry& CircuitGraph::subcircuits() const {
    return *subcircuits_;
}

//==============================================================================
// Subcircuit Expansion
//==============================================================================

void CircuitGraph::expand_subcircuit_instance(const std::string& instance_name) {
    // Find the instance
    const auto& instances = subcircuits_->get_instances();
    auto inst_it = std::find_if(instances.begin(), instances.end(),
        [&](const SubcircuitInstance& inst) {
            return inst.instance_name == instance_name;
        });

    if (inst_it == instances.end()) {
        throw std::runtime_error(fmt::format(
            "Subcircuit instance '{}' not found", instance_name));
    }

    const SubcircuitInstance& instance = *inst_it;

    // Get subcircuit definition
    const SubcircuitDefinition& definition = subcircuits_->get_subcircuit(instance.subcircuit_type);

    // Verify pin count matches
    if (instance.connected_nodes.size() != definition.pins.size()) {
        throw std::runtime_error(fmt::format(
            "Subcircuit instance '{}' has {} pins, but definition '{}' expects {} pins (line {})",
            instance_name, instance.connected_nodes.size(),
            definition.name, definition.pins.size(), instance.instance_line));
    }

    // Create pin mapping: internal_pin_name -> external_node_name
    std::map<std::string, std::string> pin_mapping;
    for (size_t i = 0; i < definition.pins.size(); ++i) {
        pin_mapping[definition.pins[i]] = instance.connected_nodes[i];
    }

    spdlog::debug("Expanding subcircuit instance '{}' of type '{}'",
                  instance_name, definition.name);

    // Perform parameter substitution
    // Merges instance parameters with default parameters and evaluates {expression} patterns
    std::vector<std::string> expanded_lines = definition.substitute_parameters(instance.instance_params);

    // Expand internal netlist
    // For each internal device line, clone the device with hierarchical node names
    for (const auto& line : expanded_lines) {
        if (line.empty() || line[0] == '*') {
            continue;  // Skip empty lines and comments
        }

        // Parse device line and create hierarchical node names
        // Format: <device_name> <node1> <node2> ... <value>
        // Example: R1 in out 1k  ->  X1.R1 X1.in X1.out 1k
        //   But if "in" is a pin, map it to connected node instead

        std::istringstream iss(line);
        std::string device_name, token;
        std::vector<std::string> tokens;

        // Read all tokens
        while (iss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) {
            continue;
        }

        device_name = tokens[0];
        char device_type = std::toupper(device_name[0]);

        // Create hierarchical device name: X1.R1
        std::string hier_device_name = instance_name + "." + device_name;

        // Map node names (replace pins with external nodes, hierarchize internal nodes)
        auto map_node = [&, this](const std::string& node) -> std::string {
            // Check if node is a pin (maps to external connection)
            auto pin_it = pin_mapping.find(node);
            if (pin_it != pin_mapping.end()) {
                return pin_it->second;  // Use external node name
            }

            // Check if node is a global node
            // Global nodes are accessible across subcircuit boundaries without hierarchization
            const auto& global_nodes = subcircuits_->get_global_nodes();
            if (global_nodes.count(node) > 0) {
                spdlog::trace("  Node '{}' is global, not hierarchizing", node);
                return node;  // Use global node name as-is
            }

            // Check if node is ground (case-insensitive)
            std::string node_upper = node;
            std::transform(node_upper.begin(), node_upper.end(), node_upper.begin(), ::toupper);
            if (node_upper == "0" || node_upper == "GND") {
                return "0";  // Ground node
            }

            // Internal node: hierarchize as X1.n1
            return instance_name + "." + node;
        };

        // Expand device based on type
        try {
            if (device_type == 'R' && tokens.size() >= 4) {
                // R<name> <n+> <n-> <value>
                std::string np = map_node(tokens[1]);
                std::string nn = map_node(tokens[2]);
                Real value = parse_value_with_unit(tokens[3]);
                add_resistor(hier_device_name, np, nn, value);
                spdlog::trace("  Expanded: {} {} {} {}", hier_device_name, np, nn, value);

            } else if (device_type == 'C' && tokens.size() >= 4) {
                // C<name> <n+> <n-> <value>
                std::string np = map_node(tokens[1]);
                std::string nn = map_node(tokens[2]);
                Real value = parse_value_with_unit(tokens[3]);
                add_capacitor(hier_device_name, np, nn, value);
                spdlog::trace("  Expanded: {} {} {} {}", hier_device_name, np, nn, value);

            } else if (device_type == 'L' && tokens.size() >= 4) {
                // L<name> <n+> <n-> <value>
                std::string np = map_node(tokens[1]);
                std::string nn = map_node(tokens[2]);
                Real value = parse_value_with_unit(tokens[3]);
                add_inductor(hier_device_name, np, nn, value);
                spdlog::trace("  Expanded: {} {} {} {}", hier_device_name, np, nn, value);

            } else if (device_type == 'V' && tokens.size() >= 4) {
                // V<name> <n+> <n-> DC <value>
                std::string np = map_node(tokens[1]);
                std::string nn = map_node(tokens[2]);
                Real dc_value = 0.0;

                // Check if "DC" keyword present
                size_t value_idx = 3;
                if (tokens.size() > 4 &&
                    (tokens[3] == "DC" || tokens[3] == "dc")) {
                    value_idx = 4;
                }

                if (value_idx < tokens.size()) {
                    dc_value = parse_value_with_unit(tokens[value_idx]);
                }

                add_voltage_source(hier_device_name, np, nn, dc_value);
                spdlog::trace("  Expanded: {} {} {} {}", hier_device_name, np, nn, dc_value);

            } else if (device_type == 'I' && tokens.size() >= 4) {
                // I<name> <n+> <n-> DC <value>
                std::string np = map_node(tokens[1]);
                std::string nn = map_node(tokens[2]);
                Real dc_value = 0.0;

                // Check if "DC" keyword present
                size_t value_idx = 3;
                if (tokens.size() > 4 &&
                    (tokens[3] == "DC" || tokens[3] == "dc")) {
                    value_idx = 4;
                }

                if (value_idx < tokens.size()) {
                    dc_value = parse_value_with_unit(tokens[value_idx]);
                }

                add_current_source(hier_device_name, np, nn, dc_value);
                spdlog::trace("  Expanded: {} {} {} {}", hier_device_name, np, nn, dc_value);

            } else if (device_type == 'M' && tokens.size() >= 6) {
                // M<name> <drain> <gate> <source> <bulk> <model> [W=<width>] [L=<length>]
                std::string drain = map_node(tokens[1]);
                std::string gate = map_node(tokens[2]);
                std::string source = map_node(tokens[3]);
                // tokens[4] is bulk (not used in simple Level 1 MOSFET)
                std::string model = tokens[5];
                std::transform(model.begin(), model.end(), model.begin(), ::toupper);

                // Parse W= and L= parameters
                Real width = 10e-6;  // Default 10 microns
                Real length = 1e-6;  // Default 1 micron
                for (size_t i = 6; i < tokens.size(); ++i) {
                    if (tokens[i].size() > 2 && tokens[i][0] == 'W' && tokens[i][1] == '=') {
                        width = parse_value_with_unit(tokens[i].substr(2));
                    } else if (tokens[i].size() > 2 && tokens[i][0] == 'L' && tokens[i][1] == '=') {
                        length = parse_value_with_unit(tokens[i].substr(2));
                    } else if (tokens[i].size() > 2 && tokens[i][0] == 'w' && tokens[i][1] == '=') {
                        width = parse_value_with_unit(tokens[i].substr(2));
                    } else if (tokens[i].size() > 2 && tokens[i][0] == 'l' && tokens[i][1] == '=') {
                        length = parse_value_with_unit(tokens[i].substr(2));
                    }
                }

                // Look up model parameters from SimulationConfig
                Real kp = 2e-5;    // Default transconductance [A/V^2]
                Real vto = 0.7;    // Default threshold voltage [V] for NMOS
                Real lambda = 0.0; // Default channel-length modulation [1/V]

                if (config_ != nullptr && config_->models.count(model) > 0) {
                    const auto& model_def = config_->models.at(model);
                    const auto& params = model_def.params;

                    // Extract KP parameter (transconductance)
                    if (params.count("kp") > 0) {
                        kp = params.at("kp");
                    } else if (params.count("KP") > 0) {
                        kp = params.at("KP");
                    }

                    // Extract VTO parameter (threshold voltage)
                    if (params.count("vto") > 0) {
                        vto = params.at("vto");
                    } else if (params.count("VTO") > 0) {
                        vto = params.at("VTO");
                    }

                    // Extract LAMBDA parameter (channel-length modulation)
                    if (params.count("lambda") > 0) {
                        lambda = params.at("lambda");
                    } else if (params.count("LAMBDA") > 0) {
                        lambda = params.at("LAMBDA");
                    }

                    spdlog::info("  Model '{}' parameters found: KP={}, VTO={}, LAMBDA={}",
                                model, kp, vto, lambda);
                } else {
                    spdlog::warn("  Model '{}' not found in SimulationConfig, using defaults: KP={}, VTO={}",
                               model, kp, vto);
                }

                // Add MOSFET based on model type
                // Default parameters for Level 1 models
                Real cgso = 0.0;   // Overlap capacitance G-S
                Real cgdo = 0.0;   // Overlap capacitance G-D
                Real cgbo = 0.0;   // Overlap capacitance G-B
                Real kf = 0.0;     // Flicker noise coefficient
                Real af = 1.0;     // Flicker noise exponent
                Real ef = 1.0;     // Flicker noise frequency exponent
                Real gamma = 0.0;  // Body effect parameter
                Real kt1 = 0.0;    // Vth temperature coefficient (linear)
                Real kt2 = 0.0;    // Vth temperature coefficient (quadratic)
                Real ute = -1.5;   // Mobility temperature exponent
                Real tnom = 300.15; // Nominal temperature [K]

                // Determine device type from model or fallback to simple NMOS/PMOS
                bool model_found = false;
                DeviceType device_type_enum = DeviceType::MOSFET_N;  // Default

                if (config_ != nullptr && config_->models.count(model) > 0) {
                    device_type_enum = config_->models.at(model).device_type;
                    model_found = true;
                    spdlog::info("  [Subcircuit expansion] Model '{}' found with device_type={}",
                                model, static_cast<int>(device_type_enum));
                } else if (model == "NMOS") {
                    device_type_enum = DeviceType::MOSFET_N;
                    model_found = true;
                } else if (model == "PMOS") {
                    device_type_enum = DeviceType::MOSFET_P;
                    model_found = true;
                }

                if (model_found) {
                    if (device_type_enum == DeviceType::MOSFET_N) {
                        spdlog::info("  [Subcircuit expansion] Adding MOSFET_N: {} D={} G={} S={} W={} L={} KP={} VTO={}",
                                    hier_device_name, drain, gate, source, width, length, kp, vto);
                        add_mosfet_n(hier_device_name, drain, gate, source,
                                     kp, width, length, vto,
                                     cgso, cgdo, cgbo,
                                     kf, af, ef, gamma,
                                     kt1, kt2, ute, tnom);
                    } else if (device_type_enum == DeviceType::MOSFET_P) {
                        // For PMOS, negate VTO if it's positive
                        Real pmos_vto = (vto > 0) ? -vto : vto;
                        spdlog::info("  [Subcircuit expansion] Adding MOSFET_P: {} D={} G={} S={} W={} L={} KP={} VTO={}",
                                    hier_device_name, drain, gate, source, width, length, kp, pmos_vto);
                        add_mosfet_p(hier_device_name, drain, gate, source,
                                     kp, width, length, pmos_vto,
                                     cgso, cgdo, cgbo,
                                     kf, af, ef, gamma,
                                     kt1, kt2, ute, tnom);
                    } else if (device_type_enum == DeviceType::MOSFET_BSIM4_N ||
                               device_type_enum == DeviceType::MOSFET_BSIM4_P) {
                        // BSIM4 model: create BSIM4Params from model parameters
                        const auto& model_def = config_->models.at(model);
                        const auto& params = model_def.params;

                        devices::BSIM4Params bsim4_params;

                        // Copy all model parameters to BSIM4Params
                        // (Following the same pattern as in netlist_parser.cpp)
                        for (const auto& [key, value] : params) {
                            // Process Parameters
                            if (key == "version") bsim4_params.version = value;
                            else if (key == "toxe") bsim4_params.toxe = value;
                            else if (key == "toxp") bsim4_params.toxp = value;
                            else if (key == "toxm") bsim4_params.toxm = value;
                            else if (key == "tox") bsim4_params.toxe = value;  // Alias
                            else if (key == "vth0") bsim4_params.vth0 = value;
                            else if (key == "k1") bsim4_params.k1 = value;
                            else if (key == "k2") bsim4_params.k2 = value;
                            else if (key == "k3") bsim4_params.k3 = value;
                            else if (key == "u0") bsim4_params.u0 = value;
                            else if (key == "ua") bsim4_params.ua = value;
                            else if (key == "ub") bsim4_params.ub = value;
                            else if (key == "uc") bsim4_params.uc = value;
                            else if (key == "vsat") bsim4_params.vsat = value;
                            else if (key == "a0") bsim4_params.a0 = value;
                            else if (key == "ags") bsim4_params.ags = value;
                            else if (key == "keta") bsim4_params.keta = value;
                            else if (key == "rdsw") bsim4_params.rdsw = value;
                            else if (key == "voff") bsim4_params.voff = value;
                            else if (key == "nfactor") bsim4_params.nfactor = value;
                            else if (key == "eta0") bsim4_params.eta0 = value;
                            else if (key == "dsub") bsim4_params.dsub = value;
                            else if (key == "pclm") bsim4_params.pclm = value;
                            else if (key == "pdiblc1") bsim4_params.pdiblc1 = value;
                            else if (key == "pdiblc2") bsim4_params.pdiblc2 = value;
                            else if (key == "drout") bsim4_params.drout = value;
                            else if (key == "delta") bsim4_params.delta = value;
                            else if (key == "mobmod") bsim4_params.mobmod = static_cast<int>(value);
                            else if (key == "kt1") bsim4_params.kt1 = value;
                            else if (key == "kt2") bsim4_params.kt2 = value;
                            else if (key == "ute") bsim4_params.ute = value;
                            else if (key == "capmod") bsim4_params.capmod = static_cast<int>(value);
                            else if (key == "cgdo") bsim4_params.cgdo = value;
                            else if (key == "cgso") bsim4_params.cgso = value;
                            else if (key == "cgbo") bsim4_params.cgbo = value;

                            // Junction capacitances (BSIM4 has separate source/drain params)
                            else if (key == "cj") {
                                bsim4_params.cjs = value;
                                bsim4_params.cjd = value;
                            }
                            else if (key == "pb") {
                                bsim4_params.pbs = value;
                                bsim4_params.pbd = value;
                            }
                            else if (key == "mj") {
                                bsim4_params.mjs = value;
                                bsim4_params.mjd = value;
                            }
                            else if (key == "cjsw") {
                                bsim4_params.cjsws = value;
                                bsim4_params.cjswd = value;
                            }
                            else if (key == "pbsw") {
                                bsim4_params.pbsws = value;
                                bsim4_params.pbswd = value;
                            }
                            else if (key == "mjsw") {
                                bsim4_params.mjsws = value;
                                bsim4_params.mjswd = value;
                            }
                            else if (key == "tnom") bsim4_params.tnom = value + 273.15;  // Celsius to Kelvin
                            // Add more parameters as needed...
                        }

                        // Override with instance parameters (W, L from tokens)
                        bsim4_params.w = width;
                        bsim4_params.l = length;

                        // Bulk node from tokens[4] (4-terminal MOSFET syntax)
                        // CRITICAL FIX: Use tokens[4] instead of hardcoding bulk=source
                        // This enables vbs != 0 for BSIM4 K1/K2 body effect
                        std::string bulk = map_node(tokens[4]);

                        if (device_type_enum == DeviceType::MOSFET_BSIM4_N) {
                            spdlog::info("  [Subcircuit expansion] Adding MOSFET_BSIM4_N: {} D={} G={} S={} B={} W={} L={}",
                                        hier_device_name, drain, gate, source, bulk, width, length);
                            add_mosfet_bsim4_n(hier_device_name, drain, gate, source, bulk, bsim4_params);
                        } else {
                            spdlog::info("  [Subcircuit expansion] Adding MOSFET_BSIM4_P: {} D={} G={} S={} B={} W={} L={}",
                                        hier_device_name, drain, gate, source, bulk, width, length);
                            add_mosfet_bsim4_p(hier_device_name, drain, gate, source, bulk, bsim4_params);
                        }
                    } else {
                        spdlog::warn("Subcircuit expansion: Unsupported MOSFET device type {} for model '{}' in line: '{}'",
                                   static_cast<int>(device_type_enum), model, line);
                    }
                } else {
                    spdlog::warn("Subcircuit expansion: Unknown MOSFET model '{}' in line: '{}'",
                               model, line);
                }

            } else if (device_type == 'X' && tokens.size() >= 2) {
                // X<name> <node1> <node2> ... <subcircuit_type> [params...]
                // Handle nested subcircuit instances

                // Find subcircuit type (first token without '=', after device name)
                std::string nested_subckt_type;
                std::vector<std::string> nested_nodes;
                std::map<std::string, Real> nested_params;

                for (size_t i = 1; i < tokens.size(); ++i) {
                    if (tokens[i].find('=') != std::string::npos) {
                        // Parameter token: KEY=VALUE
                        size_t eq_pos = tokens[i].find('=');
                        std::string param_name = tokens[i].substr(0, eq_pos);
                        std::string param_value_str = tokens[i].substr(eq_pos + 1);
                        Real param_value = parse_value_with_unit(param_value_str);
                        nested_params[param_name] = param_value;
                    } else if (nested_subckt_type.empty()) {
                        // Check if this token is a defined subcircuit type
                        std::string token_upper = tokens[i];
                        std::transform(token_upper.begin(), token_upper.end(),
                                     token_upper.begin(), ::toupper);

                        if (subcircuits_->has_subcircuit(token_upper)) {
                            // This is the subcircuit type
                            nested_subckt_type = token_upper;
                        } else {
                            // This is a node
                            nested_nodes.push_back(tokens[i]);
                        }
                    } else {
                        // After subcircuit type, only parameters should appear
                        spdlog::warn("Unexpected token '{}' after subcircuit type in nested X device: '{}'",
                                   tokens[i], line);
                    }
                }

                if (nested_subckt_type.empty()) {
                    throw std::runtime_error(fmt::format(
                        "Failed to find subcircuit type in nested X device: '{}'", line));
                }

                // Map nested nodes using pin_mapping (for hierarchical nesting)
                std::vector<std::string> mapped_nested_nodes;
                for (const auto& node : nested_nodes) {
                    mapped_nested_nodes.push_back(map_node(node));
                }

                // Register this nested subcircuit instance
                SubcircuitInstance nested_instance;
                nested_instance.instance_name = hier_device_name;  // X1.X2 format
                nested_instance.subcircuit_type = nested_subckt_type;
                nested_instance.connected_nodes = mapped_nested_nodes;
                nested_instance.instance_params = nested_params;
                nested_instance.instance_line = 0;  // Line number not available here

                subcircuits_->add_instance(nested_instance);

                spdlog::debug("  Registered nested subcircuit instance: {} of type '{}' with {} nodes",
                            hier_device_name, nested_subckt_type, mapped_nested_nodes.size());

            } else {
                spdlog::warn("Subcircuit expansion: Unsupported device type '{}' in line: '{}'",
                           device_type, line);
            }

        } catch (const std::exception& e) {
            throw std::runtime_error(fmt::format(
                "Failed to expand device '{}' in subcircuit instance '{}': {}",
                hier_device_name, instance_name, e.what()));
        }
    }

    spdlog::info("Subcircuit instance '{}' expanded successfully ({} internal devices)",
                 instance_name, definition.internal_netlist_lines.size());
}

void CircuitGraph::expand_all_subcircuits() {
    // Nested subcircuit expansion with iterative approach

    // 1. Check for circular references and get topological sort order
    auto sorted_definitions = subcircuits_->check_circular_references();

    if (!sorted_definitions.empty()) {
        spdlog::debug("Subcircuit expansion order (topological sort): {}",
                      fmt::join(sorted_definitions, " -> "));
    }

    // 2. Get initial instances
    const auto& initial_instances = subcircuits_->get_instances();

    if (initial_instances.empty()) {
        spdlog::debug("No subcircuit instances to expand");
        return;
    }

    spdlog::info("Expanding {} initial subcircuit instance(s)", initial_instances.size());

    // 3. Iterative expansion: expand instances, which may register new nested instances
    std::set<std::string> expanded_instances;
    bool has_new_instances = true;
    int iteration = 0;
    // Use centralized constant from constants.hpp
    constexpr int MAX_ITERATIONS = core::MAX_SUBCIRCUIT_ITERATIONS;

    while (has_new_instances && iteration < MAX_ITERATIONS) {
        has_new_instances = false;

        // Make a copy of the current instances to avoid iterator invalidation
        // (expansion may call add_instance(), causing vector reallocation)
        std::vector<SubcircuitInstance> current_instances = subcircuits_->get_instances();

        spdlog::trace("Expansion iteration {}: {} total instances, {} already expanded",
                     iteration, current_instances.size(), expanded_instances.size());

        for (const auto& instance : current_instances) {
            // Skip if already expanded
            if (expanded_instances.contains(instance.instance_name)) {
                continue;
            }

            // Expand this instance (may register new nested X instances)
            spdlog::debug("Expanding instance '{}' (type: {})",
                         instance.instance_name, instance.subcircuit_type);

            expand_subcircuit_instance(instance.instance_name);
            expanded_instances.insert(instance.instance_name);
            has_new_instances = true;

            // New nested instances may have been added during expansion
        }

        ++iteration;
    }

    if (iteration >= MAX_ITERATIONS) {
        throw std::runtime_error(fmt::format(
            "Subcircuit expansion exceeded maximum iterations ({}). "
            "Check for excessive nesting or circular references.", MAX_ITERATIONS));
    }

    spdlog::info("Subcircuit expansion completed in {} iteration(s), {} instance(s) expanded",
                iteration, expanded_instances.size());
}

} // namespace kirimaspice::circuit
