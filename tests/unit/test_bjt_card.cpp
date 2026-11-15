// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The parameters of a bipolar transistor's .MODEL card, and those of its
// element line after them, reach the device through one reader
// (bjt_params_from_model, set_bjt_parameter), for NPN and PNP alike.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/bjt_pnp.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"

using namespace kirimaspice;

namespace {

template <typename Bjt>
std::shared_ptr<Bjt> parse_q1(const std::string& type, const std::string& line_extra) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVC c 0 1\nVB b 0 0.7\nQ1 c b 0 qm" + line_extra + "\n.model qm " + type +
        " IS=2e-15 BF=150 NF=1.1 BR=3 NR=1.2 VAF=50 VAR=20 IKF=0.1 IKR=0.05 TNOM=50\n.op\n.end\n");
    return std::dynamic_pointer_cast<Bjt>(parsed.circuit->get_device("Q1"));
}

template <typename Bjt>
void expect_card(const std::shared_ptr<Bjt>& q) {
    ASSERT_NE(q, nullptr);
    EXPECT_DOUBLE_EQ(q->saturation_current(), 2e-15);
    EXPECT_DOUBLE_EQ(q->forward_emission_coefficient(), 1.1);
    EXPECT_DOUBLE_EQ(q->reverse_beta(), 3.0);
    EXPECT_DOUBLE_EQ(q->reverse_emission_coefficient(), 1.2);
    EXPECT_DOUBLE_EQ(q->forward_early_voltage(), 50.0);
    EXPECT_DOUBLE_EQ(q->reverse_early_voltage(), 20.0);
    EXPECT_DOUBLE_EQ(q->get_tnom(), 50.0 + 273.15);
}

}  // namespace

TEST(BjtCard, EveryParameterOfTheCardReachesTheDevice) {
    const auto n = parse_q1<devices::BJT_NPN>("npn", "");
    expect_card(n);
    EXPECT_DOUBLE_EQ(n->forward_beta(), 150.0);
    const auto p = parse_q1<devices::BJT_PNP>("pnp", "");
    expect_card(p);
    EXPECT_DOUBLE_EQ(p->forward_beta(), 150.0);
}

// The element line's parameters override the card's
TEST(BjtCard, ElementLineOverridesTheCard) {
    const auto n = parse_q1<devices::BJT_NPN>("npn", " BF=80");
    ASSERT_NE(n, nullptr);
    EXPECT_DOUBLE_EQ(n->forward_beta(), 80.0);
    EXPECT_DOUBLE_EQ(n->forward_early_voltage(), 50.0);
}
