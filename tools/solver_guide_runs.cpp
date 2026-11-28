// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tools/solver_guide_runs.cpp
// The library runs that docs/SOLVER_ALGORITHMS.md quotes in "Measure cost and
// accuracy on the same RC circuit": the RC charging example to 10 ms through
// TransientSolver with GEAR2, with fixed steps of 1 us and 15 us and with the
// adaptive path (adaptive_order, max_order 2, a 1 us first step, a 100 us
// ceiling, default tolerances).
//
// Each line "run <name> ..." gives the accepted steps, the Newton iterations,
// the LTE rejections and the largest error of v(out) against
// 1 - exp(-t / 1 ms), after linear interpolation onto a 1 us grid. The lines
// "time <t>" give the accepted times of the adaptive run, which
// tools/solver_figures.py draws. No times are measured here.

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

constexpr const char* kNetlist =
    "RC charging from an initial voltage\n"
    "V1 in 0 1\n"
    "R1 in out 1k\n"
    "C1 out 0 1u\n"
    ".IC V(in)=1 V(out)=0\n"
    ".TRAN 1u 10m 0 100u UIC\n"
    ".END\n";

constexpr Real kStop = 10e-3;
constexpr Real kTau = 1e-3;

struct Run {
    solvers::TransientResult result;
    NodeId out = NODE_GND;
};

Run run(bool adaptive, Real step) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(kNetlist);
    parsed.circuit->expand_all_subcircuits();
    auto config = parsed.config;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.use_adaptive_timestep = adaptive;
    if (adaptive) {
        config.transient.adaptive_order = true;
        config.transient.max_order = 2;
        config.transient.initial_step = 1e-6;
        config.transient.max_step = 1e-4;
    } else {
        config.transient.max_step = step;
    }
    solvers::TransientSolver solver(*parsed.circuit, config);
    return {solver.solve(0.0, kStop, step), parsed.circuit->get_node_id("out")};
}

/// The largest |v(out) - (1 - exp(-t / tau))| on a 1 us grid, with v(out)
/// interpolated linearly between the accepted points
Real max_error(const Run& r) {
    const auto& points = r.result.timepoints;
    Real worst = 0.0;
    std::size_t k = 0;
    for (int i = 0; i <= 10000; ++i) {
        const Real t = i * 1e-6;
        while (k + 1 < points.size() && points[k + 1].time < t) {
            ++k;
        }
        const std::size_t j = std::min(k + 1, points.size() - 1);
        const Real t0 = points[k].time;
        const Real t1 = points[j].time;
        const Real v0 = points[k].node_voltages.at(r.out - 1);
        const Real v1 = points[j].node_voltages.at(r.out - 1);
        const Real v = (t1 > t0) ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v0;
        worst = std::max(worst, std::abs(v - (1.0 - std::exp(-t / kTau))));
    }
    return worst;
}

}  // namespace

int main() {
    spdlog::set_level(spdlog::level::off);
    struct Case {
        const char* name;
        bool adaptive;
        Real step;
    };
    const Case cases[] = {
        {"fixed_1u", false, 1e-6}, {"fixed_15u", false, 15e-6}, {"adaptive", true, 1e-6}};
    for (const auto& c : cases) {
        const Run r = run(c.adaptive, c.step);
        if (r.result.status != ConvergenceStatus::SUCCESS || r.result.timepoints.empty()) {
            std::cerr << "solver_guide_runs: the run " << c.name << " failed\n";
            return 1;
        }
        std::cout << "run " << c.name << " accepted=" << r.result.timepoints.size() - 1
                  << " newton=" << r.result.stats.total_iterations
                  << " rejects=" << r.result.stats.timestep_rejects
                  << " max_error_mV=" << std::setprecision(6) << max_error(r) * 1e3 << "\n";
        if (c.adaptive) {
            for (const auto& point : r.result.timepoints) {
                std::cout << "time " << std::setprecision(12) << point.time << "\n";
            }
        }
    }
    return 0;
}
