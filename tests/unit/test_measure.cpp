// tests/unit/test_measure.cpp
#include <gtest/gtest.h>

#include "kirimaspice/measure/measure.hpp"
#include "kirimaspice/core/types.hpp"

#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::measure;

class MeasureTest : public ::testing::Test {
protected:
    // Helper: Create simple waveform data
    WaveformData create_sine_wave(Real frequency, Real amplitude, Real duration, std::size_t num_points) {
        WaveformData data;
        data.x_label = "time";
        data.y_label = "v(out)";

        Real dt = duration / (num_points - 1);
        for (std::size_t i = 0; i < num_points; ++i) {
            Real t = i * dt;
            Real v = amplitude * std::sin(2.0 * M_PI * frequency * t);
            data.x_values.push_back(t);
            data.y_values.push_back(v);
        }

        return data;
    }

    // Helper: Create ramp waveform
    WaveformData create_ramp(Real start_value, Real end_value, Real duration, std::size_t num_points) {
        WaveformData data;
        data.x_label = "time";
        data.y_label = "v(out)";

        Real dt = duration / (num_points - 1);
        for (std::size_t i = 0; i < num_points; ++i) {
            Real t = i * dt;
            Real v = start_value + (end_value - start_value) * t / duration;
            data.x_values.push_back(t);
            data.y_values.push_back(v);
        }

        return data;
    }

    // Helper: Create step waveform
    WaveformData create_step(Real v_low, Real v_high, Real step_time, Real duration, std::size_t num_points) {
        WaveformData data;
        data.x_label = "time";
        data.y_label = "v(out)";

        Real dt = duration / (num_points - 1);
        for (std::size_t i = 0; i < num_points; ++i) {
            Real t = i * dt;
            Real v = (t < step_time) ? v_low : v_high;
            data.x_values.push_back(t);
            data.y_values.push_back(v);
        }

        return data;
    }
};

// =============================================================================
// Test 1-6: Statistical measurements (MAX, MIN, AVG, RMS, PP, INTEG)
// =============================================================================

TEST_F(MeasureTest, MAX_SineWave) {
    // Create 1kHz sine wave, amplitude 1V, duration 1ms
    auto data = create_sine_wave(1000.0, 1.0, 1e-3, 100);

    MeasureConfig config;
    config.name = "vmax";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::MAX;
    config.signal = "v(out)";

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.value, 1.0, 0.05);  // MAX should be ~1.0V (amplitude)
    EXPECT_EQ(result.unit, "V");
    EXPECT_TRUE(result.at_time.has_value());  // Should record when max occurred
}

TEST_F(MeasureTest, MIN_SineWave) {
    // Create 1kHz sine wave, amplitude 1V, duration 1ms
    auto data = create_sine_wave(1000.0, 1.0, 1e-3, 100);

    MeasureConfig config;
    config.name = "vmin";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::MIN;
    config.signal = "v(out)";

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.value, -1.0, 0.05);  // MIN should be ~-1.0V
    EXPECT_EQ(result.unit, "V");
}

TEST_F(MeasureTest, AVG_SineWave) {
    // Create 1kHz sine wave, amplitude 1V, duration 10ms (10 complete cycles)
    auto data = create_sine_wave(1000.0, 1.0, 10e-3, 1000);

    MeasureConfig config;
    config.name = "vavg";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::AVG;
    config.signal = "v(out)";

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // Average of sine wave over complete cycles should be ~0
    EXPECT_NEAR(result.value, 0.0, 0.01);
    EXPECT_EQ(result.unit, "V");
}

TEST_F(MeasureTest, RMS_SineWave) {
    // Create 1kHz sine wave, amplitude 1V
    auto data = create_sine_wave(1000.0, 1.0, 10e-3, 1000);

    MeasureConfig config;
    config.name = "vrms";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::RMS;
    config.signal = "v(out)";

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // RMS of sine wave: A/sqrt(2) = 1.0/1.414 = 0.707
    EXPECT_NEAR(result.value, 1.0 / std::sqrt(2.0), 0.01);
    EXPECT_EQ(result.unit, "V");
}

TEST_F(MeasureTest, PP_SineWave) {
    // Create 1kHz sine wave, amplitude 1V
    auto data = create_sine_wave(1000.0, 1.0, 1e-3, 100);

    MeasureConfig config;
    config.name = "vpp";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::PP;
    config.signal = "v(out)";

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // Peak-to-peak: max - min = 1.0 - (-1.0) = 2.0V
    EXPECT_NEAR(result.value, 2.0, 0.1);
    EXPECT_EQ(result.unit, "V");
}

TEST_F(MeasureTest, INTEG_Ramp) {
    // Create ramp from 0 to 1V over 1 second
    auto data = create_ramp(0.0, 1.0, 1.0, 1000);

    MeasureConfig config;
    config.name = "vintegral";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::INTEG;
    config.signal = "v(out)";

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // Integral of ramp 0→1V over 1s: (1/2) * 1V * 1s = 0.5 V*s
    EXPECT_NEAR(result.value, 0.5, 0.01);
    EXPECT_EQ(result.unit, "V*s");
}

// =============================================================================
// Test 7-9: Range-based measurements (FROM/TO)
// =============================================================================

TEST_F(MeasureTest, AVG_WithRange) {
    // Create ramp from 0 to 10V over 10 seconds
    auto data = create_ramp(0.0, 10.0, 10.0, 1000);

    MeasureConfig config;
    config.name = "vavg_5to8";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::AVG;
    config.signal = "v(out)";
    config.from = 5.0;  // Start at 5 seconds
    config.to = 8.0;    // End at 8 seconds

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // Average from 5s to 8s: ramp is at 5V→8V, average = 6.5V
    EXPECT_NEAR(result.value, 6.5, 0.1);
}

TEST_F(MeasureTest, MAX_WithRange) {
    // Create sine wave
    auto data = create_sine_wave(1000.0, 1.0, 10e-3, 1000);

    MeasureConfig config;
    config.name = "vmax_range";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::MAX;
    config.signal = "v(out)";
    config.from = 2e-3;  // Start at 2ms
    config.to = 8e-3;    // End at 8ms

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    EXPECT_NEAR(result.value, 1.0, 0.05);
}

// =============================================================================
// Test 10-12: Interpolation-based measurements (FIND)
// =============================================================================

TEST_F(MeasureTest, FIND_Ramp) {
    // Create ramp from 0 to 10V over 10 seconds
    auto data = create_ramp(0.0, 10.0, 10.0, 100);

    MeasureConfig config;
    config.name = "v_at_5s";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::FIND;
    config.signal = "v(out)";
    config.at = 5.0;  // Find value at 5 seconds

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // At 5 seconds, ramp should be at 5V
    EXPECT_NEAR(result.value, 5.0, 0.1);
    EXPECT_EQ(result.unit, "V");
}

TEST_F(MeasureTest, FIND_OutOfRange) {
    // Create ramp from 0 to 10V over 10 seconds
    auto data = create_ramp(0.0, 10.0, 10.0, 100);

    MeasureConfig config;
    config.name = "v_out_of_range";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::FIND;
    config.signal = "v(out)";
    config.at = 20.0;  // Outside data range
    config.default_val = -999.0;  // Use default value

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.value, -999.0);  // Should return default value
}

// =============================================================================
// Test 13-15: Edge detection (WHEN, TRIG_TARG)
// =============================================================================

TEST_F(MeasureTest, WHEN_StepRising) {
    // Create step from 0V to 5V at 1ms
    auto data = create_step(0.0, 5.0, 1e-3, 10e-3, 1000);

    MeasureConfig config;
    config.name = "t_cross_2p5";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::WHEN;
    config.signal = "v(out)";
    config.when_value = 2.5;  // Detect when signal crosses 2.5V
    config.when_edge = MeasureEdge::RISE;
    config.when_occurrence = 1;  // First occurrence

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // Crossing should occur at ~1ms
    EXPECT_NEAR(result.value, 1e-3, 0.5e-3);
    EXPECT_EQ(result.unit, "s");
}

TEST_F(MeasureTest, DERIV_Ramp) {
    // Create ramp from 0 to 10V over 10 seconds (slope = 1 V/s)
    auto data = create_ramp(0.0, 10.0, 10.0, 1000);

    MeasureConfig config;
    config.name = "slope";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::DERIV;
    config.signal = "v(out)";
    config.at = 5.0;  // Derivative at 5 seconds

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_TRUE(result.success);
    // Slope should be 1 V/s
    EXPECT_NEAR(result.value, 1.0, 0.1);
    EXPECT_EQ(result.unit, "V/s");
}

// =============================================================================
// Test 16: Error handling
// =============================================================================

TEST_F(MeasureTest, InvalidData_EmptyWaveform) {
    WaveformData empty_data;

    MeasureConfig config;
    config.name = "test";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::MAX;
    config.signal = "v(out)";

    auto result = MeasureEngine::evaluate(config, empty_data);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(MeasureTest, FIND_MissingAT) {
    auto data = create_ramp(0.0, 10.0, 10.0, 100);

    MeasureConfig config;
    config.name = "test";
    config.analysis_type = SimMode::TRANSIENT;
    config.type = MeasureType::FIND;
    config.signal = "v(out)";
    // Missing: config.at

    auto result = MeasureEngine::evaluate(config, data);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}
