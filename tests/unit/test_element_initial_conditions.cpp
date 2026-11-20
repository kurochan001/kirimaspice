// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// IC= on a capacitor or inductor line is the element's voltage or current
// at the start of a transient under UIC, and is not used without UIC
// (SPICE; Capacitor/Inductor::apply_initial_condition, called by
// TransientSolver::initialize_uic_mode). A capacitor of 1 uF charged to
// 2 V and discharged through 1 kohm gives 2 exp(-t/1 ms); an inductor of
// 1 mH carrying 0.5 A into 10 ohm gives 0.5 exp(-t/0.1 ms) and a node
// voltage of -5 exp(-t/0.1 ms). ngspice-47 gives 1.80968 V at 0.1 ms and
// 0.183939 A at 0.1 ms for these two (run on 2026-11-21).

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

// The largest |v(node) - expected(t)| over the points after t = 0 (the
// point at t = 0 holds the initial node voltages, all 0 without .IC)
double worst_error(const std::string& netlist, const std::string& node, double t_stop, double h,
                   double (*expected)(double)) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    parsed.circuit->expand_all_subcircuits();
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, t_stop, h);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId id = parsed.circuit->get_node_id(node);
    double worst = 0.0;
    for (const auto& tp : result.timepoints) {
        if (tp.time > 0.0) {
            worst = std::max(worst, std::abs(tp.node_voltages[id - 1] - expected(tp.time)));
        }
    }
    return worst;
}

double rc_discharge(double t) { return 2.0 * std::exp(-t / 1e-3); }
double rl_decay(double t) { return -5.0 * std::exp(-t / 1e-4); }
double zero(double) { return 0.0; }

}  // namespace

// The trapezoidal rule starts with one backward-Euler step, whose local
// error, (h/tau)^2 / 2 of the voltage, the later steps carry; (h/tau)^2 V0
// bounds it with the trapezoidal steps' own error
TEST(ElementInitialConditions, CapacitorUnderUic) {
    const double bound = (10e-6 / 1e-3) * (10e-6 / 1e-3) * 2.0;
    EXPECT_LE(worst_error("t\nR1 out 0 1k\nC1 out 0 1u IC=2\n.tran 10u 1m UIC\n.end\n", "out", 1e-3,
                          10e-6, rc_discharge),
              bound);
}

TEST(ElementInitialConditions, InductorUnderUic) {
    const double bound = (1e-6 / 1e-4) * (1e-6 / 1e-4) * 5.0;
    EXPECT_LE(worst_error("t\nR1 a 0 10\nL1 a 0 1m IC=0.5\n.tran 1u 0.5m UIC\n.end\n", "a", 0.5e-3,
                          1e-6, rl_decay),
              bound);
}

TEST(ElementInitialConditions, NotUsedWithoutUic) {
    // The operating point of a lone RC is 0 V, and stays so
    EXPECT_LE(worst_error("t\nR1 out 0 1k\nC1 out 0 1u IC=2\n.tran 10u 1m\n.end\n", "out", 1e-3,
                          10e-6, zero),
              1e-12);
}

TEST(ElementInitialConditions, InsideASubcircuit) {
    const double bound = (10e-6 / 1e-3) * (10e-6 / 1e-3) * 2.0;
    EXPECT_LE(worst_error("t\n.subckt cell n\nC1 n 0 1u IC=2\n.ends\nR1 out 0 1k\nX1 out cell\n"
                          ".tran 10u 1m UIC\n.end\n",
                          "out", 1e-3, 10e-6, rc_discharge),
              bound);
}

TEST(ElementInitialConditions, OtherTokensAreNamedInAWarning) {
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("ic", sink);
    logger->set_level(spdlog::level::warn);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    parser::NetlistParser parser;
    parser.parse_string("t\nR1 out 0 1k\nC1 out 0 1u TC1=0.01\n.end\n");
    spdlog::set_default_logger(previous);
    EXPECT_NE(captured.str().find("C1: 'TC1=0.01' is not used"), std::string::npos) << captured.str();
}
