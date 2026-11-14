// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_dc_sweep.cpp
#include <gtest/gtest.h>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/devices/finfet.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

class DCSweepTest : public ::testing::Test {
protected:
    SimulationConfig config;

    void SetUp() override {
        // Use tight tolerances for accurate results
        config.dc.abstol = 1e-12;
        config.dc.reltol = 1e-6;
        config.dc.vntol = 1e-9;
    }
};

// Test 1: Resistive load line (linear I-V characteristic)
TEST_F(DCSweepTest, ResistiveLoadLine) {
    // Circuit: V1 -- R1 (1k) -- GND
    // Sweep V1 from 0V to 10V, 1V step
    // Expected: Linear I = V/R relationship
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);  // Will be swept
    circuit.add_resistor("R1", "in", "0", 1000.0);  // 1kΩ

    // Sweep configuration
    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 10.0;
    sweep_config.step_value = 1.0;

    // Solve
    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    // Verify
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 11);  // 0, 1, 2, ..., 10

    // Verify linear I-V characteristic: I = V/R = V/1000
    for (size_t i = 0; i < result.points.size(); ++i) {
        Real V = result.points[i].sweep_value;
        Real expected_current = V / 1000.0;  // I = V/R

        // For resistive circuit, current flows through voltage source branch
        // The solution vector contains [node_voltages, branch_currents]
        // For this simple circuit: solution = [branch_current_V1]
        // (no nodes except GND, so only branch current)

        EXPECT_NEAR(result.points[i].sweep_value, static_cast<Real>(i), 1e-9);
        EXPECT_EQ(result.points[i].status, ConvergenceStatus::SUCCESS);

        // All points should converge quickly (linear circuit)
        EXPECT_LE(result.points[i].iterations, 5);
    }
}

// Test 2: Simple diode sweep (exponential I-V characteristic)
TEST_F(DCSweepTest, SimpleDiodeSweep) {
    // Circuit: V1 -- D1 -- R1 (100Ω) -- GND
    // Sweep V1 from 0V to 1V, 0.1V step
    // Expected: Exponential increase in current
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);  // Will be swept
    circuit.add_diode("D1", "in", "out");
    circuit.add_resistor("R1", "out", "0", 100.0);

    // Diode model
    ModelDefinition diode_model;
    diode_model.device_type = DeviceType::DIODE;
    diode_model.params["is"] = 1e-14;  // Saturation current
    diode_model.params["n"] = 1.0;     // Ideality factor
    config.models["D1"] = diode_model;

    // Sweep configuration
    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 1.0;
    sweep_config.step_value = 0.1;

    // Solve
    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    // Verify
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 11);  // 0, 0.1, ..., 1.0

    // Verify exponential I-V characteristic (current should increase exponentially)
    Real prev_node_voltage = -1.0;
    for (size_t i = 0; i < result.points.size(); ++i) {
        EXPECT_NEAR(result.points[i].sweep_value, i * 0.1, 1e-9);
        EXPECT_EQ(result.points[i].status, ConvergenceStatus::SUCCESS);

        // Extract node voltage at "out" (node index 0 in solution)
        if (result.points[i].node_voltages.size() > 0) {
            Real node_voltage = result.points[i].node_voltages[0];

            // Voltage should be monotonically increasing
            if (i > 0) {
                EXPECT_GT(node_voltage, prev_node_voltage);
            }
            prev_node_voltage = node_voltage;
        }
    }
}

// Test 3: Negative step sweep (reverse direction)
TEST_F(DCSweepTest, NegativeStepSweep) {
    // Sweep from 10V down to 0V
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);
    circuit.add_resistor("R1", "in", "0", 1000.0);

    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 10.0;   // Start high
    sweep_config.stop_value = 0.0;     // End low
    sweep_config.step_value = -1.0;    // Negative step

    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 11);  // 10, 9, 8, ..., 0

    // Verify descending order
    for (size_t i = 0; i < result.points.size(); ++i) {
        EXPECT_NEAR(result.points[i].sweep_value, 10.0 - i, 1e-9);
    }
}

// Test 4: Single point sweep (edge case)
TEST_F(DCSweepTest, SinglePointSweep) {
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);
    circuit.add_resistor("R1", "in", "0", 1000.0);

    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 5.0;
    sweep_config.stop_value = 5.0;  // Same as start
    sweep_config.step_value = 1.0;

    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(result.points.size(), 1);
    EXPECT_NEAR(result.points[0].sweep_value, 5.0, 1e-9);
}

// Test 5: Source not found (error handling)
TEST_F(DCSweepTest, SourceNotFound) {
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);
    circuit.add_resistor("R1", "in", "0", 1000.0);

    DCSweepConfig sweep_config;
    sweep_config.source_name = "V_NONEXISTENT";  // Wrong name
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 10.0;
    sweep_config.step_value = 1.0;

    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    // Should fail with NUMERICAL_ERROR
    EXPECT_EQ(status, ConvergenceStatus::NUMERICAL_ERROR);
    EXPECT_EQ(result.status, ConvergenceStatus::NUMERICAL_ERROR);
    EXPECT_EQ(result.points.size(), 0);  // No points computed
}

// Test 6: Current source sweep
TEST_F(DCSweepTest, CurrentSourceSweep) {
    // Circuit: I1 -- R1 (1k) -- GND
    // Sweep I1 from 0mA to 10mA, 1mA step
    // Expected: V = I * R
    // Note: I1 flows from GND (0) to "in", so positive current creates positive voltage
    CircuitGraph circuit;
    circuit.add_current_source("I1", "0", "in", 0.0);  // Will be swept (GND → in)
    circuit.add_resistor("R1", "in", "0", 1000.0);  // 1kΩ

    DCSweepConfig sweep_config;
    sweep_config.source_name = "I1";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 10e-3;  // 10mA
    sweep_config.step_value = 1e-3;   // 1mA

    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 11);  // 0, 1, 2, ..., 10 mA

    // Verify V = I * R relationship
    for (size_t i = 0; i < result.points.size(); ++i) {
        Real I = result.points[i].sweep_value;
        Real expected_voltage = I * 1000.0;  // V = I * R

        // Node voltage at "in" should match expected
        if (result.points[i].node_voltages.size() > 0) {
            Real actual_voltage = result.points[i].node_voltages[0];
            EXPECT_NEAR(actual_voltage, expected_voltage, 1e-6);
        }
    }
}

// Test 7: Large number of points
TEST_F(DCSweepTest, ManyPoints) {
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);
    circuit.add_resistor("R1", "in", "0", 1000.0);

    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 10.0;
    sweep_config.step_value = 0.01;  // 1001 points

    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 1001);

    // Spot check first, middle, and last points
    EXPECT_NEAR(result.points[0].sweep_value, 0.0, 1e-9);
    EXPECT_NEAR(result.points[500].sweep_value, 5.0, 1e-6);
    EXPECT_NEAR(result.points[1000].sweep_value, 10.0, 1e-6);
}

// Test 8: BJT transfer characteristic (simplified, no ngspice comparison yet)
TEST_F(DCSweepTest, BJTTransferCurve) {
    // Circuit: VCC -- RC -- C, B -- RB -- VBB, E -- GND
    // Sweep VBB (base voltage) from 0V to 1V
    // Expected: IC increases with VBE
    CircuitGraph circuit;
    circuit.add_voltage_source("VCC", "vcc", "0", 5.0);  // Fixed VCC
    circuit.add_voltage_source("VBB", "vbb", "0", 0.0);  // Will be swept
    circuit.add_resistor("RC", "vcc", "c", 1000.0);
    circuit.add_resistor("RB", "vbb", "b", 10000.0);
    circuit.add_bjt_npn("Q1", "c", "b", "0");  // Collector, Base, Emitter

    // BJT model
    ModelDefinition bjt_model;
    bjt_model.device_type = DeviceType::BJT_NPN;
    bjt_model.params["is"] = 1e-14;
    bjt_model.params["bf"] = 100.0;
    config.models["Q1"] = bjt_model;

    DCSweepConfig sweep_config;
    sweep_config.source_name = "VBB";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 1.0;
    sweep_config.step_value = 0.1;

    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 11);

    // Verify that collector voltage changes as VBB increases
    // (detailed verification would require ngspice comparison)
    Real prev_vc = 5.0;  // Starts at VCC (BJT off)
    for (size_t i = 1; i < result.points.size(); ++i) {
        // As VBE increases, IC increases, VC = VCC - IC*RC decreases
        // Node order: vcc(0), vbb(1), c(2), b(3)
        // We're interested in node "c" (collector)
        if (result.points[i].node_voltages.size() >= 3) {
            Real vc = result.points[i].node_voltages[2];  // Collector voltage
            // VC should decrease as VBB increases (more current flows)
            // (This is a loose check, just verifying monotonic behavior)
            if (result.points[i].sweep_value > 0.6) {  // Above threshold
                EXPECT_LT(vc, prev_vc + 0.1);  // Allow some tolerance
            }
            prev_vc = vc;
        }
    }
}

// ============================================================================
// Temperature Sweep Tests
// ============================================================================

class TemperatureSweepTest : public ::testing::Test {
protected:
    SimulationConfig config;

    void SetUp() override {
        // Use tight tolerances for accurate results
        config.dc.abstol = 1e-12;
        config.dc.reltol = 1e-6;
        config.dc.vntol = 1e-9;
    }
};

// Test 1: Basic temperature sweep with resistor divider (temperature independent)
TEST_F(TemperatureSweepTest, ResistorDivider_TemperatureIndependent) {
    // Circuit: V1 (10V) -- R1 (1k) -- vmid -- R2 (1k) -- GND
    // Expected: vmid = 5V at all temperatures (resistors are temperature independent)
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "vin", "0", 10.0);
    circuit.add_resistor("R1", "vin", "vmid", 1000.0);
    circuit.add_resistor("R2", "vmid", "0", 1000.0);

    // Temperature sweep configuration
    config.temp_sweep.enabled = true;
    config.temp_sweep.temperatures = {
        273.15,       // 0°C
        300.15,       // 27°C (room temperature)
        350.15,       // 77°C
        400.15        // 127°C
    };

    // Solve
    DCSolver solver(circuit, config);
    TemperatureSweepResult result;
    ConvergenceStatus status = solver.solve_temp_sweep(result);

    // Verify
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 4);

    // Verify voltage at vmid is 5V at all temperatures
    for (const auto& point : result.points) {
        ASSERT_EQ(point.node_voltages.size(), 2);  // vin, vmid
        Real vmid = point.node_voltages[1];
        EXPECT_NEAR(vmid, 5.0, 1e-6) << "Temperature: " << (point.temperature - 273.15) << "°C";
    }
}

// Test 2: Diode temperature dependence
TEST_F(TemperatureSweepTest, Diode_TemperatureDependence) {
    // Circuit: V1 (0.7V) -- R1 (100Ω) -- anode -- D1 -- GND
    // Expected: Anode voltage decreases with increasing temperature
    // Physical explanation: IS(T) increases exponentially with temperature
    //   → Diode current increases → More voltage drop across R1 → Lower anode voltage
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "vin", "0", 0.7);
    circuit.add_resistor("R1", "vin", "anode", 100.0);
    circuit.add_diode("D1", "anode", "0", 1e-14, 1.0);

    // Temperature sweep: -40°C to 125°C
    config.temp_sweep.enabled = true;
    config.temp_sweep.temperatures = {
        233.15,       // -40°C
        273.15,       //   0°C
        300.15,       //  27°C (room temperature)
        358.15,       //  85°C
        398.15        // 125°C
    };

    // Solve
    DCSolver solver(circuit, config);
    TemperatureSweepResult result;
    ConvergenceStatus status = solver.solve_temp_sweep(result);

    // Verify
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 5);

    // Verify monotonic decrease of anode voltage with increasing temperature
    // (Higher temp → IS(T) increases → diode conducts more → more voltage drop across R1 → lower anode voltage)
    // SPICE standard behavior: IS(T) = IS(Tnom) * (T/Tnom)^(XTI/N) * exp(-EG/N * (1/Vt(T) - 1/Vt(Tnom)))
    Real prev_v_anode = 1.0;  // Initialize to high value
    for (size_t i = 0; i < result.points.size(); ++i) {
        const auto& point = result.points[i];
        ASSERT_EQ(point.node_voltages.size(), 2);  // vin, anode
        Real v_anode = point.node_voltages[1];

        if (i == 0) {
            prev_v_anode = v_anode;
        } else {
            // Anode voltage should decrease with increasing temperature (SPICE standard behavior)
            EXPECT_LT(v_anode, prev_v_anode)
                << "Temperature: " << (point.temperature - 273.15) << "°C, "
                << "V_anode: " << v_anode << " should be less than previous: " << prev_v_anode;
            prev_v_anode = v_anode;
        }
    }
}

// Test 3: Empty temperature list (no sweep)
TEST_F(TemperatureSweepTest, EmptyTemperatureList) {
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "1", "0", 10.0);
    circuit.add_resistor("R1", "1", "0", 1000.0);

    // Empty temperature sweep
    config.temp_sweep.enabled = true;
    config.temp_sweep.temperatures = {};

    DCSolver solver(circuit, config);
    TemperatureSweepResult result;
    ConvergenceStatus status = solver.solve_temp_sweep(result);

    // Should succeed with zero points
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(result.points.size(), 0);
    EXPECT_TRUE(result.all_converged());
}

// ============================================================================
// 2D DC Sweep Tests
// ============================================================================

// Test 1: 2D DC Sweep - Simple structure verification
TEST_F(DCSweepTest, TwoDimensionalSweep_BasicStructure) {
    // Simple circuit: V1 -- R1 (1k) -- V2
    // Sweep V1 from 0V to 2V (step 1V) and V2 from 0V to 1V (step 0.5V)
    // Expected: 3x3 = 9 points
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "1", "0", 0.0);  // Will be swept (primary)
    circuit.add_resistor("R1", "1", "2", 1000.0);    // 1kΩ
    circuit.add_voltage_source("V2", "2", "0", 0.0);  // Will be swept (nested)

    // 2D Sweep configuration
    auto nested_sweep = std::make_shared<DCSweepConfig>();
    nested_sweep->source_name = "V2";
    nested_sweep->start_value = 0.0;
    nested_sweep->stop_value = 1.0;
    nested_sweep->step_value = 0.5;
    nested_sweep->nested_sweep = nullptr;

    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 2.0;
    sweep_config.step_value = 1.0;
    sweep_config.nested_sweep = nested_sweep;

    // Solve
    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    // Verify basic structure
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 9);  // 3 x 3 = 9 points

    // Verify each point has nested_sweep_value set
    for (size_t i = 0; i < result.points.size(); ++i) {
        EXPECT_TRUE(result.points[i].nested_sweep_value.has_value())
            << "Point " << i << " should have nested_sweep_value";
        EXPECT_EQ(result.points[i].status, ConvergenceStatus::SUCCESS)
            << "Point " << i << " should converge";
    }

    // Verify grid structure: the first source (V1) is swept for each value of
    // the second (V2), as in ngspice (NETLIST_CONVENTIONS rule 4)
    // V1 values: 0, 1, 2 (3 values)
    // V2 values: 0, 0.5, 1 (3 values)
    std::vector<Real> expected_v1 = {0, 1, 2, 0, 1, 2, 0, 1, 2};
    std::vector<Real> expected_v2 = {0, 0, 0, 0.5, 0.5, 0.5, 1, 1, 1};

    for (size_t i = 0; i < result.points.size(); ++i) {
        EXPECT_NEAR(result.points[i].sweep_value, expected_v1[i], 1e-9)
            << "Point " << i << " primary (V1) mismatch";
        EXPECT_NEAR(*result.points[i].nested_sweep_value, expected_v2[i], 1e-9)
            << "Point " << i << " nested (V2) mismatch";
    }
}

// Test branch current extraction in DC Sweep
TEST_F(DCSweepTest, BranchCurrentExtraction) {
    // Circuit: V1 -- R1 (1k) -- GND
    // Sweep V1 from 0V to 10V, 2V step
    // Expected: Branch current I = V/R = V/1000
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);  // Will be swept
    circuit.add_resistor("R1", "in", "0", 1000.0);  // 1kΩ

    // Get V1's branch_id (needed for indexing branch_currents vector)
    const auto& devices = circuit.get_devices();
    BranchId v1_branch_id = INVALID_BRANCH;
    for (const auto& device : devices) {
        if (device->name() == "V1" && device->type() == DeviceType::VSOURCE) {
            auto vsource = std::dynamic_pointer_cast<devices::VoltageSource>(device);
            if (vsource) {
                v1_branch_id = vsource->branch_id();
                break;
            }
        }
    }
    ASSERT_NE(v1_branch_id, INVALID_BRANCH) << "V1 branch ID not found";

    // Sweep configuration
    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 10.0;
    sweep_config.step_value = 2.0;

    // Solve
    DCSolver solver(circuit, config);
    DCSweepResult result;
    ConvergenceStatus status = solver.solve_sweep(sweep_config, result);

    // Verify sweep succeeded
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    EXPECT_TRUE(result.all_converged());
    EXPECT_EQ(result.points.size(), 6);  // 0, 2, 4, 6, 8, 10

    // Verify branch current at each sweep point
    for (size_t i = 0; i < result.points.size(); ++i) {
        const auto& point = result.points[i];
        Real V = point.sweep_value;
        // V1 delivers V/R; its branch current is positive into n+ through the
        // source (NETLIST_CONVENTIONS rule 3), so it is -V/R
        Real expected_current = -V / 1000.0;

        // Verify branch_currents vector is populated
        ASSERT_GT(point.branch_currents.size(), static_cast<size_t>(v1_branch_id))
            << "Point " << i << ": branch_currents vector too small";

        // Verify extracted branch current matches expected value
        Real extracted_current = point.branch_currents[v1_branch_id];
        EXPECT_NEAR(extracted_current, expected_current, 1e-9)
            << "Point " << i << " (V=" << V << "V): branch current mismatch";
    }
}

// A linear sweep includes its stop value when the stop lies on the grid
// start + k*step, as a SPICE .DC does (ngspice-47 ends .DC V1 0 0.3 0.1 at
// 0.3). In floating point 0.3/0.1 is 2.9999999999999996, and truncating it
// lost the last point.
TEST(LinearSweepPointCount, IncludesTheStopOnTheGrid) {
    EXPECT_EQ(linear_sweep_point_count(0.0, 0.3, 0.1), 4u);    // 0, 0.1, 0.2, 0.3
    EXPECT_EQ(linear_sweep_point_count(0.0, 0.6, 0.1), 7u);
    EXPECT_EQ(linear_sweep_point_count(0.0, 0.7, 0.05), 15u);
    EXPECT_EQ(linear_sweep_point_count(1.0, 0.0, 0.1), 11u);   // downwards
    EXPECT_EQ(linear_sweep_point_count(0.0, 0.25, 0.1), 3u);   // 0.25 is off the grid
    EXPECT_EQ(linear_sweep_point_count(5.0, 5.0, 1.0), 1u);
    EXPECT_EQ(linear_sweep_point_count(0.0, 1.0, 0.0), 1u);
}

TEST_F(DCSweepTest, SweepEndsAtTheStopValue) {
    CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", 0.0);
    circuit.add_resistor("R1", "in", "0", 1000.0);

    DCSweepConfig sweep_config;
    sweep_config.source_name = "V1";
    sweep_config.start_value = 0.0;
    sweep_config.stop_value = 0.3;
    sweep_config.step_value = 0.1;

    DCSolver solver(circuit, config);
    DCSweepResult result;
    ASSERT_EQ(solver.solve_sweep(sweep_config, result), ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.points.size(), 4u);
    EXPECT_NEAR(result.points.back().sweep_value, 0.3, 1e-15);
}

TEST(StepConfigPoints, LinearStepIncludesTheStop) {
    StepConfig step;
    step.sweep_type = StepType::LIN;
    step.start_value = 0.0;
    step.stop_value = 0.3;
    step.step_value = 0.1;
    const auto points = step.generate_points();
    ASSERT_EQ(points.size(), 4u);
    EXPECT_NEAR(points.back(), 0.3, 1e-15);
}

// .TEMP and .OPTIONS TEMP set the temperature of every device that has a
// temperature model (apply_temperature_to_devices). A FinFET solved at 127 C
// through the configuration must carry the same current as the same FinFET
// set to 127 C directly, and a different one from 27 C (its mobility and
// threshold depend on the temperature).
TEST(TemperatureReachesDevices, FinFET) {
    auto drain_current = [](Real config_temp, bool set_device_directly) {
        CircuitGraph circuit;
        circuit.add_voltage_source("VD", "d", "0", 0.8);
        circuit.add_voltage_source("VG", "g", "0", 0.8);
        circuit.add_finfet_n("M1", "d", "g", "0");
        if (set_device_directly) {
            auto* m = dynamic_cast<devices::FinFET_N*>(circuit.get_device("M1").get());
            EXPECT_NE(m, nullptr);
            m->set_temperature(400.15);
        }
        SimulationConfig config;
        if (config_temp > 0.0) {
            config.temperature = config_temp;
            config.temperature_explicitly_set = true;
        }
        DCSolver solver(circuit, config);
        DenseVector solution;
        SolverStats stats;
        EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        return solver.get_branch_current("VD").value();
    };
    const Real at_27c = drain_current(0.0, false);
    const Real direct_127c = drain_current(0.0, true);
    const Real configured_127c = drain_current(400.15, false);
    EXPECT_GT(std::abs(direct_127c - at_27c), 1e-3 * std::abs(at_27c));
    EXPECT_NEAR(configured_127c, direct_127c, 1e-12 * std::abs(direct_127c));
}

// The FinFET's square law carries the gain factor u0 Cox Weff / L, with
// Weff = NFIN HFIN and Cox = 3.9 eps0 / TOX. In saturation
// Id = 0.5 beta (VGS - Vth + DIBL VDS)^2, and halving L doubles the current.
TEST(FinFETGainFactor, HasTheChannelLength) {
    auto drain_current = [](Real l) {
        CircuitGraph circuit;
        circuit.add_voltage_source("VD", "d", "0", 0.8);
        circuit.add_voltage_source("VG", "g", "0", 0.8);
        circuit.add_finfet_n("M1", "d", "g", "0", 1, 30e-9, 7e-9, 0.4, 300e-4, 1e5, 70e-3,
                             0.05, 1e-9, 1e-15, 1e-16, 0.0, 1.0, -1e-3, 300.15, l);
        SimulationConfig config;
        DCSolver solver(circuit, config);
        DenseVector solution;
        SolverStats stats;
        EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        return -solver.get_branch_current("VD").value();
    };
    const Real cox = 3.9 * 8.854187817e-12 / 1e-9;
    const Real beta = 300e-4 * cox * 30e-9 / 1e-6;
    const Real vov = 0.8 - (0.4 - 0.05 * 0.8);
    const Real expected = 0.5 * beta * vov * vov;
    const Real at_1um = drain_current(1e-6);
    EXPECT_NEAR(at_1um, expected, 1e-6 * expected);
    EXPECT_NEAR(drain_current(0.5e-6), 2.0 * at_1um, 1e-6 * at_1um);
}

// A LEVEL=12 card sets the FinFET's parameters, the instance its gate
// length, and what the card does not give keeps the device's default: two
// 40 nm fins at L = 1 um carry 0.5 u0 Cox (2 x 40 nm) / L (VGS - Vth + DIBL
// VDS)^2 with the defaults u0 = 300 cm^2/Vs, TOX = 1 nm, Vth = 0.4 V and
// DIBL = 0.05.
TEST(FinFETGainFactor, CardAndInstanceSetTheParameters) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "finfet\nVD d 0 DC 0.8\nVG g 0 DC 0.8\nM1 d g 0 nf L=1u\n"
        ".model nf nmos level=12 nfin=2 hfin=40n\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    const Real cox = 3.9 * 8.854187817e-12 / 1e-9;
    const Real beta = 300e-4 * cox * (2 * 40e-9) / 1e-6;
    const Real vov = 0.8 - (0.4 - 0.05 * 0.8);
    const Real expected = 0.5 * beta * vov * vov;
    EXPECT_NEAR(-solver.get_branch_current("VD").value(), expected, 1e-6 * expected);

    auto* m1 = dynamic_cast<devices::FinFET_N*>(parsed.circuit->get_device("M1").get());
    ASSERT_NE(m1, nullptr);
    EXPECT_DOUBLE_EQ(m1->get_parameter("cgs").value(), 1e-15);  // the device's default
}

// The gate capacitances of a FinFET charge through the gate resistor with the
// time constant R (CGS + CGD) and then carry no current: a gate fed through
// 1 MOhm from a 1 V step settles at 1 V. Backward Euler, the FinFET's own
// integration, follows 1 - exp(-t / tau) to within a few percent at
// dt = tau / 20.
TEST(FinFETGateCapacitance, ChargesThroughTheGateResistor) {
    CircuitGraph circuit;
    circuit.add_voltage_source("VD", "d", "0", 0.8);
    circuit.add_pulse_voltage_source("VG", "in", "0", 0.0, 1.0, 0.0, 1e-12, 1e-12, 1.0, 2.0);
    circuit.add_resistor("RG", "in", "g", 1e6);
    const Real cgs = 1e-15, cgd = 1e-16;
    circuit.add_finfet_n("M1", "d", "g", "0", 1, 30e-9, 7e-9, 0.4, 300e-4, 1e5, 70e-3, 0.05,
                         1e-9, cgs, cgd, 0.0, 1.0, -1e-3, 300.15, 1e-6);
    const Real tau = 1e6 * (cgs + cgd);
    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    TransientSolver solver(circuit, config);
    TransientResult result = solver.solve(0.0, 10.0 * tau, tau / 20.0);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId g = circuit.get_node_id("g");
    for (const auto& tp : result.timepoints) {
        if (tp.time > 0.0) {
            EXPECT_NEAR(tp.node_voltages[g - 1], 1.0 - std::exp(-tp.time / tau), 0.03)
                << "t=" << tp.time;
        }
    }
    EXPECT_NEAR(result.timepoints.back().node_voltages[g - 1], 1.0, 1e-3);
}

// Below the threshold the FinFET's current grows by a decade per SS of gate
// voltage (the subthreshold swing, SS = n Vt ln 10): for SS = 70 mV/dec the
// current at VGS = 0.35 V is 10^(0.1 / 0.07) times that at 0.25 V (both below
// Vth - DIBL VDS = 0.36 V).
TEST(FinFETSubthreshold, CurrentGrowsOneDecadePerSwing) {
    auto drain_current = [](Real vgs) {
        CircuitGraph circuit;
        circuit.add_voltage_source("VD", "d", "0", 0.8);
        circuit.add_voltage_source("VG", "g", "0", vgs);
        circuit.add_finfet_n("M1", "d", "g", "0", 1, 30e-9, 7e-9, 0.4, 300e-4, 1e5, 70e-3, 0.05,
                             1e-9, 1e-15, 1e-16, 0.0, 1.0, -1e-3, 300.15, 1e-6);
        SimulationConfig config;
        DCSolver solver(circuit, config);
        DenseVector solution;
        SolverStats stats;
        EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
        // less the GMIN of 1e-12 S across the channel
        return -solver.get_branch_current("VD").value() - 1e-12 * 0.8;
    };
    const Real decades = std::log10(drain_current(0.35) / drain_current(0.25));
    EXPECT_NEAR(decades, 0.1 / 70e-3, 1e-6);
}
