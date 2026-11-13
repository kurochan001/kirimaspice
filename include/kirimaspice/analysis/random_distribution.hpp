// include/kirimaspice/analysis/random_distribution.hpp
#pragma once

#include "kirimaspice/core/types.hpp"

#include <random>
#include <cmath>

namespace kirimaspice {
namespace analysis {

/// Random number generator for Monte Carlo analysis
/// Thread-safe random number generation with reproducible seeds
class RandomNumberGenerator {
public:
    /// Constructor with optional seed (0 = use random device)
    explicit RandomNumberGenerator(std::uint32_t seed = 0);

    /// Get current seed value
    std::uint32_t get_seed() const { return seed_; }

    /// Reset with new seed
    void reset(std::uint32_t seed);

    /// Generate uniform random number in [0, 1)
    Real uniform_01();

    /// Generate uniform random number in [min, max]
    Real uniform(Real min, Real max);

    /// Generate Gaussian (normal) random number N(mean, std_dev)
    Real gaussian(Real mean, Real std_dev);

    /// Generate log-normal random number
    Real lognormal(Real mean, Real std_dev);

private:
    std::uint32_t seed_;
    std::mt19937 rng_;  // Mersenne Twister 19937 generator
};

/// Distribution sampler for Monte Carlo parameter variation
/// Generates parameter values according to specified distribution
class DistributionSampler {
public:
    /// Constructor with distribution configuration
    explicit DistributionSampler(const MonteCarloVariation& config);

    /// Generate a single sample value
    Real sample(RandomNumberGenerator& rng);

    /// Get distribution type
    DistributionType get_type() const { return config_.distribution; }

    /// Get nominal value
    Real get_nominal() const { return config_.nominal_value; }

private:
    MonteCarloVariation config_;
};

/// Statistical calculator for Monte Carlo results
/// Computes mean, std dev, percentiles, histogram, etc.
class StatisticalAnalyzer {
public:
    /// Compute statistics from a set of values
    static MonteCarloStats compute_stats(const std::vector<Real>& values);

    /// Compute histogram from values
    static HistogramData compute_histogram(const std::vector<Real>& values,
                                            std::uint32_t num_bins = 20);

    /// Compute specific percentile (0-100)
    static Real compute_percentile(const std::vector<Real>& values, Real percentile);

    /// Compute yield percentage given spec limits
    static Real compute_yield(const std::vector<Real>& values,
                              Real spec_min,
                              Real spec_max,
                              std::uint32_t& num_passed,
                              std::uint32_t& num_failed);

private:
    /// Helper: compute mean
    static Real mean(const std::vector<Real>& values);

    /// Helper: compute variance
    static Real variance(const std::vector<Real>& values, Real mean);

    /// Helper: compute standard deviation
    static Real std_dev(const std::vector<Real>& values, Real mean);
};

} // namespace analysis
} // namespace kirimaspice
