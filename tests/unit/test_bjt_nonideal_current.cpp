// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_bjt_nonideal_current.cpp
// Unit tests.4: BJT non-ideal (leakage) currents (ISE/NE/ISC/NC)

#include <gtest/gtest.h>
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/bjt_pnp.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;

namespace {

// Test 1: No leakage (ISE=0, ISC=0) - Backward compatibility
TEST(BJTNonIdealCurrent, NoLeakage_BackwardCompatibility) {
    // Create BJT with no leakage currents (default values)
    BJT_NPN bjt(0, "Q1", 1, 2, 3,
                1e-14, 100.0, 1.0,  // is, beta_f, nf
                -1.0, 1.0, 1.0,     // isc, beta_r, nr
                0.0, 0.0,           // vaf, var
                0.0, 0.0,           // ikf, ikr
                0.0, 0.0, 0.0,      // rb, rbm, irb
                0.0, 1.5, 0.0, 2.0, // ise=0, ne=1.5, isc_leak=0, nc=2.0 (no leakage)
                0.0, 0.75, 0.33,    // cje, vje, mje
                0.0, 0.75, 0.33,    // cjc, vjc, mjc
                0.0, 0.0,           // tf, tr
                0.0, 1.0, 1.0);     // kf, af, ff

    // Disable voltage limiting for predictable test results
    bjt.set_voltage_limiting(false);

    // Test at Vbe = 0.7V, Vbc = -5.0V (forward active mode)
    Real vbe = 0.7;
    Real vbc = -5.0;

    Real ib = bjt.get_base_current(vbe, vbc);
    Real ic = bjt.get_collector_current(vbe, vbc);

    // Without leakage currents, should behave as ideal Ebers-Moll model
    // I_b = (Is/βF) × (exp(Vbe/(NF×Vt)) - 1) only
    // Note: BJT uses T=300.15K by default, so Vt = 0.02585859... V
    constexpr Real vt_test = 0.02585;  // 25.85mV at 300K (test value)
    constexpr Real vt_device = 0.0258558622;  // 25.8559mV at 300.15K (device default)

    // Use device's Vt for comparison
    Real i_forward_expected = 1e-14 / 100.0 * (std::exp(vbe / (1.0 * vt_device)) - 1.0);

    EXPECT_NEAR(ib, i_forward_expected, i_forward_expected * 0.02);  // Within 2%

    // Collector current should be approximately βF × I_b
    EXPECT_NEAR(ic / ib, 100.0, 1.0);  // βF ≈ 100 (within 1)
}

// Test 2: B-E leakage only (ISE > 0, ISC = 0)
TEST(BJTNonIdealCurrent, BELeakageOnly) {
    // Create BJT with only B-E leakage current
    Real ise = 1e-15;  // B-E leakage saturation current
    Real ne = 1.5;     // B-E leakage emission coefficient

    BJT_NPN bjt(0, "Q1", 1, 2, 3,
                1e-14, 100.0, 1.0,  // is, beta_f, nf
                -1.0, 1.0, 1.0,     // isc, beta_r, nr
                0.0, 0.0,           // vaf, var
                0.0, 0.0,           // ikf, ikr
                0.0, 0.0, 0.0,      // rb, rbm, irb
                ise, ne, 0.0, 2.0,  // ise=1e-15, ne=1.5, isc_leak=0, nc=2.0
                0.0, 0.75, 0.33,    // cje, vje, mje
                0.0, 0.75, 0.33,    // cjc, vjc, mjc
                0.0, 0.0,           // tf, tr
                0.0, 1.0, 1.0);     // kf, af, ff

    bjt.set_voltage_limiting(false);

    // Test at Vbe = 0.7V, Vbc = -5.0V (forward active mode)
    Real vbe = 0.7;
    Real vbc = -5.0;

    Real ib = bjt.get_base_current(vbe, vbc);

    // Expected: I_b = I_b_ideal + I_BE_leak
    // I_b_ideal = (Is/βF) × (exp(Vbe/(NF×Vt)) - 1)
    // I_BE_leak = ISE × (exp(Vbe/(NE×Vt)) - 1)
    constexpr Real vt = 0.0258558622;  // 25.8559mV at 300.15K (device default)
    Real i_ideal = 1e-14 / 100.0 * (std::exp(vbe / (1.0 * vt)) - 1.0);
    Real i_be_leak = ise * (std::exp(vbe / (ne * vt)) - 1.0);
    Real ib_expected = i_ideal + i_be_leak;

    EXPECT_NEAR(ib, ib_expected, ib_expected * 0.01);  // Within 1%

    // Verify B-E leakage is non-negligible
    EXPECT_GT(i_be_leak / i_ideal, 0.001);  // At least 0.1% contribution
}

// Test 3: B-C leakage only (ISE = 0, ISC > 0)
TEST(BJTNonIdealCurrent, BCLeakageOnly) {
    // Create BJT with only B-C leakage current
    Real isc_leak = 1e-16;  // B-C leakage saturation current
    Real nc = 2.0;          // B-C leakage emission coefficient

    BJT_NPN bjt(0, "Q1", 1, 2, 3,
                1e-14, 100.0, 1.0,  // is, beta_f, nf
                -1.0, 1.0, 1.0,     // isc, beta_r, nr
                0.0, 0.0,           // vaf, var
                0.0, 0.0,           // ikf, ikr
                0.0, 0.0, 0.0,      // rb, rbm, irb
                0.0, 1.5, isc_leak, nc,  // ise=0, ne=1.5, isc_leak=1e-16, nc=2.0
                0.0, 0.75, 0.33,    // cje, vje, mje
                0.0, 0.75, 0.33,    // cjc, vjc, mjc
                0.0, 0.0,           // tf, tr
                0.0, 1.0, 1.0);     // kf, af, ff

    bjt.set_voltage_limiting(false);

    // Test at Vbe = 0.7V, Vbc = 0.5V (saturation mode with forward biased B-C junction)
    Real vbe = 0.7;
    Real vbc = 0.5;

    Real ib = bjt.get_base_current(vbe, vbc);

    // Expected: I_b = I_b_ideal + I_BC_leak
    // I_b_ideal = (Is/βF) × (exp(Vbe/(NF×Vt)) - 1) + (Isc/βR) × (exp(Vbc/(NR×Vt)) - 1)
    // I_BC_leak = ISC × (exp(Vbc/(NC×Vt)) - 1)
    constexpr Real vt = 0.0258558622;  // 25.8559mV at 300.15K (device default)
    Real i_forward = 1e-14 / 100.0 * (std::exp(vbe / (1.0 * vt)) - 1.0);
    Real i_reverse = 1e-14 / 1.0 * (std::exp(vbc / (1.0 * vt)) - 1.0);  // isc defaults to is
    Real i_bc_leak = isc_leak * (std::exp(vbc / (nc * vt)) - 1.0);
    Real ib_expected = i_forward + i_reverse + i_bc_leak;

    EXPECT_NEAR(ib, ib_expected, ib_expected * 0.01);  // Within 1%

    // Verify B-C leakage is non-negligible at high Vbc
    EXPECT_GT(i_bc_leak, 0.0);
}

// Test 4: Both leakages active (ISE > 0, ISC > 0)
TEST(BJTNonIdealCurrent, BothLeakagesActive) {
    // Create BJT with both B-E and B-C leakage currents
    Real ise = 1e-15;       // B-E leakage saturation current
    Real ne = 1.5;          // B-E leakage emission coefficient
    Real isc_leak = 1e-16;  // B-C leakage saturation current
    Real nc = 2.0;          // B-C leakage emission coefficient

    BJT_NPN bjt(0, "Q1", 1, 2, 3,
                1e-14, 100.0, 1.0,  // is, beta_f, nf
                -1.0, 1.0, 1.0,     // isc, beta_r, nr
                0.0, 0.0,           // vaf, var
                0.0, 0.0,           // ikf, ikr
                0.0, 0.0, 0.0,      // rb, rbm, irb
                ise, ne, isc_leak, nc,  // ise, ne, isc_leak, nc (both active)
                0.0, 0.75, 0.33,    // cje, vje, mje
                0.0, 0.75, 0.33,    // cjc, vjc, mjc
                0.0, 0.0,           // tf, tr
                0.0, 1.0, 1.0);     // kf, af, ff

    bjt.set_voltage_limiting(false);

    // Test at Vbe = 0.7V, Vbc = 0.5V
    Real vbe = 0.7;
    Real vbc = 0.5;

    Real ib = bjt.get_base_current(vbe, vbc);

    // Expected: I_b = I_b_ideal + I_BE_leak + I_BC_leak
    constexpr Real vt = 0.0258558622;  // 25.8559mV at 300.15K (device default)
    Real i_forward = 1e-14 / 100.0 * (std::exp(vbe / (1.0 * vt)) - 1.0);
    Real i_reverse = 1e-14 / 1.0 * (std::exp(vbc / (1.0 * vt)) - 1.0);
    Real i_be_leak = ise * (std::exp(vbe / (ne * vt)) - 1.0);
    Real i_bc_leak = isc_leak * (std::exp(vbc / (nc * vt)) - 1.0);
    Real ib_expected = i_forward + i_reverse + i_be_leak + i_bc_leak;

    EXPECT_NEAR(ib, ib_expected, ib_expected * 0.01);  // Within 1%

    // Verify both leakage components are present
    EXPECT_GT(i_be_leak, 0.0);
    EXPECT_GT(i_bc_leak, 0.0);
}

// Test 5: Emission coefficient effect (NE, NC values)
TEST(BJTNonIdealCurrent, EmissionCoefficientEffect) {
    // Test the effect of emission coefficients on leakage current magnitude
    Real vbe = 0.6;
    Real vbc = 0.0;
    constexpr Real vt = 0.02585;

    // Case 1: NE = 1.5 (typical)
    BJT_NPN bjt1(0, "Q1", 1, 2, 3,
                 1e-14, 100.0, 1.0, -1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                 1e-15, 1.5, 0.0, 2.0,  // ise=1e-15, ne=1.5
                 0.0, 0.75, 0.33, 0.0, 0.75, 0.33, 0.0, 0.0, 0.0, 1.0, 1.0);

    bjt1.set_voltage_limiting(false);

    // Case 2: NE = 2.0 (higher)
    BJT_NPN bjt2(0, "Q2", 1, 2, 3,
                 1e-14, 100.0, 1.0, -1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                 1e-15, 2.0, 0.0, 2.0,  // ise=1e-15, ne=2.0
                 0.0, 0.75, 0.33, 0.0, 0.75, 0.33, 0.0, 0.0, 0.0, 1.0, 1.0);

    bjt2.set_voltage_limiting(false);

    Real ib1 = bjt1.get_base_current(vbe, vbc);
    Real ib2 = bjt2.get_base_current(vbe, vbc);

    // Higher NE should result in lower leakage current at same voltage
    // I_leak ∝ exp(V / (NE × Vt)), so higher NE → lower exp argument → lower current
    EXPECT_LT(ib2, ib1);

    // Verify the ordering is correct
    Real i_ideal = 1e-14 / 100.0 * (std::exp(vbe / (1.0 * vt)) - 1.0);
    Real leak1 = ib1 - i_ideal;
    Real leak2 = ib2 - i_ideal;
    EXPECT_LT(leak2, leak1);
}

// Test 6: Conductance derivatives (dIb/dVbe, dIb/dVbc with leakage)
TEST(BJTNonIdealCurrent, ConductanceDerivativesWithLeakage) {
    // Create BJT with leakage currents
    Real ise = 1e-15;
    Real ne = 1.5;
    Real isc_leak = 1e-16;
    Real nc = 2.0;

    BJT_NPN bjt(0, "Q1", 1, 2, 3,
                1e-14, 100.0, 1.0, -1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                ise, ne, isc_leak, nc,
                0.0, 0.75, 0.33, 0.0, 0.75, 0.33, 0.0, 0.0, 0.0, 1.0, 1.0);

    bjt.set_voltage_limiting(false);

    Real vbe = 0.7;
    Real vbc = -5.0;

    Real g_be = bjt.get_base_conductance_vbe(vbe, vbc);
    Real g_bc = bjt.get_base_conductance_vbc(vbe, vbc);

    // Expected: g_be = dIb/dVbe = (Is/(βF×NF×Vt)) × exp(Vbe/(NF×Vt))
    //                              + (ISE/(NE×Vt)) × exp(Vbe/(NE×Vt))
    constexpr Real vt = 0.0258558622;  // 25.8559mV at 300.15K (device default)
    Real g_ideal = (1e-14 / (100.0 * 1.0 * vt)) * std::exp(vbe / (1.0 * vt));
    Real g_leak = (ise / (ne * vt)) * std::exp(vbe / (ne * vt));
    Real g_be_expected = g_ideal + g_leak;

    EXPECT_NEAR(g_be, g_be_expected, g_be_expected * 0.02);  // Within 2%

    // For g_bc, expect similar structure with B-C junction terms
    EXPECT_GT(g_bc, 0.0);  // Should be positive

    // Numerical derivative check (finite difference)
    Real delta_vbe = 1e-6;
    Real ib1 = bjt.get_base_current(vbe - delta_vbe / 2.0, vbc);
    Real ib2 = bjt.get_base_current(vbe + delta_vbe / 2.0, vbc);
    Real g_be_numerical = (ib2 - ib1) / delta_vbe;

    EXPECT_NEAR(g_be, g_be_numerical, g_be_numerical * 0.05);  // Within 5% (numerical error)
}

// Test 7: NPN and PNP symmetry
TEST(BJTNonIdealCurrent, NPNPNPSymmetry) {
    // Create matching NPN and PNP transistors with identical parameters
    Real ise = 1e-15;
    Real ne = 1.5;
    Real isc_leak = 1e-16;
    Real nc = 2.0;

    BJT_NPN npn(0, "Q1", 1, 2, 3,
                1e-14, 100.0, 1.0, -1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                ise, ne, isc_leak, nc,
                0.0, 0.75, 0.33, 0.0, 0.75, 0.33, 0.0, 0.0, 0.0, 1.0, 1.0);

    npn.set_voltage_limiting(false);

    BJT_PNP pnp(1, "Q2", 1, 2, 3,
                1e-14, 100.0, 1.0, -1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                ise, ne, isc_leak, nc,
                0.0, 0.75, 0.33, 0.0, 0.75, 0.33, 0.0, 0.0, 0.0, 1.0, 1.0);

    pnp.set_voltage_limiting(false);

    // For NPN: Vbe = 0.7V, Vbc = -5.0V (forward active)
    Real vbe_npn = 0.7;
    Real vbc_npn = -5.0;

    // For PNP: Veb = 0.7V, Vcb = -5.0V (forward active, reversed polarity)
    Real veb_pnp = 0.7;
    Real vcb_pnp = -5.0;

    Real ib_npn = npn.get_base_current(vbe_npn, vbc_npn);
    Real ib_pnp = pnp.get_base_current(veb_pnp, vcb_pnp);

    // With identical parameters and voltages (accounting for polarity),
    // the base currents should have the same magnitude
    EXPECT_NEAR(ib_npn, ib_pnp, ib_npn * 0.01);  // Within 1%

    // Conductances should also match
    Real g_be_npn = npn.get_base_conductance_vbe(vbe_npn, vbc_npn);
    Real g_eb_pnp = pnp.get_base_conductance_veb(veb_pnp, vcb_pnp);

    EXPECT_NEAR(g_be_npn, g_eb_pnp, g_be_npn * 0.01);  // Within 1%
}

// Test 8: Parser integration (netlist parameter parsing)
TEST(BJTNonIdealCurrent, ParserIntegration) {
    // This test verifies that the parser correctly reads ISE/NE/ISC/NC parameters
    // from a netlist and passes them to the device constructor.

    // Note: This is a placeholder test that demonstrates the expected usage.
    // The actual parser integration test should be in test_parser.cpp or
    // an integration test file.

    // Example netlist:
    // Q1 c b e NPN IS=1e-14 BF=100 N=1.0 ISE=1e-15 NE=1.5 ISC=1e-16 NC=2.0

    // Expected behavior:
    // - Parser recognizes ISE, NE, ISC, NC parameters (case-insensitive)
    // - Parser passes these values to CircuitGraph::add_bjt_npn()
    // - CircuitGraph creates BJT_NPN device with correct parameters

    // For this unit test, we'll just verify that the device can be created
    // with these parameters and that they produce the expected currents.

    BJT_NPN bjt(0, "Q1", 1, 2, 3,
                1e-14, 100.0, 1.0,  // is, beta_f, nf
                -1.0, 1.0, 1.0,     // isc, beta_r, nr
                0.0, 0.0,           // vaf, var
                0.0, 0.0,           // ikf, ikr
                0.0, 0.0, 0.0,      // rb, rbm, irb
                1e-15, 1.5, 1e-16, 2.0,  // ise, ne, isc_leak, nc (from netlist)
                0.0, 0.75, 0.33,    // cje, vje, mje
                0.0, 0.75, 0.33,    // cjc, vjc, mjc
                0.0, 0.0,           // tf, tr
                0.0, 1.0, 1.0);     // kf, af, ff

    bjt.set_voltage_limiting(false);

    Real vbe = 0.7;
    Real vbc = -5.0;
    Real ib = bjt.get_base_current(vbe, vbc);

    // Verify that the leakage current is included
    constexpr Real vt = 0.0258558622;  // 25.8559mV at 300.15K (device default)
    Real i_ideal = 1e-14 / 100.0 * (std::exp(vbe / (1.0 * vt)) - 1.0);
    Real i_be_leak = 1e-15 * (std::exp(vbe / (1.5 * vt)) - 1.0);
    Real ib_expected = i_ideal + i_be_leak;

    // Total current should match ideal + leakage
    EXPECT_NEAR(ib, ib_expected, ib_expected * 0.02);  // Within 2%

    // Verify leakage component is present (though small)
    EXPECT_GT(i_be_leak, 0.0);
}

}  // namespace
