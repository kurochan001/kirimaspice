// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_bjt_base_resistance.cpp
// Base Resistance Model (RB/RBM/IRB) Tests
#include <gtest/gtest.h>
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/bjt_pnp.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;

//==============================================================================
// Helper Functions
//==============================================================================

/// Create NPN BJT without base resistance (RB = 0)
BJT_NPN create_bjt_npn_no_rb() {
    return BJT_NPN(1, "Q1", 1, 2, 0,
                   1e-14,  // Is
                   100.0,  // beta_f
                   1.0,    // nf
                   -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                   0.0, 0.0,        // vaf=0, var=0 (no Early effect)
                   0.0, 0.0,        // ikf=0, ikr=0 (no high-current effect)
                   0.0, 0.0, 0.0,   // rb=0, rbm=0, irb=0 (NO base resistance)
                   0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                   0.0, 0.75, 0.33, // cje, vje, mje
                   0.0, 0.75, 0.33, // cjc, vjc, mjc
                   0.0, 0.0,        // tf, tr
                   0.0, 1.0, 1.0);  // kf, af, ff
}

/// Create NPN BJT with fixed base resistance (IRB = 0, no current dependence)
BJT_NPN create_bjt_npn_fixed_rb() {
    return BJT_NPN(1, "Q1", 1, 2, 0,
                   1e-14,  // Is
                   100.0,  // beta_f
                   1.0,    // nf
                   -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                   0.0, 0.0,        // vaf=0, var=0 (no Early effect)
                   0.0, 0.0,        // ikf=0, ikr=0 (no high-current effect)
                   100.0, 0.0, 0.0, // rb=100Ω, rbm=0, irb=0 (FIXED base resistance)
                   0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                   0.0, 0.75, 0.33, // cje, vje, mje
                   0.0, 0.75, 0.33, // cjc, vjc, mjc
                   0.0, 0.0,        // tf, tr
                   0.0, 1.0, 1.0);  // kf, af, ff
}

/// Create NPN BJT with current-dependent base resistance
BJT_NPN create_bjt_npn_with_rb() {
    return BJT_NPN(1, "Q1", 1, 2, 0,
                   1e-14,  // Is
                   100.0,  // beta_f
                   1.0,    // nf
                   -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                   0.0, 0.0,        // vaf=0, var=0 (no Early effect)
                   0.0, 0.0,        // ikf=0, ikr=0 (no high-current effect)
                   100.0, 10.0, 1e-3, // rb=100Ω, rbm=10Ω, irb=1mA (current-dependent)
                   0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                   0.0, 0.75, 0.33, // cje, vje, mje
                   0.0, 0.75, 0.33, // cjc, vjc, mjc
                   0.0, 0.0,        // tf, tr
                   0.0, 1.0, 1.0);  // kf, af, ff
}

/// Create PNP BJT with current-dependent base resistance
BJT_PNP create_bjt_pnp_with_rb() {
    return BJT_PNP(1, "Q2", 1, 2, 0,
                   1e-14,  // Is
                   100.0,  // beta_f
                   1.0,    // nf
                   -1.0, 1.0, 1.0,  // isc, beta_r, nr (defaults)
                   0.0, 0.0,        // vaf=0, var=0 (no Early effect)
                   0.0, 0.0,        // ikf=0, ikr=0 (no high-current effect)
                   100.0, 10.0, 1e-3, // rb=100Ω, rbm=10Ω, irb=1mA (current-dependent)
                   0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                   0.0, 0.75, 0.33, // cje, vje, mje
                   0.0, 0.75, 0.33, // cjc, vjc, mjc
                   0.0, 0.0,        // tf, tr
                   0.0, 1.0, 1.0);  // kf, af, ff
}

//==============================================================================
// NPN Tests: Base Resistance
//==============================================================================

TEST(BJT_NPN_BaseResistance, NoBaseResistance_ReturnsZero) {
    auto bjt = create_bjt_npn_no_rb();

    Real vbe = 0.7;  // Active mode
    Real vbc = -2.0;

    Real rb = bjt.get_base_resistance(vbe, vbc);

    EXPECT_DOUBLE_EQ(rb, 0.0);  // RB = 0 → no base resistance
}

TEST(BJT_NPN_BaseResistance, FixedBaseResistance_ConstantValue) {
    auto bjt = create_bjt_npn_fixed_rb();

    // Test at different currents
    Real vbe_low = 0.6;   // Low current
    Real vbe_mid = 0.7;   // Medium current
    Real vbe_high = 0.8;  // High current
    Real vbc = -2.0;

    Real rb_low = bjt.get_base_resistance(vbe_low, vbc);
    Real rb_mid = bjt.get_base_resistance(vbe_mid, vbc);
    Real rb_high = bjt.get_base_resistance(vbe_high, vbc);

    // With IRB = 0, base resistance should be constant (RB = 100Ω)
    // (slight variation due to qb factor, but should be close to RB)
    EXPECT_NEAR(rb_low, 100.0, 5.0);   // Within 5Ω
    EXPECT_NEAR(rb_mid, 100.0, 5.0);   // Within 5Ω
    EXPECT_NEAR(rb_high, 100.0, 5.0);  // Within 5Ω

    // Should be approximately constant across current levels
    EXPECT_NEAR(rb_low, rb_mid, 2.0);
    EXPECT_NEAR(rb_mid, rb_high, 2.0);
}

TEST(BJT_NPN_BaseResistance, CurrentDependent_DecreasesAtHighCurrent) {
    auto bjt = create_bjt_npn_with_rb();

    // Low current: Vbe = 0.5V → Ib ≈ 0.001 μA << IRB (1mA)
    Real vbe_low = 0.5;
    Real vbc = -2.0;
    Real rb_low = bjt.get_base_resistance(vbe_low, vbc);

    // High current: Vbe = 0.8V → Ib ≈ 50 μA ~ 0.05×IRB
    Real vbe_high = 0.8;
    Real rb_high = bjt.get_base_resistance(vbe_high, vbc);

    // At low currents: R_b → RB (100Ω)
    EXPECT_NEAR(rb_low, 100.0, 10.0);  // Within 10%

    // At high currents: R_b decreases toward RBM (10Ω)
    EXPECT_LT(rb_high, rb_low);  // Base resistance should decrease

    // Should not go below RBM
    EXPECT_GE(rb_high, 10.0);  // R_b ≥ RBM

    // Verify ratio
    Real ratio = rb_low / rb_high;
    EXPECT_GT(ratio, 1.5);  // At least 50% reduction at high current
}

TEST(BJT_NPN_BaseResistance, VeryHighCurrent_ApproachesRbm) {
    auto bjt = create_bjt_npn_with_rb();

    // Very high current: Vbe = 0.9V, Ib >> IRB = 1mA. SPICE's Gummel-Poon base
    // resistance with IRB is RBM + 3 (RB - RBM) (tan z - z) / (z tan^2 z), z
    // growing toward pi/2 with Ib, so R_b falls toward RBM = 10 Ohm, not to 0
    // (current crowding leaves the minimum resistance RBM).

    Real vbe_very_high = 0.9;
    Real vbc = -2.0;
    Real rb = bjt.get_base_resistance(vbe_very_high, vbc);

    EXPECT_GT(rb, 10.0);                // above RBM
    EXPECT_LT(rb, 10.0 + 0.1 * 90.0);  // within a tenth of RB - RBM of it
}

TEST(BJT_NPN_BaseResistance, HalfValueCurrent_ApproximatelyHalfway) {
    auto bjt = create_bjt_npn_with_rb();

    // At I_b = IRB, base resistance should be approximately halfway:
    // R_b = (RBM + RB × q_b) / 2 ≈ (10 + 100) / 2 = 55Ω (if qb ≈ 1)
    //
    // Need to find Vbe where I_b ≈ IRB = 1mA
    // Ib = (Is/βF) × exp(Vbe/Vt) ≈ 1e-14/100 × exp(Vbe/0.02585)
    // 1e-3 = 1e-16 × exp(Vbe/0.02585)
    // exp(Vbe/0.02585) = 1e13
    // Vbe/0.02585 = ln(1e13) = 29.93
    // Vbe ≈ 0.774V

    Real vbe = 0.774;
    Real vbc = -2.0;
    Real rb = bjt.get_base_resistance(vbe, vbc);

    // At I_b = IRB, resistance should be between RBM and RB
    EXPECT_GT(rb, 10.0);   // Above RBM
    EXPECT_LT(rb, 100.0);  // Below RB

    // Should be approximately halfway (with some tolerance for qb factor)
    EXPECT_NEAR(rb, 55.0, 20.0);  // Within 20Ω of midpoint
}

TEST(BJT_NPN_BaseResistance, PositiveValues) {
    auto bjt = create_bjt_npn_with_rb();

    Real vbe = 0.7;
    Real vbc = -2.0;
    Real rb = bjt.get_base_resistance(vbe, vbc);

    // Base resistance must always be positive
    EXPECT_GT(rb, 0.0);

    // And within reasonable bounds [RBM, RB]
    EXPECT_GE(rb, 10.0);   // ≥ RBM
    EXPECT_LE(rb, 100.0);  // ≤ RB
}

//==============================================================================
// PNP Tests: Base Resistance
//==============================================================================

TEST(BJT_PNP_BaseResistance, CurrentDependent_DecreasesAtHighCurrent) {
    auto bjt = create_bjt_pnp_with_rb();

    // Low current: Veb = 0.5V
    Real veb_low = 0.5;
    Real vcb = -2.0;
    Real rb_low = bjt.get_base_resistance(veb_low, vcb);

    // High current: Veb = 0.8V
    Real veb_high = 0.8;
    Real rb_high = bjt.get_base_resistance(veb_high, vcb);

    // Base resistance should decrease at high current
    EXPECT_LT(rb_high, rb_low);

    // Verify reasonable values
    EXPECT_NEAR(rb_low, 100.0, 10.0);  // Low current → RB
    EXPECT_GT(rb_high, 10.0);          // High current → closer to RBM
    EXPECT_LT(rb_high, rb_low * 0.7);  // At least 30% reduction
}

TEST(BJT_PNP_BaseResistance, PositiveValues) {
    auto bjt = create_bjt_pnp_with_rb();

    Real veb = 0.7;
    Real vcb = -2.0;
    Real rb = bjt.get_base_resistance(veb, vcb);

    // Base resistance must always be positive
    EXPECT_GT(rb, 0.0);

    // And within reasonable bounds [RBM, RB]
    EXPECT_GE(rb, 10.0);   // ≥ RBM
    EXPECT_LE(rb, 100.0);  // ≤ RB
}

//==============================================================================
// Advanced Tests: Base Resistance with Early Effect and High-Current Effects
//==============================================================================

TEST(BJT_NPN_BaseResistance, WithEarlyEffect_QbModulation) {
    // Create BJT with both base resistance AND Early effect
    BJT_NPN bjt(1, "Q1", 1, 2, 0,
                1e-14,  // Is
                100.0,  // beta_f
                1.0,    // nf
                -1.0, 1.0, 1.0,  // isc, beta_r, nr
                100.0, 0.0,      // vaf=100V, var=0 (Early effect)
                0.0, 0.0,        // ikf=0, ikr=0
                100.0, 10.0, 0.0,  // rb, rbm, irb = 0: R_b follows qb
                0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                0.0, 0.75, 0.33,
                0.0, 0.75, 0.33,
                0.0, 0.0,
                0.0, 1.0, 1.0);

    Real vbe = 0.7;
    Real vbc_low = -1.0;  // Low Vce: qb = 1 / (1 + 1/100)
    Real vbc_high = -5.0; // High Vce: qb = 1 / (1 + 5/100)

    Real rb_low_vce = bjt.get_base_resistance(vbe, vbc_low);
    Real rb_high_vce = bjt.get_base_resistance(vbe, vbc_high);

    // SPICE's Gummel-Poon without IRB: R_b = RBM + (RB - RBM) / qb, and
    // qb = 1 / (1 - Vbc/VAF) without IKF, so a higher Vce gives a smaller qb
    // and a larger R_b (with IRB, R_b depends on Ib alone)
    EXPECT_NEAR(rb_low_vce, 10.0 + 90.0 * 1.01, 1e-6);
    EXPECT_NEAR(rb_high_vce, 10.0 + 90.0 * 1.05, 1e-6);
    EXPECT_LT(rb_low_vce, rb_high_vce);
}

TEST(BJT_NPN_BaseResistance, WithHighCurrentEffect_QbModulation) {
    // Create BJT with both base resistance AND high-current effect
    BJT_NPN bjt(1, "Q1", 1, 2, 0,
                1e-14,  // Is
                100.0,  // beta_f
                1.0,    // nf
                -1.0, 1.0, 1.0,  // isc, beta_r, nr
                0.0, 0.0,        // vaf=0, var=0
                1e-3, 0.0,       // ikf=1mA, ikr=0 (high-current effect)
                100.0, 10.0, 1e-3, // rb, rbm, irb
                0.0, 1.5, 0.0, 2.0,  // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                0.0, 0.75, 0.33,
                0.0, 0.75, 0.33,
                0.0, 0.0,
                0.0, 1.0, 1.0);

    Real vbe_low = 0.6;   // Low current → q1 ≈ 1
    Real vbe_high = 0.85; // High current → q1 > 1
    Real vbc = -2.0;

    Real rb_low = bjt.get_base_resistance(vbe_low, vbc);
    Real rb_high = bjt.get_base_resistance(vbe_high, vbc);

    // With high-current effect: higher Ic → higher q1 → higher qb → higher R_b (qb term)
    // BUT also higher Ib → lower R_b (current_factor term)
    // The current_factor dominates, so R_b should still decrease overall
    EXPECT_LT(rb_high, rb_low);

    // Both should be positive
    EXPECT_GT(rb_low, 0.0);
    EXPECT_GT(rb_high, 0.0);
}
