// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The step limiting of a FET's Newton iteration starts from the voltages of
// its last evaluation (core::limit_fet_gate_step, limit_fet_drain_step).
// Those voltages are state: reset_state sets them back to 0 V, as for a new
// device, and restore_state to their values at save_state, as for the
// other state a rejected step restores. The level-1 MOSFET and the MESFET.
//
// A device that has iterated at VGS = VDS = 10 V takes that point without
// limiting; a new one limits the step from 0 V to it. After reset_state, and
// after save_state, the iteration and restore_state, the device must stamp
// that point as a new one does.

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "kirimaspice/devices/mesfet.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"

using namespace kirimaspice;

namespace {

struct Stamp {
    std::map<std::pair<std::uint32_t, std::uint32_t>, Real> g;
    std::vector<Real> b;
    bool operator==(const Stamp& other) const { return g == other.g && b == other.b; }
};

// The DC stamp of the device (drain node 1, gate node 2, source ground) at
// VD = VG = v
Stamp stamp(const devices::Device& device, Real v) {
    MNASystem mna;
    mna.resize(2, 0);
    DenseVector voltages(2, v);
    device.stamp_dc_nonlinear(mna, voltages);
    Stamp s;
    for (std::size_t k = 0; k < mna.G.nnz(); ++k) {
        s.g[{mna.G.rows[k], mna.G.cols[k]}] += mna.G.values[k];
    }
    s.b.assign(mna.b.begin(), mna.b.end());
    return s;
}

void iterate_at(const devices::Device& device, Real v) {
    for (int i = 0; i < 20; ++i) {
        stamp(device, v);
    }
}

template <typename Make>
void check_limiting_state(Make make) {
    auto fresh = make();
    const Stamp first = stamp(*fresh, 10.0);

    auto iterated = make();
    iterate_at(*iterated, 10.0);
    ASSERT_FALSE(stamp(*iterated, 10.0) == first) << "the case must limit a new device's step";

    auto reset = make();
    iterate_at(*reset, 10.0);
    reset->reset_state();
    EXPECT_TRUE(stamp(*reset, 10.0) == first) << "reset_state";

    auto restored = make();
    restored->save_state();
    iterate_at(*restored, 10.0);
    restored->restore_state();
    EXPECT_TRUE(stamp(*restored, 10.0) == first) << "save_state, restore_state";
}

}  // namespace

TEST(LimitingState, Level1MosfetResetsAndRestoresTheLastEvaluation) {
    check_limiting_state([] {
        return std::make_unique<devices::MOSFET_N>(1, "M1", 1, 2, 0, 1e-4, 10e-6, 1e-6, 0.7);
    });
}

TEST(LimitingState, MesfetResetsAndRestoresTheLastEvaluation) {
    check_limiting_state([] {
        return std::make_unique<devices::MESFET_N>(1, "Z1", 1, 2, 0);
    });
}
