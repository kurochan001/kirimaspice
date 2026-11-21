// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/noise_solver.hpp
#pragma once

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <map>
#include <string>
#include <vector>

namespace kirimaspice {
namespace solvers {

/**
 * @brief Noise analysis result
 *
 * Contains frequency-domain noise analysis results including:
 * - Output noise at specified output node
 * - Input-referred noise (output noise divided by gain)
 * - Per-device noise contributions
 */
struct NoiseResult {
    ConvergenceStatus status;                                ///< Solver status
    std::vector<Real> frequency_points;                      ///< Frequency points (Hz)
    std::vector<Real> output_noise;                          ///< Output noise (V/√Hz)
    std::vector<Real> input_referred_noise;                  ///< Input-referred noise (V/√Hz)
    std::map<std::string, std::vector<Real>> contributions;  ///< Per-device contributions (V²/Hz)
};

/**
 * @brief Noise Solver for circuit noise analysis
 *
 * Implements AC-based noise analysis:
 * 1. Compute DC operating point (using DCSolver)
 * 2. Perform AC analysis to get transfer functions (using ACSolver)
 * 3. Collect noise sources from all devices
 * 4. Compute noise transfer function from each source to output
 * 5. Calculate total output noise and input-referred noise
 *
 * Supported noise types:
 * - Thermal noise: 4kTR (resistors)
 * - Shot noise: 2qI (diodes, BJTs)
 * - Flicker noise: K_f * I^α / f^β (BJTs, MOSFETs)
 */
class NoiseSolver {
public:
    /**
     * @brief Construct noise solver
     * @param circuit Circuit to analyze
     * @param config Simulation configuration
     */
    NoiseSolver(circuit::CircuitGraph& circuit, const SimulationConfig& config);

    /**
     * @brief Run noise analysis
     * @param input_node Input node for input-referred noise calculation
     * @param output_node Output node for noise measurement
     * @return Noise analysis result
     *
     * Performs:
     * 1. DC operating point calculation
     * 2. AC analysis over specified frequency range
     * 3. Noise source collection from devices
     * 4. Transfer function calculation for each noise source
     * 5. Total output noise integration
     * 6. Input-referred noise calculation (output_noise / voltage_gain)
     */
    NoiseResult solve(NodeId input_node, NodeId output_node);

    /**
     * @brief Get DC solution (for debugging)
     * @return DC node voltages
     */
    const DenseVector& get_dc_solution() const { return dc_solution_; }

private:
    /**
     * @brief Collect noise sources from all devices
     * @param frequency Current frequency point (Hz)
     * @return Vector of noise sources
     *
     * Calls device->get_noise_sources() for each device at current DC operating point
     */
    std::vector<NoiseSource> collect_noise_sources(Real frequency);

    /**
     * @brief Calculate noise transfer function from source to output
     * @param source Noise source
     * @param ac_solution AC solution vector at current frequency
     * @param output_node Output node for noise measurement
     * @return Transfer function magnitude squared (V²/V² or V²/A²)
     *
     * For voltage noise source: H(jω) = V_out / V_noise
     * For current noise source: H(jω) = V_out / I_noise
     */
    Real calculate_transfer_function(const NoiseSource& source,
                                      const ComplexDenseVector& ac_solution,
                                      NodeId output_node);

    /**
     * @brief Calculate voltage gain from input to output
     * @param input_node Input node
     * @param output_node Output node
     * @param lambda Adjoint solution vector (Λ)
     * @return Voltage gain magnitude (|V_out / V_in|)
     */
    Real calculate_voltage_gain(NodeId input_node,
                                NodeId output_node,
                                const ComplexDenseVector& lambda);

    /**
     * @brief Generate frequency points for sweep
     * @return Vector of frequencies (Hz)
     */
    std::vector<Real> generate_frequency_points() const;

    /**
     * @brief Count devices that need branch variables
     * @return Number of branch devices (voltage sources, VCVS, CCVS)
     */
    std::uint32_t count_branch_devices() const;

    /**
     * @brief Solve adjoint system Y^H × Λ = rhs
     * @param Y Admittance matrix
     * @param rhs Right-hand side vector (e_out)
     * @return Adjoint solution vector Λ
     *
     * Uses Hermitian transpose (conjugate transpose) of Y
     */
    ComplexDenseVector solve_adjoint_system(const ComplexSparseMatrix& Y,
                                             const ComplexDenseVector& rhs);

    // Circuit and configuration
    circuit::CircuitGraph& circuit_;  ///< Circuit to analyze
    SimulationConfig config_;         ///< Simulation configuration
    bool noise_detail_enabled_ = false; ///< Detailed per-source logging

    // DC operating point
    DenseVector dc_solution_;        ///< DC node voltages and branch currents

    // Physical constants
    static constexpr Real k_B = 1.380649e-23;  ///< Boltzmann constant (J/K)
    static constexpr Real q_e = 1.602176634e-19; ///< Elementary charge (C)
    static constexpr Real T_nominal = 300.15;   ///< Nominal temperature (K, 27°C)
};

}  // namespace solvers
}  // namespace kirimaspice
