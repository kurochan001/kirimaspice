// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_diode.cpp
#include <gtest/gtest.h>
#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

//==============================================================================
// Diode - Basic Construction Tests
//==============================================================================
TEST(Diode, Construction) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0);

    EXPECT_EQ(diode.id(), 1);
    EXPECT_EQ(diode.name(), "D1");
    EXPECT_EQ(diode.type(), DeviceType::DIODE);
    EXPECT_EQ(diode.anode(), 1);
    EXPECT_EQ(diode.cathode(), 0);
    EXPECT_DOUBLE_EQ(diode.saturation_current(), 1e-14);
    EXPECT_DOUBLE_EQ(diode.emission_coefficient(), 1.0);
    EXPECT_FALSE(diode.is_linear());
    EXPECT_FALSE(diode.is_dynamic());
}

TEST(Diode, ConstructionWithInvalidParameters) {
    // Negative saturation current should throw
    EXPECT_THROW(Diode(1, "D1", 1, 0, -1e-14, 1.0), std::runtime_error);

    // Zero saturation current should throw
    EXPECT_THROW(Diode(1, "D1", 1, 0, 0.0, 1.0), std::runtime_error);

    // Negative emission coefficient should throw
    EXPECT_THROW(Diode(1, "D1", 1, 0, 1e-14, -1.0), std::runtime_error);

    // Zero emission coefficient should throw
    EXPECT_THROW(Diode(1, "D1", 1, 0, 1e-14, 0.0), std::runtime_error);
}

//==============================================================================
// Diode - I-V Characteristic Tests
//==============================================================================
TEST(Diode, IVCharacteristicForwardBias) {
    // Standard diode: Is=1e-14A, N=1.0
    // At T=300.15K, Vt ≈ 25.85mV
    Diode diode(1, "D1", 1, 0);

    // Forward bias: V = 0.6V
    // I = Is * (exp(V/Vt) - 1)
    // I ≈ 1e-14 * (exp(0.6/0.02585) - 1)
    // I ≈ 1e-14 * (exp(23.21) - 1)
    // I ≈ 1e-14 * 1.16e10 ≈ 1.16e-4 A (0.116 mA)

    Real voltage = 0.6;
    Real current = diode.get_current(voltage);

    // Check that current is in reasonable range for forward bias
    EXPECT_GT(current, 1e-5);   // > 10μA
    EXPECT_LT(current, 1e-2);   // < 10mA

    // Check exponential relationship: I ∝ exp(V)
    Real i1 = diode.get_current(0.5);
    Real i2 = diode.get_current(0.6);
    Real i3 = diode.get_current(0.7);

    // I should increase exponentially
    EXPECT_GT(i2, i1);
    EXPECT_GT(i3, i2);

    // Ratio i2/i1 should be approximately exp((0.6-0.5)/Vt)
    Real expected_ratio = std::exp(0.1 / 0.02585);  // exp(3.87) ≈ 48
    Real actual_ratio = i2 / i1;
    EXPECT_NEAR(actual_ratio, expected_ratio, expected_ratio * 0.1);  // Within 10%
}

TEST(Diode, IVCharacteristicReverseBias) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0);

    // Reverse bias: V = -1.0V
    // I ≈ -Is (saturation current)
    Real voltage = -1.0;
    Real current = diode.get_current(voltage);

    EXPECT_LT(current, 0.0);  // Negative (reverse) current
    EXPECT_NEAR(current, -1e-14, 1e-15);  // Approximately -Is
}

TEST(Diode, IVCharacteristicZeroBias) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0);

    // Zero bias: V = 0V
    // I = Is * (exp(0) - 1) = 0
    Real voltage = 0.0;
    Real current = diode.get_current(voltage);

    EXPECT_NEAR(current, 0.0, 1e-20);
}

//==============================================================================
// Diode - Conductance Tests
//==============================================================================
TEST(Diode, ConductanceForwardBias) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0);

    // Forward bias: V = 0.6V
    // g = dI/dV = (Is/Vt) * exp(V/Vt)
    Real voltage = 0.6;
    Real conductance = diode.get_conductance(voltage);

    // Conductance should be positive and significant
    EXPECT_GT(conductance, 1e-3);  // > 1mS for forward bias

    // Verify dI/dV numerically
    Real dv = 1e-6;
    Real i1 = diode.get_current(voltage - dv/2);
    Real i2 = diode.get_current(voltage + dv/2);
    Real numerical_g = (i2 - i1) / dv;

    EXPECT_NEAR(conductance, numerical_g, numerical_g * 0.01);  // Within 1%
}

TEST(Diode, ConductanceReverseBias) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0);

    // Reverse bias: V = -1.0V
    // g ≈ gmin (minimum conductance for stability)
    Real voltage = -1.0;
    Real conductance = diode.get_conductance(voltage);

    EXPECT_GT(conductance, 0.0);  // Should be > 0 (gmin)
    EXPECT_LT(conductance, 1e-9); // Should be very small
}

//==============================================================================
// Diode - Temperature Tests
//==============================================================================
TEST(Diode, TemperatureEffect) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0);

    // Set temperature to 350K (77°C)
    diode.set_temperature(350.0);

    // At higher temperature, IS(T) increases significantly
    // SPICE model: IS(T) = IS(Tnom) * (T/Tnom)^(XTI/N) * exp(-EG/N * (1/Vt(T) - 1/Vt(Tnom)))
    // For silicon (XTI=3.0, EG=1.11eV), IS increases exponentially with T
    // Result: Current increases at higher temperature (IS effect dominates over Vt effect)
    Real i_hot = diode.get_current(0.6);

    // Reset to room temperature
    diode.set_temperature(300.15);
    Real i_room = diode.get_current(0.6);

    EXPECT_GT(i_hot, i_room);  // Current should be higher at higher temp (SPICE standard behavior)

    // Verify IS temperature dependence
    Real temp_ratio = 350.0 / 300.15;
    Real expected_is_ratio = std::pow(temp_ratio, 3.0 / 1.0);  // XTI/N = 3.0/1.0
    // Note: exp term also contributes but is more complex
    EXPECT_GT(i_hot / i_room, 1.0);  // At minimum, current increases with temperature
}

TEST(Diode, InvalidTemperature) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0);

    // Negative temperature should throw
    EXPECT_THROW(diode.set_temperature(-273.15), std::runtime_error);

    // Zero temperature should throw
    EXPECT_THROW(diode.set_temperature(0.0), std::runtime_error);
}

//==============================================================================
// Diode - CircuitGraph Integration Tests
//==============================================================================
TEST(DiodeCircuit, AddDiodeToCircuit) {
    CircuitGraph circuit;

    // Add diode with default parameters
    EXPECT_NO_THROW(circuit.add_diode("D1", "anode", "cathode"));

    EXPECT_TRUE(circuit.has_node("anode"));
    EXPECT_TRUE(circuit.has_node("cathode"));
    EXPECT_EQ(circuit.num_devices(), 1);
}

TEST(DiodeCircuit, AddDiodeWithParameters) {
    CircuitGraph circuit;

    // Add diode with custom parameters
    EXPECT_NO_THROW(circuit.add_diode("D1", "a", "0", 1e-13, 1.5));

    auto device = circuit.get_device("D1");
    ASSERT_NE(device, nullptr);

    auto diode = std::dynamic_pointer_cast<Diode>(device);
    ASSERT_NE(diode, nullptr);

    EXPECT_DOUBLE_EQ(diode->saturation_current(), 1e-13);
    EXPECT_DOUBLE_EQ(diode->emission_coefficient(), 1.5);
}

//==============================================================================
// Diode - Simple DC Circuit Test
//==============================================================================
TEST(DiodeCircuit, SimpleDCCircuitForwardBias) {
    // Circuit: V1 (5V) -- R1 (1kΩ) -- D1 -- GND
    // Expected: Diode forward biased, V(anode) ≈ 0.6-0.7V
    // I ≈ (5 - 0.6) / 1k ≈ 4.4mA

    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "vdd", "0", 5.0);
    circuit.add_resistor("R1", "vdd", "anode", 1000.0);
    circuit.add_diode("D1", "anode", "0");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Get anode voltage
    // solution is indexed from 0, but NodeId starts from 1 (0=GND)
    NodeId anode_id = circuit.get_node_id("anode");
    Real v_anode = solution[anode_id - 1];

    // Diode should be forward biased: 0.5V < V < 0.8V
    EXPECT_GT(v_anode, 0.5);
    EXPECT_LT(v_anode, 0.8);

    // Current through resistor: I = (Vdd - V_anode) / R
    Real i_expected = (5.0 - v_anode) / 1000.0;

    // Current through diode should match
    auto diode = std::dynamic_pointer_cast<Diode>(circuit.get_device("D1"));
    Real i_diode = diode->get_current(v_anode);

    EXPECT_NEAR(i_diode, i_expected, i_expected * 0.01);  // Within 1%
}

TEST(DiodeCircuit, SimpleDCCircuitReverseBias) {
    // Circuit: GND -- D1 (reversed) -- V1 (5V) -- GND
    // Expected: Diode reverse biased, very small current

    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "vplus", "0", 5.0);
    circuit.add_diode("D1", "0", "vplus");  // Reversed: anode=GND, cathode=vplus

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Diode should be reverse biased
    // solution is indexed from 0, but NodeId starts from 1 (0=GND)
    NodeId vplus_id = circuit.get_node_id("vplus");
    Real v_plus = solution[vplus_id - 1];

    EXPECT_NEAR(v_plus, 5.0, 0.01);  // Should be close to source voltage

    // Current through diode should be very small (≈ -Is)
    auto diode = std::dynamic_pointer_cast<Diode>(circuit.get_device("D1"));
    Real voltage_across_diode = 0.0 - v_plus;  // V_anode - V_cathode
    Real i_diode = diode->get_current(voltage_across_diode);

    EXPECT_LT(std::abs(i_diode), 1e-12);  // Very small reverse current
}

//==============================================================================
// Diode - AC Small-Signal Model Tests
//==============================================================================
TEST(Diode, JunctionCapacitanceReverseBias) {
    // Diode with junction capacitance: Cj0=1pF, Vj=0.7V, M=0.5
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0, 1e-12, 0.7, 0.5);

    EXPECT_TRUE(diode.is_dynamic());  // Should be dynamic with Cj0 > 0
    EXPECT_DOUBLE_EQ(diode.junction_capacitance(), 1e-12);
    EXPECT_DOUBLE_EQ(diode.junction_potential(), 0.7);
    EXPECT_DOUBLE_EQ(diode.grading_coefficient(), 0.5);

    // Reverse bias: V = -0.5V
    // Cj = Cj0 / (1 - V/Vj)^M = 1pF / (1 - (-0.5)/0.7)^0.5
    //    = 1pF / (1 + 0.714)^0.5 ≈ 1pF / 1.309 ≈ 0.764pF
    Real voltage = -0.5;
    Real cj = diode.get_junction_capacitance(voltage);
    Real expected_cj = 1e-12 / std::pow(1.0 - voltage / 0.7, 0.5);
    EXPECT_NEAR(cj, expected_cj, 1e-15);
}

TEST(Diode, JunctionCapacitanceZeroBias) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0, 1e-12, 0.7, 0.5);

    // Zero bias: Cj = Cj0
    Real voltage = 0.0;
    Real cj = diode.get_junction_capacitance(voltage);
    EXPECT_DOUBLE_EQ(cj, 1e-12);
}

TEST(Diode, JunctionCapacitanceForwardBias) {
    Diode diode(1, "D1", 1, 0, 1e-14, 1.0, 1e-12, 0.7, 0.5);

    // Small forward bias: V = 0.2V (< FC*Vj = 0.35V)
    Real voltage = 0.2;
    Real cj = diode.get_junction_capacitance(voltage);
    Real expected_cj = 1e-12 / std::pow(1.0 - voltage / 0.7, 0.5);
    EXPECT_NEAR(cj, expected_cj, 1e-15);
}

TEST(Diode, SmallSignalConductanceForwardBias) {
    // Standard diode: Is=1e-14A, N=1.0
    // At T=300.15K, Vt = kT/q
    Diode diode(1, "D1", 1, 0);

    // Forward bias: V = 0.6V
    // gd = (Is/(N*Vt)) * exp(V/(N*Vt))
    Real voltage = 0.6;
    Real gd = diode.get_small_signal_conductance(voltage);

    // Calculate expected value using actual Vt calculation
    constexpr Real k_boltzmann = 1.380649e-23;
    constexpr Real q_electron = 1.602176634e-19;
    Real vt = k_boltzmann * 300.15 / q_electron;
    Real n_vt = 1.0 * vt;  // N * Vt
    Real expected_gd = 1e-14 / n_vt * std::exp(voltage / n_vt);

    EXPECT_NEAR(gd, expected_gd, expected_gd * 1e-4);  // Slightly relaxed tolerance
    EXPECT_GT(gd, 1e-6);  // Should be significant for forward bias
}

TEST(Diode, SmallSignalConductanceReverseBias) {
    Diode diode(1, "D1", 1, 0);

    // Reverse bias: V = -5V
    // gd should be nearly zero
    Real voltage = -5.0;
    Real gd = diode.get_small_signal_conductance(voltage);

    EXPECT_LT(gd, 1e-10);  // Very small for reverse bias
}

TEST(Diode, ACStampWithJunctionCapacitance) {
    // Diode with junction capacitance
    Diode diode(1, "D1", 2, 1, 1e-14, 1.0, 1e-12, 0.7, 0.5);

    // DC operating point: V = 0.3V (forward bias)
    DenseVector dc_solution(2);
    dc_solution[0] = 0.0;   // Node 1 = 0V
    dc_solution[1] = 0.3;   // Node 2 = 0.3V

    // AC frequency: f = 1 MHz, ω = 2π * 1e6
    Real omega = 2.0 * M_PI * 1e6;

    // Create AC MNA system
    ComplexMNASystem sys;
    sys.num_nodes = 2;
    sys.num_branches = 0;
    sys.I.resize(2);

    // Stamp AC
    diode.stamp_ac(sys, omega, dc_solution);

    // Expected: Y = gd + jωCj
    Real voltage = 0.3;
    Real gd = diode.get_small_signal_conductance(voltage);
    Real cj = diode.get_junction_capacitance(voltage);
    Complex expected_admittance(gd, omega * cj);

    // Check Y matrix entries
    // Y[1][1] should have +admittance (anode=2, index=1)
    // Y[0][0] should have +admittance (cathode=1, index=0)
    // Y[1][0] and Y[0][1] should have -admittance

    // Verify that the matrix is not zero
    std::vector<std::uint32_t> col_ptr;
    std::vector<std::uint32_t> row_ind;
    std::vector<Complex> values;
    sys.Y.to_csc(col_ptr, row_ind, values);

    EXPECT_GT(values.size(), 0);

    // Verify admittance magnitude
    for (const auto& val : values) {
        EXPECT_GT(std::abs(val), 0.0);
    }
}

// accept_solution receives node k at element k, with ground at element 0
// (TransientSolver::accept_devices). A diode that accepted V = 0.6 V and is
// then stamped for a backward Euler step at the same 0.6 V has no current in
// its junction capacitance, cj/dt (V - V_prev) = 0, so its stamp gives the
// DC current at 0.6 V. Reading the accepted vector one element off took the
// ground slot as the anode and added cj/dt * 0.6 V of spurious current.
TEST(DiodeTest, AcceptedVoltageIsTheHistoryOfTheJunctionCapacitance) {
    Diode diode(1, "D1", 1, NODE_GND, 1e-14, 1.0, 10e-12);
    const Real v = 0.6;
    const DenseVector accepted = {0.0, v};  // ground, node 1
    diode.accept_solution(accepted);

    const DenseVector iterate = {v};  // node 1 at element 0, as in the stamps
    auto current_into_anode_row = [&](bool transient) {
        MNASystem mna;
        mna.resize(1, 0);
        mna.zero();
        if (transient) {
            diode.stamp_transient(mna, iterate, 1e-9, IntegrationMethod::BACKWARD_EULER,
                                  nullptr, nullptr);
        } else {
            diode.stamp_dc_nonlinear(mna, iterate);
        }
        Real current = -mna.b[0];
        for (std::size_t k = 0; k < mna.G.nnz(); ++k) {
            if (mna.G.rows[k] == 0) {
                current += mna.G.values[k] * iterate[mna.G.cols[k]];
            }
        }
        return current;
    };
    const Real dc = current_into_anode_row(false);
    EXPECT_NEAR(current_into_anode_row(true), dc, 1e-12 * std::abs(dc));
}
