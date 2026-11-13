// src/analysis/random_distribution.cpp
#include "kirimaspice/analysis/random_distribution.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <chrono>

namespace kirimaspice {
namespace analysis {

// ============================================================================
// RandomNumberGenerator implementation
// ============================================================================

RandomNumberGenerator::RandomNumberGenerator(std::uint32_t seed)
    : seed_(seed)
{
    if (seed_ == 0) {
        // Use high-resolution clock for truly random seed
        std::random_device rd;
        seed_ = rd();
    }
    rng_.seed(seed_);
}

void RandomNumberGenerator::reset(std::uint32_t seed) {
    seed_ = seed;
    if (seed_ == 0) {
        std::random_device rd;
        seed_ = rd();
    }
    rng_.seed(seed_);
}

Real RandomNumberGenerator::uniform_01() {
    std::uniform_real_distribution<Real> dist(0.0, 1.0);
    return dist(rng_);
}

Real RandomNumberGenerator::uniform(Real min, Real max) {
    std::uniform_real_distribution<Real> dist(min, max);
    return dist(rng_);
}

Real RandomNumberGenerator::gaussian(Real mean, Real std_dev) {
    std::normal_distribution<Real> dist(mean, std_dev);
    return dist(rng_);
}

Real RandomNumberGenerator::lognormal(Real mean, Real std_dev) {
    // Log-normal distribution: if X ~ N(μ, σ²), then exp(X) ~ LogNormal
    // For desired mean m and std dev s:
    // σ² = ln(1 + (s/m)²)
    // μ = ln(m) - σ²/2
    Real cv = std_dev / mean;  // Coefficient of variation
    Real sigma_squared = std::log(1.0 + cv * cv);
    Real sigma = std::sqrt(sigma_squared);
    Real mu = std::log(mean) - sigma_squared / 2.0;

    std::lognormal_distribution<Real> dist(mu, sigma);
    return dist(rng_);
}

// ============================================================================
// DistributionSampler implementation
// ============================================================================

DistributionSampler::DistributionSampler(const MonteCarloVariation& config)
    : config_(config)
{
}

Real DistributionSampler::sample(RandomNumberGenerator& rng) {
    switch (config_.distribution) {
        case DistributionType::UNIFORM: {
            // Uniform distribution [min, max]
            return rng.uniform(config_.min_value, config_.max_value);
        }

        case DistributionType::GAUSSIAN: {
            // Gaussian distribution N(nominal, sigma)
            Real value = rng.gaussian(config_.nominal_value, config_.sigma);

            // Optional: enforce min/max limits if specified
            if (config_.min_value < config_.max_value) {
                value = std::max(config_.min_value, std::min(config_.max_value, value));
            }

            return value;
        }

        case DistributionType::LOGNORMAL: {
            // Log-normal distribution
            return rng.lognormal(config_.nominal_value, config_.sigma);
        }

        default:
            // Fallback: return nominal value
            return config_.nominal_value;
    }
}

// ============================================================================
// StatisticalAnalyzer implementation
// ============================================================================

Real StatisticalAnalyzer::mean(const std::vector<Real>& values) {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

Real StatisticalAnalyzer::variance(const std::vector<Real>& values, Real mean_val) {
    if (values.size() <= 1) return 0.0;

    Real sum_sq = 0.0;
    for (Real v : values) {
        Real diff = v - mean_val;
        sum_sq += diff * diff;
    }
    return sum_sq / (values.size() - 1);  // Sample variance (n-1)
}

Real StatisticalAnalyzer::std_dev(const std::vector<Real>& values, Real mean_val) {
    return std::sqrt(variance(values, mean_val));
}

MonteCarloStats StatisticalAnalyzer::compute_stats(const std::vector<Real>& values) {
    MonteCarloStats stats;

    if (values.empty()) {
        return stats;
    }

    // Mean
    stats.mean = mean(values);

    // Variance and standard deviation
    stats.variance = variance(values, stats.mean);
    stats.std_dev = std::sqrt(stats.variance);

    // Min and max
    auto minmax = std::minmax_element(values.begin(), values.end());
    stats.min = *minmax.first;
    stats.max = *minmax.second;

    // Percentiles (requires sorted data)
    std::vector<Real> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    stats.percentile_1 = compute_percentile(sorted, 1.0);
    stats.percentile_5 = compute_percentile(sorted, 5.0);
    stats.percentile_25 = compute_percentile(sorted, 25.0);
    stats.median = compute_percentile(sorted, 50.0);
    stats.percentile_75 = compute_percentile(sorted, 75.0);
    stats.percentile_95 = compute_percentile(sorted, 95.0);
    stats.percentile_99 = compute_percentile(sorted, 99.0);

    // Six Sigma level (simplified: distance from mean to spec in units of sigma)
    // Cpk = min((USL - μ)/(3σ), (μ - LSL)/(3σ))
    // For now, just report 3σ range
    if (stats.std_dev > 1e-12) {
        stats.sigma_level = 3.0;  // Placeholder: assume 3-sigma process
    }

    return stats;
}

HistogramData StatisticalAnalyzer::compute_histogram(const std::vector<Real>& values,
                                                      std::uint32_t num_bins) {
    HistogramData hist;
    hist.num_bins = num_bins;

    if (values.empty() || num_bins == 0) {
        return hist;
    }

    // Find min and max
    auto minmax = std::minmax_element(values.begin(), values.end());
    Real min_val = *minmax.first;
    Real max_val = *minmax.second;

    // Handle degenerate case (all values identical)
    if (max_val - min_val < 1e-12) {
        hist.bin_edges = {min_val, min_val + 1e-12};
        hist.counts = {static_cast<std::uint32_t>(values.size())};
        hist.bin_width = 1e-12;
        hist.num_bins = 1;
        return hist;
    }

    // Create bins with uniform width
    hist.bin_width = (max_val - min_val) / num_bins;
    hist.bin_edges.reserve(num_bins + 1);
    hist.counts.resize(num_bins, 0);

    for (std::uint32_t i = 0; i <= num_bins; ++i) {
        hist.bin_edges.push_back(min_val + i * hist.bin_width);
    }

    // Count values in each bin
    for (Real v : values) {
        // Find bin index
        int bin = static_cast<int>((v - min_val) / hist.bin_width);

        // Handle edge case: value equals max (place in last bin)
        if (bin >= static_cast<int>(num_bins)) {
            bin = num_bins - 1;
        }

        // Clamp to valid range
        if (bin >= 0 && bin < static_cast<int>(num_bins)) {
            hist.counts[bin]++;
        }
    }

    return hist;
}

Real StatisticalAnalyzer::compute_percentile(const std::vector<Real>& values, Real percentile) {
    if (values.empty()) return 0.0;
    if (percentile <= 0.0) return values.front();
    if (percentile >= 100.0) return values.back();

    // Assume values are already sorted
    // Use linear interpolation between nearest ranks
    Real rank = (percentile / 100.0) * (values.size() - 1);
    std::size_t lower = static_cast<std::size_t>(std::floor(rank));
    std::size_t upper = static_cast<std::size_t>(std::ceil(rank));

    if (lower == upper) {
        return values[lower];
    }

    // Linear interpolation
    Real frac = rank - lower;
    return values[lower] * (1.0 - frac) + values[upper] * frac;
}

Real StatisticalAnalyzer::compute_yield(const std::vector<Real>& values,
                                         Real spec_min,
                                         Real spec_max,
                                         std::uint32_t& num_passed,
                                         std::uint32_t& num_failed) {
    num_passed = 0;
    num_failed = 0;

    for (Real v : values) {
        if (v >= spec_min && v <= spec_max) {
            ++num_passed;
        } else {
            ++num_failed;
        }
    }

    if (values.empty()) return 0.0;
    return (100.0 * num_passed) / values.size();
}

} // namespace analysis
} // namespace kirimaspice
