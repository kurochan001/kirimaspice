// include/kirimaspice/circuit/subcircuit.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace kirimaspice::circuit {

/// Forward declaration
class CircuitGraph;

/// Subcircuit definition (.SUBCKT ... .ENDS)
/// Contains the internal circuit structure and interface pins
struct SubcircuitDefinition {
    /// Subcircuit name
    std::string name;

    /// Interface pins (ordered list of node names)
    /// Example: .SUBCKT OPAMP inp inn out vcc vee
    /// pins = {"inp", "inn", "out", "vcc", "vee"}
    std::vector<std::string> pins;

    /// Default parameters
    /// Example: .SUBCKT BUFFER in out GAIN=10 OFFSET=0
    /// default_params = {{"GAIN", 10.0}, {"OFFSET", 0.0}}
    std::map<std::string, Real> default_params;

    /// Internal circuit definition (as raw netlist lines)
    /// Stored as lines to allow deferred parsing with parameter substitution
    std::vector<std::string> internal_netlist_lines;

    /// Line number where .SUBCKT was defined (for error messages)
    std::size_t definition_line = 0;

    /// Constructor
    SubcircuitDefinition() = default;

    SubcircuitDefinition(const std::string& subckt_name,
                        const std::vector<std::string>& interface_pins,
                        std::size_t line_num = 0)
        : name(subckt_name)
        , pins(interface_pins)
        , definition_line(line_num) {}

    /// Check if subcircuit has a given parameter
    bool has_parameter(const std::string& param_name) const {
        return default_params.find(param_name) != default_params.end();
    }

    /// Get parameter value (with default if not found)
    Real get_parameter(const std::string& param_name, Real fallback = 0.0) const {
        auto it = default_params.find(param_name);
        return (it != default_params.end()) ? it->second : fallback;
    }

    /// Substitute parameters in internal netlist lines
    /// Merges instance parameters with default parameters and evaluates expressions
    /// @param instance_params Parameters from instance (e.g., X1 ... GAIN=20)
    /// @return Netlist lines with parameter expressions replaced by values
    std::vector<std::string> substitute_parameters(
        const std::map<std::string, Real>& instance_params) const;
};

/// Subcircuit instance (X device)
/// Represents a usage of a subcircuit in the main circuit
struct SubcircuitInstance {
    /// Instance name (e.g., "X1")
    std::string instance_name;

    /// Subcircuit type name (e.g., "OPAMP")
    std::string subcircuit_type;

    /// Connected nodes (external connections)
    /// Example: X1 signal gnd output vdd vss OPAMP
    /// connected_nodes = {"signal", "gnd", "output", "vdd", "vss"}
    std::vector<std::string> connected_nodes;

    /// Instance parameters (overrides default parameters)
    /// Example: X1 in out BUFFER GAIN=20 OFFSET=0.1
    /// instance_params = {{"GAIN", 20.0}, {"OFFSET", 0.1}}
    std::map<std::string, Real> instance_params;

    /// Line number where instance was defined (for error messages)
    std::size_t instance_line = 0;

    /// Constructor
    SubcircuitInstance() = default;

    SubcircuitInstance(const std::string& inst_name,
                      const std::string& subckt_type,
                      const std::vector<std::string>& nodes,
                      std::size_t line_num = 0)
        : instance_name(inst_name)
        , subcircuit_type(subckt_type)
        , connected_nodes(nodes)
        , instance_line(line_num) {}
};

/// Subcircuit registry
/// Manages all defined subcircuits and their instances
class SubcircuitRegistry {
public:
    SubcircuitRegistry() = default;

    /// Register a new subcircuit definition
    /// @throws std::runtime_error if subcircuit already exists
    void register_subcircuit(const SubcircuitDefinition& def);

    /// Check if a subcircuit is defined
    bool has_subcircuit(const std::string& name) const;

    /// Get subcircuit definition
    /// @throws std::runtime_error if not found
    const SubcircuitDefinition& get_subcircuit(const std::string& name) const;

    /// Add a subcircuit instance
    void add_instance(const SubcircuitInstance& instance);

    /// Get all instances
    const std::vector<SubcircuitInstance>& get_instances() const {
        return instances_;
    }

    /// Get all subcircuit definitions
    const std::map<std::string, SubcircuitDefinition>& get_definitions() const {
        return definitions_;
    }

    /// Detect circular references in subcircuit hierarchy
    /// @throws std::runtime_error if circular reference detected
    /// @return Ordered list of subcircuit names (dependency order, leaves first)
    std::vector<std::string> check_circular_references() const;

    /// Clear all definitions and instances
    void clear() {
        definitions_.clear();
        instances_.clear();
        global_nodes_.clear();
    }

    // Global nodes support
    /// Set global nodes from .GLOBAL directive
    void set_global_nodes(const std::set<std::string>& nodes) {
        global_nodes_ = nodes;
    }

    /// Get global nodes set
    const std::set<std::string>& get_global_nodes() const {
        return global_nodes_;
    }

private:
    /// Subcircuit definitions (key: subcircuit name in UPPER CASE)
    std::map<std::string, SubcircuitDefinition> definitions_;

    /// Subcircuit instances (X devices)
    std::vector<SubcircuitInstance> instances_;

    /// Global nodes - accessible across subcircuit boundaries
    std::set<std::string> global_nodes_;

    /// Helper: Depth-first search for topological sort + circular reference detection
    /// Post-order DFS: nodes are added to result after all dependencies are visited
    /// @param subckt_name Current subcircuit being visited
    /// @param visited Visit status (0=unvisited, 1=visiting, 2=visited)
    /// @param dependencies Dependency graph (subcircuit -> list of used subcircuits)
    /// @param result Topological sort result (post-order, will be reversed later)
    /// @param path Current DFS path (for circular reference error messages)
    /// @param depth Current recursion depth (for stack overflow prevention)
    void dfs_topological_sort(
        const std::string& subckt_name,
        std::map<std::string, int>& visited,
        const std::map<std::string, std::vector<std::string>>& dependencies,
        std::vector<std::string>& result,
        std::vector<std::string>& path,
        int depth = 0) const;
};

} // namespace kirimaspice::circuit
