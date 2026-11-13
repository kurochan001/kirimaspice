// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_bsim1.cpp
// BSIM1 (Level 9) regression tests
// Coverage:
//   1. Parameter validation helpers
//   2. Basic device construction (N/P)
//   3. IV behavior monotonicity
//   4. Parser recognition for LEVEL=9 models

#include <gtest/gtest.h>
#include <string>
#include "kirimaspice/devices/mosfet_bsim1.hpp"  // Step 5.5: Unified BSIM1 template
#include "kirimaspice/devices/mosfet_bsim1_params.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::parser;

TEST(BSIM1Params, DefaultsValidate) {
    MOSFETBsim1Params params;
    EXPECT_TRUE(params.validate());

    params.w = 0.0;
    EXPECT_FALSE(params.validate());
    params.w = 1e-4;

    params.l = 0.0;
    EXPECT_FALSE(params.validate());
    params.l = 1e-5;

    params.tox = -1e-7;
    EXPECT_FALSE(params.validate());
}

TEST(BSIM1Params, EffectiveDimensionsApplyDelta) {
    MOSFETBsim1Params params;
    params.w = 10e-6;
    params.l = 1e-6;
    params.dw = 0.2e-6;
    params.dl = 0.05e-6;

    EXPECT_DOUBLE_EQ(params.get_weff(), params.w - params.dw);
    EXPECT_DOUBLE_EQ(params.get_leff(), params.l - params.dl);
}

TEST(BSIM1Device, NChannelCurrentRespondsToGateBias) {
    MOSFETBsim1Params params;
    params.w = 10e-6;
    params.l = 1e-6;
    params.vfb = -0.5;
    params.phi = 0.7;
    params.k1 = 0.5;

    MOSFET_BSIM1_N dev(1, "MN1", 1, 2, 3, 0, params);
    EXPECT_FALSE(dev.is_linear());
    EXPECT_TRUE(dev.is_dynamic());

    // Low gate bias should be in cutoff
    Real ids_low = dev.get_drain_current(0.1, 1.0, 0.0);
    // Higher gate bias should increase Ids
    Real ids_high = dev.get_drain_current(1.2, 1.0, 0.0);
    EXPECT_LT(ids_low, ids_high);
}

TEST(BSIM1Device, PChannelCurrentRespondsToGateBias) {
    MOSFETBsim1Params params;
    params.w = 10e-6;
    params.l = 1e-6;
    params.vfb = -0.9;  // Negative flat-band for PMOS (typical value)
    params.phi = 0.3;   // Reduced phi to lower Vth
    params.k1 = 0.2;    // Reduced k1 to lower Vth

    MOSFET_BSIM1_P dev(1, "MP1", 1, 2, 3, 0, params);
    EXPECT_FALSE(dev.is_linear());
    EXPECT_TRUE(dev.is_dynamic());

    // For P-channel: higher Vsg should give more current
    Real ids_low = dev.get_drain_current(0.8, 0.5, 0.0);   // Low gate bias
    Real ids_high = dev.get_drain_current(2.0, 0.5, 0.0);  // Higher gate bias

    EXPECT_LT(ids_low, ids_high);
}

TEST(BSIM1Parser, RecognizesLevel9Nmos) {
    std::string netlist = R"(* BSIM1 NMOS
.MODEL L9NMOS NMOS LEVEL=9 VFB=-0.5 PHI=0.7 K1=0.5
M1 out in 0 0 L9NMOS W=10u L=1u
VDD out 0 1.8
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    bool found = false;
    for (const auto& dev : result.circuit->get_devices()) {
        if (dev->type() == DeviceType::MOSFET_BSIM1_N) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(BSIM1Parser, RecognizesLevel9Pmos) {
    std::string netlist = R"(* BSIM1 PMOS
.MODEL L9PMOS PMOS LEVEL=9 VFB=0.5 PHI=0.7 K1=0.5
M1 out in vdd vdd L9PMOS W=10u L=1u
VDD vdd 0 1.8
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ASSERT_NE(result.circuit, nullptr);

    bool found = false;
    for (const auto& dev : result.circuit->get_devices()) {
        if (dev->type() == DeviceType::MOSFET_BSIM1_P) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}
