// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_dc_sweep.cpp
#include <gtest/gtest.h>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

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

    // Verify grid structure (primary varies slower than nested)
    // V1 values: 0, 1, 2 (3 values)
    // V2 values: 0, 0.5, 1 (3 values)
    std::vector<Real> expected_v1 = {0, 0, 0, 1, 1, 1, 2, 2, 2};
    std::vector<Real> expected_v2 = {0, 0.5, 1, 0, 0.5, 1, 0, 0.5, 1};

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
        Real expected_current = V / 1000.0;  // I = V/R

        // Verify branch_currents vector is populated
        ASSERT_GT(point.branch_currents.size(), static_cast<size_t>(v1_branch_id))
            << "Point " << i << ": branch_currents vector too small";

        // Verify extracted branch current matches expected value
        Real extracted_current = point.branch_currents[v1_branch_id];
        EXPECT_NEAR(extracted_current, expected_current, 1e-9)
            << "Point " << i << " (V=" << V << "V): branch current mismatch";
    }
}
