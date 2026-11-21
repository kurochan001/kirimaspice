// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The frequencies of DEC and OCT sweeps, as SPICE's AC analysis takes them:
// up by a fixed ratio from the start while within the stop plus
// ratio * stop * RELTOL; OCT by 2^(1/N); DEC by 10^(1/N) below a decade, and
// over a decade or more by the ratio that reaches the stop in
// floor(N log10(stop/start)) steps. The expected grids are ngspice-47's
// (`print frequency`, run 2026-11-21).

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "kirimaspice/solvers/ac_solver.hpp"

using namespace kirimaspice;
using SweepType = SimulationConfig::AC::SweepType;

namespace {

std::vector<Real> grid(SweepType type, std::uint32_t n, Real start, Real stop, Real reltol = 1e-3) {
    SimulationConfig::AC ac;
    ac.sweep_type = type;
    ac.num_points = n;
    ac.start_freq = start;
    ac.stop_freq = stop;
    return solvers::ac_frequency_points(ac, reltol);
}

void expect_grid(const std::vector<Real>& got, const std::vector<Real>& expected) {
    ASSERT_EQ(got.size(), expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_NEAR(got[i], expected[i], 5e-7 * expected[i]) << i;  // ngspice prints 7 digits
    }
}

}  // namespace

TEST(ACFrequencyGrid, DecadeBelowADecadeIsNotStretched) {
    expect_grid(grid(SweepType::DEC, 10, 10, 25), {10, 12.58925, 15.84893, 19.95262});
    expect_grid(grid(SweepType::DEC, 10, 1, 9.5),
                {1, 1.258925, 1.584893, 1.995262, 2.511886, 3.162278, 3.981072, 5.011872, 6.309573, 7.943282});
}

TEST(ACFrequencyGrid, DecadeOverADecadeEndsOnTheStop) {
    expect_grid(grid(SweepType::DEC, 7, 1, 20), {1, 1.394951, 1.945888, 2.714418, 3.786479, 5.281952,
                                                 7.368063, 10.27809, 14.33742, 20});
    expect_grid(grid(SweepType::DEC, 2, 1, 50), {1, 3.684031, 13.57209, 50});
    expect_grid(grid(SweepType::DEC, 3, 1, 1000),
                {1, 2.154435, 4.641589, 10, 21.54435, 46.41589, 100, 215.4435, 464.1589, 1000});
    const auto points = grid(SweepType::DEC, 10, 1, 99.9);
    EXPECT_EQ(points.size(), 20u);
    EXPECT_EQ(points.back(), 99.9);
}

TEST(ACFrequencyGrid, OctaveIsNeverStretched) {
    expect_grid(grid(SweepType::OCT, 1, 1, 3), {1, 2});
    expect_grid(grid(SweepType::OCT, 1, 1, 20), {1, 2, 4, 8, 16});
    expect_grid(grid(SweepType::OCT, 2, 1, 8), {1, 1.414214, 2, 2.828427, 4, 5.656854, 8});
}

TEST(ACFrequencyGrid, LastPointWithinTheToleranceOfTheStop) {
    // 10 Hz is within 10 * 9.97 * RELTOL of 9.97 Hz, 3.162 Hz not within
    // 3.162 * 3.15 * RELTOL of 3.15 Hz, 2 Hz within 2 * 1.999 * RELTOL of
    // 1.999 Hz and not of 1.996 Hz
    expect_grid(grid(SweepType::DEC, 1, 1, 9.97), {1, 10});
    expect_grid(grid(SweepType::DEC, 2, 1, 3.15), {1});
    expect_grid(grid(SweepType::OCT, 1, 1, 1.999), {1, 2});
    expect_grid(grid(SweepType::OCT, 1, 1, 1.996), {1});
    // With .options reltol=1e-4 ngspice-47 stops at 1 Hz
    expect_grid(grid(SweepType::DEC, 1, 1, 9.97, 1e-4), {1});
}
