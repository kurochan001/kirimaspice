// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_triac.cpp
// TRIAC Device Tests

#include <gtest/gtest.h>
#include "kirimaspice/devices/triac.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

//==============================================================================
// Test Fixture
//==============================================================================

class TRIACTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

//==============================================================================
// 1. Device Construction Tests (2 tests)
//==============================================================================

TEST_F(TRIACTest, TRIAC_Construction) {
    // Test TRIAC construction
    TRIAC triac(1, "Y1", 1, 2, 3,
                400.0, 20e-3, 10e-3, 1.0, 0.1, 1.5, 10.0, 300.15);

    EXPECT_EQ(triac.id(), 1);
    EXPECT_EQ(triac.name(), "Y1");
    EXPECT_EQ(triac.type(), DeviceType::TRIAC);
    EXPECT_FALSE(triac.is_linear());
    EXPECT_FALSE(triac.is_dynamic());

    const auto& nodes = triac.nodes();
    EXPECT_EQ(nodes.size(), 3);  // MT2, MT1, Gate
}

TEST_F(TRIACTest, TRIAC_ParameterValidation) {
    // Test parameter access
    TRIAC triac(1, "Y1", 1, 2, 3,
                400.0, 20e-3, 10e-3, 1.0, 0.1, 1.5, 10.0, 300.15);

    // Get parameters
    EXPECT_EQ(triac.get_parameter("vbo").value(), 400.0);
    EXPECT_EQ(triac.get_parameter("igt").value(), 20e-3);
    EXPECT_EQ(triac.get_parameter("ih").value(), 10e-3);
    EXPECT_EQ(triac.get_parameter("ron").value(), 0.1);

    // Set parameters
    EXPECT_TRUE(triac.set_parameter("vbo", 600.0));
    EXPECT_EQ(triac.get_parameter("vbo").value(), 600.0);

    // Perturbable parameters
    auto perturbable = triac.get_perturbable_parameters();
    EXPECT_GT(perturbable.size(), 0);
}

//==============================================================================
// 2. Positive Direction Operation Tests (3 tests)
//==============================================================================

TEST_F(TRIACTest, PositiveDirection_BlockingState) {
    // Test blocking state in positive direction (Vmt < VBO, no gate trigger)
    CircuitGraph circuit;

    // Add voltage sources
    circuit.add_voltage_source("Vmt", "mt2", "0", 50.0);   // Vmt = 50V < VBO = 400V
    circuit.add_voltage_source("Vg", "gate", "0", 0.0);    // No gate voltage

    // Add TRIAC (default VBO=400V, IGT=20mA, IH=10mA)
    circuit.add_triac("Y1", "mt2", "0", "gate");

    // Solve DC operating point
    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    // In blocking state, TRIAC should not conduct (open circuit)
}

TEST_F(TRIACTest, PositiveDirection_Q1Plus_Trigger) {
    // Test Q1+ quadrant trigger (MT2+, G+)
    CircuitGraph circuit;

    circuit.add_voltage_source("Vmt", "mt2", "mt1", 5.0);    // Positive Vmt
    circuit.add_voltage_source("Vg", "gate", "mt1", 2.0);    // Positive Vg (> VGT=1.0V)
    circuit.add_resistor("Rmt", "mt2_in", "mt2", 10.0);      // Current limiting
    circuit.add_voltage_source("Vin", "mt2_in", "0", 50.0);  // Supply voltage

    circuit.add_triac("Y1", "mt2", "mt1", "gate");
    circuit.add_resistor("Rg", "gate_in", "gate", 100.0);    // Gate resistor
    circuit.add_voltage_source("Vg_supply", "gate_in", "0", 10.0);

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    // In triggered state, TRIAC should conduct (low resistance RON=0.1Ω)
}

TEST_F(TRIACTest, PositiveDirection_Turnoff) {
    // Once triggered, the TRIAC conducts (Vmt - VTM) / RON without gate
    // current, until that current falls below IH; then it blocks until it is
    // triggered again (devices/triac.hpp, operating regions 3 and 4)
    TRIAC triac(1, "Y1", 1, 2, 3,
                400.0, 20e-3, 10e-3, 1.0, 0.1, 1.5, 10.0, 300.15);
    triac.reset_state();

    // The current from MT2 to MT1 that the stamp at voltages v represents:
    // the MT2 row (index 0) holds the main path only
    auto mt_current = [&](const DenseVector& v) {
        MNASystem mna;
        mna.resize(3, 0);
        mna.zero();
        triac.stamp_dc_nonlinear(mna, v);
        Real i = -mna.b[0];
        for (std::size_t k = 0; k < mna.G.nnz(); ++k) {
            if (mna.G.rows[k] == 0) {
                i += mna.G.values[k] * v[mna.G.cols[k]];
            }
        }
        return i;
    };

    // Gate at 4 V: Ig = (4 V - VGT) / 100 ohm = 30 mA >= IGT, so it triggers
    EXPECT_NEAR(mt_current({5.0, 0.0, 4.0}), (5.0 - 1.5) / 0.1, 1e-9);
    // Latched: it conducts without gate current
    EXPECT_NEAR(mt_current({5.0, 0.0, 0.0}), (5.0 - 1.5) / 0.1, 1e-9);
    // (1.5005 V - VTM) / RON = 5 mA < IH = 10 mA: it turns off
    EXPECT_NEAR(mt_current({1.5005, 0.0, 0.0}), 0.0, 1e-12);
    // and blocks 5 V again without gate current
    EXPECT_NEAR(mt_current({5.0, 0.0, 0.0}), 0.0, 1e-12);
}

//==============================================================================
// 3. Negative Direction Operation Tests (3 tests)
//==============================================================================

TEST_F(TRIACTest, NegativeDirection_BlockingState) {
    // Test blocking state in negative direction (|Vmt| < VBO, no gate trigger)
    CircuitGraph circuit;

    circuit.add_voltage_source("Vmt", "mt1", "mt2", 50.0);   // Vmt = -50V, |Vmt| < VBO
    circuit.add_voltage_source("Vg", "gate", "0", 0.0);      // No gate voltage

    circuit.add_triac("Y1", "mt2", "mt1", "gate");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    // In blocking state, TRIAC should not conduct
}

TEST_F(TRIACTest, NegativeDirection_Q3Minus_Trigger) {
    // Test Q3- quadrant trigger (MT2-, G-)
    CircuitGraph circuit;

    circuit.add_voltage_source("Vmt", "mt1", "mt2", 5.0);    // Negative Vmt (MT2 < MT1)
    circuit.add_voltage_source("Vg", "mt1", "gate", 2.0);    // Negative Vg relative to MT1
    circuit.add_resistor("Rmt", "mt1_in", "mt1", 10.0);      // Current limiting
    circuit.add_voltage_source("Vin", "mt1_in", "0", 50.0);  // Supply voltage

    circuit.add_triac("Y1", "mt2", "mt1", "gate");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    // In triggered state (Q3-), TRIAC should conduct
}

TEST_F(TRIACTest, BidirectionalOperation) {
    // Test that TRIAC can conduct in both directions
    // This is a conceptual test; actual bidirectional behavior requires separate circuits
    CircuitGraph circuit_pos, circuit_neg;

    // Positive direction circuit
    circuit_pos.add_voltage_source("V1", "mt2", "0", 50.0);
    circuit_pos.add_voltage_source("Vg1", "gate", "0", 2.0);
    circuit_pos.add_triac("Y1", "mt2", "0", "gate");

    // Negative direction circuit
    circuit_neg.add_voltage_source("V2", "0", "mt2", 50.0);  // Reverse polarity
    circuit_neg.add_voltage_source("Vg2", "0", "gate", 2.0);
    circuit_neg.add_triac("Y2", "mt2", "0", "gate");

    DCSolver solver_pos(circuit_pos);
    DCSolver solver_neg(circuit_neg);

    DenseVector sol_pos, sol_neg;
    SolverStats stats_pos, stats_neg;

    ConvergenceStatus status_pos = solver_pos.solve(sol_pos, stats_pos);
    ConvergenceStatus status_neg = solver_neg.solve(sol_neg, stats_neg);

    EXPECT_EQ(status_pos, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(status_neg, ConvergenceStatus::SUCCESS);
    // Both directions should converge successfully
}

//==============================================================================
// 4. CircuitGraph Integration Tests (2 tests)
//==============================================================================

TEST_F(TRIACTest, CircuitGraph_AddTRIAC) {
    // Test adding TRIAC to circuit
    CircuitGraph circuit;

    circuit.add_triac("Y1", "mt2", "mt1", "gate",
                      400.0, 20e-3, 10e-3, 1.0, 0.1, 1.5, 10.0, 300.15);

    // Verify circuit was created successfully
    EXPECT_NO_THROW({
        DCSolver solver(circuit);
    });
}

TEST_F(TRIACTest, CircuitGraph_ACDimmerCircuit) {
    // Test AC dimmer circuit (simplified DC equivalent for testing)
    CircuitGraph circuit;

    // AC source (represented as DC for this test)
    circuit.add_voltage_source("Vac", "ac_in", "0", 120.0);  // 120V AC (peak)

    // Load resistor
    circuit.add_resistor("Rload", "load", "0", 100.0);

    // TRIAC with gate control
    circuit.add_triac("Y1", "ac_in", "load", "gate");

    // Gate trigger circuit
    circuit.add_voltage_source("Vgate", "gate", "0", 2.0);  // Trigger voltage

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    // AC dimmer circuit should converge
}

TEST_F(TRIACTest, ConductingThermalNoiseIsFourKTOverRon) {
    // A conducting TRIAC gives one thermal current-noise source across MT2-MT1
    // whose spectral density is 4kT/Ron (defaults: Ron = 0.1 ohm, T = 300.15 K).
    CircuitGraph circuit;
    circuit.add_voltage_source("Vmt", "mt2", "mt1", 5.0);
    // 4 V gives a gate current of (4 - 1) V / 100 ohm = 30 mA, above IGT = 20 mA.
    circuit.add_voltage_source("Vg", "gate", "mt1", 4.0);
    circuit.add_resistor("Rmt", "mt2_in", "mt2", 10.0);
    circuit.add_voltage_source("Vin", "mt2_in", "0", 50.0);
    circuit.add_triac("Y1", "mt2", "mt1", "gate");
    circuit.add_resistor("Rg", "gate_in", "gate", 100.0);
    circuit.add_voltage_source("Vg_supply", "gate_in", "0", 10.0);

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);

    std::vector<NoiseSource> sources;
    for (const auto& device : circuit.get_devices()) {
        if (device->name() == "Y1") {
            sources = device->get_noise_sources(solution, 1.0e3);
        }
    }
    ASSERT_EQ(sources.size(), 1u);
    const Real expected = 4.0 * 1.380649e-23 * 300.15 / 0.1;
    EXPECT_NEAR(sources[0].spectral_density, expected, expected * 1e-12);
    EXPECT_TRUE(sources[0].is_current);
}
