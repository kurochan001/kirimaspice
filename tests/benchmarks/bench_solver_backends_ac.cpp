// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/benchmarks/bench_solver_backends_ac.cpp
// AC analysis performance of the KLU backend

#include <benchmark/benchmark.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <fstream>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"

using namespace kirimaspice;

static std::string rc_mesh_netlist(int stages) {
    // Build an n-stage RC ladder for AC benchmarking
    std::string nl;
    nl += "* RC ladder AC benchmark\n";
    nl += "V1 in 0 DC 0 AC 1\n";
    for (int i = 1; i <= stages; ++i) {
        std::string a = (i == 1) ? "in" : ("n" + std::to_string(i-1));
        std::string b = (i == stages) ? "out" : ("n" + std::to_string(i));
        nl += "R" + std::to_string(i) + " " + a + " " + b + " 1k\n";
        nl += "C" + std::to_string(i) + " " + b + " 0 1n\n";
    }
    nl += ".ac dec 10 1e3 1e6\n.end\n";
    return nl;
}

static void BM_AC_KLU(benchmark::State& state) {
    spdlog::set_level(spdlog::level::off);
    parser::NetlistParser p;
    auto parsed = p.parse_string(rc_mesh_netlist(static_cast<int>(state.range(0))));
    solvers::set_cli_backend_override(std::make_optional<std::string>("KLU"));
    for (auto _ : state) {
        auto t0 = std::chrono::high_resolution_clock::now();
        solvers::ACSolver ac(*parsed.circuit, parsed.config);
        auto r = ac.solve();
        auto t1 = std::chrono::high_resolution_clock::now();
        benchmark::DoNotOptimize(r.status);
        if (const char* csv = std::getenv("KIRIMASPICE_BENCH_CSV")) {
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::ofstream ofs(csv, std::ios::app);
            if (ofs.tellp() == 0) ofs << "backend,stages,ms\n";
            ofs << "KLU," << state.range(0) << "," << ms << "\n";
        }
    }
}
BENCHMARK(BM_AC_KLU)->Arg(50)->Arg(100);

