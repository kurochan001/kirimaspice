// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <cmath>

using namespace kirimaspice;

// Test to verify Mutual Inductance transient response fix
// specifically focusing on correct timestep scaling in accept_solution
TEST(MutualInductanceFixTest, TransformerRatioCheck) {
    // 1:1 Transformer, k=0.99
    // Input: Pulse 0->1V, tr=1ns
    // Expected Output: Pulse peak approx 0.99V
    std::string netlist = R"(Mutual Inductance Fix Verification
V1 in 0 DC 0 PULSE(0 1 1n 1n 1n 10n 20n)
L1 in 0 1u
L2 out 0 1u
K1 L1 L2 0.5
Rload out 0 1k
.TRAN 0.1n 30n
.END
)";

    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    auto& circuit = parsed.circuit;

    // Force variable timestep BDF2 (GEAR2)
    SimulationConfig cfg = parsed.config;
    cfg.transient.use_adaptive_timestep = true;
    cfg.transient.method = IntegrationMethod::GEAR2;
    cfg.transient.initial_step = 0.1e-9;
    cfg.transient.max_step = 1.0e-9;
    // ngspice寄りに緩和（拒否連鎖を避ける）
    cfg.transient.min_step = 1e-15;
    cfg.transient.trtol = 7.0;
    cfg.transient.lte_reltol = 1e-3;
    cfg.transient.lte_abstol = 1e-6;
    cfg.transient.reltol = 1e-3;

    solvers::TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, 30e-9, 0.1e-9);

    NodeId n_in = circuit->get_node_id("in");
    NodeId n_out = circuit->get_node_id("out");

    Real max_vin = 0.0;
    Real max_vout = 0.0;

    for (const auto& tp : result.timepoints) {
        Real vin = std::abs(tp.node_voltages[n_in - 1]);
        Real vout = std::abs(tp.node_voltages[n_out - 1]);
        max_vin = std::max(max_vin, vin);
        max_vout = std::max(max_vout, vout);
    }

    std::cout << "Max Vin: " << max_vin << " V" << std::endl;
    std::cout << "Max Vout: " << max_vout << " V" << std::endl;
    std::cout << "Ratio: " << max_vout / max_vin << std::endl;

    // Ideally ratio should be k * sqrt(L2/L1) = 0.5
    // Allow small error (numerical damping)
    EXPECT_NEAR(max_vout / max_vin, 0.5, 0.05); // 5% tolerance
}

TEST(MutualInductanceFixTest, TransformerRatioCheck_Gear3) {
    // Same test with GEAR3 to check high-order correction
    std::string netlist = R"(Mutual Inductance Fix Verification Gear3
V1 in 0 DC 0 PULSE(0 1 1n 1n 1n 10n 20n)
L1 in 0 1u
L2 out 0 1u
K1 L1 L2 0.5
Rload out 0 1k
.TRAN 0.1n 30n
.END
)";

    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    auto& circuit = parsed.circuit;

    SimulationConfig cfg = parsed.config;
    cfg.transient.use_adaptive_timestep = true;
    cfg.transient.method = IntegrationMethod::GEAR3;
    cfg.transient.initial_step = 0.1e-9;
    cfg.transient.max_step = 1.0e-9;
    cfg.transient.min_step = 1e-15;
    cfg.transient.trtol = 7.0;
    cfg.transient.lte_reltol = 1e-3;
    cfg.transient.lte_abstol = 1e-6;
    cfg.transient.reltol = 1e-3;

    solvers::TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, 30e-9, 0.1e-9);

    NodeId n_in = circuit->get_node_id("in");
    NodeId n_out = circuit->get_node_id("out");

    Real max_vin = 0.0;
    Real max_vout = 0.0;

    for (const auto& tp : result.timepoints) {
        Real vin = std::abs(tp.node_voltages[n_in - 1]);
        Real vout = std::abs(tp.node_voltages[n_out - 1]);
        max_vin = std::max(max_vin, vin);
        max_vout = std::max(max_vout, vout);
    }

    std::cout << "Max Vin: " << max_vin << " V" << std::endl;
    std::cout << "Max Vout: " << max_vout << " V" << std::endl;
    std::cout << "Ratio: " << max_vout / max_vin << std::endl;

    EXPECT_NEAR(max_vout / max_vin, 0.5, 0.05);
}
