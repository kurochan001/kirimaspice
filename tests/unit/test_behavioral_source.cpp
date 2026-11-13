// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/devices/behavioral_source.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"

using kirimaspice::devices::BehavioralSource;
using kirimaspice::BehavioralSourceType;
using kirimaspice::DenseVector;
using kirimaspice::MNASystem;
using kirimaspice::NodeId;
using kirimaspice::Real;

namespace {

MNASystem make_mna(std::uint32_t nodes, std::uint32_t branches = 0) {
    MNASystem mna;
    mna.resize(nodes, branches);
    mna.zero();
    return mna;
}

DenseVector make_voltages(std::uint32_t nodes) {
    return DenseVector(nodes, 0.0);
}

} // namespace

TEST(BehavioralSource, NullEvaluatorReturnsZero) {
    // CURRENT source with null evaluator should fall back to zero without throwing.
    BehavioralSource src(/*id*/1, "BNULL",
                         /*np*/1, /*nn*/0,
                         BehavioralSourceType::CURRENT,
                         "I=TIME", nullptr);

    auto mna = make_mna(/*nodes*/1);
    auto v = make_voltages(1);

    src.stamp_dc_nonlinear(mna, v);

    // No entries should be added because evaluated value is 0.
    EXPECT_EQ(mna.b[0], 0.0);
    EXPECT_TRUE(mna.G.nnz() == 0);
}

TEST(BehavioralSource, ExceptionEvaluatorFallsBackToZero) {
    auto throwing_eval = [](Real /*t*/, const DenseVector& /*v*/) -> Real {
        throw std::runtime_error("boom");
    };

    BehavioralSource src(/*id*/2, "BEXC",
                         /*np*/1, /*nn*/0,
                         BehavioralSourceType::CURRENT,
                         "I=bad()", throwing_eval);

    auto mna = make_mna(/*nodes*/1);
    auto v = make_voltages(1);

    src.stamp_dc_nonlinear(mna, v);

    EXPECT_EQ(mna.b[0], 0.0);
    EXPECT_TRUE(mna.G.nnz() == 0);
}
