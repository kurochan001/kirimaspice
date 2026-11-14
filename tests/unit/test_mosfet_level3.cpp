// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_mosfet_level3.cpp
#include <gtest/gtest.h>
#include "kirimaspice/devices/mosfet_level3.hpp"  // Unified template
#include "kirimaspice/circuit/circuit_graph.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::circuit;

//==============================================================================
// Helper: Create standard MOSFETLevel3Params for testing
//==============================================================================
MOSFETLevel3Params create_standard_params() {
    MOSFETLevel3Params params;

    // Process parameters
    params.tox = 20e-9;      // 20nm oxide thickness
    params.nsub = 1e17;      // 10^17 cm^-3 substrate doping
    params.nss = 0.0;        // No surface states
    params.tpg = 1;          // N+ poly gate

    // Threshold voltage
    params.vto = 0.7;        // 0.7V threshold
    params.phi = 0.7;        // 0.7V surface potential
    params.gamma = 0.5;      // 0.5 V^0.5 body effect

    // Mobility
    params.uo = 600.0;       // 600 cm^2/V-s mobility
    params.theta = 0.1;      // Mobility degradation
    params.vmax = 1e5;       // 10^5 cm/s saturation velocity

    // Short-channel effects
    params.eta = 0.0;        // No static feedback
    params.kappa = 0.0;      // Disabled for now (needs proper scaling)
    params.delta = 0.0;      // No narrow-width effect

    // Geometry
    params.w = 10e-6;        // 10µm width
    params.l = 1e-6;         // 1µm length
    params.ld = 0.0;         // No lateral diffusion
    params.wd = 0.0;

    // Capacitances (zero for now)
    params.cgso = 0.0;
    params.cgdo = 0.0;
    params.cgbo = 0.0;
    params.cbd = 0.0;
    params.cbs = 0.0;

    return params;
}

//==============================================================================
// MOSFET_Level3_N - Basic Construction Tests
//==============================================================================
TEST(MOSFET_Level3_N, Construction) {
    auto params = create_standard_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    EXPECT_EQ(mosfet.id(), 1);
    EXPECT_EQ(mosfet.name(), "M1");
    EXPECT_EQ(mosfet.type(), DeviceType::MOSFET_LEVEL3_N);
    EXPECT_EQ(mosfet.drain(), 1);
    EXPECT_EQ(mosfet.gate(), 2);
    EXPECT_EQ(mosfet.source(), 0);
    EXPECT_EQ(mosfet.bulk(), 0);
    EXPECT_FALSE(mosfet.is_linear());
    EXPECT_FALSE(mosfet.is_dynamic());  // No capacitances
}

TEST(MOSFET_Level3_N, ConstructionWithInvalidParameters) {
    auto params = create_standard_params();

    // Negative W should throw
    params.w = -10e-6;
    EXPECT_THROW(MOSFET_Level3_N(1, "M1", 1, 2, 0, 0, params), std::runtime_error);

    // Zero W should throw
    params = create_standard_params();
    params.w = 0.0;
    EXPECT_THROW(MOSFET_Level3_N(1, "M1", 1, 2, 0, 0, params), std::runtime_error);

    // Negative L should throw
    params = create_standard_params();
    params.l = -1e-6;
    EXPECT_THROW(MOSFET_Level3_N(1, "M1", 1, 2, 0, 0, params), std::runtime_error);

    // Zero L should throw
    params = create_standard_params();
    params.l = 0.0;
    EXPECT_THROW(MOSFET_Level3_N(1, "M1", 1, 2, 0, 0, params), std::runtime_error);
}

//==============================================================================
// MOSFET_Level3_N - Threshold Voltage Tests
//==============================================================================
TEST(MOSFET_Level3_N, ThresholdVoltage_ZeroBodyBias) {
    auto params = create_standard_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Vbs = 0V → Vth = Vto (no body effect)
    Real vth = mosfet.get_threshold_voltage(0.0);

    EXPECT_NEAR(vth, params.vto, 1e-6);
}

TEST(MOSFET_Level3_N, ThresholdVoltage_BodyEffect) {
    auto params = create_standard_params();
    params.gamma = 0.5;  // 0.5 V^0.5
    params.phi = 0.7;    // 0.7V
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Vbs = -2V (reverse bias) → Vth increases
    Real vbs = -2.0;
    Real vth = mosfet.get_threshold_voltage(vbs);

    // Vth(Vbs) = Vto + gamma × (sqrt(phi - Vbs) - sqrt(phi))
    // Vth = 0.7 + 0.5 × (sqrt(0.7 - (-2.0)) - sqrt(0.7))
    // Vth = 0.7 + 0.5 × (sqrt(2.7) - sqrt(0.7))
    // Vth = 0.7 + 0.5 × (1.643 - 0.837) = 0.7 + 0.403 = 1.103V
    Real expected_vth = params.vto + params.gamma *
        (std::sqrt(params.phi - vbs) - std::sqrt(params.phi));

    EXPECT_NEAR(vth, expected_vth, 1e-6);
    EXPECT_GT(vth, params.vto);  // Vth should increase with reverse bias
}

//==============================================================================
// MOSFET_Level3_N - Operating Region Tests
//==============================================================================
TEST(MOSFET_Level3_N, RegionDetection_Cutoff) {
    auto params = create_standard_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Vgs < Vth → Cutoff
    Real vgs = 0.5;  // < 0.7V
    Real vds = 1.0;
    Real vbs = 0.0;

    auto region = mosfet.get_region(vgs, vds, vbs);
    EXPECT_EQ(region, MOSFET_Level3_N::Region::CUTOFF);
}

TEST(MOSFET_Level3_N, RegionDetection_Linear) {
    auto params = create_standard_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Vgs > Vth, Vds < Vgs - Vth → Linear
    Real vgs = 2.0;   // > 0.7V
    Real vds = 0.5;   // < Vgs - Vth = 1.3V
    Real vbs = 0.0;

    auto region = mosfet.get_region(vgs, vds, vbs);
    EXPECT_EQ(region, MOSFET_Level3_N::Region::LINEAR);
}

TEST(MOSFET_Level3_N, RegionDetection_Saturation) {
    auto params = create_standard_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Vgs > Vth, Vds >= Vgs - Vth → Saturation
    Real vgs = 2.0;   // > 0.7V
    Real vds = 2.0;   // >= Vgs - Vth = 1.3V
    Real vbs = 0.0;

    auto region = mosfet.get_region(vgs, vds, vbs);
    EXPECT_EQ(region, MOSFET_Level3_N::Region::SATURATION);
}

//==============================================================================
// MOSFET_Level3_N - Drain Current Tests
//==============================================================================
TEST(MOSFET_Level3_N, DrainCurrent_CutoffRegion) {
    auto params = create_standard_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    Real vgs = 0.5;  // < Vth
    Real vds = 1.0;
    Real vbs = 0.0;

    Real ids = mosfet.get_drain_current(vgs, vds, vbs);

    EXPECT_NEAR(ids, 0.0, 1e-20);
}

TEST(MOSFET_Level3_N, DrainCurrent_LinearRegion) {
    auto params = create_standard_params();
    params.theta = 0.0;   // No mobility degradation
    params.vmax = 1e10;   // No velocity saturation
    params.kappa = 0.0;   // No CLM
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    Real vgs = 2.0;
    Real vds = 0.5;
    Real vbs = 0.0;

    Real ids = mosfet.get_drain_current(vgs, vds, vbs);

    // Simple linear region formula (no advanced effects)
    // Ids = beta × ((Vgs - Vth) × Vds - 0.5 × Vds²)
    Real vth = mosfet.get_threshold_voltage(vbs);
    Real vgs_eff = vgs - vth;

    // Should be in linear region and have non-zero current
    EXPECT_GT(ids, 0.0);
    EXPECT_LT(ids, 1e-3);  // Reasonable current range
}

TEST(MOSFET_Level3_N, DrainCurrent_SaturationRegion) {
    auto params = create_standard_params();
    params.theta = 0.0;   // No mobility degradation
    params.vmax = 1e10;   // No velocity saturation
    params.kappa = 0.0;   // No CLM
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    Real vgs = 2.0;
    Real vds = 2.0;
    Real vbs = 0.0;

    Real ids = mosfet.get_drain_current(vgs, vds, vbs);

    // Should be in saturation and have non-zero current
    EXPECT_GT(ids, 0.0);
    EXPECT_LT(ids, 1e-3);  // Reasonable current range
}

TEST(MOSFET_Level3_N, DrainCurrent_MobilityDegradation) {
    auto params = create_standard_params();
    params.vmax = 1e10;   // No velocity saturation
    params.kappa = 0.0;   // No CLM

    // Compare with and without mobility degradation
    params.theta = 0.0;
    MOSFET_Level3_N mosfet_no_deg(1, "M1", 1, 2, 0, 0, params);

    params.theta = 0.1;  // With degradation
    MOSFET_Level3_N mosfet_with_deg(2, "M2", 1, 2, 0, 0, params);

    Real vgs = 2.0;
    Real vds = 2.0;
    Real vbs = 0.0;

    Real ids_no_deg = mosfet_no_deg.get_drain_current(vgs, vds, vbs);
    Real ids_with_deg = mosfet_with_deg.get_drain_current(vgs, vds, vbs);

    // Current should decrease with mobility degradation
    EXPECT_LT(ids_with_deg, ids_no_deg);
}

TEST(MOSFET_Level3_N, DrainCurrent_ChannelLengthModulation) {
    auto params = create_standard_params();
    params.theta = 0.0;   // No mobility degradation
    params.vmax = 0.0;    // No velocity saturation

    // Compare with and without CLM: level 3 takes it from KAPPA with NSUB
    // (LAMBDA is not a level 3 parameter)
    params.kappa = 0.0;
    MOSFET_Level3_N mosfet_no_clm(1, "M1", 1, 2, 0, 0, params);

    params.kappa = 0.2;  // With CLM
    MOSFET_Level3_N mosfet_with_clm(2, "M2", 1, 2, 0, 0, params);

    Real vgs = 2.0;
    Real vds = 2.0;
    Real vbs = 0.0;

    Real ids_no_clm = mosfet_no_clm.get_drain_current(vgs, vds, vbs);
    Real ids_with_clm = mosfet_with_clm.get_drain_current(vgs, vds, vbs);

    // Current should increase with CLM (in saturation)
    EXPECT_GT(ids_with_clm, ids_no_clm);
}

//==============================================================================
// MOSFET_Level3_N - Small-Signal Parameters Tests
//==============================================================================
TEST(MOSFET_Level3_N, Transconductance_Saturation) {
    auto params = create_standard_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    Real vgs = 2.0;
    Real vds = 2.0;
    Real vbs = 0.0;

    Real gm = mosfet.get_transconductance(vgs, vds, vbs);

    // gm should be positive in saturation
    EXPECT_GT(gm, 0.0);
    EXPECT_LT(gm, 1e-2);  // Reasonable range
}

TEST(MOSFET_Level3_N, OutputConductance_Saturation) {
    auto params = create_standard_params();
    params.kappa = 0.2;  // Enable CLM (level 3 takes it from KAPPA with NSUB)
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    Real vgs = 2.0;
    Real vds = 2.0;
    Real vbs = 0.0;

    Real gds = mosfet.get_output_conductance(vgs, vds, vbs);

    // gds should be positive (CLM effect)
    EXPECT_GT(gds, 0.0);
    EXPECT_LT(gds, 1e-3);  // Should be small
}

TEST(MOSFET_Level3_N, BulkTransconductance_BodyEffect) {
    auto params = create_standard_params();
    params.gamma = 0.5;  // Enable body effect
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 3, params);  // Separate bulk node

    Real vgs = 2.0;
    Real vds = 2.0;
    Real vbs = -1.0;  // Reverse bias

    Real gmb = mosfet.get_bulk_transconductance(vgs, vds, vbs);

    // gmb should be positive (current increases when Vbs increases toward 0V)
    // Physical interpretation: dIds/dVbs > 0 because less reverse bias → lower Vth → higher Ids
    EXPECT_GT(gmb, 0.0);
    EXPECT_LT(gmb, 1e-3);  // Reasonable range
}

//==============================================================================
// MOSFET_Level3_P - Basic Tests
//==============================================================================
TEST(MOSFET_Level3_P, Construction) {
    auto params = create_standard_params();
    params.vto = -0.7;  // Negative for P-channel
    MOSFET_Level3_P mosfet(1, "M1", 1, 2, 3, 3, params);

    EXPECT_EQ(mosfet.id(), 1);
    EXPECT_EQ(mosfet.name(), "M1");
    EXPECT_EQ(mosfet.type(), DeviceType::MOSFET_LEVEL3_P);
    EXPECT_EQ(mosfet.drain(), 1);
    EXPECT_EQ(mosfet.gate(), 2);
    EXPECT_EQ(mosfet.source(), 3);
    EXPECT_EQ(mosfet.bulk(), 3);
    EXPECT_FALSE(mosfet.is_linear());
}

TEST(MOSFET_Level3_P, DrainCurrent_CutoffRegion) {
    auto params = create_standard_params();
    params.vto = -0.7;  // Negative for P-channel
    MOSFET_Level3_P mosfet(1, "M1", 1, 2, 3, 3, params);

    // P-channel: Vsg < |Vth| → Cutoff
    Real vsg = 0.5;  // < |Vth| = 0.7V
    Real vsd = 1.0;
    Real vsb = 0.0;

    Real ids = mosfet.get_drain_current(vsg, vsd, vsb);

    EXPECT_NEAR(ids, 0.0, 1e-20);
}

TEST(MOSFET_Level3_P, DrainCurrent_SaturationRegion) {
    auto params = create_standard_params();
    params.vto = -0.7;
    params.theta = 0.0;
    params.vmax = 1e10;
    params.kappa = 0.0;
    MOSFET_Level3_P mosfet(1, "M1", 1, 2, 3, 3, params);

    // P-channel saturation: Vsg > |Vth|, Vsd >= Vsg - |Vth|
    Real vsg = 2.0;
    Real vsd = 2.0;
    Real vsb = 0.0;

    Real ids = mosfet.get_drain_current(vsg, vsd, vsb);

    // Should have non-zero positive current (flows S→D)
    EXPECT_GT(ids, 0.0);
    EXPECT_LT(ids, 1e-3);
}

//==============================================================================
// MOSFET_Level3 - N vs P Symmetry Tests
//==============================================================================
TEST(MOSFET_Level3_Symmetry, NChannelVsPChannel_SameMagnitude) {
    // Create identical parameters for N and P
    auto params_n = create_standard_params();
    params_n.vto = 0.7;

    auto params_p = create_standard_params();
    params_p.vto = -0.7;  // Opposite polarity

    MOSFET_Level3_N mosfet_n(1, "MN", 1, 2, 0, 0, params_n);
    MOSFET_Level3_P mosfet_p(2, "MP", 1, 2, 3, 3, params_p);

    // Same drive conditions (opposite polarity)
    Real vgs_n = 2.0;
    Real vds_n = 2.0;
    Real vbs_n = 0.0;

    Real vsg_p = 2.0;  // Equivalent to Vgs for N
    Real vsd_p = 2.0;  // Equivalent to Vds for N
    Real vsb_p = 0.0;

    Real ids_n = mosfet_n.get_drain_current(vgs_n, vds_n, vbs_n);
    Real ids_p = mosfet_p.get_drain_current(vsg_p, vsd_p, vsb_p);

    // Currents should be approximately equal in magnitude
    EXPECT_NEAR(ids_n, ids_p, ids_n * 0.05);  // Within 5%
}
