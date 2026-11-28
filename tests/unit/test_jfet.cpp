// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_jfet.cpp
#include <gtest/gtest.h>
#include "kirimaspice/devices/jfet.hpp"  // Unified JFET template
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <map>
#include <string>

using namespace kirimaspice;
using namespace kirimaspice::devices;

//==============================================================================
// JFET_N - Basic Construction Tests
//==============================================================================
TEST(JFET_N, Construction) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12);

    EXPECT_EQ(jfet.id(), 1);
    EXPECT_EQ(jfet.name(), "J1");
    EXPECT_EQ(jfet.type(), DeviceType::JFET_N);
    EXPECT_EQ(jfet.drain(), 1);
    EXPECT_EQ(jfet.gate(), 2);
    EXPECT_EQ(jfet.source(), 0);
    EXPECT_DOUBLE_EQ(jfet.transconductance_parameter(), 1e-4);
    EXPECT_DOUBLE_EQ(jfet.pinchoff_voltage(), -2.0);
    EXPECT_DOUBLE_EQ(jfet.channel_modulation(), 0.01);
    EXPECT_DOUBLE_EQ(jfet.gate_saturation_current(), 1e-14);
    EXPECT_FALSE(jfet.is_linear());
    EXPECT_TRUE(jfet.is_dynamic());  // Has capacitances
}

TEST(JFET_N, ConstructionWithInvalidParameters) {
    // Negative BETA should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, -1e-4, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Zero BETA should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 0.0, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative LAMBDA should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, -0.01, 1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative IS should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, -1e-14, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Zero IS should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 0.0, 1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative n should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, -1.0, 1e-12, 1e-12),
                 std::runtime_error);

    // Negative capacitance should throw
    EXPECT_THROW(JFET_N(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0, -1e-12, 1e-12),
                 std::runtime_error);
}

//==============================================================================
// JFET_N - I-V Characteristic Tests
//==============================================================================
TEST(JFET_N, DrainCurrentCutoffRegion) {
    // Standard N-channel JFET: BETA=1e-4, Vto=-2.0V, LAMBDA=0.01
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Cutoff region: Vgs < Vto (-2.0V)
    Real vgs = -2.5;  // < -2.0V
    Real vds = 1.0;

    Real id = jfet.get_drain_current(vgs, vds);

    // Id should be zero in cutoff
    EXPECT_NEAR(id, 0.0, 1e-20);
}

TEST(JFET_N, DrainCurrentLinearRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Linear region: Vgs > Vto, Vds < Vgs - Vto
    Real vgs = -1.0;  // > Vto (-2.0V)
    Real vds = 0.5;   // < Vgs - Vto = 1.0V

    // Id = BETA × (2(Vgs - Vto) × Vds - Vds²) × (1 + LAMBDA × Vds)
    // Id = 1e-4 × (2(-1.0 - (-2.0)) × 0.5 - 0.5²) × (1 + 0.01 × 0.5)
    // Id = 1e-4 × (2 × 1.0 × 0.5 - 0.25) × 1.005
    // Id = 1e-4 × (1.0 - 0.25) × 1.005
    // Id = 1e-4 × 0.75 × 1.005 = 7.5375e-5 A

    Real id = jfet.get_drain_current(vgs, vds);

    Real vgs_eff = vgs - (-2.0);  // 1.0V
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_id = beta * (2.0 * vgs_eff * vds - vds * vds) * (1.0 + lambda * vds);

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

TEST(JFET_N, DrainCurrentSaturationRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Saturation region: Vgs > Vto, Vds >= Vgs - Vto
    Real vgs = -1.0;  // > Vto (-2.0V)
    Real vds = 2.0;   // >= Vgs - Vto = 1.0V

    // Id = BETA × (Vgs - Vto)² × (1 + LAMBDA × Vds)
    // Id = 1e-4 × 1.0² × (1 + 0.01 × 2.0)
    // Id = 1e-4 × 1.0 × 1.02 = 1.02e-4 A

    Real id = jfet.get_drain_current(vgs, vds);

    Real vgs_eff = vgs - (-2.0);  // 1.0V
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_id = beta * vgs_eff * vgs_eff * (1.0 + lambda * vds);

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

TEST(JFET_N, DrainCurrentZeroBias) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Vgs = 0.0V (> Vto = -2.0V, so device is on)
    // Vds = 0.0V (linear region boundary)
    Real vgs = 0.0;
    Real vds = 0.0;

    Real id = jfet.get_drain_current(vgs, vds);

    // At Vds=0, Id should be zero (boundary condition)
    EXPECT_NEAR(id, 0.0, 1e-20);
}

//==============================================================================
// JFET_N - Conductance Tests
//==============================================================================
TEST(JFET_N, TransconductanceLinearRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Linear region
    Real vgs = -1.0;
    Real vds = 0.5;

    // gm = 2 × BETA × Vds × (1 + LAMBDA × Vds)
    Real gm = jfet.get_transconductance(vgs, vds);

    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_gm = 2.0 * beta * vds * (1.0 + lambda * vds);

    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);  // Within 1%
}

TEST(JFET_N, TransconductanceSaturationRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // Saturation region
    Real vgs = -1.0;
    Real vds = 2.0;

    // gm = 2 × BETA × (Vgs - Vto) × (1 + LAMBDA × Vds)
    Real gm = jfet.get_transconductance(vgs, vds);

    Real vgs_eff = vgs - (-2.0);  // 1.0V
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real expected_gm = 2.0 * beta * vgs_eff * (1.0 + lambda * vds);

    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);  // Within 1%
}

TEST(JFET_N, OutputConductanceLinearRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;
    Real vds = 0.5;

    Real gds = jfet.get_output_conductance(vgs, vds);

    // gds should be positive in linear region
    EXPECT_GT(gds, 0.0);
}

TEST(JFET_N, OutputConductanceSaturationRegion) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;
    Real vds = 2.0;

    Real gds = jfet.get_output_conductance(vgs, vds);

    // gds = Id_basic × LAMBDA in saturation region
    Real vgs_eff = vgs - (-2.0);
    Real beta = 1e-4;
    Real lambda = 0.01;
    Real id_basic = beta * vgs_eff * vgs_eff;
    Real expected_gds = id_basic * lambda;

    EXPECT_NEAR(gds, expected_gds, expected_gds * 0.01);
}

//==============================================================================
// JFET_N - Gate Current Tests
//==============================================================================
TEST(JFET_N, GateCurrentReverseBias) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0);

    // Normal operation: Vgs ≤ 0 (reverse biased)
    Real vgs = -0.5;

    Real ig = jfet.get_gate_current(vgs);

    // Gate current should be very small (reverse saturation)
    // Ig ≈ -IS for negative Vgs
    EXPECT_LT(std::abs(ig), 1e-13);  // Less than 100fA
}

TEST(JFET_N, GateCurrentForwardBias) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0);

    // Forward bias (not normal operation): Vgs > 0
    Real vgs = 0.7;

    Real ig = jfet.get_gate_current(vgs);

    // Gate current should be significant in forward bias
    EXPECT_GT(ig, 1e-12);  // Greater than 1pA
}

TEST(JFET_N, GateConductance) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0);

    Real vgs = 0.0;

    Real gg = jfet.get_gate_conductance(vgs);

    // Gate conductance should be positive
    EXPECT_GT(gg, 0.0);
}

//==============================================================================
// JFET_N - Region Detection Tests
//==============================================================================
TEST(JFET_N, RegionDetectionCutoff) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -2.5;  // < Vto
    Real vds = 1.0;

    auto region = jfet.get_region(vgs, vds);

    EXPECT_EQ(region, JFET_N::Region::CUTOFF);
}

TEST(JFET_N, RegionDetectionLinear) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;  // > Vto
    Real vds = 0.5;   // < Vgs - Vto

    auto region = jfet.get_region(vgs, vds);

    EXPECT_EQ(region, JFET_N::Region::LINEAR);
}

TEST(JFET_N, RegionDetectionSaturation) {
    JFET_N jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vgs = -1.0;  // > Vto
    Real vds = 2.0;   // >= Vgs - Vto

    auto region = jfet.get_region(vgs, vds);

    EXPECT_EQ(region, JFET_N::Region::SATURATION);
}

//==============================================================================
// JFET_P - Basic Construction Tests
//==============================================================================
TEST(JFET_P, Construction) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01, 1e-14, 1.0, 1e-12, 1e-12);

    EXPECT_EQ(jfet.id(), 1);
    EXPECT_EQ(jfet.name(), "J1");
    EXPECT_EQ(jfet.type(), DeviceType::JFET_P);
    EXPECT_EQ(jfet.drain(), 1);
    EXPECT_EQ(jfet.gate(), 2);
    EXPECT_EQ(jfet.source(), 0);
    EXPECT_DOUBLE_EQ(jfet.transconductance_parameter(), 1e-4);
    EXPECT_DOUBLE_EQ(jfet.pinchoff_voltage(), -2.0);
    EXPECT_FALSE(jfet.is_linear());
    EXPECT_TRUE(jfet.is_dynamic());
}

//==============================================================================
// JFET_P - I-V Characteristic Tests
//==============================================================================
// The P-channel device works on vgs and vds of its polarity, as SPICE's
// JFET does: v = -(Vg - Vs) = Vs - Vg and -(Vd - Vs) = Vs - Vd, with the same
// VTO = -2 V as the N-channel device, so it conducts at Vg = Vs and is cut
// off when the gate is 2 V or more above the source. ngspice-45.2 gives
// I(Vd) = 1.05 mA for a PJF of BETA = 1m, VTO = -2, LAMBDA = 0.01 with the
// gate 1 V above the source and the drain 5 V below it, BETA (1 V)^2 (1 +
// 0.01 * 5) (run 2026-11-28).
TEST(JFET_P, DrainCurrentCutoffRegion) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // The gate 2.5 V above the source: Vs - Vg = -2.5 V <= VTO
    EXPECT_NEAR(jfet.get_drain_current(-2.5, 1.0), 0.0, 1e-20);
}

TEST(JFET_P, DrainCurrentSaturationRegion) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    // The gate 1 V above the source, Vs - Vg = -1 V: vgst = -1 - (-2) = 1 V,
    // and Vs - Vd = 2 V >= vgst
    const Real id = jfet.get_drain_current(-1.0, 2.0);
    const Real expected = 1e-4 * 1.0 * 1.0 * (1.0 + 0.01 * 2.0);  // 1.02e-4 A
    EXPECT_NEAR(id, expected, expected * 1e-12);
}

//==============================================================================
// JFET_P - Region Detection Tests
//==============================================================================
TEST(JFET_P, RegionDetectionSaturation) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vsg = -1.0;  // vgst = vsg - VTO = 1 V
    Real vsd = 2.0;   // >= vgst

    auto region = jfet.get_region(vsg, vsd);

    EXPECT_EQ(region, JFET_P::Region::SATURATION);
}

TEST(JFET_P, RegionDetectionCutoff) {
    JFET_P jfet(1, "J1", 1, 2, 0, 1e-4, -2.0, 0.01);

    Real vsg = -2.5;  // <= VTO: the gate 2.5 V above the source
    Real vsd = 1.0;

    auto region = jfet.get_region(vsg, vsd);

    EXPECT_EQ(region, JFET_P::Region::CUTOFF);
}

//==============================================================================
// J elements of a netlist (parse_jfet_device), against ngspice-45.2
//==============================================================================
namespace {

const std::string kCards =
    ".model jn NJF(BETA=1m VTO=-2 LAMBDA=0.01)\n.model jp PJF(BETA=1m VTO=-2 LAMBDA=0.01)\n";

struct OperatingPoint {
    parser::NetlistParseResult parsed;
    DenseVector x;
    Real voltage(const std::string& node) const { return x[parsed.circuit->get_node_id(node) - 1]; }
    Real current(const std::string& source) const {
        for (std::uint32_t i = 0; i < x.size(); ++i) {
            if (parsed.circuit->unknown_name(i) == "I(" + source + ")") return x[i];
        }
        throw std::runtime_error("no I(" + source + ")");
    }
};

OperatingPoint operating_point(const std::string& lines) {
    parser::NetlistParser parser;
    OperatingPoint op{parser.parse_string("t\n" + lines + kCards + ".end\n"), {}};
    op.parsed.circuit->expand_all_subcircuits();
    solvers::DCSolver solver(*op.parsed.circuit, op.parsed.config);
    SolverStats stats;
    EXPECT_EQ(solver.solve(op.x, stats), ConvergenceStatus::SUCCESS) << lines;
    return op;
}

}  // namespace

// The currents ngspice-45.2 gives (run 2026-11-28) with BETA = 1m,
// VTO = -2 V and LAMBDA = 0.01: saturation and the linear region, a
// P-channel device on and at Vg = Vs, a gate junction forward-biased, the
// inverse mode (Vds < 0), an area factor of 3, and the gate current, which
// is GMIN = 1e-12 S across each reverse-biased junction with -IS
TEST(JfetElement, MatchesNgspice) {
    const struct {
        std::string what, lines;
        Real id, ig;  // I(Vd), I(Vg)
    } cases[] = {
        {"saturation", "Vd d 0 5\nVg g 0 -1\nJ1 d g 0 jn\n", -1.05e-3, 7.02e-12},
        {"linear", "Vd d 0 0.5\nVg g 0 -1\nJ1 d g 0 jn\n", -7.5375e-4, 2.52e-12},
        {"p-channel", "Vd d 0 -5\nVg g 0 1\nJ1 d g 0 jp\n", 1.05e-3, -7.02e-12},
        {"p-channel at Vg = Vs", "Vd d 0 -5\nVg g 0 0\nJ1 d g 0 jp\n", 4.2e-3, -5.01e-12},
        {"gate forward", "Vd d 0 1\nVg g 0 0.6\nJ1 d g 0 jn\n", -4.242e-3, -1.1872e-4},
        {"inverse mode", "Vd d 0 -0.5\nVg g 0 -1\nJ1 d g 0 jn\n", 1.25625e-3, 1.52e-12},
        {"area 3", "Vd d 0 5\nVg g 0 -1\nJ1 d g 0 jn 3\n", -3.15e-3, 7.06e-12},
    };
    for (const auto& c : cases) {
        const auto op = operating_point(c.lines);
        EXPECT_NEAR(op.current("Vd"), c.id, 1e-6 * std::abs(c.id)) << c.what;
        EXPECT_NEAR(op.current("Vg"), c.ig, 1e-4 * std::abs(c.ig) + 1e-17) << c.what;
    }
    // Self-biased amplifiers: V(o) = 11.80135 V, and -11.80135 V for PJF
    EXPECT_NEAR(operating_point("Vdd dd 0 15\nRd dd o 2k\nVg g 0 0\nRs s 0 500\nJ1 o g s jn\n").voltage("o"),
                11.80135, 1e-5);
    EXPECT_NEAR(operating_point("Vdd dd 0 -15\nRd dd o 2k\nVg g 0 0\nRs s 0 500\nJ1 o g s jp\n").voltage("o"),
                -11.80135, 1e-5);
    // In a subcircuit body: the nodes of J are renamed into the instance
    EXPECT_NEAR(operating_point(".subckt cs o g\nRs s 0 500\nJ1 o g s jn\n.ends\n"
                                "Vdd dd 0 15\nRd dd o 2k\nVg g 0 0\nX1 o g cs\n").voltage("o"),
                11.80135, 1e-5);
}

// IS follows the temperature as IS exp((T/TNOM - 1) EG/(N Vt)), with EG =
// 1.11: at 127 C the gate junctions at 0.5 V carry 1.80349 mA in
// ngspice-45.2, and I(Vd) = 0.9017438 mA (its constants differ in the
// fifth digit)
TEST(JfetElement, GateCurrentAtTemperature) {
    const auto op = operating_point(".options temp=127\nVd d 0 0\nVg g 0 0.5\nJ1 d g 0 jn\n");
    EXPECT_NEAR(op.current("Vg"), -1.80349e-3, 1e-4 * 1.80349e-3);
    EXPECT_NEAR(op.current("Vd"), 0.9017438e-3, 1e-4 * 0.9017438e-3);
}

// In AC, gm and gds at the operating point and the depletion capacitances
// C0/sqrt(1 - v/PB): with CGS = CGD = 1p at Vgs = -1 and Vgd = -6 V,
// ngspice-45.2 gives I(Vg) = -2e-12 - j 6.81770e-6 A and I(Vd) = -2.1e-3 +
// j 2.374821e-6 A at 1 MHz; forward-biased beyond FC PB, at Vgs = 0.6 V,
// the capacitance continues linearly: Im I(Vg) = -9.77434e-6 A
TEST(JfetElement, AcCapacitancesAndGain) {
    auto ac_currents = [](const std::string& lines) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\n" + lines + ".ac lin 1 1Meg 1Meg\n.end\n");
        solvers::ACSolver solver(*parsed.circuit, parsed.config);
        const auto ac = solver.solve();
        EXPECT_EQ(ac.status, ConvergenceStatus::SUCCESS);
        std::map<std::string, Complex> currents;
        const auto& x = ac.frequency_points.at(0).solution;
        for (std::uint32_t i = 0; i < x.size(); ++i) currents[parsed.circuit->unknown_name(i)] = x[i];
        return currents;
    };
    auto rev = ac_currents("Vd d 0 5\nVg g 0 DC -1 AC 1\nJ1 d g 0 jn\n"
                           ".model jn NJF(BETA=1m VTO=-2 LAMBDA=0.01 CGS=1p CGD=1p)\n");
    EXPECT_NEAR(std::abs(rev.at("I(Vg)") - Complex(-2e-12, -6.81770e-6)), 0.0, 1e-11);
    EXPECT_NEAR(std::abs(rev.at("I(Vd)") - Complex(-2.1e-3, 2.374821e-6)), 0.0, 1e-9);
    auto fwd = ac_currents("Vd d 0 0\nVg g 0 DC 0.6 AC 1\nJ1 d g 0 jn\n.model jn NJF(BETA=1m VTO=-2 CGS=1p)\n");
    EXPECT_NEAR(fwd.at("I(Vg)").imag(), -9.77434e-6, 1e-11);
}

// In the transient the capacitances are integrated (CapacitorHistory): a
// gate driven through 10 kOhm from -3 V to 0 V with CGS = CGD = 1p, the drain
// at 5 V through 1 kOhm; ngspice-45.2 with RELTOL 1e-6 gives these voltages
// (run 2026-11-28), here within 2 mV
TEST(JfetElement, TransientMatchesNgspice) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nVin in 0 PULSE(-3 0 1n 1n 1n 50n 100n)\nRg in g 10k\nVdd dd 0 5\nRd dd d 1k\nJ1 d g 0 jn\n"
        ".model jn NJF(BETA=1m VTO=-2 LAMBDA=0.01 CGS=1p CGD=1p)\n.end\n");
    SimulationConfig config = parsed.config;
    config.transient.use_adaptive_timestep = false;
    solvers::TransientSolver solver(*parsed.circuit, config);
    const auto result = solver.solve(0.0, 60e-9, 0.05e-9);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const auto g = parsed.circuit->get_node_id("g") - 1, d = parsed.circuit->get_node_id("d") - 1;
    const struct {
        Real t, vg, vd;
    } reference[] = {{5e-9, -1.99925, 5.07773}, {10e-9, -1.34069, 4.62327}, {20e-9, -0.79731, 3.55555},
                     {40e-9, -0.37899, 2.33832}, {55e-9, -0.50238, 2.52202}};
    for (const auto& r : reference) {
        const auto it = std::min_element(result.timepoints.begin(), result.timepoints.end(),
                                         [&](const auto& a, const auto& b) {
                                             return std::abs(a.time - r.t) < std::abs(b.time - r.t);
                                         });
        ASSERT_NEAR(it->time, r.t, 1e-12);
        EXPECT_NEAR(it->node_voltages[g], r.vg, 2e-3) << r.t;
        EXPECT_NEAR(it->node_voltages[d], r.vd, 2e-3) << r.t;
    }
}

// A J line needs an NJF or PJF card, defined anywhere in the netlist
TEST(JfetElement, ModelIsNeeded) {
    parser::NetlistParser parser;
    EXPECT_NO_THROW(parser.parse_string("t\nVd d 0 5\nJ1 d 0 0 jn\n.model jn NJF(BETA=1m)\n.end\n"));
    EXPECT_THROW(parser.parse_string("t\nVd d 0 5\nJ1 d 0 0 jx\n.end\n"), parser::ParseError);
    EXPECT_THROW(parser.parse_string("t\nVd d 0 5\nJ1 d 0 0 dm\n.model dm D\n.end\n"), parser::ParseError);
}
