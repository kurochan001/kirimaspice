// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Noise sources as SPICE gives them: a resistor's 4kT/R at its own
// temperature; the channel thermal noise 4kT (2/3) gm of the level-1 MOSFET
// at its temperature, and of the level-3 MOSFET in either direction of the
// channel. Before, the resistor and level 1 took 300.15 K at any
// temperature, and level 3 took 8/3 and gave none in reverse.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

constexpr double kBoltzmann = 1.380649e-23;

struct Solved {
    parser::NetlistParseResult parsed;
    DenseVector x;
};

Solved solve(const std::string& netlist) {
    parser::NetlistParser parser;
    Solved s{parser.parse_string(netlist), {}};
    solvers::DCSolver solver(*s.parsed.circuit, s.parsed.config);
    SolverStats stats;
    EXPECT_EQ(solver.solve(s.x, stats), ConvergenceStatus::SUCCESS) << netlist;
    return s;
}

double unknown(const Solved& s, const std::string& name) {
    for (std::uint32_t i = 0; i < s.x.size(); ++i) {
        if (s.parsed.circuit->unknown_name(i) == name) {
            return s.x[i];
        }
    }
    ADD_FAILURE() << "no unknown " << name;
    return 0.0;
}

double channel_noise(const Solved& s) {
    const auto sources = s.parsed.circuit->get_device("M1")->get_noise_sources(s.x, 1e3);
    for (const auto& source : sources) {
        if (source.name.find("thermal") != std::string::npos) {
            return source.spectral_density;
        }
    }
    ADD_FAILURE() << "no thermal noise source";
    return 0.0;
}

std::string mosfet(double vd, double vg, double vs, const std::string& card) {
    return "t\nVD d 0 " + std::to_string(vd) + "\nVG g 0 " + std::to_string(vg) + "\nVS s 0 " +
           std::to_string(vs) + "\nM1 d g s 0 mm W=10u L=2u\n" + card + "\n.end\n";
}

}  // namespace

TEST(NoiseCoefficients, ResistorAtItsTemperature) {
    devices::Resistor r(1, "R1", 1, 0, 1e3);
    r.set_temperature(400.15);
    const auto sources = r.get_noise_sources(DenseVector(2, 0.0), 1e3);
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_NEAR(sources[0].spectral_density, 4.0 * kBoltzmann * 400.15 / 1e3, 1e-30);
}

TEST(NoiseCoefficients, Level3ChannelNoiseIsTwoThirdsOfGm) {
    const std::string card = ".model mm NMOS LEVEL=3 VTO=0.7 KP=5e-5";
    const double dv = 1e-4;
    const double gm = (-unknown(solve(mosfet(2.0, 1.5 + dv, 0.0, card)), "I(VD)") +
                       unknown(solve(mosfet(2.0, 1.5 - dv, 0.0, card)), "I(VD)")) /
                      (2.0 * dv);
    ASSERT_GT(gm, 1e-6);
    const double forward = channel_noise(solve(mosfet(2.0, 1.5, 0.0, card)));
    EXPECT_NEAR(forward, 4.0 * kBoltzmann * 300.15 * (2.0 / 3.0) * gm, 1e-4 * forward);
    // The same bias with drain and source exchanged: the same noise
    const double reverse = channel_noise(solve(mosfet(0.0, 1.5, 2.0, card)));
    EXPECT_NEAR(reverse, forward, 1e-6 * forward);
}

TEST(NoiseCoefficients, Level1ChannelNoiseAtItsTemperature) {
    const std::string card = ".model mm NMOS VTO=0.7 KP=5e-5\n.options temp=127";
    const double dv = 1e-4;
    const double gm = (-unknown(solve(mosfet(2.0, 1.5 + dv, 0.0, card)), "I(VD)") +
                       unknown(solve(mosfet(2.0, 1.5 - dv, 0.0, card)), "I(VD)")) /
                      (2.0 * dv);
    ASSERT_GT(gm, 1e-6);
    const double noise = channel_noise(solve(mosfet(2.0, 1.5, 0.0, card)));
    EXPECT_NEAR(noise, 4.0 * kBoltzmann * 400.15 * (2.0 / 3.0) * gm, 1e-4 * noise);
}
