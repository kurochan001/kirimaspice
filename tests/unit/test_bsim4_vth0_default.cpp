// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A BSIM4 card without VTH0 takes TYPE (VFB + phi + K1 sqrt(phi)), with VFB
// = -1 V unless the card gives it (BSIM4 manual, appendix A, the notes to
// VTH0 and VFB). The expected drain currents are ngspice-47's for these
// cards at W = L = 5 um and VD = VG = 2 V (-2 V for PMOS), run 2026-11-21;
// before, the card without parameters gave 2.626e-4 A, the current of
// VTH0 = 0.7 V.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double drain_current(const std::string& card, double v) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nVD d 0 " + std::to_string(v) + "\nVG g 0 " + std::to_string(v) +
                                      "\nM1 d g 0 0 NX W=5u L=5u\n.model NX " + card + "\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS) << card;
    for (std::uint32_t i = 0; i < x.size(); ++i) {
        if (parsed.circuit->unknown_name(i) == "I(VD)") {
            return x[i];
        }
    }
    ADD_FAILURE() << "no I(VD)";
    return 0.0;
}

}  // namespace

TEST(BSIM4Vth0Default, FollowsTheFlatBandVoltage) {
    // 1e-4 relative: the k/q of the BSIM4 code and the one here differ by
    // 2.8e-5 (KNOWN_ISSUES)
    const struct { const char* card; double v, ngspice; } cases[] = {
        {"NMOS LEVEL=14", 2.0, -5.65003e-04},
        {"PMOS LEVEL=14", -2.0, 2.201266e-04},
        {"NMOS LEVEL=14 VFB=-0.9", 2.0, -4.97525e-04},
        {"NMOS LEVEL=14 NDEP=3e17", 2.0, -2.47523e-04},
        {"NMOS LEVEL=54 K1=0.6", 2.0, -3.87042e-04},
        {"NMOS LEVEL=14 VTH0=0.5 VFB=-0.9", 2.0, -3.62466e-04},
    };
    for (const auto& c : cases) {
        EXPECT_NEAR(drain_current(c.card, c.v), c.ngspice, 1e-4 * std::abs(c.ngspice)) << c.card;
    }
}
