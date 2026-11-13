// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_mosfet_level3_capacitance.cpp
// Meyer gate capacitance model tests

#include <gtest/gtest.h>
#include "kirimaspice/devices/mosfet_level3.hpp"  // Step 5.4: Unified template
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;

//==============================================================================
// Helper: Create standard MOSFETLevel3Params with capacitances
//==============================================================================
MOSFETLevel3Params create_capacitance_params() {
    MOSFETLevel3Params params;

    // Process parameters
    params.tox = 20e-9;      // 20nm oxide thickness
    params.nsub = 1e17;      // 10^17 cm^-3 substrate doping
    params.nss = 0.0;
    params.tpg = 1;

    // Threshold voltage
    params.vto = 0.7;
    params.phi = 0.7;
    params.gamma = 0.5;

    // Mobility
    params.uo = 600.0;
    params.theta = 0.1;
    params.vmax = 1e5;

    // Short-channel effects
    params.eta = 0.0;
    params.kappa = 0.0;
    params.delta = 0.0;

    // Geometry
    params.w = 10e-6;        // 10µm width
    params.l = 1e-6;         // 1µm length
    params.ld = 0.0;
    params.wd = 0.0;

    // Overlap capacitances (per unit width/length)
    params.cgso = 1e-10;     // 0.1 fF/µm
    params.cgdo = 1e-10;     // 0.1 fF/µm
    params.cgbo = 1e-11;     // 0.01 fF/µm

    // Junction capacitances
    params.cbs = 1e-15;      // 1 fF
    params.cbd = 1e-15;      // 1 fF
    params.cj = 5e-4;        // 0.5 mF/m^2
    params.mj = 0.5;         // Grading coefficient
    params.pb = 0.8;         // Built-in potential [V]

    return params;
}

//==============================================================================
// MOSFET_Level3_N - Gate Capacitance Tests (Meyer Model)
//==============================================================================

TEST(MOSFET_Level3_N_Capacitance, Cutoff_CgbMaximum) {
    auto params = create_capacitance_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Cutoff region: Vgs < Vth
    Real vgs = 0.5;  // < 0.7V
    Real vds = 1.0;
    Real vbs = 0.0;

    Real cgs = mosfet.get_gate_source_capacitance(vgs, vds, vbs);
    Real cgd = mosfet.get_gate_drain_capacitance(vgs, vds, vbs);
    Real cgb = mosfet.get_gate_bulk_capacitance(vgs, vds, vbs);

    // In cutoff: Cgs = Cgso*W, Cgd = Cgdo*W, Cgb = Cox*W*L + Cgbo*L
    Real expected_cgs = params.cgso * params.get_weff();
    Real expected_cgd = params.cgdo * params.get_weff();

    // Cox = eps_ox / tox
    Real eps_ox = 3.45e-11;  // F/m
    Real cox = eps_ox / params.tox;
    Real expected_cgb = cox * params.get_weff() * params.get_leff() +
                        params.cgbo * params.get_leff();

    EXPECT_NEAR(cgs, expected_cgs, 1e-16);
    EXPECT_NEAR(cgd, expected_cgd, 1e-16);
    EXPECT_NEAR(cgb, expected_cgb, 1e-16);

    // Cgb should be dominant in cutoff
    EXPECT_GT(cgb, cgs);
    EXPECT_GT(cgb, cgd);
}

TEST(MOSFET_Level3_N_Capacitance, Linear_SymmetricDistribution) {
    auto params = create_capacitance_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Linear region: Vgs > Vth, Vds < Vgs - Vth
    Real vgs = 2.0;
    Real vds = 0.5;  // < Vgs - Vth = 2.0 - 0.7 = 1.3
    Real vbs = 0.0;

    Real cgs = mosfet.get_gate_source_capacitance(vgs, vds, vbs);
    Real cgd = mosfet.get_gate_drain_capacitance(vgs, vds, vbs);
    Real cgb = mosfet.get_gate_bulk_capacitance(vgs, vds, vbs);

    // In linear: Cgs = Cgd = (2/3)*Cox*W*L + Cgso/Cgdo*W
    Real eps_ox = 3.45e-11;
    Real cox = eps_ox / params.tox;
    Real cox_wl = cox * params.get_weff() * params.get_leff();
    Real expected_cgs = (2.0 / 3.0) * cox_wl + params.cgso * params.get_weff();
    Real expected_cgd = (2.0 / 3.0) * cox_wl + params.cgdo * params.get_weff();
    Real expected_cgb = params.cgbo * params.get_leff();  // Side overlap only

    EXPECT_NEAR(cgs, expected_cgs, 1e-16);
    EXPECT_NEAR(cgd, expected_cgd, 1e-16);
    EXPECT_NEAR(cgb, expected_cgb, 1e-16);

    // Cgs and Cgd should be approximately equal in linear region
    EXPECT_NEAR(cgs, cgd, 1e-15);
}

TEST(MOSFET_Level3_N_Capacitance, Saturation_CgdMinimum) {
    auto params = create_capacitance_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Saturation region: Vgs > Vth, Vds >= Vgs - Vth
    Real vgs = 2.0;
    Real vds = 2.0;  // >= Vgs - Vth = 2.0 - 0.7 = 1.3
    Real vbs = 0.0;

    Real cgs = mosfet.get_gate_source_capacitance(vgs, vds, vbs);
    Real cgd = mosfet.get_gate_drain_capacitance(vgs, vds, vbs);
    Real cgb = mosfet.get_gate_bulk_capacitance(vgs, vds, vbs);

    // In saturation: Cgs = (2/3)*Cox*W*L + Cgso*W, Cgd = Cgdo*W (overlap only)
    Real eps_ox = 3.45e-11;
    Real cox = eps_ox / params.tox;
    Real cox_wl = cox * params.get_weff() * params.get_leff();
    Real expected_cgs = (2.0 / 3.0) * cox_wl + params.cgso * params.get_weff();
    Real expected_cgd = params.cgdo * params.get_weff();  // Overlap only
    Real expected_cgb = params.cgbo * params.get_leff();

    EXPECT_NEAR(cgs, expected_cgs, 1e-16);
    EXPECT_NEAR(cgd, expected_cgd, 1e-16);
    EXPECT_NEAR(cgb, expected_cgb, 1e-16);

    // Cgd should be much smaller than Cgs in saturation (no channel capacitance)
    EXPECT_LT(cgd, cgs * 0.1);
}

TEST(MOSFET_Level3_N_Capacitance, RegionTransition_ContinuousChange) {
    auto params = create_capacitance_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    Real vgs = 2.0;
    Real vbs = 0.0;

    // Sweep Vds from 0 to 3V, crossing linear-to-saturation boundary
    Real vds_lin = 0.5;    // Linear region
    Real vds_sat = 2.0;    // Saturation region

    Real cgs_lin = mosfet.get_gate_source_capacitance(vgs, vds_lin, vbs);
    Real cgd_lin = mosfet.get_gate_drain_capacitance(vgs, vds_lin, vbs);

    Real cgs_sat = mosfet.get_gate_source_capacitance(vgs, vds_sat, vbs);
    Real cgd_sat = mosfet.get_gate_drain_capacitance(vgs, vds_sat, vbs);

    // Cgs should remain approximately constant across transition
    EXPECT_NEAR(cgs_lin, cgs_sat, cgs_lin * 0.01);

    // Cgd should decrease significantly in saturation
    EXPECT_GT(cgd_lin, cgd_sat * 3.0);
}

//==============================================================================
// Junction Capacitance Tests (Voltage Dependence)
//==============================================================================

TEST(MOSFET_Level3_N_Capacitance, JunctionCap_ReverseBias) {
    auto params = create_capacitance_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Reverse bias: Vbs < 0
    Real vbs_0 = 0.0;
    Real vbs_1 = -1.0;
    Real vbs_2 = -2.0;

    Real cbs_0 = mosfet.get_bulk_source_capacitance(vbs_0);
    Real cbs_1 = mosfet.get_bulk_source_capacitance(vbs_1);
    Real cbs_2 = mosfet.get_bulk_source_capacitance(vbs_2);

    // Capacitance should decrease with increasing reverse bias
    EXPECT_GT(cbs_0, cbs_1);
    EXPECT_GT(cbs_1, cbs_2);

    // Verify formula: C(V) = Cj0 / (1 - V/Vj)^M
    Real cj0 = params.cbs;
    Real vj = params.pb;
    Real m = params.mj;

    Real expected_cbs_1 = cj0 / std::pow(1.0 - vbs_1 / vj, m);
    Real expected_cbs_2 = cj0 / std::pow(1.0 - vbs_2 / vj, m);

    EXPECT_NEAR(cbs_1, expected_cbs_1, 1e-18);
    EXPECT_NEAR(cbs_2, expected_cbs_2, 1e-18);
}

TEST(MOSFET_Level3_N_Capacitance, JunctionCap_ForwardBias) {
    auto params = create_capacitance_params();
    MOSFET_Level3_N mosfet(1, "M1", 1, 2, 0, 0, params);

    // Forward bias: Vbs > 0 (linear approximation)
    Real vbs = 0.2;

    Real cbs = mosfet.get_bulk_source_capacitance(vbs);

    // Linear approximation: C(V) = Cj0 * (1 + M*V/Vj)
    Real cj0 = params.cbs;
    Real vj = params.pb;
    Real m = params.mj;

    Real expected_cbs = cj0 * (1.0 + m * vbs / vj);

    EXPECT_NEAR(cbs, expected_cbs, 1e-18);
}

//==============================================================================
// MOSFET_Level3_P - Capacitance Tests
//==============================================================================

TEST(MOSFET_Level3_P_Capacitance, Cutoff_CgbMaximum) {
    auto params = create_capacitance_params();
    params.vto = -0.7;  // P-channel has negative Vto

    MOSFET_Level3_P mosfet(1, "M1", 0, 2, 3, 3, params);

    // Cutoff region: Vsg < |Vth|
    Real vsg = 0.5;  // < 0.7V
    Real vsd = 1.0;
    Real vsb = 0.0;

    Real cgs = mosfet.get_gate_source_capacitance(vsg, vsd, vsb);
    Real cgd = mosfet.get_gate_drain_capacitance(vsg, vsd, vsb);
    Real cgb = mosfet.get_gate_bulk_capacitance(vsg, vsd, vsb);

    Real expected_cgs = params.cgso * params.get_weff();
    Real expected_cgd = params.cgdo * params.get_weff();

    Real eps_ox = 3.45e-11;
    Real cox = eps_ox / params.tox;
    Real expected_cgb = cox * params.get_weff() * params.get_leff() +
                        params.cgbo * params.get_leff();

    EXPECT_NEAR(cgs, expected_cgs, 1e-16);
    EXPECT_NEAR(cgd, expected_cgd, 1e-16);
    EXPECT_NEAR(cgb, expected_cgb, 1e-16);

    // Cgb should be dominant in cutoff
    EXPECT_GT(cgb, cgs);
    EXPECT_GT(cgb, cgd);
}

TEST(MOSFET_Level3_P_Capacitance, Saturation_CgdMinimum) {
    auto params = create_capacitance_params();
    params.vto = -0.7;

    MOSFET_Level3_P mosfet(1, "M1", 0, 2, 3, 3, params);

    // Saturation region: Vsg > |Vth|, Vsd >= Vsg - |Vth|
    Real vsg = 2.0;
    Real vsd = 2.0;
    Real vsb = 0.0;

    Real cgs = mosfet.get_gate_source_capacitance(vsg, vsd, vsb);
    Real cgd = mosfet.get_gate_drain_capacitance(vsg, vsd, vsb);

    // In saturation: Cgd should be minimal (overlap only)
    Real expected_cgd = params.cgdo * params.get_weff();

    EXPECT_NEAR(cgd, expected_cgd, 1e-16);

    // Cgd should be much smaller than Cgs
    EXPECT_LT(cgd, cgs * 0.1);
}
