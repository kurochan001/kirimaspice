// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_mosfet_p.cpp
#include <gtest/gtest.h>
#include "kirimaspice/devices/mosfet_level1.hpp"  // Step 5.1: Unified MOSFET Level 1
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

//==============================================================================
// MOSFET_P - Basic Construction Tests
//==============================================================================
TEST(MOSFET_P, Construction) {
    // Standard PMOS: Kp=2e-5, W=10µm, L=1µm, Vth=-0.7V (negative for P-channel)
    MOSFET_P mosfet(1, "M1", 1, 2, 3, 2e-5, 10e-6, 1e-6, -0.7);

    EXPECT_EQ(mosfet.id(), 1);
    EXPECT_EQ(mosfet.name(), "M1");
    EXPECT_EQ(mosfet.type(), DeviceType::MOSFET_P);
    EXPECT_EQ(mosfet.drain(), 1);
    EXPECT_EQ(mosfet.gate(), 2);
    EXPECT_EQ(mosfet.source(), 3);
    EXPECT_DOUBLE_EQ(mosfet.process_transconductance(), 2e-5);
    EXPECT_DOUBLE_EQ(mosfet.channel_width(), 10e-6);
    EXPECT_DOUBLE_EQ(mosfet.channel_length(), 1e-6);
    EXPECT_DOUBLE_EQ(mosfet.threshold_voltage(), -0.7);
    EXPECT_FALSE(mosfet.is_linear());
    EXPECT_FALSE(mosfet.is_dynamic());
}

TEST(MOSFET_P, ConstructionWithInvalidParameters) {
    // Negative Kp should throw
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, -2e-5, 10e-6, 1e-6, -0.7), std::runtime_error);

    // Zero Kp should throw
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, 0.0, 10e-6, 1e-6, -0.7), std::runtime_error);

    // Negative W should throw
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, 2e-5, -10e-6, 1e-6, -0.7), std::runtime_error);

    // Zero W should throw
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, 2e-5, 0.0, 1e-6, -0.7), std::runtime_error);

    // Negative L should throw
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, 2e-5, 10e-6, -1e-6, -0.7), std::runtime_error);

    // Zero L should throw
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, 2e-5, 10e-6, 0.0, -0.7), std::runtime_error);

    // PMOS-specific: Positive Vth should throw (Vth must be negative for P-channel)
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, 2e-5, 10e-6, 1e-6, 0.7), std::runtime_error);

    // PMOS-specific: Zero Vth should throw (Vth must be negative for P-channel)
    EXPECT_THROW(MOSFET_P(1, "M1", 1, 2, 3, 2e-5, 10e-6, 1e-6, 0.0), std::runtime_error);
}

//==============================================================================
// MOSFET_P - I-V Characteristic Tests
//==============================================================================
TEST(MOSFET_P, DrainCurrentCutoffRegion) {
    // Standard PMOS: Kp=2e-5, W=10µm, L=1µm, Vth=-0.7V
    // β = Kp × W/L = 2e-5 × 10 = 2e-4 A/V²
    MOSFET_P mosfet(1, "M1", 1, 2, 3);

    // Cutoff region: Vsg < |Vth|
    // For Vth = -0.7V, |Vth| = 0.7V
    Real vsg = 0.5;  // < 0.7V (cutoff)
    Real vsd = 1.0;

    Real id = mosfet.get_drain_current(vsg, vsd);

    // Id should be zero in cutoff
    EXPECT_NEAR(id, 0.0, 1e-20);
}

TEST(MOSFET_P, DrainCurrentLinearRegion) {
    MOSFET_P mosfet(1, "M1", 1, 2, 3, 2e-5, 10e-6, 1e-6, -0.7);

    // Linear region: Vsg > |Vth|, Vsd < Vsg - |Vth|
    Real vsg = 2.0;  // > |Vth| = 0.7V
    Real vsd = 0.5;  // < Vsg - |Vth| = 2.0 - 0.7 = 1.3V

    // Id = β × ((Vsg - |Vth|) × Vsd - Vsd²/2)
    // β = 2e-4 A/V²
    // Vsg_eff = Vsg - |Vth| = 2.0 - 0.7 = 1.3V
    // Id = 2e-4 × (1.3 × 0.5 - 0.5²/2)
    // Id = 2e-4 × (0.65 - 0.125) = 2e-4 × 0.525 = 1.05e-4 A

    Real id = mosfet.get_drain_current(vsg, vsd);

    Real vth_abs = 0.7;
    Real vsg_eff = vsg - vth_abs;  // 1.3V
    Real beta = 2e-4;
    Real expected_id = beta * (vsg_eff * vsd - 0.5 * vsd * vsd);

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

TEST(MOSFET_P, DrainCurrentSaturationRegion) {
    MOSFET_P mosfet(1, "M1", 1, 2, 3, 2e-5, 10e-6, 1e-6, -0.7);

    // Saturation region: Vsg > |Vth|, Vsd >= Vsg - |Vth|
    Real vsg = 2.0;  // > |Vth| = 0.7V
    Real vsd = 2.0;  // >= Vsg - |Vth| = 1.3V

    // Id = (β/2) × (Vsg - |Vth|)²
    // β = 2e-4 A/V²
    // Vsg_eff = 2.0 - 0.7 = 1.3V
    // Id = (2e-4 / 2) × 1.3² = 1e-4 × 1.69 = 1.69e-4 A

    Real id = mosfet.get_drain_current(vsg, vsd);

    Real vth_abs = 0.7;
    Real vsg_eff = vsg - vth_abs;  // 1.3V
    Real beta = 2e-4;
    Real expected_id = 0.5 * beta * vsg_eff * vsg_eff;

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

//==============================================================================
// MOSFET_P - Region Detection Tests
//==============================================================================
TEST(MOSFET_P, RegionDetection) {
    MOSFET_P mosfet(1, "M1", 1, 2, 3, 2e-5, 10e-6, 1e-6, -0.7);

    // Cutoff: Vsg < |Vth|
    EXPECT_EQ(mosfet.get_region(0.5, 1.0), MOSFET_P::Region::CUTOFF);

    // Linear: Vsg > |Vth|, Vsd < Vsg - |Vth|
    EXPECT_EQ(mosfet.get_region(2.0, 0.5), MOSFET_P::Region::LINEAR);

    // Saturation: Vsg > |Vth|, Vsd >= Vsg - |Vth|
    EXPECT_EQ(mosfet.get_region(2.0, 2.0), MOSFET_P::Region::SATURATION);
}

//==============================================================================
// MOSFET_P - Small-Signal Parameter Tests
//==============================================================================
TEST(MOSFET_P, TransconductanceAndOutputConductance) {
    MOSFET_P mosfet(1, "M1", 1, 2, 3, 2e-5, 10e-6, 1e-6, -0.7);

    // Saturation region: Vsg = 2.0V, Vsd = 2.0V
    Real vsg = 2.0;
    Real vsd = 2.0;
    Real vth_abs = 0.7;
    Real vsg_eff = vsg - vth_abs;  // 1.3V
    Real beta = 2e-4;

    // Transconductance in saturation: gm = β × (Vsg - |Vth|)
    Real gm = mosfet.get_transconductance(vsg, vsd);
    Real expected_gm = beta * vsg_eff;  // 2e-4 × 1.3 = 2.6e-4 S
    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);

    // Output conductance in saturation: gds ≈ gmin (ideal Level 1)
    Real gds = mosfet.get_output_conductance(vsg, vsd);
    EXPECT_NEAR(gds, 1e-12, 1e-15);  // gmin_default = 1e-12

    // Linear region: Vsg = 2.0V, Vsd = 0.5V
    vsd = 0.5;

    // Transconductance in linear: gm = β × Vsd
    gm = mosfet.get_transconductance(vsg, vsd);
    expected_gm = beta * vsd;  // 2e-4 × 0.5 = 1e-4 S
    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);

    // Output conductance in linear: gds = β × (Vsg - |Vth| - Vsd)
    gds = mosfet.get_output_conductance(vsg, vsd);
    Real expected_gds = beta * (vsg_eff - vsd);  // 2e-4 × (1.3 - 0.5) = 1.6e-4 S
    EXPECT_NEAR(gds, expected_gds, expected_gds * 0.01);
}

//==============================================================================
// MOSFET_P Circuit Tests
//==============================================================================
TEST(MOSFET_P_Circuit, SimplePMOSCircuit) {
    // Create circuit with PMOS in common-source configuration
    // VDD (5V) → M1 source
    // M1 gate ← Vg (2V, so Vsg = 3V > |Vth|)
    // M1 drain → Vout (output)

    CircuitGraph circuit;
    circuit.add_node("vdd");    // VDD = 5V (PMOS source)
    circuit.add_node("gate");   // Gate
    circuit.add_node("drain");  // Drain (output)

    // Add PMOS: name="M1", drain="drain", gate="gate", source="vdd"
    circuit.add_mosfet_p("M1", "drain", "gate", "vdd", 2e-5, 10e-6, 1e-6, -0.7);

    // Verify device is added correctly
    EXPECT_EQ(circuit.num_devices(), 1);

    auto device = circuit.get_device("M1");
    EXPECT_EQ(device->name(), "M1");
    EXPECT_EQ(device->type(), DeviceType::MOSFET_P);

    auto* pmos = dynamic_cast<MOSFET_P*>(device.get());
    ASSERT_NE(pmos, nullptr);

    // Verify PMOS parameters
    EXPECT_DOUBLE_EQ(pmos->process_transconductance(), 2e-5);
    EXPECT_DOUBLE_EQ(pmos->channel_width(), 10e-6);
    EXPECT_DOUBLE_EQ(pmos->channel_length(), 1e-6);
    EXPECT_DOUBLE_EQ(pmos->threshold_voltage(), -0.7);
}
