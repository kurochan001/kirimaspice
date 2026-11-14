// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_jfet.cpp
#include <gtest/gtest.h>
#include "kirimaspice/devices/jfet.hpp"  // Unified JFET template
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;

//==============================================================================
// JFET_N - Basic Construction Tests
//==============================================================================
TEST(JFET_N, Construction) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12);

    EXPECT_EQ(jfet.id(), 1);
    EXPECT_EQ(jfet.name(), "J1");
    EXPECT_EQ(jfet.type(), DeviceType::JFET_N);
    EXPECT_EQ(jfet.drain(), 1);
    EXPECT_EQ(jfet.gate(), 2);
    EXPECT_EQ(jfet.source(), 0);
    EXPECT_DOUBLE_EQ(jfet.transconductance_parameter(), 1e-4);
    EXPECT_DOUBLE_EQ(jfet.pinchoff_voltage(), -2.0);
    EXPECT_DOUBLE_EQ(jfet.channel_modulation(), 0.01);
    EXPECT_DOUBLE_EQ(jfet.gate_saturation_current(), 1e-14);
    EXPECT_FALSE(jfet.is_linear());
    EXPECT_TRUE(jfet.is_dynamic());  // Has capacitances
}

TEST(JFET_N, ConstructionWithInvalidParameters) {
    // Negative BETA should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, -1e-4, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Zero BETA should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 0.0, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative LAMBDA should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, -0.01, 1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative IS should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, -1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Zero IS should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 0.0, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative n should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, -1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative capacitance should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0, -1e-12, 1e-12),
                 std::runtime_error);
}

//==============================================================================
// JFET_N - I-V Characteristic Tests
//==============================================================================
TEST(JFET_N, DrainCurrentCutoffRegion) {
    // Standard N-channel JFET: BETA=1e-4, Vto=-2.0V, LAMBDA=0.01
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Cutoff region: Vgs < Vto (-2.0V)
    Real vgs = -2.5;  // < -2.0V
    Real vds = 1.0;

    Real id = jfet.get_drain_current(vgs, vds);

    // Id should be zero in cutoff
    EXPECT_NEAR(id, 0.0, 1e-20);
}

TEST(JFET_N, DrainCurrentLinearRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Linear region: Vgs > Vto, Vds < Vgs - Vto
    Real vgs = -1.0;  // > Vto (-2.0V)
    Real vds = 0.5;   // < Vgs - Vto = 1.0V

    // Id = BETA × (2(Vgs - Vto) × Vds - Vds²) × (1 + LAMBDA × Vds)
    // Id = 1e-4 × (2(-1.0 - (-2.0)) × 0.5 - 0.5²) × (1 + 0.01 × 0.5)
    // Id = 1e-4 × (2 × 1.0 × 0.5 - 0.25) × 1.005
    // Id = 1e-4 × (1.0 - 0.25) × 1.005
    // Id = 1e-4 × 0.75 × 1.005 = 7.5375e-5 A

    Real id = jfet.get_drain_current(vgs, vds);

    Real vgs_eff = vgs - (-2.0);  // 1.0V
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_id = beta * (2.0 * vgs_eff * vds - vds * vds) * (1.0 + lambda * vds);

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

TEST(JFET_N, DrainCurrentSaturationRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Saturation region: Vgs > Vto, Vds >= Vgs - Vto
    Real vgs = -1.0;  // > Vto (-2.0V)
    Real vds = 2.0;   // >= Vgs - Vto = 1.0V

    // Id = BETA × (Vgs - Vto)² × (1 + LAMBDA × Vds)
    // Id = 1e-4 × 1.0² × (1 + 0.01 × 2.0)
    // Id = 1e-4 × 1.0 × 1.02 = 1.02e-4 A

    Real id = jfet.get_drain_current(vgs, vds);

    Real vgs_eff = vgs - (-2.0);  // 1.0V
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_id = beta * vgs_eff * vgs_eff * (1.0 + lambda * vds);

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

TEST(JFET_N, DrainCurrentZeroBias) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Vgs = 0.0V (> Vto = -2.0V, so device is on)
    // Vds = 0.0V (linear region boundary)
    Real vgs = 0.0;
    Real vds = 0.0;

    Real id = jfet.get_drain_current(vgs, vds);

    // At Vds=0, Id should be zero (boundary condition)
    EXPECT_NEAR(id, 0.0, 1e-20);
}

//==============================================================================
// JFET_N - Conductance Tests
//==============================================================================
TEST(JFET_N, TransconductanceLinearRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Linear region
    Real vgs = -1.0;
    Real vds = 0.5;

    // gm = 2 × BETA × Vds × (1 + LAMBDA × Vds)
    Real gm = jfet.get_transconductance(vgs, vds);

    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_gm = 2.0 * beta * vds * (1.0 + lambda * vds);

    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);  // Within 1%
}

TEST(JFET_N, TransconductanceSaturationRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Saturation region
    Real vgs = -1.0;
    Real vds = 2.0;

    // gm = 2 × BETA × (Vgs - Vto) × (1 + LAMBDA × Vds)
    Real gm = jfet.get_transconductance(vgs, vds);

    Real vgs_eff = vgs - (-2.0);  // 1.0V
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_gm = 2.0 * beta * vgs_eff * (1.0 + lambda * vds);

    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);  // Within 1%
}

TEST(JFET_N, OutputConductanceLinearRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;
    Real vds = 0.5;

    Real gds = jfet.get_output_conductance(vgs, vds);

    // gds should be positive in linear region
    EXPECT_GT(gds, 0.0);
}

TEST(JFET_N, OutputConductanceSaturationRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;
    Real vds = 2.0;

    Real gds = jfet.get_output_conductance(vgs, vds);

    // gds = Id_basic × LAMBDA in saturation region
    Real vgs_eff = vgs - (-2.0);
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real id_basic = beta * vgs_eff * vgs_eff;
    Real expected_gds = id_basic * lambda;

    EXPECT_NEAR(gds, expected_gds, expected_gds * 0.01);
}

//==============================================================================
// JFET_N - Gate Current Tests
//==============================================================================
TEST(JFET_N, GateCurrentReverseBias) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0);

    // Normal operation: Vgs ≤ 0 (reverse biased)
    Real vgs = -0.5;

    Real ig = jfet.get_gate_current(vgs);

    // Gate current should be very small (reverse saturation)
    // Ig ≈ -IS for negative Vgs
    EXPECT_LT(std::abs(ig), 1e-13);  // Less than 100fA
}

TEST(JFET_N, GateCurrentForwardBias) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0);

    // Forward bias (not normal operation): Vgs > 0
    Real vgs = 0.7;

    Real ig = jfet.get_gate_current(vgs);

    // Gate current should be significant in forward bias
    EXPECT_GT(ig, 1e-12);  // Greater than 1pA
}

TEST(JFET_N, GateConductance) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0);

    Real vgs = 0.0;

    Real gg = jfet.get_gate_conductance(vgs);

    // Gate conductance should be positive
    EXPECT_GT(gg, 0.0);
}

//==============================================================================
// JFET_N - Region Detection Tests
//==============================================================================
TEST(JFET_N, RegionDetectionCutoff) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -2.5;  // < Vto
    Real vds = 1.0;

    auto region = jfet.get_region(vgs, vds);

    EXPECT_EQ(region, JFET_N::Region::CUTOFF);
}

TEST(JFET_N, RegionDetectionLinear) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;  // > Vto
    Real vds = 0.5;   // < Vgs - Vto

    auto region = jfet.get_region(vgs, vds);

    EXPECT_EQ(region, JFET_N::Region::LINEAR);
}

TEST(JFET_N, RegionDetectionSaturation) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;  // > Vto
    Real vds = 2.0;   // >= Vgs - Vto

    auto region = jfet.get_region(vgs, vds);

    EXPECT_EQ(region, JFET_N::Region::SATURATION);
}

//==============================================================================
// JFET_P - Basic Construction Tests
//==============================================================================
TEST(JFET_P, Construction) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12);

    EXPECT_EQ(jfet.id(), 1);
    EXPECT_EQ(jfet.name(), "J1");
    EXPECT_EQ(jfet.type(), DeviceType::JFET_P);
    EXPECT_EQ(jfet.drain(), 1);
    EXPECT_EQ(jfet.gate(), 2);
    EXPECT_EQ(jfet.source(), 0);
    EXPECT_DOUBLE_EQ(jfet.transconductance_parameter(), 1e-4);
    EXPECT_DOUBLE_EQ(jfet.pinchoff_voltage(), -2.0);
    EXPECT_FALSE(jfet.is_linear());
    EXPECT_TRUE(jfet.is_dynamic());
}

//==============================================================================
// JFET_P - I-V Characteristic Tests
//==============================================================================
TEST(JFET_P, DrainCurrentCutoffRegion) {
    // P-channel JFET: BETA=1e-4, Vto=-2.0V (internal model convention)
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Cutoff region: Vsg < |Vto| (2.0V)
    // Vsg = Vs - Vg, for cutoff Vsg < 2.0V
    Real vsg = 1.5;   // < 2.0V
    Real vsd = 1.0;

    Real id = jfet.get_drain_current(vsg, vsd);

    // Id should be zero in cutoff (P-channel: Id < 0 when on)
    EXPECT_NEAR(id, 0.0, 1e-20);
}

TEST(JFET_P, DrainCurrentSaturationRegion) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Saturation region: Vsg > |Vto|, Vsd >= Vsg - |Vto|
    Real vsg = 3.0;   // > 2.0V
    Real vsd = 2.0;   // >= Vsg - |Vto| = 1.0V

    Real id = jfet.get_drain_current(vsg, vsd);

    // Id should be positive for P-channel (same convention as MOSFET_P)
    EXPECT_GT(id, 0.0);

    // Expected value calculation
    Real vto_abs = std::abs(-2.0);  // 2.0V
    Real vsg_eff = vsg - vto_abs;   // 3.0 - 2.0 = 1.0V
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected = beta * vsg_eff * vsg_eff * (1.0 + lambda * vsd);
    // expected = 1e-4 × 1.0² × (1 + 0.01 × 2.0) = 1e-4 × 1.02 = 1.02e-4

    EXPECT_NEAR(id, expected, expected * 0.01);
}

//==============================================================================
// JFET_P - Region Detection Tests
//==============================================================================
TEST(JFET_P, RegionDetectionSaturation) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vsg = 3.0;   // > |Vto|
    Real vsd = 2.0;   // >= Vsg - |Vto|

    auto region = jfet.get_region(vsg, vsd);

    EXPECT_EQ(region, JFET_P::Region::SATURATION);
}

TEST(JFET_P, RegionDetectionCutoff) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vsg = 1.5;   // < |Vto|
    Real vsd = 1.0;

    auto region = jfet.get_region(vsg, vsd);

    EXPECT_EQ(region, JFET_P::Region::CUTOFF);
}
