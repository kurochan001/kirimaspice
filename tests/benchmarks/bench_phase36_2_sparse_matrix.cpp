// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/benchmarks/bench_phase36_2_sparse_matrix.cpp
// Benchmark for sparse matrix parallel zero() operations

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>
#include <cstdlib>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;

//==============================================================================
// Helper function to set thread count
// Note: Avoid putenv() as it is not thread-safe
//==============================================================================
static void set_omp_threads(int num_threads) {
#ifdef _OPENMP
    omp_set_num_threads(num_threads);
#endif
}

//==============================================================================
// Benchmark: Large resistor ladder DC (1000 nodes, above threshold)
//==============================================================================
static void BM_LargeResistorLadder_DC_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    // Create 1000-node resistor ladder
    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 1000) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_LargeResistorLadder_DC_1Thread)->Unit(benchmark::kMillisecond);

static void BM_LargeResistorLadder_DC_2Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(2);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 1000) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_LargeResistorLadder_DC_2Threads)->Unit(benchmark::kMillisecond);

static void BM_LargeResistorLadder_DC_4Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(4);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 1000) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_LargeResistorLadder_DC_4Threads)->Unit(benchmark::kMillisecond);

static void BM_LargeResistorLadder_DC_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 1000) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_LargeResistorLadder_DC_8Threads)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Very large resistor ladder DC (2000 nodes, well above threshold)
//==============================================================================
static void BM_VeryLargeResistorLadder_DC_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 2000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 2000) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_VeryLargeResistorLadder_DC_1Thread)->Unit(benchmark::kMillisecond);

static void BM_VeryLargeResistorLadder_DC_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 2000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 2000) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_VeryLargeResistorLadder_DC_8Threads)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Small resistor ladder DC (500 nodes, below threshold)
// Should show no benefit or slight overhead from parallelization check
//==============================================================================
static void BM_SmallResistorLadder_DC_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 500; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 500) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_SmallResistorLadder_DC_1Thread)->Unit(benchmark::kMillisecond);

static void BM_SmallResistorLadder_DC_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 500; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 500) ? "0" : std::to_string(i);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

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
BENCHMARK(BM_SmallResistorLadder_DC_8Threads)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Large RC network AC (1000 nodes, ComplexMNASystem::zero())
//==============================================================================
static void BM_LargeRCNetwork_AC_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 1.0);  // DC=0, AC=1

    // Create 1000-node RC ladder
    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);   // 1kΩ
        circuit->add_capacitor(c_name, n_curr, "0", 1e-9);    // 1nF
    }

    SimulationConfig config;
    config.ac.start_freq = 1e3;   // 1 kHz
    config.ac.stop_freq = 1e6;    // 1 MHz
    config.ac.num_points = 10;    // 10 points per decade
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;

    for (auto _ : state) {
        solvers::ACSolver solver(*circuit, config);
        auto result = solver.solve();
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LargeRCNetwork_AC_1Thread)->Unit(benchmark::kMillisecond);

static void BM_LargeRCNetwork_AC_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 1.0);

    // Create 1000-node RC ladder
    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);
        circuit->add_capacitor(c_name, n_curr, "0", 1e-9);
    }

    SimulationConfig config;
    config.ac.start_freq = 1e3;
    config.ac.stop_freq = 1e6;
    config.ac.num_points = 10;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::DEC;

    for (auto _ : state) {
        solvers::ACSolver solver(*circuit, config);
        auto result = solver.solve();
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LargeRCNetwork_AC_8Threads)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
