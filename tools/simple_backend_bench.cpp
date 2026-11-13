#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace kirimaspice;

static std::string rc_ladder(int stages) {
    std::string nl;
    nl += "* RC ladder AC\n";
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

int main(int argc, char** argv) {
    int stages = (argc > 1) ? std::atoi(argv[1]) : 50;
    int reps   = (argc > 2) ? std::atoi(argv[2]) : 50;
    const char* backend = std::getenv("KIRIMASPICE_SOLVER_BACKEND");
    const char* csv = std::getenv("KIRIMASPICE_BENCH_CSV");
    if (!backend) backend = "KLU";

    parser::NetlistParser p;
    auto parsed = p.parse_string(rc_ladder(stages));

    solvers::set_cli_backend_override(std::string(backend));

    double total_ms = 0.0;
    for (int i = 0; i < reps; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        solvers::ACSolver ac(*parsed.circuit, parsed.config);
        auto res = ac.solve();
        if (res.status != ConvergenceStatus::SUCCESS) {
            std::cerr << "AC solve failed at iter " << i << "\n";
            return 1;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        total_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    double avg_ms = total_ms / reps;
    std::cout << "backend=" << backend << ", stages=" << stages << ", reps=" << reps
              << ", avg_ms=" << avg_ms << "\n";
    if (csv) {
        std::ofstream ofs(csv, std::ios::app);
        if (ofs.tellp() == 0) ofs << "backend,stages,ms\n";
        ofs << backend << "," << stages << "," << avg_ms << "\n";
    }
    return 0;
}

