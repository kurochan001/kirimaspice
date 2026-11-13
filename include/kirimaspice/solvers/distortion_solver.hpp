// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/distortion_solver.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

#include <vector>
#include <string>

namespace kirimaspice {

/// Distortion analysis solver
/// Computes harmonic and intermodulation distortion from AC analysis results
class DistortionSolver {
public:
    /// Perform distortion analysis
    /// @param circuit Circuit graph
    /// @param config Simulation configuration (must have AC and distortion enabled)
    /// @param ac_freqs Frequency points from AC analysis
    /// @param ac_results Complex voltage/current results from AC analysis
    /// @return Distortion analysis result
    static DistortionResult analyze(const circuit::CircuitGraph& circuit,
                                     const SimulationConfig& config,
                                     const std::vector<Real>& ac_freqs,
                                     const std::vector<std::vector<std::complex<Real>>>& ac_results);

    /// Print distortion analysis result to console
    /// @param result Distortion result to print
    static void print_result(const DistortionResult& result);

private:
    /// Compute harmonic distortion from AC sweep data
    /// @param fundamental_freq Fundamental frequency [Hz]
    /// @param ac_freqs Frequency points from AC analysis
    /// @param ac_mags Magnitude data at each frequency
    /// @param num_harmonics Number of harmonics to analyze (default: 9)
    /// @return Harmonic distortion components
    static std::vector<HarmonicComponent> compute_harmonics(
        Real fundamental_freq,
        const std::vector<Real>& ac_freqs,
        const std::vector<Real>& ac_mags,
        std::uint32_t num_harmonics = 9);

    /// Compute total harmonic distortion (THD)
    /// @param harmonics Harmonic components
    /// @return THD percentage
    static Real compute_thd(const std::vector<HarmonicComponent>& harmonics);

    /// Compute intermodulation distortion
    /// @param f1 First tone frequency [Hz]
    /// @param f2 Second tone frequency [Hz]
    /// @param ac_freqs Frequency points from AC analysis
    /// @param ac_mags Magnitude data at each frequency
    /// @return IMD result structure
    static Real compute_imd(
        Real f1,
        Real f2,
        const std::vector<Real>& ac_freqs,
        const std::vector<Real>& ac_mags);

    /// Find magnitude at a specific frequency from AC data
    /// @param target_freq Target frequency [Hz]
    /// @param ac_freqs Frequency points from AC analysis
    /// @param ac_mags Magnitude data
    /// @return Magnitude at target frequency (interpolated if needed)
    static Real find_magnitude_at_frequency(
        Real target_freq,
        const std::vector<Real>& ac_freqs,
        const std::vector<Real>& ac_mags);
};

} // namespace kirimaspice
