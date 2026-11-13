// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/integration/test_monte_carlo_analysis.cpp
#include <gtest/gtest.h>
#include "kirimaspice/analysis/monte_carlo_executor.hpp"
#include "kirimaspice/analysis/random_distribution.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <filesystem>
#include <fstream>

using namespace kirimaspice;
using namespace kirimaspice::analysis;

//==============================================================================
// Monte Carlo Integration Tests
//==============================================================================

// Test 1: Random distribution sampling (Gaussian)
TEST(MonteCarloIntegration, RandomDistributionGaussian) {
    // Test Gaussian distribution sampling
    RandomNumberGenerator rng(42);  // Fixed seed

    MonteCarloVariation var;
    var.distribution = DistributionType::GAUSSIAN;
    var.nominal_value = 100.0;
    var.sigma = 10.0;
    var.min_value = 0.0;
    var.max_value = 1000.0;

    DistributionSampler sampler(var);

    // Sample 100 values
    std::vector<Real> samples;
    for (int i = 0; i < 100; ++i) {
        Real value = sampler.sample(rng);
        samples.push_back(value);
    }

    // Compute statistics
    auto stats = StatisticalAnalyzer::compute_stats(samples);

    // Mean should be close to nominal (100)
    EXPECT_NEAR(stats.mean, 100.0, 20.0);  // Allow 20% deviation

    // Std dev should be close to sigma (10)
    EXPECT_GT(stats.std_dev, 5.0);
    EXPECT_LT(stats.std_dev, 15.0);

    // Most values should be within 3-sigma range
    EXPECT_GE(stats.min, 50.0);
    EXPECT_LE(stats.max, 150.0);
}

// Test 2: Random distribution sampling (Uniform)
TEST(MonteCarloIntegration, RandomDistributionUniform) {
    // Test Uniform distribution sampling
    RandomNumberGenerator rng(123);  // Fixed seed

    MonteCarloVariation var;
    var.distribution = DistributionType::UNIFORM;
    var.nominal_value = 50.0;  // Not used for uniform
    var.min_value = 10.0;
    var.max_value = 90.0;

    DistributionSampler sampler(var);

    // Sample 200 values
    std::vector<Real> samples;
    for (int i = 0; i < 200; ++i) {
        Real value = sampler.sample(rng);
        samples.push_back(value);
    }

    // All values should be within [min, max]
    for (Real v : samples) {
        EXPECT_GE(v, 10.0);
        EXPECT_LE(v, 90.0);
    }

    // Compute statistics
    auto stats = StatisticalAnalyzer::compute_stats(samples);

    // Mean should be near (min+max)/2 = 50
    EXPECT_NEAR(stats.mean, 50.0, 10.0);

    // For uniform distribution, std dev = (max-min)/sqrt(12) ≈ 23
    EXPECT_GT(stats.std_dev, 15.0);
    EXPECT_LT(stats.std_dev, 30.0);
}

// Test 3: Test statistical analysis functions
TEST(MonteCarloIntegration, StatisticalAnalysis) {
    // Generate some test data
    std::vector<Real> values = {
        1.0, 2.0, 3.0, 4.0, 5.0,
        6.0, 7.0, 8.0, 9.0, 10.0
    };

    // Compute statistics
    auto stats = StatisticalAnalyzer::compute_stats(values);

    EXPECT_DOUBLE_EQ(stats.mean, 5.5);
    EXPECT_DOUBLE_EQ(stats.min, 1.0);
    EXPECT_DOUBLE_EQ(stats.max, 10.0);
    EXPECT_DOUBLE_EQ(stats.median, 5.5);

    // Standard deviation for 1-10 sequence
    EXPECT_NEAR(stats.std_dev, 3.0276, 0.01);

    // Test histogram generation
    auto hist = StatisticalAnalyzer::compute_histogram(values, 5);
    EXPECT_EQ(hist.num_bins, 5);
    EXPECT_EQ(hist.counts.size(), 5);

    // Each bin should have 2 values (uniform distribution)
    for (auto count : hist.counts) {
        EXPECT_EQ(count, 2);
    }
}

// Test 4: Test yield calculation
TEST(MonteCarloIntegration, YieldCalculation) {
    std::vector<Real> values = {
        1.0, 2.0, 3.0, 4.0, 5.0,
        6.0, 7.0, 8.0, 9.0, 10.0
    };

    std::uint32_t num_passed = 0;
    std::uint32_t num_failed = 0;

    // Spec: [3.0, 7.0] - should pass 5 values (3,4,5,6,7)
    Real yield = StatisticalAnalyzer::compute_yield(
        values,
        3.0,  // spec_min
        7.0,  // spec_max
        num_passed,
        num_failed
    );

    EXPECT_EQ(num_passed, 5);
    EXPECT_EQ(num_failed, 5);
    EXPECT_DOUBLE_EQ(yield, 50.0);  // 50% yield

    // Spec: [1.0, 10.0] - should pass all 10 values
    yield = StatisticalAnalyzer::compute_yield(
        values,
        1.0,
        10.0,
        num_passed,
        num_failed
    );

    EXPECT_EQ(num_passed, 10);
    EXPECT_EQ(num_failed, 0);
    EXPECT_DOUBLE_EQ(yield, 100.0);  // 100% yield
}

// Test 5: Test reproducibility with fixed seed
TEST(MonteCarloIntegration, ReproducibilityWithSeed) {
    // Test that fixed seed produces reproducible results
    RandomNumberGenerator rng1(999);
    RandomNumberGenerator rng2(999);

    MonteCarloVariation var;
    var.distribution = DistributionType::GAUSSIAN;
    var.nominal_value = 50.0;
    var.sigma = 5.0;
    var.min_value = 0.0;
    var.max_value = 100.0;

    DistributionSampler sampler1(var);
    DistributionSampler sampler2(var);

    // Sample 50 values from each
    std::vector<Real> samples1, samples2;
    for (int i = 0; i < 50; ++i) {
        samples1.push_back(sampler1.sample(rng1));
        samples2.push_back(sampler2.sample(rng2));
    }

    // All samples should be identical
    ASSERT_EQ(samples1.size(), samples2.size());
    for (size_t i = 0; i < samples1.size(); ++i) {
        EXPECT_DOUBLE_EQ(samples1[i], samples2[i]);
    }

    // Statistics should also be identical
    auto stats1 = StatisticalAnalyzer::compute_stats(samples1);
    auto stats2 = StatisticalAnalyzer::compute_stats(samples2);

    EXPECT_DOUBLE_EQ(stats1.mean, stats2.mean);
    EXPECT_DOUBLE_EQ(stats1.std_dev, stats2.std_dev);
    EXPECT_DOUBLE_EQ(stats1.min, stats2.min);
    EXPECT_DOUBLE_EQ(stats1.max, stats2.max);
}
