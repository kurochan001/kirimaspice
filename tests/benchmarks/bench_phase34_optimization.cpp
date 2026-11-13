// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/benchmarks/bench_phase34_optimization.cpp
// Benchmark for optimizations (memory alignment + prefetch hints)

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;

//==============================================================================
// Benchmark: Transient analysis with many refactorize() calls
// This tests the effect of prefetch hints in pattern verification
//==============================================================================
static void BM_Transient_ManyRefactorize(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);

    // Create RC ladder circuit (50 nodes)
    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 50; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);   // 1kΩ
        circuit->add_capacitor(c_name, n_curr, "0", 1e-9);    // 1nF
    }

    SimulationConfig config;
    config.transient.start_time = 0.0;
    config.transient.end_time = 5e-6;  // 5μs (many time steps)
    config.transient.max_step = 1e-9;  // 1ns (small steps → many refactorize())
    config.transient.initial_step = 1e-12;
    config.transient.method = IntegrationMethod::GEAR2;  // More stable for long simulation (BDF2)
    config.transient.use_adaptive_timestep = true;

    // Benchmark transient analysis
    // Each iteration involves 5000+ refactorize() calls (pattern verification)
    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(config.transient.start_time,
                                    config.transient.end_time,
                                    config.transient.max_step);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_ManyRefactorize)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Large matrix CSC conversion
// This tests the effect of memory alignment
//==============================================================================
static void BM_CSC_Conversion_LargeMatrix(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);

    // Create large resistor mesh (100x100 = 10,000 nodes, ~20,000 resistors)
    auto circuit = std::make_unique<circuit::CircuitGraph>();

    const int SIZE = 100;

    // Voltage source at corner
    circuit->add_voltage_source("V1", "n_0_0", "0", 10.0);

    // Horizontal resistors
    for (int row = 0; row < SIZE; ++row) {
        for (int col = 0; col < SIZE - 1; ++col) {
            std::string n1 = "n_" + std::to_string(row) + "_" + std::to_string(col);
            std::string n2 = "n_" + std::to_string(row) + "_" + std::to_string(col + 1);
            std::string r_name = "Rh_" + std::to_string(row) + "_" + std::to_string(col);
            circuit->add_resistor(r_name, n1, n2, 100.0);
        }
    }

    // Vertical resistors
    for (int row = 0; row < SIZE - 1; ++row) {
        for (int col = 0; col < SIZE; ++col) {
            std::string n1 = "n_" + std::to_string(row) + "_" + std::to_string(col);
            std::string n2 = "n_" + std::to_string(row + 1) + "_" + std::to_string(col);
            std::string r_name = "Rv_" + std::to_string(row) + "_" + std::to_string(col);
            circuit->add_resistor(r_name, n1, n2, 100.0);
        }
    }

    // Ground at opposite corner
    circuit->add_resistor("Rgnd", "n_" + std::to_string(SIZE - 1) + "_" + std::to_string(SIZE - 1), "0", 1.0);

    // Benchmark DC analysis (includes multiple CSC conversions)
    for (auto _ : state) {
        solvers::DCSolver solver(*circuit);
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);
        benchmark::DoNotOptimize(status);
        benchmark::DoNotOptimize(solution);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CSC_Conversion_LargeMatrix)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Repeated pattern verification (stress test)
//==============================================================================
static void BM_PatternVerification_Stress(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);

    // Create nonlinear circuit (diodes) to force many Newton-Raphson iterations
    auto circuit = std::make_unique<circuit::CircuitGraph>();

    // Diode chain (50 diodes)
    circuit->add_voltage_source("V1", "1", "0", 10.0);

    for (int i = 1; i <= 50; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 100.0);
        circuit->add_diode(d_name, n_curr, "0",
                          1e-14,  // IS
                          1.7);   // N (high value to force more NR iterations)
    }

    SimulationConfig config;
    config.dc.max_iterations = 100;  // Allow more iterations
    config.dc.enable_voltage_limiting = true;

    // Benchmark DC analysis (nonlinear → many NR iterations → many pattern verifications)
    for (auto _ : state) {
        solvers::DCSolver solver(*circuit, config);
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);
        benchmark::DoNotOptimize(status);
        benchmark::DoNotOptimize(solution);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PatternVerification_Stress)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
