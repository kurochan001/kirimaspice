// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Newton's iteration has converged only where Kirchhoff's current law holds
// with the devices' own currents, not merely where the step has become small
// (kirchhoff_current_law_holds, DCSolver::check_convergence). A device whose
// conductance is not the derivative of its current makes the steps small far
// from the solution: the IGBT below, whose output conductance below VCE = 0
// was KP Vov^2 / VCE_SAT = 100 S while its current there is 0, once
// "converged" at V(c) = -1404.7 V, where RL carries 15 A into the collector
// and the IGBT none (its solution is V(c) = 100/10001 V, IGBTNetlist tests).

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::solvers;

namespace {

struct OperatingPoint {
    ConvergenceStatus status;
    DenseVector solution;
    parser::NetlistParseResult parsed;
};

OperatingPoint solve(const std::string& netlist) {
    parser::NetlistParser parser;
    OperatingPoint op{ConvergenceStatus::MAX_ITERATIONS, {}, parser.parse_string(netlist)};
    DCSolver solver(*op.parsed.circuit, op.parsed.config);
    SolverStats stats;
    op.status = solver.solve(op.solution, stats);
    return op;
}

Real voltage(const OperatingPoint& op, const std::string& node) {
    return op.solution[op.parsed.circuit->get_node_id(node) - 1];
}

}  // namespace

// The collector current of the IGBT is never negative, so a solution has
// V(c) <= VCC; one below 0 V has 15 A in RL and nothing in the IGBT. The
// solver may fail to find the operating point, but it must not report one
// where KCL does not hold.
TEST(DcConvergenceKcl, NoOperatingPointWhereKclFails) {
    const auto op = solve(
        "igbt\nVCC vcc 0 DC 100\nRL vcc c 100\nVG g 0 DC 15\nM1 c g 0 m\n"
        ".model m nmos level=21\n.end\n");
    ASSERT_NE(op.parsed.circuit, nullptr);
    if (op.status == ConvergenceStatus::SUCCESS) {
        EXPECT_GE(voltage(op, "c"), 0.0);
        EXPECT_LE(voltage(op, "c"), 100.0);
    }
}

// Where the iteration converges, KCL holds to RELTOL: a diode fed from 5 V
// through 1 kOhm carries IS (exp(V/Vt) - 1) = (5 - V) / 1k.
TEST(DcConvergenceKcl, ConvergedDiodeMeetsKcl) {
    const auto op = solve("diode\nV1 a 0 DC 5\nR1 a d 1k\nD1 d 0 dm\n.model dm D IS=1e-14\n.end\n");
    ASSERT_EQ(op.status, ConvergenceStatus::SUCCESS);
    const Real vd = voltage(op, "d");
    const Real vt = 1.380649e-23 * 300.15 / 1.602176634e-19;
    const Real i_diode = 1e-14 * (std::exp(vd / vt) - 1.0);
    const Real i_resistor = (5.0 - vd) / 1e3;
    EXPECT_NEAR(i_diode, i_resistor, 1e-3 * i_resistor);
}
