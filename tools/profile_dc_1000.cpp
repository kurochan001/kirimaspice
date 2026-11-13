// Simple profiling tool to measure parallel DC solve bottleneck
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <spdlog/spdlog.h>
#include <omp.h>
#include <iostream>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

int main(int argc, char** argv) {
    // Set log level to info to see profiling data
    spdlog::set_level(spdlog::level::info);

    int num_threads = 1;
    if (argc > 1) {
        num_threads = std::atoi(argv[1]);
    }

#ifdef _OPENMP
    omp_set_num_threads(num_threads);
    std::cout << "OpenMP threads: " << num_threads << std::endl;
#else
    std::cout << "OpenMP not available" << std::endl;
#endif

    // Build 1000-device circuit (998 diodes)
    std::cout << "Building 1000-device circuit..." << std::endl;
    auto circuit = std::make_unique<circuit::CircuitGraph>();
    circuit->add_voltage_source("V1", "1", "0", 5.0);
    circuit->add_resistor("R1", "1", "2", 100.0);

    for (int i = 1; i <= 998; ++i) {
        std::string d_name = "D" + std::to_string(i);
        std::string n_prev = std::to_string(i + 1);
        std::string n_next = (i == 998) ? "0" : std::to_string(i + 2);
        circuit->add_diode(d_name, n_prev, n_next, 1e-14, 1.0);
    }
    std::cout << "Circuit built: 1000 devices" << std::endl;

    // Solve DC
    std::cout << "\nSolving DC operating point..." << std::endl;
    solvers::DCSolver solver(*circuit);
    DenseVector solution;
    SolverStats stats;

    auto status = solver.solve(solution, stats);

    std::cout << "\nDC solve status: " << (status == ConvergenceStatus::SUCCESS ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "Total iterations: " << stats.total_iterations << std::endl;
    std::cout << "Linear solves: " << stats.linear_solves << std::endl;

    return 0;
}
