// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tools/gen_mutual_wave.cpp
// Generate coupled-inductor step-response waveforms with multiple integration methods.

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <fmt/core.h>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::solvers;

namespace {

std::string make_netlist(double k_coupling) {
    return fmt::format(R"(
Mutual transformer step (validation)
V1 in 0 DC 0 PULSE(0 1 0 0.1n 0.1n 5n 10n)
L1 in n1 1u
Rpri n1 0 1
L2 out 0 1u
K1 L1 L2 {0:.3f}
Rload out 0 50
.TRAN 0.1n 20n
.END
)", k_coupling);
}

bool run_method(const std::string& name,
                IntegrationMethod method,
                Real dt,
                Real tstop,
                const std::filesystem::path& out_path,
                double k_coupling)
{
    NetlistParser parser;
    auto parsed = parser.parse_string(make_netlist(k_coupling));
    auto& circuit = parsed.circuit;

    SimulationConfig cfg = parsed.config;
    cfg.transient.use_adaptive_timestep = false;
    cfg.transient.initial_step = dt;
    cfg.transient.max_step = dt;
    cfg.transient.min_step = dt;
    cfg.transient.method = method;
    cfg.transient.adaptive_order = false;

    TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, tstop, dt);
    if (result.status != ConvergenceStatus::SUCCESS) {
        std::cerr << "[gen_mutual_wave] " << name
                  << " failed (status=" << static_cast<int>(result.status) << ")\n";
        return false;
    }

    NodeId n_out = circuit->get_node_id("out");
    NodeId n_in = circuit->get_node_id("in");
    std::ofstream ofs(out_path);
    if (!ofs) {
        std::cerr << "[gen_mutual_wave] cannot open " << out_path << "\n";
        return false;
    }
    for (const auto& tp : result.timepoints) {
        ofs << tp.time << " " << tp.node_voltages[n_out - 1] << "\n";
    }
    if (!result.timepoints.empty()) {
        const auto& tp0 = result.timepoints.front();
        Real v_in0 = tp0.node_voltages[n_in - 1];
        Real v_out0 = tp0.node_voltages[n_out - 1];
        Real v_in1 = v_in0;
        Real v_out1 = v_out0;
        if (result.timepoints.size() > 1) {
            v_in1 = result.timepoints[1].node_voltages[n_in - 1];
            v_out1 = result.timepoints[1].node_voltages[n_out - 1];
        }
        std::cout << "[gen_mutual_wave] t0 v(in)=" << v_in0 << " v(out)=" << v_out0
                  << " | t1 v(in)=" << v_in1 << " v(out)=" << v_out1 << "\n";
    }
    std::cout << "[gen_mutual_wave] wrote " << out_path << " (" << result.timepoints.size()
              << " points)\n";
    return true;
}

} // namespace

int main() {
    using std::filesystem::path;
    path out_dir = "validation";
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        std::cerr << "[gen_mutual_wave] failed to create validation directory: " << ec.message() << "\n";
        return 1;
    }

    const Real dt = 1e-10;
    const Real tstop = 20e-9;

    struct Case {
        std::string name;
        IntegrationMethod method;
    };
    std::vector<Case> cases = {
        {"xfmr_step_kirimaspice_trbdf2", IntegrationMethod::TR_BDF2},
        {"xfmr_step_kirimaspice_gear2",  IntegrationMethod::GEAR2},
        {"xfmr_step_kirimaspice_gear3",  IntegrationMethod::GEAR3},
        {"xfmr_step_kirimaspice_trap",   IntegrationMethod::TRAPEZOIDAL},
    };

    std::vector<std::pair<std::string,double>> couplings = {
        {"",       0.99},
        {"_k097",  0.97},
        {"_k095",  0.95},
        {"_k090",  0.90},
    };

    bool ok = true;
    for (const auto& kv : couplings) {
        for (const auto& c : cases) {
            path out = out_dir / (c.name + kv.first + ".txt");
            ok &= run_method(out.filename().string(), c.method, dt, tstop, out, kv.second);
        }
    }

    return ok ? 0 : 2;
}
