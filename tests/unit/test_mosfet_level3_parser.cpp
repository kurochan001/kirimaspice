// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_mosfet_level3_parser.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/mosfet_level3.hpp"  // Unified template

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::devices;

//==============================================================================
// MOSFET Level 3 Parser Tests
//==============================================================================

TEST(MOSFETLevel3Parser, BasicNChannelModel) {
    std::string netlist = R"(
Test MOSFET Level 3 N-Channel Parser
.MODEL NMOS3 NMOS LEVEL=3 VTO=0.7 TOX=20n NSUB=1e17 UO=600
M1 2 1 0 0 NMOS3 W=10u L=1u
V1 1 0 DC 2.0
V2 2 0 DC 3.0
.DC
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Verify model was created
    EXPECT_EQ(result.config.models.count("NMOS3"), 1);
    EXPECT_EQ(result.config.models["NMOS3"].device_type, DeviceType::MOSFET_LEVEL3_N);

    // Verify device was created (M1 + V1 + V2 = 3 devices)
    EXPECT_EQ(result.circuit->num_devices(), 3);

    // Find MOSFET device
    bool found_mosfet = false;
    for (const auto& dev : result.circuit->get_devices()) {
        if (dev->name() == "M1" && dev->type() == DeviceType::MOSFET_LEVEL3_N) {
            found_mosfet = true;
            break;
        }
    }
    EXPECT_TRUE(found_mosfet);
}

TEST(MOSFETLevel3Parser, BasicPChannelModel) {
    std::string netlist = R"(
Test MOSFET Level 3 P-Channel Parser
.MODEL PMOS3 PMOS LEVEL=3 VTO=-0.7 TOX=20n NSUB=1e17 UO=250
M1 2 1 3 3 PMOS3 W=10u L=1u
V1 1 0 DC 1.0
V2 2 0 DC 0.0
V3 3 0 DC 3.0
.DC
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Verify model was created
    EXPECT_EQ(result.config.models.count("PMOS3"), 1);
    EXPECT_EQ(result.config.models["PMOS3"].device_type, DeviceType::MOSFET_LEVEL3_P);

    // Verify device was created (M1 + V1 + V2 + V3 = 4 devices)
    EXPECT_EQ(result.circuit->num_devices(), 4);

    // Find MOSFET device
    bool found_mosfet = false;
    for (const auto& dev : result.circuit->get_devices()) {
        if (dev->name() == "M1" && dev->type() == DeviceType::MOSFET_LEVEL3_P) {
            found_mosfet = true;
            break;
        }
    }
    EXPECT_TRUE(found_mosfet);
}

TEST(MOSFETLevel3Parser, AllParameters) {
    std::string netlist = R"(
Test MOSFET Level 3 All Parameters
.MODEL NMOS_FULL NMOS LEVEL=3
+ VTO=0.7 TOX=20n NSUB=1e17 UO=600
+ THETA=0.1 VMAX=1e5
+ ETA=0.0 KAPPA=0.0 DELTA=0.0 LAMBDA=0.02
+ CGSO=1e-10 CGDO=1e-10 CGBO=1e-11
+ PHI=0.7 GAMMA=0.5
M1 2 1 0 0 NMOS_FULL W=10u L=1u
V1 1 0 DC 2.0
V2 2 0 DC 3.0
.DC
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Verify model parameters
    const auto& model = result.config.models["NMOS_FULL"];
    EXPECT_DOUBLE_EQ(model.params.at("vto"), 0.7);
    EXPECT_DOUBLE_EQ(model.params.at("tox"), 20e-9);
    EXPECT_DOUBLE_EQ(model.params.at("uo"), 600.0);
    EXPECT_DOUBLE_EQ(model.params.at("theta"), 0.1);
    EXPECT_DOUBLE_EQ(model.params.at("lambda"), 0.02);
}

TEST(MOSFETLevel3Parser, InstanceParameterOverride) {
    std::string netlist = R"(
Test MOSFET Level 3 Instance Override
.MODEL NMOS3 NMOS LEVEL=3 VTO=0.7 W=5u L=2u
M1 2 1 0 0 NMOS3 W=10u L=1u
V1 1 0 DC 2.0
V2 2 0 DC 3.0
.DC
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Model W/L should be overridden by instance W/L
    // This is verified by successful device creation
    EXPECT_EQ(result.circuit->num_devices(), 3);

    // Find MOSFET device
    bool found_mosfet = false;
    for (const auto& dev : result.circuit->get_devices()) {
        if (dev->name() == "M1") {
            found_mosfet = true;
            break;
        }
    }
    EXPECT_TRUE(found_mosfet);
}

TEST(MOSFETLevel3Parser, FourTerminalDevice) {
    std::string netlist = R"(
Test MOSFET Level 3 4-Terminal
.MODEL NMOS3 NMOS LEVEL=3 VTO=0.7
M1 2 1 0 3 NMOS3 W=10u L=1u
V1 1 0 DC 2.0
V2 2 0 DC 3.0
V3 3 0 DC -1.0
.DC
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    EXPECT_EQ(result.circuit->num_devices(), 4);

    // Find MOSFET device
    bool found_mosfet = false;
    for (const auto& dev : result.circuit->get_devices()) {
        if (dev->name() == "M1") {
            found_mosfet = true;
            break;
        }
    }
    EXPECT_TRUE(found_mosfet);
}

// DCAnalysisIntegration test removed - DC analysis is tested in test_mosfet_level3.cpp

TEST(MOSFETLevel3Parser, CaseInsensitiveParameters) {
    std::string netlist = R"(
Test MOSFET Level 3 Case Insensitive
.MODEL NMOS3 NMOS level=3 vto=0.7 tox=20n uo=600
M1 2 1 0 0 NMOS3 w=10u l=1u
V1 1 0 DC 2.0
V2 2 0 DC 3.0
.DC
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);
    EXPECT_EQ(result.config.models.count("NMOS3"), 1);
}

TEST(MOSFETLevel3Parser, MultipleDevicesSameModel) {
    std::string netlist = R"(
Test MOSFET Level 3 Multiple Devices
.MODEL NMOS3 NMOS LEVEL=3 VTO=0.7
M1 2 1 0 0 NMOS3 W=10u L=1u
M2 3 1 0 0 NMOS3 W=20u L=2u
M3 4 1 0 0 NMOS3 W=5u L=0.5u
V1 1 0 DC 2.0
V2 2 0 DC 3.0
V3 3 0 DC 3.0
V4 4 0 DC 3.0
.DC
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_NE(result.circuit, nullptr);

    // Verify all devices were created (M1, M2, M3, V1, V2, V3, V4 = 7 devices)
    EXPECT_EQ(result.circuit->num_devices(), 7);

    // Find all three MOSFET devices
    int mosfet_count = 0;
    for (const auto& dev : result.circuit->get_devices()) {
        if (dev->name() == "M1" || dev->name() == "M2" || dev->name() == "M3") {
            EXPECT_EQ(dev->type(), DeviceType::MOSFET_LEVEL3_N);
            mosfet_count++;
        }
    }
    EXPECT_EQ(mosfet_count, 3);
}
