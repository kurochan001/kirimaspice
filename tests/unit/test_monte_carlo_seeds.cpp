// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_monte_carlo_seeds.cpp
// Each Monte Carlo run draws from a generator of its own, seeded from the
// base seed of .MC and the run's index so that the runs of two base seeds
// are unrelated: with seed + run, base seeds s and s + 1 shared all runs but
// one. The executor's base seed stays what it was given.

#include <gtest/gtest.h>
#include "kirimaspice/analysis/monte_carlo_executor.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

using namespace kirimaspice;
using namespace kirimaspice::analysis;

namespace {

class MonteCarloSeeds : public ::testing::Test {
protected:
    void SetUp() override {
        static unsigned counter = 0;
        path_ = std::filesystem::temp_directory_path() /
                ("kirimaspice_mc_seeds_" + std::to_string(::getpid()) + "_" +
                 std::to_string(counter++) + ".cir");
        std::ofstream file(path_);
        file << "Monte Carlo seeds\n"
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

    static SimulationConfig config(std::uint32_t seed) {
        SimulationConfig config;
        config.monte_carlo.num_runs = 20;
        config.monte_carlo.seed = seed;
        for (const char* device : {"R1", "R2"}) {
            MonteCarloVariation variation;
            variation.device_name = device;
            variation.parameter_name = "resistance";
            variation.distribution = DistributionType::UNIFORM;
            variation.nominal_value = 1000.0;
            variation.tolerance = 0.05;
            variation.min_value = 950.0;
            variation.max_value = 1050.0;
            config.monte_carlo.variations.push_back(variation);
        }
        return config;
    }

    std::filesystem::path path_;
};

TEST_F(MonteCarloSeeds, NeighbouringBaseSeedsShareNoRun) {
    for (bool fast : {false, true}) {
        MonteCarloExecutor first(config(12345));
        MonteCarloExecutor second(config(12346));
        const auto a = fast ? first.execute_dc_monte_carlo_fast(path_.string(), "out")
                            : first.execute_dc_monte_carlo(path_.string(), "out");
        const auto b = fast ? second.execute_dc_monte_carlo_fast(path_.string(), "out")
                            : second.execute_dc_monte_carlo(path_.string(), "out");
        ASSERT_EQ(a.parameter_samples.size(), 20u);
        ASSERT_EQ(b.parameter_samples.size(), 20u);

        std::set<Real> drawn;
        for (const auto& run : a.parameter_samples) {
            drawn.insert(run.at("R1.resistance"));
        }
        int shared = 0;
        for (const auto& run : b.parameter_samples) {
            shared += drawn.count(run.at("R1.resistance")) ? 1 : 0;
        }
        EXPECT_EQ(shared, 0) << (fast ? "fast path" : "normal path");
    }
}

TEST_F(MonteCarloSeeds, TheBaseSeedStaysAfterTheRuns) {
    MonteCarloExecutor executor(config(12345));
    executor.execute_dc_monte_carlo(path_.string(), "out");
    EXPECT_EQ(executor.get_seed(), 12345u);
    executor.execute_dc_monte_carlo_fast(path_.string(), "out");
    EXPECT_EQ(executor.get_seed(), 12345u);
}

TEST_F(MonteCarloSeeds, ARepeatedRunDrawsTheSameSamples) {
    MonteCarloExecutor executor(config(12345));
    const auto once = executor.execute_dc_monte_carlo(path_.string(), "out");
    const auto again = executor.execute_dc_monte_carlo(path_.string(), "out");
    ASSERT_EQ(once.parameter_samples.size(), again.parameter_samples.size());
    for (std::size_t run = 0; run < once.parameter_samples.size(); ++run) {
        EXPECT_EQ(once.parameter_samples[run], again.parameter_samples[run]) << "run " << run;
    }
}

// Run seeds are never 0, which RandomNumberGenerator takes as "seed from the
// random device", and differ between runs and between neighbouring bases
TEST(MonteCarloRunSeed, DistinctAndNeverZero) {
    std::set<std::uint32_t> seeds;
    for (std::uint32_t base : {0u, 1u, 2u, 12345u, 12346u, 0xFFFFFFFFu}) {
        for (std::uint32_t run = 0; run < 100; ++run) {
            const auto seed = MonteCarloExecutor::run_seed(base, run);
            EXPECT_NE(seed, 0u);
            seeds.insert(seed);
        }
    }
    EXPECT_EQ(seeds.size(), 600u);
}

}  // namespace
