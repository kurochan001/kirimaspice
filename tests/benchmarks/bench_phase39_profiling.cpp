// Detailed profiling to identify parallelization bottleneck
#include <benchmark/benchmark.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <omp.h>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// Helper to set OpenMP threads
static void set_omp_threads(int n) {
#ifdef _OPENMP
    omp_set_num_threads(n);
#else
    (void)n;
#endif
}

// Profiling DC solve with 1000 diodes (1 thread)
static void BM_Profile_DC1000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    // Build circuit (1000 diodes)
    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
    }

    // Profile multiple sections
    double total_init_time = 0.0;
    double total_solve_time = 0.0;
    double total_cleanup_time = 0.0;

    for (auto _ : state) {
        auto t0 = std::chrono::high_resolution_clock::now();

        // Create solver
        solvers::DCSolver solver(*circuit);

        auto t1 = std::chrono::high_resolution_clock::now();

        // Solve
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);

        auto t2 = std::chrono::high_resolution_clock::now();

        // Cleanup (destructors)
        benchmark::DoNotOptimize(status);
        benchmark::DoNotOptimize(solution);

        auto t3 = std::chrono::high_resolution_clock::now();

        // Accumulate timings
        total_init_time += std::chrono::duration<double>(t1 - t0).count();
        total_solve_time += std::chrono::duration<double>(t2 - t1).count();
        total_cleanup_time += std::chrono::duration<double>(t3 - t2).count();
    }

    state.counters["init_ms"] = benchmark::Counter(total_init_time * 1000.0 / state.iterations(), benchmark::Counter::kAvgIterations);
    state.counters["solve_ms"] = benchmark::Counter(total_solve_time * 1000.0 / state.iterations(), benchmark::Counter::kAvgIterations);
    state.counters["cleanup_ms"] = benchmark::Counter(total_cleanup_time * 1000.0 / state.iterations(), benchmark::Counter::kAvgIterations);

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Profile_DC1000_1Thread)->Unit(benchmark::kMillisecond);

// Profiling DC solve with 1000 diodes (8 threads)
static void BM_Profile_DC1000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    // Build circuit (1000 diodes)
    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
    }

    // Profile multiple sections
    double total_init_time = 0.0;
    double total_solve_time = 0.0;
    double total_cleanup_time = 0.0;

    for (auto _ : state) {
        auto t0 = std::chrono::high_resolution_clock::now();

        // Create solver
        solvers::DCSolver solver(*circuit);

        auto t1 = std::chrono::high_resolution_clock::now();

        // Solve
        DenseVector solution;
        SolverStats stats;
        auto status = solver.solve(solution, stats);

        auto t2 = std::chrono::high_resolution_clock::now();

        // Cleanup (destructors)
        benchmark::DoNotOptimize(status);
        benchmark::DoNotOptimize(solution);

        auto t3 = std::chrono::high_resolution_clock::now();

        // Accumulate timings
        total_init_time += std::chrono::duration<double>(t1 - t0).count();
        total_solve_time += std::chrono::duration<double>(t2 - t1).count();
        total_cleanup_time += std::chrono::duration<double>(t3 - t2).count();
    }

    state.counters["init_ms"] = benchmark::Counter(total_init_time * 1000.0 / state.iterations(), benchmark::Counter::kAvgIterations);
    state.counters["solve_ms"] = benchmark::Counter(total_solve_time * 1000.0 / state.iterations(), benchmark::Counter::kAvgIterations);
    state.counters["cleanup_ms"] = benchmark::Counter(total_cleanup_time * 1000.0 / state.iterations(), benchmark::Counter::kAvgIterations);

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Profile_DC1000_8Threads)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
