// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/benchmarks/bench_large_circuit.cpp
// Performance benchmark for large circuits

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;

//==============================================================================
// Benchmark: Large resistor network (300 nodes)
//==============================================================================
static void BM_LargeResistorNetwork_DC(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);  // Disable logging for benchmarks

    // Create 300-node resistor ladder network
    // V1 -- R1 -- n1 -- R2 -- n2 -- R3 -- ... -- R299 -- n299 -- R300 -- GND
    auto circuit = std::make_unique<circuit::CircuitGraph>();

    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // Create 300 resistors
    for (int i = 1; i <= 300; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_next = (i == 300) ? "0" : std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
    }

    // Benchmark DC analysis
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
BENCHMARK(BM_LargeResistorNetwork_DC)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Large RC network transient (100 nodes)
//==============================================================================
static void BM_LargeRCNetwork_Transient(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);

    // Create 100-node RC ladder network
    auto circuit = std::make_unique<circuit::CircuitGraph>();

    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // Create 100 RC stages
    for (int i = 1; i <= 100; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);   // 1kΩ
        circuit->add_capacitor(c_name, n_curr, "0", 1e-9);    // 1nF
    }

    SimulationConfig config;
    config.transient.start_time = 0.0;
    config.transient.end_time = 1e-6;  // 1μs
    config.transient.max_step = 1e-9;  // 1ns
    config.transient.initial_step = 1e-12;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.use_adaptive_timestep = true;

    // Benchmark transient analysis
    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(config.transient.start_time,
                                    config.transient.end_time,
                                    config.transient.max_step);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LargeRCNetwork_Transient)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: BJT amplifier chain (50 stages)
//==============================================================================
static void BM_BJTAmplifierChain_DC(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);

    auto circuit = std::make_unique<circuit::CircuitGraph>();

    // Power supply
    circuit->add_voltage_source("Vcc", "vcc", "0", 12.0);
    circuit->add_voltage_source("Vin", "in", "0", 0.7);

    // Create 50-stage BJT amplifier chain
    for (int i = 1; i <= 50; ++i) {
        std::string base = (i == 1) ? "in" : "out" + std::to_string(i - 1);
        std::string collector = "col" + std::to_string(i);
        std::string emitter = "em" + std::to_string(i);
        std::string out = "out" + std::to_string(i);

        // BJT common-emitter stage
        circuit->add_bjt_npn("Q" + std::to_string(i), collector, base, emitter,
                             1e-14, 100.0, 1.0, 0.7);  // IS, BF, BR, VJE

        // Collector resistor
        circuit->add_resistor("Rc" + std::to_string(i), "vcc", collector, 1e3);

        // Emitter resistor
        circuit->add_resistor("Re" + std::to_string(i), emitter, "0", 100.0);

        // Coupling capacitor (treated as open for DC)
        circuit->add_resistor("Rout" + std::to_string(i), collector, out, 10e3);
    }

    SimulationConfig config;
    config.dc.enable_source_stepping = true;
    config.dc.enable_gmin_stepping = true;

    // Benchmark DC analysis
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
BENCHMARK(BM_BJTAmplifierChain_DC)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Device stamp performance (1000 resistors)
//==============================================================================
static void BM_DeviceStamp_1000Resistors(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);

    // Create circuit with 1000 resistors in parallel
    auto circuit = std::make_unique<circuit::CircuitGraph>();

    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 1000; ++i) {
        circuit->add_resistor("R" + std::to_string(i), "1", "0", 1000.0 + i);
    }

    // Benchmark DC analysis (focuses on MNA stamping)
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
BENCHMARK(BM_DeviceStamp_1000Resistors)->Unit(benchmark::kMicrosecond);

//==============================================================================
// Benchmark: MNA matrix assembly (sparse matrix operations)
//==============================================================================
static void BM_MNAAssembly_LargeCircuit(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);

    // Create 500-node mesh network (stress test for MNA)
    // Grid: 20x25 resistor mesh
    auto circuit = std::make_unique<circuit::CircuitGraph>();

    const int ROWS = 20;
    const int COLS = 25;

    // Voltage source at top-left corner
    circuit->add_voltage_source("V1", "n_0_0", "0", 10.0);

    // Horizontal resistors
    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS - 1; ++col) {
            std::string n1 = "n_" + std::to_string(row) + "_" + std::to_string(col);
            std::string n2 = "n_" + std::to_string(row) + "_" + std::to_string(col + 1);
            std::string r_name = "Rh_" + std::to_string(row) + "_" + std::to_string(col);
            circuit->add_resistor(r_name, n1, n2, 100.0);
        }
    }

    // Vertical resistors
    for (int row = 0; row < ROWS - 1; ++row) {
        for (int col = 0; col < COLS; ++col) {
            std::string n1 = "n_" + std::to_string(row) + "_" + std::to_string(col);
            std::string n2 = "n_" + std::to_string(row + 1) + "_" + std::to_string(col);
            std::string r_name = "Rv_" + std::to_string(row) + "_" + std::to_string(col);
            circuit->add_resistor(r_name, n1, n2, 100.0);
        }
    }

    // Ground connections at corners
    circuit->add_resistor("Rgnd1", "n_0_" + std::to_string(COLS - 1), "0", 1.0);
    circuit->add_resistor("Rgnd2", "n_" + std::to_string(ROWS - 1) + "_0", "0", 1.0);
    circuit->add_resistor("Rgnd3", "n_" + std::to_string(ROWS - 1) + "_" + std::to_string(COLS - 1), "0", 1.0);

    // Benchmark DC analysis (500 nodes, ~1000 resistors)
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
BENCHMARK(BM_MNAAssembly_LargeCircuit)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
