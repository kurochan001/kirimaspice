// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/analysis/monte_carlo_gpu.cpp
#include "kirimaspice/analysis/monte_carlo_gpu.hpp"
#include "kirimaspice/analysis/monte_carlo_executor.hpp"
#include <spdlog/spdlog.h>

namespace kirimaspice::analysis {

MonteCarloResult MonteCarloBatchExecutor::execute_dc_monte_carlo_gpu(
    const SimulationConfig& base_config,
    const std::string& netlist_path,
    const std::string& output_node,
    Real spec_min,
    Real spec_max,
    std::uint32_t batch_size
) {
    (void)batch_size; // PoC: batching reserved for CUDA build

    // PoC fallback: use fast single-parse path.
    MonteCarloExecutor exec(base_config);
    spdlog::info("[MC-GPU] Using fast CPU path (CUDA off). batch_size={} (reserved)", batch_size);
    return exec.execute_dc_monte_carlo_fast(netlist_path, output_node, spec_min, spec_max);
}

} // namespace kirimaspice::analysis

