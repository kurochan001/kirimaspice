// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/validation/test_bjt_dc_validation.cpp
// BJT DC Operating Point Validation against ngspice
// Verifies BJT Ebers-Moll model implementation accuracy

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

// Test fixture for BJT DC validation
class BJT_DC_Validation : public ::testing::Test {
protected:
    CircuitGraph circuit;
    DCSolver* solver = nullptr;

    void SetUp() override {
        // Will be set up in each test
    }

    void TearDown() override {
        if (solver) {
            delete solver;
            solver = nullptr;
        }
    }

    // Build circuit and solve
    ConvergenceStatus solve_circuit(DenseVector& solution, SolverStats& stats) {
        solver = new DCSolver(circuit);
        return solver->solve(solution, stats);
    }
};

// Test Case 1: Standard Common-Emitter Configuration
// Circuit: VCC=5V, Vbase=0.75V, Rb=1k, Rc=1k
// ngspice reference: validation/bjt_dc_op.cir
TEST_F(BJT_DC_Validation, CommonEmitterStandard) {
    // Build circuit (matching validation/bjt_dc_op.cir)
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 0.75);
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0",
                        1e-14,  // IS
                        100.0,  // BF (beta_f)
                        1.0);   // NF

    // Get node IDs
    NodeId node_b = circuit.get_node_id("b");
    NodeId node_coll = circuit.get_node_id("coll");
    NodeId node_vcc = circuit.get_node_id("vcc");

    // Solve DC operating point
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solve_circuit(solution, stats);

    // Verify convergence
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS)
        << "DC analysis failed to converge";

    // Extract node voltages
    Real v_b = solution[node_b - 1];
    Real v_coll = solution[node_coll - 1];
    Real v_vcc = solution[node_vcc - 1];

    // ngspice reference values (from validation/bjt_dc_op_result.txt):
    // b    = 6.958898e-01
    // coll = 1.742518e-01
    // vcc  = 5.000000e+00
    // vcc#branch = -4.82575e-03 (collector current)

    const Real ngspice_v_b = 0.6958898;
    const Real ngspice_v_coll = 0.1742518;
    const Real ngspice_v_vcc = 5.0;

    // Tolerance: 1% relative error or 10mV absolute
    const Real tol_relative = 0.01;  // 1%
    const Real tol_absolute = 0.01;   // 10mV

    // Verify VCC (should be exact)
    EXPECT_NEAR(v_vcc, ngspice_v_vcc, 1e-6)
        << "VCC voltage mismatch";

    // Verify base voltage (after Rb)
    Real error_b = std::abs(v_b - ngspice_v_b);
    Real error_b_relative = error_b / ngspice_v_b;
    EXPECT_TRUE(error_b < tol_absolute || error_b_relative < tol_relative)
        << "Base voltage error too large: " << v_b << " vs " << ngspice_v_b
        << " (error: " << error_b << "V, " << (error_b_relative * 100) << "%)";

    // Verify collector voltage
    Real error_coll = std::abs(v_coll - ngspice_v_coll);
    Real error_coll_relative = error_coll / ngspice_v_coll;
    EXPECT_TRUE(error_coll < tol_absolute || error_coll_relative < tol_relative)
        << "Collector voltage error too large: " << v_coll << " vs " << ngspice_v_coll
        << " (error: " << error_coll << "V, " << (error_coll_relative * 100) << "%)";

    // Calculate collector current from voltage drop across Rc
    Real i_c = (v_vcc - v_coll) / 1000.0;  // Current through Rc
    const Real ngspice_i_c = 0.0048257;    // 4.826mA

    Real error_ic = std::abs(i_c - ngspice_i_c);
    Real error_ic_relative = error_ic / ngspice_i_c;
    EXPECT_TRUE(error_ic < 1e-4 || error_ic_relative < tol_relative)
        << "Collector current error too large: " << i_c << "A vs " << ngspice_i_c << "A"
        << " (error: " << (error_ic * 1000) << "mA, " << (error_ic_relative * 100) << "%)";
}

// Test Case 2: Lower Base Voltage (Cutoff Region)
TEST_F(BJT_DC_Validation, CutoffRegion) {
    // Build circuit with lower base voltage
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 0.5);  // Below turn-on voltage
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    // Get node IDs
    NodeId node_coll = circuit.get_node_id("coll");
    NodeId node_vcc = circuit.get_node_id("vcc");

    // Solve DC operating point
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solve_circuit(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    Real v_coll = solution[node_coll - 1];
    Real v_vcc = solution[node_vcc - 1];

    // In cutoff, collector should be near VCC (minimal current)
    EXPECT_NEAR(v_coll, v_vcc, 0.1)
        << "In cutoff region, V_coll should be close to VCC";

    // Collector current should be very small (< 10μA)
    // Note: At Vbase=0.5V, some leakage current is expected (~2-3μA is realistic)
    Real i_c = (v_vcc - v_coll) / 1000.0;
    EXPECT_LT(i_c, 1e-5)
        << "In cutoff region, collector current should be < 10μA";
}

// Test Case 3: Higher Base Voltage (Active Region)
TEST_F(BJT_DC_Validation, ActiveRegionHighBias) {
    // Build circuit with higher base voltage
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 1.0);  // Higher bias
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    // Get node IDs
    NodeId node_b = circuit.get_node_id("b");
    NodeId node_coll = circuit.get_node_id("coll");
    NodeId node_vcc = circuit.get_node_id("vcc");

    // Solve DC operating point
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solve_circuit(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    Real v_b = solution[node_b - 1];
    Real v_coll = solution[node_coll - 1];
    Real v_vcc = solution[node_vcc - 1];

    // Verify BJT is in active region
    EXPECT_GT(v_b, 0.6) << "Base voltage should be above turn-on";
    EXPECT_GT(v_coll, v_b - 0.7) << "Collector should be above base-0.7V (not in saturation)";

    // Higher current should result in lower collector voltage
    Real i_c = (v_vcc - v_coll) / 1000.0;
    EXPECT_GT(i_c, 1e-4) << "Collector current should be significant (> 0.1mA)";
}

// Test Case 4: Different BJT Parameters (BF=50)
TEST_F(BJT_DC_Validation, DifferentBeta) {
    // Build circuit with different beta
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 0.75);
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0",
                        1e-14,  // IS
                        50.0,   // BF=50 (instead of 100)
                        1.0);   // NF

    // Get node IDs
    NodeId node_b = circuit.get_node_id("b");
    NodeId node_coll = circuit.get_node_id("coll");
    NodeId node_vcc = circuit.get_node_id("vcc");

    // Solve DC operating point
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solve_circuit(solution, stats);

    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);

    Real v_b = solution[node_b - 1];
    Real v_coll = solution[node_coll - 1];
    Real v_vcc = solution[node_vcc - 1];

    // Lower beta → higher base current → higher voltage drop across Rb
    // → lower actual base voltage
    EXPECT_LT(v_b, 0.70) << "Lower beta should result in lower base voltage";

    // Collector current should be similar (determined by base-emitter voltage)
    Real i_c = (v_vcc - v_coll) / 1000.0;
    EXPECT_GT(i_c, 1e-4) << "Collector current should still be significant";
}
