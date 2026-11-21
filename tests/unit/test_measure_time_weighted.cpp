// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_measure_time_weighted.cpp
// AVG, RMS and INTEG of .measure weigh each sample by the time it covers:
// AVG = (1 / (TO - FROM)) * integral of y, RMS = sqrt of the same mean of
// y^2, INTEG = integral of y, each over [FROM, TO] exactly, with y
// interpolated at FROM and TO when they fall between samples.
//
// The waveform is ngspice's PWL(0 0 1m 1 10m 1): a ramp from 0 to 1 over
// 1 ms, then 1 until 10 ms. Expected values are analytic; ngspice-47 gives
// the same for the deck
//   V1 a 0 PWL(0 0 1m 1 10m 1) / R1 a 0 1k / .tran 10u 10m
// (AVG 0.95, RMS 0.966092 over 0..10 ms; RMS 0.927961 and INTEG 1.375e-3
// over 0.5..2 ms).

#include <gtest/gtest.h>

#include "kirimaspice/measure/measure.hpp"

#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::measure;

namespace {

// Samples of the PWL as a transient run takes them: dense on the ramp
// (every 10 us), then none until the end of the flat part
WaveformData dense_ramp() {
    WaveformData data;
    data.x_label = "time";
    data.y_label = "v(a)";
    for (int k = 0; k <= 100; ++k) {
        data.x_values.push_back(k * 1e-5);
        data.y_values.push_back(k / 100.0);
    }
    data.x_values.push_back(10e-3);
    data.y_values.push_back(1.0);
    return data;
}

// The corners of the PWL alone
WaveformData corners() {
    WaveformData data;
    data.x_label = "time";
    data.y_label = "v(a)";
    data.x_values = {0.0, 1e-3, 10e-3};
    data.y_values = {0.0, 1.0, 1.0};
    return data;
}

MeasureConfig measure_of(MeasureType type) {
    MeasureConfig config;
    config.name = "m";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = type;
    config.signal = "v(a)";
    return config;
}

}  // namespace

// (0.5 * 1 ms + 9 ms) / 10 ms; the mean of the samples would be about 0.505
TEST(MeasureTimeWeighted, AvgOverTheWholeRun) {
    const auto result = MeasureEngine::evaluate(measure_of(MeasureType::AVG), dense_ramp());
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NEAR(result.value, 0.95, 1e-12);
}

// sqrt((1 ms / 3 + 9 ms) / 10 ms) = sqrt(14 / 15); the trapezoidal rule on
// y^2 errs by 1.7e-8 V^2 s on the ramp
TEST(MeasureTimeWeighted, RmsOverTheWholeRun) {
    const auto result = MeasureEngine::evaluate(measure_of(MeasureType::RMS), dense_ramp());
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NEAR(result.value, std::sqrt(14.0 / 15.0), 1e-5);
}

// From 0.5 ms (y = 0.5, between samples) to 2 ms (y = 1, between samples):
// (0.75 * 0.5 ms + 1 ms) / 1.5 ms = 11 / 12
TEST(MeasureTimeWeighted, AvgInterpolatesFromAndTo) {
    auto config = measure_of(MeasureType::AVG);
    config.from = 0.5e-3;
    config.to = 2e-3;
    const auto result = MeasureEngine::evaluate(config, corners());
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NEAR(result.value, 11.0 / 12.0, 1e-12);
}

// sqrt(((1 - 0.125) / 3 * 1 ms + 1 ms) / 1.5 ms) = sqrt(31 / 36)
TEST(MeasureTimeWeighted, RmsOverPartOfTheRun) {
    auto config = measure_of(MeasureType::RMS);
    config.from = 0.5e-3;
    config.to = 2e-3;
    const auto result = MeasureEngine::evaluate(config, dense_ramp());
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NEAR(result.value, std::sqrt(31.0 / 36.0), 1e-5);
}

// 0.75 * 0.5 ms + 1 ms = 1.375 ms
TEST(MeasureTimeWeighted, IntegInterpolatesFromAndTo) {
    auto config = measure_of(MeasureType::INTEG);
    config.from = 0.5e-3;
    config.to = 2e-3;
    const auto result = MeasureEngine::evaluate(config, corners());
    ASSERT_TRUE(result.success) << result.error_message;
    EXPECT_NEAR(result.value, 1.375e-3, 1e-15);
}

// An interval of no width has no mean
TEST(MeasureTimeWeighted, EmptyIntervalFails) {
    auto config = measure_of(MeasureType::AVG);
    config.from = 2e-3;
    config.to = 2e-3;
    EXPECT_FALSE(MeasureEngine::evaluate(config, corners()).success);
}
