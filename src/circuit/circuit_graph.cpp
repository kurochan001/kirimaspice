// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/circuit/circuit_graph.cpp
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/circuit/subcircuit.hpp"
#include "kirimaspice/util/expression_functions.hpp"
#include "kirimaspice/core/types.hpp"  // For SimulationConfig and ModelDefinition
#include "kirimaspice/core/constants.hpp"  // For MAX_SUBCIRCUIT_ITERATIONS
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/devices/capacitor.hpp"
#include "kirimaspice/devices/inductor.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/devices/waveform.hpp"
#include "kirimaspice/devices/current_source.hpp"
#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/bjt_pnp.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"  // Unified MOSFET Level 1
#include "kirimaspice/devices/mosfet_level3.hpp"  // Unified MOSFET Level 3
#include "kirimaspice/devices/mosfet_bsim3.hpp"  // Unified BSIM3 template
#include "kirimaspice/devices/mosfet_bsim1.hpp"    // Unified BSIM1 (Level 9)
#include "kirimaspice/devices/mosfet_bsim4.hpp"  // Unified BSIM4 template
#include "kirimaspice/devices/mosfet_sic.hpp"    // Unified SiC MOSFET
#include "kirimaspice/devices/gan_hemt.hpp"      // Unified GaN HEMT
#include "kirimaspice/devices/finfet.hpp"        // Unified FinFET
#include "kirimaspice/devices/vdmos.hpp"         // Unified VDMOS
#include "kirimaspice/devices/igbt.hpp"          // Unified IGBT
#include "kirimaspice/devices/scr.hpp"           // Unified SCR
#include "kirimaspice/devices/triac.hpp"
#include "kirimaspice/devices/mesfet.hpp"        // Unified MESFET
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
#include <fmt/ranges.h>  // fmt::join
#include <exprtk.hpp>
#include <stdexcept>
#include <algorithm>
#include <set>

#include "kirimaspice/util/string_utils.hpp"
#include "kirimaspice/util/units.hpp"
#include "kirimaspice/util/source_syntax.hpp"

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
    add_node(std::string(NODE_GND_NAME));

    spdlog::debug("CircuitGraph created");
}

CircuitGraph::~CircuitGraph() = default;

// Move constructor and assignment (defined here to allow incomplete SubcircuitRegistry type)
CircuitGraph::CircuitGraph(CircuitGraph&&) noexcept = default;
CircuitGraph& CircuitGraph::operator=(CircuitGraph&&) noexcept = default;

void CircuitGraph::set_simulation_config(const SimulationConfig* config) {
    config_ = config;
    spdlog::debug("SimulationConfig set (subcircuit expansion takes its models from the parser's reader)");
}

//==============================================================================
// Node Management
//==============================================================================
// Node names are compared without case, as SPICE does: "OUT" and "out" are
// one node, kept under its name in lower case (add_node, get_node_id,
// has_node)
static std::string fold_node_name(const std::string& name) {
    std::string folded = name;
    for (char& c : folded) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return folded;
}

NodeId CircuitGraph::add_node(const std::string& written) {
    const std::string name = fold_node_name(written);
    if (is_ground_name(name) && name != NODE_GND_NAME) {
        return NODE_GND;  // gnd in any case is ground
    }

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

NodeId CircuitGraph::get_node_id(const std::string& written) const {
    if (is_ground_name(written)) {
        return NODE_GND;
    }
    const std::string name = fold_node_name(written);
    auto it = node_map_.find(name);
    if (it == node_map_.end()) {
        throw std::runtime_error(fmt::format("Node not found: {}", name));
    }
    return it->second;
}

bool CircuitGraph::has_node(const std::string& name) const {
    return is_ground_name(name) || node_map_.find(fold_node_name(name)) != node_map_.end();
}

std::uint32_t CircuitGraph::num_nodes() const {
    return static_cast<std::uint32_t>(node_map_.size());
}

std::uint32_t CircuitGraph::num_unknowns() const {
    return num_nodes() - 1 + static_cast<std::uint32_t>(voltage_sources_.size());
}

std::string CircuitGraph::unknown_name(std::uint32_t index) const {
    const std::uint32_t nodes = num_nodes() - 1;
    if (index < nodes) {
        const NodeId id = index + 1;
        return "V(" + (id < node_names_.size() ? node_names_[id] : std::to_string(id)) + ")";
    }
    if (index - nodes < voltage_sources_.size()) {
        return "I(" + voltage_sources_[index - nodes]->name() + ")";
    }
    return "unknown " + std::to_string(index);
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
    register_device(name, resistor);

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
                                 Real capacitance,
                                 std::optional<Real> initial_condition) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto capacitor = std::make_shared<devices::Capacitor>(id, name, np_id, nn_id, capacitance);
    if (initial_condition) {
        capacitor->set_initial_condition(*initial_condition);
    }

    register_device(name, capacitor);

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added capacitor: {} {} {} {}F", name, np, nn, capacitance);
}

void CircuitGraph::add_inductor(const std::string& name,
                                const std::string& np,
                                const std::string& nn,
                                Real inductance,
                                std::optional<Real> initial_condition) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;
    auto inductor = std::make_shared<devices::Inductor>(id, name, np_id, nn_id, inductance);
    if (initial_condition) {
        inductor->set_initial_condition(*initial_condition);
    }

    register_device(name, inductor);

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

    register_device(name, vsource);

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

    register_device(name, isource);

    auto [edge, success] = boost::add_edge(np_id, nn_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added current source: {} {} {} IDC={}A AC={}A∠{}°",
                 name, np, nn, dc_value, ac_magnitude, ac_phase);
}

// The ExprTk variable of the k-th node that a B source expression names. A
// number, not the node's name: node names may hold any character, where
// ExprTk names may not.
static std::string node_voltage_variable(std::size_t k) {
    return "node_voltage_" + std::to_string(k);
}

// Rewrite each V(node) of a B source expression, with V in either case, as
// the variable of its node, and each V(n1,n2) as the difference of the
// variables of its two nodes, as SPICE reads it; list the distinct nodes
// named in the order of the variables. This is the only place that reads
// node references in B expressions.
static std::string rewrite_node_voltages(const std::string& expression,
                                         std::vector<std::string>& nodes) {
    std::string out;
    out.reserve(expression.size());
    std::size_t i = 0;
    while (i < expression.size()) {
        const char c = expression[i];
        const bool starts_name = i == 0 ||
            !(std::isalnum(static_cast<unsigned char>(expression[i - 1])) || expression[i - 1] == '_');
        if ((c == 'V' || c == 'v') && starts_name && i + 1 < expression.size() &&
            expression[i + 1] == '(') {
            const std::size_t close = expression.find(')', i + 2);
            if (close != std::string::npos) {
                auto trimmed = [](std::string name) {
                    name.erase(0, name.find_first_not_of(" \t"));
                    name.erase(name.find_last_not_of(" \t") + 1);
                    return name;
                };
                auto variable_of = [&](const std::string& node) {
                    auto it = std::find(nodes.begin(), nodes.end(), node);
                    if (it == nodes.end()) {
                        it = nodes.insert(nodes.end(), node);
                    }
                    return node_voltage_variable(static_cast<std::size_t>(it - nodes.begin()));
                };
                const std::string inside = expression.substr(i + 2, close - i - 2);
                const std::size_t comma = inside.find(',');
                if (comma == std::string::npos) {
                    const std::string node = trimmed(inside);
                    if (!node.empty()) {
                        out += variable_of(node);
                        i = close + 1;
                        continue;
                    }
                } else {
                    const std::string first = trimmed(inside.substr(0, comma));
                    const std::string second = trimmed(inside.substr(comma + 1));
                    if (!first.empty() && !second.empty()) {
                        out += "(" + variable_of(first) + " - " + variable_of(second) + ")";
                        i = close + 1;
                        continue;
                    }
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

void CircuitGraph::add_behavioral_source(const std::string& name,
                                         const std::string& np,
                                         const std::string& nn,
                                         BehavioralSourceType type,
                                         const std::string& expression,
                                         const std::map<std::string, std::string>& functions,
                                         const std::map<std::string, Real>& parameters) {
    NodeId np_id = add_node(np);
    NodeId nn_id = add_node(nn);

    DeviceId id = next_device_id_++;

    std::vector<std::string> referenced_nodes;
    const std::string processed_expr = rewrite_node_voltages(expression, referenced_nodes);

    // The nodes the expression reads. A node that no element names is still
    // a node of the circuit, as in ngspice-47, which reports it as singular.
    std::vector<NodeId> referenced_ids;
    for (const auto& node : referenced_nodes) {
        referenced_ids.push_back(add_node(node));
    }

    // The expression, compiled once; each evaluation sets TIME and the node
    // voltages and evaluates it. An expression that cannot be compiled is an
    // error in the netlist rather than a source of 0 at every evaluation. It
    // may call the .FUNC functions (util::ExpressionFunctions), which live
    // as long as the compiled expression.
    struct Compiled {
        Real time = 0.0;
        std::vector<Real> voltages;
        util::ExpressionFunctions functions;
        exprtk::symbol_table<Real> symbol_table;
        exprtk::expression<Real> expr;
    };
    auto compiled = std::make_shared<Compiled>();
    compiled->voltages.assign(referenced_nodes.size(), 0.0);
    compiled->functions = util::ExpressionFunctions(functions, parameters);
    compiled->symbol_table.add_variable("TIME", compiled->time);
    compiled->symbol_table.add_constants();
    compiled->functions.add_to(compiled->symbol_table);
    for (std::size_t i = 0; i < referenced_nodes.size(); ++i) {
        compiled->symbol_table.add_variable(node_voltage_variable(i), compiled->voltages[i]);
    }
    compiled->expr.register_symbol_table(compiled->symbol_table);
    {
        exprtk::parser<Real> parser;
        if (!parser.compile(util::expand_scale_factors(processed_expr), compiled->expr)) {
            throw std::runtime_error(fmt::format(
                "cannot compile the expression '{}': {}", expression, parser.error()));
        }
    }

    auto evaluator = [compiled, referenced_ids](Real time, const DenseVector& node_voltages) -> Real {
        compiled->time = time;
        for (std::size_t i = 0; i < referenced_ids.size(); ++i) {
            const NodeId node = referenced_ids[i];
            const auto k = static_cast<std::size_t>(node) - 1;
            compiled->voltages[i] = (node == NODE_GND || k >= node_voltages.size())
                                        ? 0.0
                                        : node_voltages[k];
        }
        return compiled->expr.value();
    };

    auto bsource = std::make_shared<devices::BehavioralSource>(
        id, name, np_id, nn_id, type, expression, evaluator, referenced_ids);

    // If voltage source, add to voltage_sources_ and assign branch_id
    if (type == BehavioralSourceType::VOLTAGE) {
        BranchId branch_id = static_cast<BranchId>(voltage_sources_.size());
        bsource->set_branch_id(branch_id);
        voltage_sources_.push_back(bsource);
    }

    register_device(name, bsource);

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
    add_voltage_source_with_waveform(
        name, np, nn, 0.0, false, 0.0, 0.0,
        std::make_shared<devices::SinWaveform>(offset, amplitude, frequency, delay, damping,
                                                phase));
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
    add_voltage_source_with_waveform(
        name, np, nn, 0.0, false, 0.0, 0.0,
        std::make_shared<devices::PulseWaveform>(v1, v2, delay, rise_time, fall_time,
                                                  pulse_width, period));
}

void CircuitGraph::add_pwl_voltage_source(const std::string& name,
                                          const std::string& np,
                                          const std::string& nn,
                                          const std::vector<std::pair<Real, Real>>& points) {
    add_voltage_source_with_waveform(name, np, nn, 0.0, false, 0.0, 0.0,
                                     std::make_shared<devices::PWLWaveform>(points));
}

void CircuitGraph::add_voltage_source_with_waveform(
    const std::string& name, const std::string& np, const std::string& nn, Real dc_value,
    bool dc_given, Real ac_magnitude, Real ac_phase,
    std::shared_ptr<const devices::Waveform> waveform) {
    add_voltage_source(name, np, nn, dc_value, ac_magnitude, ac_phase);
    std::dynamic_pointer_cast<devices::VoltageSource>(get_device(name))
        ->set_waveform(std::move(waveform), dc_given);
}

void CircuitGraph::add_current_source_with_waveform(
    const std::string& name, const std::string& np, const std::string& nn, Real dc_value,
    bool dc_given, Real ac_magnitude, Real ac_phase,
    std::shared_ptr<const devices::Waveform> waveform) {
    add_current_source(name, np, nn, dc_value, ac_magnitude, ac_phase);
    std::dynamic_pointer_cast<devices::CurrentSource>(get_device(name))
        ->set_waveform(std::move(waveform), dc_given);
}

void CircuitGraph::add_diode(const std::string& name,
                             const std::string& anode,
                             const std::string& cathode,
                             Real is,
                             Real n,
                             Real cj0,
                             Real vj,
                             Real m) {
    NodeId anode_id = add_node(anode);
    NodeId cathode_id = add_node(cathode);

    DeviceId id = next_device_id_++;
    auto diode = std::make_shared<devices::Diode>(id, name, anode_id, cathode_id, is, n, cj0, vj, m);

    register_device(name, diode);

    auto [edge, success] = boost::add_edge(anode_id, cathode_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }

    spdlog::debug("Added diode: {} {} {} Is={:.3e}A N={:.3f}", name, anode, cathode, is, n);
}

void CircuitGraph::add_bjt(const std::string& name, const std::string& collector,
                           const std::string& base, const std::string& emitter, bool pnp,
                           const devices::BJTParams& p) {
    const NodeId collector_id = add_node(collector);
    const NodeId base_id = add_node(base);
    const NodeId emitter_id = add_node(emitter);
    const DeviceId id = next_device_id_++;
    std::shared_ptr<devices::Device> bjt;
    if (pnp) {
        bjt = std::make_shared<devices::BJT_PNP>(
            id, name, collector_id, base_id, emitter_id, p.is, p.beta_f, p.nf, p.isc, p.beta_r,
            p.nr, p.vaf, p.var, p.ikf, p.ikr, p.rb, p.rbm, p.irb, p.ise, p.ne, p.isc_leak, p.nc,
            p.cje, p.vje, p.mje, p.cjc, p.vjc, p.mjc, p.tf, p.tr, p.kf, p.af, p.ff, p.xti, p.xtb,
            p.eg, p.tnom);
    } else {
        bjt = std::make_shared<devices::BJT_NPN>(
            id, name, collector_id, base_id, emitter_id, p.is, p.beta_f, p.nf, p.isc, p.beta_r,
            p.nr, p.vaf, p.var, p.ikf, p.ikr, p.rb, p.rbm, p.irb, p.ise, p.ne, p.isc_leak, p.nc,
            p.cje, p.vje, p.mje, p.cjc, p.vjc, p.mjc, p.tf, p.tr, p.kf, p.af, p.ff, p.xti, p.xtb,
            p.eg, p.tnom);
    }
    register_device(name, bjt);
    auto [edge, success] = boost::add_edge(collector_id, emitter_id, graph_);
    if (success) {
        graph_[edge].device_id = id;
    }
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

    register_device(name, bjt);

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

    register_device(name, bjt);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, mosfet);

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

    register_device(name, hemt);

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

    register_device(name, hemt);

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
                                 Real kf, Real af, Real kt1, Real tnom, Real l) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto finfet = std::make_shared<devices::FinFET_N>(
        id, name, drain_id, gate_id, source_id,
        nfin, hfin, tfin, vth, u0, vsat, ss, dibl, tox,
        cgs, cgd, kf, af, kt1, tnom, l);

    register_device(name, finfet);

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
                                 Real kf, Real af, Real kt1, Real tnom, Real l) {
    NodeId drain_id = add_node(drain);
    NodeId gate_id = add_node(gate);
    NodeId source_id = add_node(source);

    DeviceId id = next_device_id_++;
    auto finfet = std::make_shared<devices::FinFET_P>(
        id, name, drain_id, gate_id, source_id,
        nfin, hfin, tfin, vth, u0, vsat, ss, dibl, tox,
        cgs, cgd, kf, af, kt1, tnom, l);

    register_device(name, finfet);

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

    register_device(name, vdmos);

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

    register_device(name, vdmos);

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

    register_device(name, igbt);

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

    register_device(name, igbt);

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

    register_device(name, scr);

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

    register_device(name, scr);

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

    register_device(name, triac);

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
    register_device(name, mesfet);

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
    register_device(name, mesfet);

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

    register_device(name, vcvs);

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

    register_device(name, cccs);

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

    register_device(name, vccs);

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

    register_device(name, ccvs);

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

    register_device(name, mutual);

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

    register_device(name, tline);

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

    register_device(name, tline);

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

namespace {

// The key of an element name in the circuit's map: element names are not
// case-sensitive, as in SPICE (node names are)
std::string device_key(const std::string& name) {
    return util::to_upper_copy(name);
}

}  // namespace

void CircuitGraph::register_device(const std::string& name,
                                   const std::shared_ptr<devices::Device>& device) {
    if (device_map_.contains(device_key(name))) {
        throw std::runtime_error(fmt::format("Element '{}' is already defined", name));
    }
    devices_.push_back(device);
    device_map_.emplace(device_key(name), device);
}

std::uint32_t CircuitGraph::num_devices() const {
    return static_cast<std::uint32_t>(devices_.size());
}

const std::vector<std::shared_ptr<devices::Device>>& CircuitGraph::get_devices() const {
    return devices_;
}

std::shared_ptr<devices::Device> CircuitGraph::get_device(const std::string& name) const {
    auto it = device_map_.find(device_key(name));
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

            // Not a voltage source
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

            // Not a voltage source
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

    // Inductors that two or more K lines couple among themselves form one
    // group, whose inductance matrix is inverted as a whole
    // (MutualInductance::CoupledGroup): the inductors joined by K lines,
    // found by union-find, in the order the K lines name them
    std::vector<std::shared_ptr<devices::MutualInductance>> mutuals;
    for (auto& device : devices_) {
        if (device->type() == DeviceType::MUTUAL_INDUCTANCE) {
            mutuals.push_back(std::dynamic_pointer_cast<devices::MutualInductance>(device));
        }
    }
    std::vector<const devices::Device*> inductors;
    std::map<const devices::Device*, std::size_t> index_of;
    auto index = [&](const devices::Device* d) {
        auto [it, inserted] = index_of.try_emplace(d, inductors.size());
        if (inserted) inductors.push_back(d);
        return it->second;
    };
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (const auto& k : mutuals) {
        pairs.emplace_back(index(get_device(k->inductor1_name()).get()),
                           index(get_device(k->inductor2_name()).get()));
    }
    std::vector<std::size_t> parent(inductors.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    std::function<std::size_t(std::size_t)> find = [&](std::size_t i) {
        return parent[i] == i ? i : (parent[i] = find(parent[i]));
    };
    for (const auto& [a, b] : pairs) parent[find(a)] = find(b);
    std::map<std::size_t, std::vector<std::size_t>> k_of_root;
    for (std::size_t m = 0; m < mutuals.size(); ++m) k_of_root[find(pairs[m].first)].push_back(m);
    for (const auto& [root, ks] : k_of_root) {
        if (ks.size() < 2) {
            mutuals[ks.front()]->clear_group();  // a pair keeps its own stamps
            continue;
        }
        auto group = std::make_shared<devices::MutualInductance::CoupledGroup>();
        std::map<std::size_t, std::size_t> local;
        auto local_index = [&](std::size_t global) {
            auto [it, inserted] = local.try_emplace(global, group->inductors.size());
            if (inserted) group->inductors.push_back(inductors[global]);
            return it->second;
        };
        for (const std::size_t m : ks) {
            const std::size_t i = local_index(pairs[m].first);
            const std::size_t j = local_index(pairs[m].second);
            group->couplings.push_back({i, j, mutuals[m].get()});
        }
        for (std::size_t n = 0; n < ks.size(); ++n) {
            mutuals[ks[n]]->set_group(group, n == 0);
        }
        spdlog::debug("Coupled inductors: {} K lines couple {} inductors, stamped by {}", ks.size(),
                      group->inductors.size(), mutuals[ks.front()]->name());
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

    // Create pin mapping: internal_pin_name -> external_node_name. Node
    // names are compared without case (fold_node_name), as in add_node, so
    // a pin or a .GLOBAL node matches the body however either is written
    std::map<std::string, std::string> pin_mapping;
    for (size_t i = 0; i < definition.pins.size(); ++i) {
        pin_mapping[fold_node_name(definition.pins[i])] = instance.connected_nodes[i];
    }
    std::set<std::string> global_nodes;
    for (const auto& global : subcircuits_->get_global_nodes()) {
        global_nodes.insert(fold_node_name(global));
    }

    spdlog::debug("Expanding subcircuit instance '{}' of type '{}'",
                  instance_name, definition.name);

    if (!subcircuit_line_reader_) {
        throw std::runtime_error(fmt::format(
            "Subcircuit instance '{}': no reader for subcircuit bodies (the netlist parser sets one)",
            instance_name));
    }

    // Perform parameter substitution
    // Merges instance parameters with default parameters and evaluates {expression} patterns
    std::vector<SubstitutedLine> expanded_lines = definition.substitute_parameters(
        instance.instance_params, subcircuit_line_reader_->global_params(),
        subcircuit_line_reader_->global_functions());

    // The circuit's node for a node of the body: a pin's external node, a
    // global node, ground, or the node of this instance, X1.n1
    auto map_node = [&](const std::string& node) -> std::string {
        const std::string folded = fold_node_name(node);
        auto pin_it = pin_mapping.find(folded);
        if (pin_it != pin_mapping.end()) {
            return pin_it->second;
        }
        if (global_nodes.count(folded) > 0) {
            return node;
        }
        if (is_ground_name(node)) {
            return std::string(NODE_GND_NAME);
        }
        return instance_name + "." + node;
    };

    // Each element line of the body, as the parser reads a line of the top
    // level (SubcircuitLineReader)
    for (const auto& [line, parameters] : expanded_lines) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '*') {
            continue;
        }
        subcircuit_line_reader_->read(*this, line, definition.name, instance_name, map_node,
                                      definition.definition_line, parameters);
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

    // The controlling sources of F and H and the inductors of K that the
    // expansion added, as the parser resolves those of the top level
    resolve_controlled_source_references();
    resolve_mutual_inductance_references();
}

} // namespace kirimaspice::circuit
