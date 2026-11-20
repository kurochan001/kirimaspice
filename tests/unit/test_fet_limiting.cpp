// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// SPICE3 limits the gate voltage of a FET between Newton iterations
// (DEVfetlim, core::limit_fet_gate_step) and its drain voltage (DEVlimvds,
// core::limit_fet_drain_step). The level-1 MOSFET and the MESFET evaluate
// at the limited voltages and say so (limited_in_last_stamp), so that the
// solver does not accept such an iteration; at a converged iterate nothing
// is limited.

#include <gtest/gtest.h>

#include "kirimaspice/core/device_utils.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

TEST(FetLimiting, GateStepFollowsTheRegions) {
    // vto = 1 V: vtox = 4.5 V; hi = 2|vold - vto| + 2, lo = |vold - vto| + 1
    using core::limit_fet_gate_step;
    // off (vold < vto): a rise stops at vto + 0.5, or at vold + lo below it
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(10.0, 0.0, 1.0), 1.5);
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(0.5, 0.0, 1.0), 0.5);
    // (a rise to vto + 0.5 or less is always below vold + lo, so unchanged)
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(1.4, -2.0, 1.0), 1.4);
    // off: a fall of more than hi stops at vold - hi
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(-20.0, 0.0, 1.0), -4.0);
    // middle (vto <= vold < vtox): a rise stops at vto + 4, a fall at vto - 0.5
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(10.0, 2.0, 1.0), 5.0);
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(-3.0, 2.0, 1.0), 0.5);
    // on (vold >= vtox): hi = 12, lo = 6 from vold = 6
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(20.0, 6.0, 1.0), 18.0);
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(5.0, 6.0, 1.0), 5.0);
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(-10.0, 6.0, 1.0), 3.0);
    // converged
    EXPECT_DOUBLE_EQ(limit_fet_gate_step(2.345, 2.345, 1.0), 2.345);
}

namespace {

// Stamp the first device of netlist twice: at a point far from the last
// evaluation (0 V everywhere), then at the same point again
std::pair<bool, bool> limited_far_then_same(const std::string& netlist, const std::string& name,
                                            const DenseVector& v) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    const auto device = parsed.circuit->get_device(name);
    MNASystem mna;
    mna.resize(static_cast<std::uint32_t>(v.size()), 0);
    mna.zero();
    device->stamp_dc_nonlinear(mna, v);
    const bool far = device->limited_in_last_stamp();
    // Converge onto the limited point and stamp there twice
    for (int i = 0; i < 20; ++i) {
        mna.zero();
        device->stamp_dc_nonlinear(mna, v);
    }
    return {far, device->limited_in_last_stamp()};
}

}  // namespace

TEST(FetLimiting, Level1MosfetLimitsALargeStep) {
    // d = 1, g = 2: VGS from 0 to 10 V is limited (vto + 0.5 from off); the
    // same point, once reached by repeated stamps, is not
    const auto [far, same] =
        limited_far_then_same("t\nM1 1 2 0 0 NM\n.model NM NMOS LEVEL=1 VTO=1 KP=1e-4\n.end\n", "M1",
                              DenseVector{10.0, 10.0});
    EXPECT_TRUE(far);
    EXPECT_FALSE(same);
}

TEST(FetLimiting, MesfetLimitsALargeStep) {
    const auto [far, same] = limited_far_then_same(
        "t\nZ1 1 2 0 ZM\n.model ZM NMF VTO=-2 BETA=0.01\n.end\n", "Z1", DenseVector{10.0, 5.0});
    EXPECT_TRUE(far);
    EXPECT_FALSE(same);
}
