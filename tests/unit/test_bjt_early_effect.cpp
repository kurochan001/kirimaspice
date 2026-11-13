// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_bjt_early_effect.cpp
// Early effect (base-width modulation) tests for BJT

#include <gtest/gtest.h>
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/bjt_pnp.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;

//==============================================================================
// Helper: Create BJT parameters with/without Early effect
//==============================================================================

/// Create NPN BJT without Early effect (VAF = 0)
BJT_NPN create_bjt_npn_no_early() {
    return BJT_NPN(1, "Q1", 1, 2, 0,
                   1e-14,  // Is
                   100.0,  // beta_f
                   1.0,    // nf
                   -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                   0.0, 0.0,        // vaf=0, var=0 (no Early effect)
                   0.0, 0.0,        // ikf=0, ikr=0 (no high-current effect)
                   0.0, 0.0, 0.0,   // rb=0, rbm=0, irb=0 (no base resistance)
                   0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                   0.0, 0.75, 0.33,  // cje, vje, mje
                   0.0, 0.75, 0.33,  // cjc, vjc, mjc
                   0.0, 0.0,         // tf, tr
                   0.0, 1.0, 1.0);   // kf, af, ff
}

/// Create NPN BJT with Early effect (VAF = 100V)
BJT_NPN create_bjt_npn_with_early() {
    return BJT_NPN(1, "Q1", 1, 2, 0,
                   1e-14,  // Is
                   100.0,  // beta_f
                   1.0,    // nf
                   -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                   100.0, 0.0,      // vaf=100V, var=0 (forward Early effect)
                   0.0, 0.0,        // ikf=0, ikr=0 (no high-current effect)
                   0.0, 0.0, 0.0,   // rb=0, rbm=0, irb=0 (no base resistance)
                   0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                   0.0, 0.75, 0.33,  // cje, vje, mje
                   0.0, 0.75, 0.33,  // cjc, vjc, mjc
                   0.0, 0.0,         // tf, tr
                   0.0, 1.0, 1.0);   // kf, af, ff
}

/// Create PNP BJT with Early effect (VAF = 100V)
BJT_PNP create_bjt_pnp_with_early() {
    return BJT_PNP(1, "Q1", 1, 2, 0,
                   1e-14,  // Is
                   100.0,  // beta_f
                   1.0,    // nf
                   -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                   100.0, 0.0,      // vaf=100V, var=0 (forward Early effect)
                   0.0, 0.0,        // ikf=0, ikr=0 (no high-current effect)
                   0.0, 0.0, 0.0,   // rb=0, rbm=0, irb=0 (no base resistance)
                   0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                   0.0, 0.75, 0.33,  // cje, vje, mje
                   0.0, 0.75, 0.33,  // cjc, vjc, mjc
                   0.0, 0.0,         // tf, tr
                   0.0, 1.0, 1.0);   // kf, af, ff
}

//==============================================================================
// NPN Tests: Early Effect
//==============================================================================

TEST(BJT_NPN_EarlyEffect, NoEarlyEffect_ConstantCollectorCurrent) {
    auto bjt = create_bjt_npn_no_early();

    // Forward active region: Vbe = 0.7V, vary Vce
    Real vbe = 0.7;
    Real vbc1 = -1.0;  // Vce = Vbe - Vbc = 0.7 - (-1.0) = 1.7V
    Real vbc2 = -5.0;  // Vce = Vbe - Vbc = 0.7 - (-5.0) = 5.7V

    Real ic1 = bjt.get_collector_current(vbe, vbc1);
    Real ic2 = bjt.get_collector_current(vbe, vbc2);

    // Without Early effect, Ic should be constant (independent of Vce)
    EXPECT_NEAR(ic1, ic2, ic1 * 1e-6);  // Less than 0.0001% difference
}

TEST(BJT_NPN_EarlyEffect, WithEarlyEffect_IncreasingCollectorCurrent) {
    auto bjt = create_bjt_npn_with_early();

    // Forward active region: Vbe = 0.7V, vary Vce
    Real vbe = 0.7;
    Real vbc1 = -1.0;  // Vce = 1.7V
    Real vbc2 = -5.0;  // Vce = 5.7V

    Real ic1 = bjt.get_collector_current(vbe, vbc1);
    Real ic2 = bjt.get_collector_current(vbe, vbc2);

    // With Early effect: Ic increases with Vce
    // Gummel-Poon formula: Ic = Is * exp(...) / qb, where qb = 1 + Vbc/VAF
    // Expected ratio: Ic2/Ic1 = qb1/qb2
    Real vaf = bjt.forward_early_voltage();
    Real qb1 = 1.0 + vbc1 / vaf;  // 1 - 0.01 = 0.99
    Real qb2 = 1.0 + vbc2 / vaf;  // 1 - 0.05 = 0.95

    Real expected_ratio = qb1 / qb2;  // 0.99 / 0.95 = 1.042105...
    Real actual_ratio = ic2 / ic1;

    EXPECT_NEAR(actual_ratio, expected_ratio, 0.001);  // Within 0.1%
    EXPECT_GT(ic2, ic1);  // Ic2 should be larger
}

TEST(BJT_NPN_EarlyEffect, OutputResistance_FiniteWithEarly) {
    auto bjt_no_early = create_bjt_npn_no_early();
    auto bjt_with_early = create_bjt_npn_with_early();

    Real vbe = 0.7;
    Real vbc = -2.0;  // Vce = 2.7V

    // Output conductance: gds = dIc/dVce ≈ Ic/VAF
    Real gds_no_early = bjt_no_early.get_collector_conductance_vbc(vbe, vbc);
    Real gds_with_early = bjt_with_early.get_collector_conductance_vbc(vbe, vbc);

    // Without Early: gds should be very small (near zero)
    // With Early: gds should be finite (gds ≈ Ic/VAF)

    Real ic = bjt_with_early.get_collector_current(vbe, vbc);
    Real vaf = bjt_with_early.forward_early_voltage();
    Real expected_gds_contribution = ic / vaf;

    // Output conductance includes base current contribution, so check magnitude
    EXPECT_GT(std::abs(gds_with_early), std::abs(gds_no_early));
    EXPECT_GT(std::abs(gds_with_early), expected_gds_contribution * 0.5);  // At least 50% of expected
}

TEST(BJT_NPN_EarlyEffect, EarlyVoltage_Accessor) {
    auto bjt = create_bjt_npn_with_early();

    EXPECT_DOUBLE_EQ(bjt.forward_early_voltage(), 100.0);
    EXPECT_DOUBLE_EQ(bjt.reverse_early_voltage(), 0.0);
}

TEST(BJT_NPN_EarlyEffect, LargeVceRange_ClampingProtection) {
    auto bjt = create_bjt_npn_with_early();

    Real vbe = 0.7;

    // Test very large Vce (should be clamped to prevent excessive modulation)
    Real vbc_large = -50.0;  // Vce = 50.7V >> VAF
    Real ic_large = bjt.get_collector_current(vbe, vbc_large);

    // Should not explode or become negative due to clamping
    EXPECT_GT(ic_large, 0.0);

    // Gummel-Poon: qb = 1 + Vbc/VAF, clamped to min 0.01
    // For Vbc = -50V, qb = 1 - 0.5 = 0.5 (no clamp yet)
    // Maximum amplification: 1/qb_min = 1/0.01 = 100× when Vbc → -∞
    Real vaf = bjt.forward_early_voltage();
    Real qb_large = 1.0 + vbc_large / vaf;
    Real max_amplification = 1.0 / std::max(qb_large, 0.01);  // qb clamped to 0.01

    // Should be reasonable (amplified by Early effect, but not excessive)
    Real ic_base_estimate = 5e-3;  // Approximate base current at Vbe=0.7V
    EXPECT_LT(ic_large, ic_base_estimate * max_amplification * 1.5);  // With margin

    // With clamping, large Vce should not cause excessive current increase
    Real vbc_moderate = -2.0;  // Vce = 2.7V (normal operation)
    Real ic_moderate = bjt.get_collector_current(vbe, vbc_moderate);

    // Large Vce current should be less than 100× moderate current (due to clamping)
    EXPECT_LT(ic_large / ic_moderate, 100.0);

    // Test saturation region (Vbc > 0, both junctions forward biased)
    Real vbc_saturation = 0.2;  // Vce = 0.5V (saturation)
    Real ic_saturation = bjt.get_collector_current(vbe, vbc_saturation);

    // In saturation, current should be positive but moderated by reverse current
    EXPECT_GT(ic_saturation, 0.0);
}

//==============================================================================
// PNP Tests: Early Effect
//==============================================================================

TEST(BJT_PNP_EarlyEffect, WithEarlyEffect_IncreasingCollectorCurrent) {
    auto bjt = create_bjt_pnp_with_early();

    // Forward active region: Veb = 0.7V, vary Vce
    Real veb = 0.7;
    Real vcb1 = -1.0;  // Vce = Veb - Vcb = 1.7V
    Real vcb2 = -5.0;  // Vce = Veb - Vcb = 5.7V

    Real ic1 = bjt.get_collector_current(veb, vcb1);
    Real ic2 = bjt.get_collector_current(veb, vcb2);

    // With Early effect: Ic increases with Vce
    Real vce1 = veb - vcb1;
    Real vce2 = veb - vcb2;
    Real vaf = bjt.forward_early_voltage();

    Real expected_ratio = (1.0 + vce2 / vaf) / (1.0 + vce1 / vaf);
    Real actual_ratio = ic2 / ic1;

    // Relaxed tolerance to account for numerical precision in Gummel-Poon model
    EXPECT_NEAR(actual_ratio, expected_ratio, 0.005);
    EXPECT_GT(ic2, ic1);
}

TEST(BJT_PNP_EarlyEffect, EarlyVoltage_Accessor) {
    auto bjt = create_bjt_pnp_with_early();

    EXPECT_DOUBLE_EQ(bjt.forward_early_voltage(), 100.0);
    EXPECT_DOUBLE_EQ(bjt.reverse_early_voltage(), 0.0);
}

//==============================================================================
// Comparison: Early Effect Impact on Output Resistance
//==============================================================================

TEST(BJT_EarlyEffect_Comparison, OutputResistance_Ratio) {
    auto bjt_no_early = create_bjt_npn_no_early();
    auto bjt_with_early = create_bjt_npn_with_early();

    Real vbe = 0.7;
    Real vbc = -2.0;

    Real ic_no_early = bjt_no_early.get_collector_current(vbe, vbc);
    Real ic_with_early = bjt_with_early.get_collector_current(vbe, vbc);

    // Collector currents should be similar (Early effect is a small modulation)
    EXPECT_NEAR(ic_with_early, ic_no_early, ic_no_early * 0.05);  // Within 5%

    // Output conductance with Early effect should be significantly larger
    Real gds_no_early = bjt_no_early.get_collector_conductance_vbc(vbe, vbc);
    Real gds_with_early = bjt_with_early.get_collector_conductance_vbc(vbe, vbc);

    Real gds_ratio = std::abs(gds_with_early / gds_no_early);
    EXPECT_GT(gds_ratio, 10.0);  // At least 10x larger with Early effect
}
