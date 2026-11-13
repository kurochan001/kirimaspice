// include/kirimaspice/analysis/monte_carlo_gpu.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include <string>

namespace kirimaspice::analysis {

/// Monte Carlo GPU/Batch executor (PoC stub)
/// - Provides a GPU-friendly entry point.
/// - Falls back to the fast CPU path when CUDA is not enabled.
class MonteCarloBatchExecutor {
public:
    /// Execute DC Monte Carlo using batched strategy.
    /// If built with CUDA support, this entry point can be wired to CUDA streams.
    /// Otherwise, it falls back to MonteCarloExecutor::execute_dc_monte_carlo_fast().
    static MonteCarloResult execute_dc_monte_carlo_gpu(
        const SimulationConfig& base_config,
        const std::string& netlist_path,
        const std::string& output_node,
        Real spec_min = -1e9,
        Real spec_max = 1e9,
        std::uint32_t batch_size = 1
    );
};

} // namespace kirimaspice::analysis

