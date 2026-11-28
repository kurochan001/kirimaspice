// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/noise_solver.hpp"

// Simple AC + noise regression on an RC lowpass.
// Compares AC magnitude to analytic |H(jw)| and noise to 4kTR_eq.

using namespace kirimaspice;

TEST(NoiseAcRegression, RcLowpass) {
    // Netlist: V1 -> R -> C -> GND, observe node out
    const char* netlist = R"(* RC lowpass for AC/noise regression
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 5 1k 10Meg
.noise V(out) V1 dec 5 1k 10Meg
.end
)";

    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    ASSERT_TRUE(parsed.circuit);

    // AC solve
    solvers::ACSolver ac_solver(*parsed.circuit, parsed.config);
    auto ac_result = ac_solver.solve();
    ASSERT_EQ(ac_result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(ac_result.frequency_points.size(), 0);

    // Analytic magnitude |H(jw)| = 1/sqrt(1+(wRC)^2)
    const Real R = 1000.0;
    const Real C = 1e-9;
    for (const auto& fp : ac_result.frequency_points) {
        Real omega = fp.omega;
        Real mag_expected = 1.0 / std::sqrt(1.0 + std::pow(omega * R * C, 2));
        // Take output node voltage magnitude at 'out' (node index 1 → dc_solution[0]=in)
        // AC solver stores complex solution; magnitude of Vout relative to 1V source
        NodeId out_id = parsed.circuit->get_node_id("out");
        ASSERT_GT(fp.solution.size(), static_cast<size_t>(out_id - 1))
            << "AC solution size too small for output node access";
        Real vout_mag = std::abs(fp.solution[out_id - 1]);
        EXPECT_NEAR(vout_mag, mag_expected, mag_expected * 1e-6);
    }

    // Noise solve
    solvers::NoiseSolver noise_solver(*parsed.circuit, parsed.config);
    NodeId input_node = parsed.circuit->get_node_id("in");
    NodeId output_node = parsed.circuit->get_node_id("out");
    auto noise_result = noise_solver.solve(input_node, output_node);
    ASSERT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    ASSERT_FALSE(noise_result.frequency_points.empty());

    // Expected thermal noise spectral density at output.
    // For RC lowpass: output_noise = sqrt(4kT*R) * |H(f)|
    // where |H(f)| = 1/sqrt(1 + (ωRC)²) is the transfer function magnitude.
    constexpr Real kB = 1.380649e-23;
    Real T = parsed.config.temperature;
    Real input_noise = std::sqrt(4.0 * kB * T * R);

    ASSERT_EQ(noise_result.output_noise.size(), noise_result.frequency_points.size());
    for (size_t i = 0; i < noise_result.output_noise.size(); ++i) {
        // frequency_points are stored in Hz, convert to angular frequency (rad/s)
        Real freq_hz = noise_result.frequency_points[i];
        Real omega = 2.0 * M_PI * freq_hz;
        Real transfer_mag = 1.0 / std::sqrt(1.0 + std::pow(omega * R * C, 2));
        Real expected_output_noise = input_noise * transfer_mag;
        // The RC low-pass is exact: relative 1e-6 is far above rounding and
        // far below any error in the PSD (2kTR in place of 4kTR is 0.707 times)
        EXPECT_NEAR(noise_result.output_noise[i], expected_output_noise,
                    expected_output_noise * 1e-6);
    }
}
