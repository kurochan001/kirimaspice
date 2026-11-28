// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/noise_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::solvers;

// Simple smoke test: RC low-pass, check noise solver runs and outputs finite values.
TEST(NoiseSolver, RCLowpassRuns) {
    std::string netlist = R"(* RC low-pass for noise smoke test
Vin in 0 DC 0
R1 in out 1k
C1 out 0 1u
.ac dec 1 1k 1Meg
.end
)";
    NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    auto& circuit = parsed.circuit;

    NoiseSolver solver(*circuit, parsed.config);
    auto result = solver.solve(circuit->get_node_id("in"), circuit->get_node_id("out"));

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_FALSE(result.output_noise.empty());
    for (double n : result.output_noise) {
        EXPECT_TRUE(std::isfinite(n));
    }

    // Theoretical output noise spectrum for this RC:
    // Resistor thermal noise PSD: 4 k T R  [V^2/Hz] (series model)
    // Output transfer: |H(jw)|^2 = 1 / (1 + (w R C)^2)
    // Expected output noise (V/√Hz) = sqrt(4 k T R) * |H(jw)|
    constexpr double kB = 1.380649e-23;
    constexpr double T = 300.15;   // Same as resistor noise model
    constexpr double R = 1e3;      // 1 kΩ
    constexpr double C = 1e-6;     // 1 µF

    const double base_vn = std::sqrt(4.0 * kB * T * R);  // ~12.9 nV/√Hz
    std::vector<double> freqs = {1e3, 1e4, 1e5, 1e6};
    ASSERT_EQ(result.output_noise.size(), freqs.size());

    for (size_t i = 0; i < freqs.size(); ++i) {
        double wrc = 2.0 * M_PI * freqs[i] * R * C;
        double mag = 1.0 / std::sqrt(1.0 + wrc * wrc);
        double expected = base_vn * mag;
        double measured = result.output_noise[i];
        // Allow 10% relative error; model and solver both should match within this margin.
        double rel_err = std::abs(measured - expected) / expected;
        EXPECT_LT(rel_err, 0.10) << "freq=" << freqs[i] << "Hz expected=" << expected
                                 << " measured=" << measured;
    }
}
