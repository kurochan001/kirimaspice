// include/kirimaspice/solvers/pole_zero_solver.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

#include <vector>
#include <string>

namespace kirimaspice {

/// Pole-Zero solver
/// Computes poles and zeros of a transfer function using AC analysis
class PoleZeroSolver {
public:
    /// Perform pole-zero analysis
    /// @param circuit Circuit graph
    /// @param config Simulation configuration with .PZ settings
    /// @return Pole-zero analysis result
    static PoleZeroResult analyze(const circuit::CircuitGraph& circuit,
                                   const SimulationConfig& config);

    /// Print pole-zero analysis result to console
    /// @param result Pole-zero analysis result
    static void print_result(const PoleZeroResult& result);

private:
    /// Compute poles of the transfer function
    /// Uses AC analysis at multiple frequencies to find denominator roots
    /// @param circuit Circuit graph
    /// @param node1 Output node 1
    /// @param node2 Output node 2 (often ground)
    /// @param input_source Input voltage source name
    /// @return Vector of poles
    static std::vector<ComplexPoleZero> compute_poles(
        const circuit::CircuitGraph& circuit,
        const std::string& node1,
        const std::string& node2,
        const std::string& input_source);

    /// Compute zeros of the transfer function
    /// Uses AC analysis to find numerator roots
    /// @param circuit Circuit graph
    /// @param node1 Output node 1
    /// @param node2 Output node 2 (often ground)
    /// @param input_source Input voltage source name
    /// @return Vector of zeros
    static std::vector<ComplexPoleZero> compute_zeros(
        const circuit::CircuitGraph& circuit,
        const std::string& node1,
        const std::string& node2,
        const std::string& input_source);

    /// Compute DC gain H(0)
    /// @param circuit Circuit graph
    /// @param config Simulation configuration (gmin, temperature, etc.)
    /// @param node1 Output node 1
    /// @param node2 Output node 2 (often ground)
    /// @param input_source Input voltage source name
    /// @return DC gain
    static Real compute_dc_gain(
        const circuit::CircuitGraph& circuit,
        const SimulationConfig& config,
        const std::string& node1,
        const std::string& node2,
        const std::string& input_source);

    /// Calculate natural frequency and damping ratio from complex pole/zero
    /// @param pz Complex pole or zero (will be updated with frequency and damping)
    static void calculate_frequency_damping(ComplexPoleZero& pz);
};

} // namespace kirimaspice
