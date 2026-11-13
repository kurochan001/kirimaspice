// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/util/units.hpp"

using kirimaspice::Real;
using kirimaspice::util::parse_value_with_unit;

TEST(UnitsParse, MilliAmpAndMicroFarad) {
    // 10mA -> 0.01 A
    EXPECT_NEAR(parse_value_with_unit("10mA"), 0.01, 1e-9);
    // 4.7uF -> 4.7e-6 F
    EXPECT_NEAR(parse_value_with_unit("4.7uF"), 4.7e-6, 1e-12);

    // MEG (mega) prefix in SPICE style
    EXPECT_NEAR(parse_value_with_unit("2meg"), 2e6, 1e-3);
    // milli-ohm should stay milli even with following unit letters
    EXPECT_NEAR(parse_value_with_unit("5mOhm"), 5e-3, 1e-12);
    // kHz suffix keeps kilo scaling and drops unit
    EXPECT_NEAR(parse_value_with_unit("1kHz"), 1e3, 1e-9);
    // pico-farad upper bound
    EXPECT_NEAR(parse_value_with_unit("0.5pF"), 0.5e-12, 1e-15);
    // tera prefix
    EXPECT_NEAR(parse_value_with_unit("7T"), 7e12, 1e-3);
}

TEST(UnitsParse, MixedCaseAndFollowedUnits) {
    // Lower-case m should mean milli even when followed by unit letters
    EXPECT_NEAR(parse_value_with_unit("1mV"), 1e-3, 1e-9);
    EXPECT_NEAR(parse_value_with_unit("3mOhm"), 3e-3, 1e-12);

    // Upper-case MEG and mixed-case should still map to mega
    EXPECT_NEAR(parse_value_with_unit("1MEG"), 1e6, 1e-3);
    EXPECT_NEAR(parse_value_with_unit("2Meg"), 2e6, 1e-3);

    // Letters after a scale factor are dropped; M is milli whatever follows
    // (NETLIST_CONVENTIONS rule 1, as in ngspice), so 10MHz is 10 mHz
    EXPECT_NEAR(parse_value_with_unit("10MHz"), 10e-3, 1e-15);
    EXPECT_NEAR(parse_value_with_unit("0.5GHz"), 0.5e9, 1e-3);

    // Very small prefix to exercise precision warning path (no throw expected)
    EXPECT_NEAR(parse_value_with_unit("1fA"), 1e-15, 1e-21);
}
