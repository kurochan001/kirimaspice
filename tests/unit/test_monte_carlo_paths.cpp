// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_monte_carlo_paths.cpp
// The normal and the fast Monte Carlo paths must sample the same values for
// the same seed, and both must apply them: the divider output is an analytic
// function of the sampled resistances.

#include <gtest/gtest.h>
#include "kirimaspice/analysis/monte_carlo_executor.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace kirimaspice;
using namespace kirimaspice::analysis;

namespace {

class MonteCarloPaths : public ::testing::Test {
protected:
    void SetUp() override {
        static unsigned counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("kirimaspice_mc_paths_" + std::to_string(::getpid()) + "_" +
                 std::to_string(counter++) + ".cir");
        std::ofstream file(path_);
        file << "Monte Carlo path comparison\n"
             << "V1 in 0 10\n"
             << "R1 in out 1k\n"
             << "R2 out 0 1k\n"
             << ".op\n"
             << ".end\n";
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    SimulationConfig config() const {
        SimulationConfig config;
        config.monte_carlo.num_runs = 20;
        config.monte_carlo.seed = 12345;
        config.monte_carlo.variations = {variation("R1"), variation("R2")};
        return config;
    }

    static MonteCarloVariation variation(const std::string& device) {
        MonteCarloVariation variation;
        variation.device_name = device;
        variation.parameter_name = "resistance";
        variation.distribution = DistributionType::UNIFORM;
        variation.nominal_value = 1000.0;
        variation.tolerance = 0.05;
        variation.min_value = 950.0;
        variation.max_value = 1050.0;
        return variation;
    }

    std::filesystem::path path_;
};

// The sampled resistances turn the 10 V source into V1 * R2 / (R1 + R2).
TEST_F(MonteCarloPaths, TheFastPathAppliesTheSamplesAnalytically) {
    MonteCarloExecutor executor(config());
    auto result = executor.execute_dc_monte_carlo_fast(path_.string(), "out");

    ASSERT_EQ(result.num_runs, 20u);
    ASSERT_EQ(result.num_converged, 20u);
    ASSERT_EQ(result.parameter_samples.size(), 20u);
    ASSERT_EQ(result.all_values.size(), 20u);

    for (std::size_t run = 0; run < result.parameter_samples.size(); ++run) {
        const auto& samples = result.parameter_samples[run];
        const Real r1 = samples.at("R1.resistance");
        const Real r2 = samples.at("R2.resistance");
        const Real expected = 10.0 * r2 / (r1 + r2);
        EXPECT_NEAR(result.all_values[run], expected, 1e-12 * expected)
            << "run " << run << ", R1=" << r1 << ", R2=" << r2;
    }
}

TEST_F(MonteCarloPaths, NormalAndFastPathsMatchRunByRun) {
    MonteCarloExecutor normal(config());
    auto expected = normal.execute_dc_monte_carlo(path_.string(), "out");
    MonteCarloExecutor fast(config());
    auto actual = fast.execute_dc_monte_carlo_fast(path_.string(), "out");

    ASSERT_EQ(expected.num_runs, actual.num_runs);
    ASSERT_EQ(expected.num_converged, actual.num_converged);
    ASSERT_EQ(expected.parameter_samples.size(), actual.parameter_samples.size());
    ASSERT_EQ(expected.all_values.size(), actual.all_values.size());

    for (std::size_t run = 0; run < expected.parameter_samples.size(); ++run) {
        const auto& expected_samples = expected.parameter_samples[run];
        const auto& actual_samples = actual.parameter_samples[run];
        ASSERT_EQ(expected_samples.size(), actual_samples.size());
        for (const auto& [name, value] : expected_samples) {
            ASSERT_TRUE(actual_samples.contains(name)) << "run " << run;
            EXPECT_DOUBLE_EQ(value, actual_samples.at(name)) << "run " << run;
        }
    }

    for (std::size_t run = 0; run < expected.all_values.size(); ++run) {
        EXPECT_NEAR(expected.all_values[run], actual.all_values[run],
                    1e-12 * std::abs(expected.all_values[run]))
            << "run " << run;
    }
}

// A device parameter that changes derived state (Level1 recomputes beta when
// KP is set) must also agree between the two paths.
TEST_F(MonteCarloPaths, MosfetParameterVariationMatchesBetweenPaths) {
    {
        std::ofstream file(path_);
        file << "Monte Carlo MOSFET path comparison\n"
             << "VDD vdd 0 5\n"
             << "VIN in 0 1.5\n"
             << "M1 out in 0 0 NMOS W=10u L=1u\n"
             << "RD vdd out 10k\n"
             << ".model NMOS NMOS (LEVEL=1 VTO=0.7 KP=120u)\n"
             << ".op\n"
             << ".end\n";
    }

    SimulationConfig config;
    config.monte_carlo.num_runs = 10;
    config.monte_carlo.seed = 777;
    MonteCarloVariation variation;
    variation.device_name = "M1";
    variation.parameter_name = "KP";
    variation.distribution = DistributionType::UNIFORM;
    variation.nominal_value = 120e-6;
    variation.tolerance = 0.1;
    variation.min_value = 100e-6;
    variation.max_value = 140e-6;
    config.monte_carlo.variations = {variation};

    MonteCarloExecutor normal(config);
    auto expected = normal.execute_dc_monte_carlo(path_.string(), "out");
    MonteCarloExecutor fast(config);
    auto actual = fast.execute_dc_monte_carlo_fast(path_.string(), "out");

    ASSERT_EQ(expected.num_converged, 10u);
    ASSERT_EQ(actual.num_converged, 10u);
    ASSERT_EQ(expected.all_values.size(), actual.all_values.size());
    for (std::size_t run = 0; run < expected.all_values.size(); ++run) {
        EXPECT_DOUBLE_EQ(expected.parameter_samples[run].at("M1.KP"),
                         actual.parameter_samples[run].at("M1.KP")) << "run " << run;
        EXPECT_NEAR(expected.all_values[run], actual.all_values[run],
                    1e-12 * std::abs(expected.all_values[run]))
            << "run " << run;
    }
}


// Run i must draw from a generator of its own, seeded with
// MonteCarloExecutor::run_seed(seed, i); that is what makes builds with and
// without OpenMP agree for the same seed.
TEST_F(MonteCarloPaths, TheSamplesUseOneSeedPerRun) {
    const auto cfg = config();
    MonteCarloExecutor executor(cfg);
    auto result = executor.execute_dc_monte_carlo(path_.string(), "out");

    ASSERT_EQ(result.parameter_samples.size(), 20u);
    for (std::uint32_t run = 0; run < result.parameter_samples.size(); ++run) {
        RandomNumberGenerator rng(MonteCarloExecutor::run_seed(cfg.monte_carlo.seed, run));
        DistributionSampler first(variation("R1"));
        DistributionSampler second(variation("R2"));
        EXPECT_DOUBLE_EQ(result.parameter_samples[run].at("R1.resistance"),
                         first.sample(rng)) << "run " << run;
        EXPECT_DOUBLE_EQ(result.parameter_samples[run].at("R2.resistance"),
                         second.sample(rng)) << "run " << run;
    }
}

}  // namespace
