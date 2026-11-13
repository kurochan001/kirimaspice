// tests/benchmarks/bench_phase37_device_stamping.cpp
// Benchmark for parallel device stamping in DC and Transient solvers

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>
#include <cstdlib>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;

//==============================================================================
// Helper function to set thread count
//==============================================================================
static void set_omp_threads(int num_threads) {
#ifdef _OPENMP
    omp_set_num_threads(num_threads);
#endif
}

//==============================================================================
// Benchmark: DC analysis with nonlinear devices (diode rectifier chain)
// Device count: 50 (threshold boundary), 100, 200, 500
// Thread count: 1, 2, 4, 8
//==============================================================================

// 50 devices (threshold boundary - should be sequential)
static void BM_DC_Diode50_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    // Create 50 diode rectifiers (50 devices total: 1 voltage source + 1 resistor + 48 diodes)
    for (int i = 1; i <= 48; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 48) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);  // IS=1e-14, N=1.0
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
BENCHMARK(BM_DC_Diode50_1Thread)->Unit(benchmark::kMillisecond);

// 100 devices (above threshold - should use parallel stamping)
static void BM_DC_Diode100_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    // Create 100 diodes
    for (int i = 1; i <= 98; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 98) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode100_1Thread)->Unit(benchmark::kMillisecond);

static void BM_DC_Diode100_2Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(2);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 98; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 98) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode100_2Threads)->Unit(benchmark::kMillisecond);

static void BM_DC_Diode100_4Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(4);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 98; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 98) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode100_4Threads)->Unit(benchmark::kMillisecond);

static void BM_DC_Diode100_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 98; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 98) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode100_8Threads)->Unit(benchmark::kMillisecond);

// 200 devices (well above threshold)
static void BM_DC_Diode200_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 198; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 198) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode200_1Thread)->Unit(benchmark::kMillisecond);

static void BM_DC_Diode200_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 198; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 198) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode200_8Threads)->Unit(benchmark::kMillisecond);

// 500 devices (large circuit)
static void BM_DC_Diode500_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 498; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 498) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode500_1Thread)->Unit(benchmark::kMillisecond);

static void BM_DC_Diode500_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 498; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 498) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode500_8Threads)->Unit(benchmark::kMillisecond);

//==============================================================================
// Benchmark: Transient analysis with dynamic devices (RC circuit)
//==============================================================================

static void BM_Transient_RC100_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // Create 100-device RC ladder (50 resistors + 50 capacitors)
    for (int i = 1; i <= 50; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);   // 1kΩ
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);    // 1µF
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;   // 1ms
    Real timestep = 1e-6; // 1µs

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC100_1Thread)->Unit(benchmark::kMillisecond);

static void BM_Transient_RC100_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 50; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;   // 1ms
    Real timestep = 1e-6; // 1µs

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC100_8Threads)->Unit(benchmark::kMillisecond);

static void BM_Transient_RC200_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // 200-device RC ladder (100 resistors + 100 capacitors)
    for (int i = 1; i <= 100; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;   // 1ms
    Real timestep = 1e-6; // 1µs

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC200_1Thread)->Unit(benchmark::kMillisecond);

static void BM_Transient_RC200_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 100; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;   // 1ms
    Real timestep = 1e-6; // 1µs

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC200_8Threads)->Unit(benchmark::kMillisecond);

//==============================================================================
// Large-scale circuit benchmarks (1000, 2000, 5000 devices)
// Goal: Identify the threshold where parallelization becomes beneficial
//==============================================================================

// 1000 devices (DC analysis with diodes)
static void BM_DC_Diode1000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    // Create 1000 diodes
    for (int i = 1; i <= 998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode1000_1Thread)->Unit(benchmark::kMillisecond);

static void BM_DC_Diode1000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode1000_8Threads)->Unit(benchmark::kMillisecond);

// 2000 devices (DC analysis with diodes)
static void BM_DC_Diode2000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    // Create 2000 diodes
    for (int i = 1; i <= 1998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 1998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode2000_1Thread)->Unit(benchmark::kMillisecond);

static void BM_DC_Diode2000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 1998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 1998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode2000_8Threads)->Unit(benchmark::kMillisecond);

// 1000 devices (Transient analysis with RC circuit)
static void BM_Transient_RC1000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // Create 1000-device RC ladder (500 resistors + 500 capacitors)
    for (int i = 1; i <= 500; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);   // 1kΩ
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);    // 1µF
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;   // 1ms
    Real timestep = 1e-6; // 1µs

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC1000_1Thread)->Unit(benchmark::kMillisecond);

static void BM_Transient_RC1000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 500; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;
    Real timestep = 1e-6;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC1000_8Threads)->Unit(benchmark::kMillisecond);

// 2000 devices (Transient analysis with RC circuit)
static void BM_Transient_RC2000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // Create 2000-device RC ladder (1000 resistors + 1000 capacitors)
    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);   // 1kΩ
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);    // 1µF
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;   // 1ms
    Real timestep = 1e-6; // 1µs

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC2000_1Thread)->Unit(benchmark::kMillisecond);

static void BM_Transient_RC2000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 1000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = (i == 1) ? "1" : std::to_string(i - 1);
        std::string n_curr = std::to_string(i);

        circuit->add_resistor(r_name, n_prev, n_curr, 1e3);
        circuit->add_capacitor(c_name, n_curr, "0", 1e-6);
    }

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.max_iterations = 50;

    Real t_start = 0.0;
    Real t_stop = 1e-3;
    Real timestep = 1e-6;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC2000_8Threads)->Unit(benchmark::kMillisecond);

// ====================================================================
// Ultra-large scale benchmarks (5000 and 10000 devices)
// Goal: Identify the circuit size where parallelization becomes beneficial
// ====================================================================

// DC benchmark: 5000 diodes, 1 thread
static void BM_DC_Diode5000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    // Create 4998 diodes (5000 total devices)
    for (int i = 1; i <= 4998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 4998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode5000_1Thread)->Unit(benchmark::kMillisecond);

// DC benchmark: 5000 diodes, 8 threads
static void BM_DC_Diode5000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 4998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 4998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode5000_8Threads)->Unit(benchmark::kMillisecond);

// DC benchmark: 10000 diodes, 1 thread
static void BM_DC_Diode10000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    // Create 9998 diodes (10000 total devices)
    for (int i = 1; i <= 9998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 9998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode10000_1Thread)->Unit(benchmark::kMillisecond);

// DC benchmark: 10000 diodes, 8 threads
static void BM_DC_Diode10000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 9998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 9998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode10000_8Threads)->Unit(benchmark::kMillisecond);

// Transient benchmark: 5000 RC elements (2500 R + 2500 C), 1 thread
static void BM_Transient_RC5000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // Create 2500 RC pairs (5000 total devices)
    for (int i = 1; i <= 2500; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = std::to_string(i);
        std::string n_next = std::to_string(i + 1);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
        circuit->add_capacitor(c_name, n_next, "0", 1e-9);
    }

    Real t_start = 0.0;
    Real t_stop = 1e-6;  // 1 microsecond
    Real timestep = 1e-9;  // 1 nanosecond

    SimulationConfig config;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.use_adaptive_timestep = false;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC5000_1Thread)->Unit(benchmark::kMillisecond);

// Transient benchmark: 5000 RC elements, 8 threads
static void BM_Transient_RC5000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 2500; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = std::to_string(i);
        std::string n_next = std::to_string(i + 1);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
        circuit->add_capacitor(c_name, n_next, "0", 1e-9);
    }

    Real t_start = 0.0;
    Real t_stop = 1e-6;
    Real timestep = 1e-9;

    SimulationConfig config;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.use_adaptive_timestep = false;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC5000_8Threads)->Unit(benchmark::kMillisecond);

// Transient benchmark: 10000 RC elements (5000 R + 5000 C), 1 thread
static void BM_Transient_RC10000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    // Create 5000 RC pairs (10000 total devices)
    for (int i = 1; i <= 5000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = std::to_string(i);
        std::string n_next = std::to_string(i + 1);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
        circuit->add_capacitor(c_name, n_next, "0", 1e-9);
    }

    Real t_start = 0.0;
    Real t_stop = 1e-6;
    Real timestep = 1e-9;

    SimulationConfig config;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.use_adaptive_timestep = false;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC10000_1Thread)->Unit(benchmark::kMillisecond);

// Transient benchmark: 10000 RC elements, 8 threads
static void BM_Transient_RC10000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 5000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = std::to_string(i);
        std::string n_next = std::to_string(i + 1);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
        circuit->add_capacitor(c_name, n_next, "0", 1e-9);
    }

    Real t_start = 0.0;
    Real t_stop = 1e-6;
    Real timestep = 1e-9;

    SimulationConfig config;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.use_adaptive_timestep = false;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC10000_8Threads)->Unit(benchmark::kMillisecond);

// ====================================================================
// Extended ultra-large scale (20k, 50k, 100k devices)
// Goal: Complete parallelization analysis with full scaling curve
// ====================================================================

// DC 20k devices, 1 thread
static void BM_DC_Diode20000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 19998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 19998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode20000_1Thread)->Unit(benchmark::kMillisecond);

// DC 20k devices, 8 threads
static void BM_DC_Diode20000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 19998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 19998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode20000_8Threads)->Unit(benchmark::kMillisecond);

// DC 50k devices, 1 thread
static void BM_DC_Diode50000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 49998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 49998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode50000_1Thread)->Unit(benchmark::kMillisecond);

// DC 50k devices, 8 threads
static void BM_DC_Diode50000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 49998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 49998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode50000_8Threads)->Unit(benchmark::kMillisecond);

// DC 100k devices, 1 thread
static void BM_DC_Diode100000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 99998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 99998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode100000_1Thread)->Unit(benchmark::kMillisecond);

// DC 100k devices, 8 threads
static void BM_DC_Diode100000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 99998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 99998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
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
BENCHMARK(BM_DC_Diode100000_8Threads)->Unit(benchmark::kMillisecond);

// Transient 20k RC, 1 thread
static void BM_Transient_RC20000_1Thread(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(1);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 10000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = std::to_string(i);
        std::string n_next = std::to_string(i + 1);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
        circuit->add_capacitor(c_name, n_next, "0", 1e-9);
    }

    Real t_start = 0.0;
    Real t_stop = 1e-6;
    Real timestep = 1e-9;

    SimulationConfig config;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.use_adaptive_timestep = false;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC20000_1Thread)->Unit(benchmark::kMillisecond);

// Transient 20k RC, 8 threads
static void BM_Transient_RC20000_8Threads(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    set_omp_threads(8);

    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);

    for (int i = 1; i <= 10000; ++i) {
        std::string r_name = "R" + std::to_string(i);
        std::string c_name = "C" + std::to_string(i);
        std::string n_prev = std::to_string(i);
        std::string n_next = std::to_string(i + 1);
        circuit->add_resistor(r_name, n_prev, n_next, 1000.0);
        circuit->add_capacitor(c_name, n_next, "0", 1e-9);
    }

    Real t_start = 0.0;
    Real t_stop = 1e-6;
    Real timestep = 1e-9;

    SimulationConfig config;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    config.transient.use_adaptive_timestep = false;

    for (auto _ : state) {
        solvers::TransientSolver solver(*circuit, config);
        auto result = solver.solve(t_start, t_stop, timestep);
        benchmark::DoNotOptimize(result);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Transient_RC20000_8Threads)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
