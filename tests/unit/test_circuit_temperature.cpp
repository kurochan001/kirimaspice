// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// SPICE evaluates every device at the circuit temperature, 27 C when the
// netlist gives no .TEMP or .OPTIONS TEMP, whatever the TNOM of its card.
// Every analysis applies it at its start (apply_temperature_to_devices from
// DCSolver::solve, DCSolver::solve_sweep and TransientSolver::solve; AC and
// noise through DCSolver::solve).
//
// The deck: Vcc vcc 0 2; Rc vcc c 10k; Vb b 0 DC 0.7 AC 1; Q1 c b 0 qm;
// .model qm npn is=1e-15 bf=100 tnom=50. ngspice-47, run 2026-11-21, with no
// .TEMP: v(c) = 1.785773 V in op, dc (Vb 0.7 V), and tran (1u 5u), and
// v(c) = -8.28252 at 1 kHz in ac (Ic = 21.4227 uA). Evaluated at its TNOM of
// 50 C instead, the device carries 82.6 uA and v(c) is 1.174 V.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

const std::string kDeck =
    "bjt tnom 50\nVcc vcc 0 2\nRc vcc c 10k\nVb b 0 DC 0.7 AC 1\nQ1 c b 0 qm\n"
    ".model qm npn is=1e-15 bf=100 tnom=50\n";

constexpr double kVc = 1.785773;     // ngspice-47
constexpr double kGain = -8.28252;   // ngspice-47, vr(c) at 1 kHz

parser::NetlistParseResult parse(const std::string& extra = "") {
    parser::NetlistParser parser;
    return parser.parse_string(kDeck + extra + ".end\n");
}

double op_vc(const std::string& extra = "") {
    auto parsed = parse(extra);
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    return solution.at(parsed.circuit->get_node_id("c") - 1);
}

}  // namespace

TEST(CircuitTemperature, OperatingPointIsAt27CWithoutTemp) {
    EXPECT_NEAR(op_vc(), kVc, 1e-5);
    // .OPTIONS TEMP sets the configuration's temperature (the program
    // resolves .TEMP into it before the analyses)
    EXPECT_NEAR(op_vc(), op_vc(".options temp=27\n"), 1e-12);
    EXPECT_GT(std::abs(op_vc(".options temp=50\n") - op_vc()), 0.1);
}

TEST(CircuitTemperature, DcSweepIsAt27CWithoutTemp) {
    auto parsed = parse();
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DCSweepConfig sweep;
    sweep.source_name = "Vb";
    sweep.start_value = 0.7;
    sweep.stop_value = 0.7;
    sweep.step_value = 0.1;
    DCSweepResult result;
    ASSERT_EQ(solver.solve_sweep(sweep, result), ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.points.size(), 1u);
    EXPECT_NEAR(result.points[0].node_voltages.at(parsed.circuit->get_node_id("c") - 1), kVc, 1e-5);
}

TEST(CircuitTemperature, TransientIsAt27CWithoutTemp) {
    auto parsed = parse();
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 5e-6, 1e-6);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_FALSE(result.timepoints.empty());
    const NodeId c = parsed.circuit->get_node_id("c");
    EXPECT_NEAR(result.timepoints.front().node_voltages.at(c - 1), kVc, 1e-5);
    EXPECT_NEAR(result.timepoints.back().node_voltages.at(c - 1), kVc, 1e-5);
}

TEST(CircuitTemperature, AcIsAt27CWithoutTemp) {
    auto parsed = parse();
    SimulationConfig config = parsed.config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = config.ac.stop_freq = 1e3;
    config.ac.num_points = 1;
    solvers::ACSolver solver(*parsed.circuit, config);
    const auto result = solver.solve();
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const auto v_c = result.frequency_points.at(0).solution.at(parsed.circuit->get_node_id("c") - 1);
    EXPECT_NEAR(v_c.real(), kGain, 1e-4 * std::abs(kGain));
    EXPECT_NEAR(v_c.imag(), 0.0, 1e-9);
}
