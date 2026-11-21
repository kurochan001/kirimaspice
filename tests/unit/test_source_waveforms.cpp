// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Independent sources with a DC value, an AC input and a transient waveform,
// as in SPICE: the operating point and the DC sweep take the DC value (or the
// waveform at time 0 when the line has none), the transient takes the
// waveform from its operating point at time 0 on, and AC takes the AC input.
// ngspice-47 behaves so (V1 in 0 DC 5 AC 1 PULSE(2 3 ...): .op 5 V, the
// transient starts at 2 V, .dc sweeps V1, AC |V(out)| = 0.7071 at the RC
// corner), and its PULSE, SIN, EXP, PWL and SFFM values agree with the
// formulas below to 5e-9 V (.tran 10u 1m, checked by running it).

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::solvers;

namespace {

parser::NetlistParseResult parse(const std::string& netlist) {
    parser::NetlistParser parser;
    return parser.parse_string(netlist);
}

Real operating_point(const parser::NetlistParseResult& parsed, const std::string& node) {
    DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    return solution.empty() ? std::nan("") : solution[parsed.circuit->get_node_id(node) - 1];
}

TransientResult transient(const parser::NetlistParseResult& parsed, Real tstop, Real tstep) {
    SimulationConfig config = parsed.config;
    config.transient.use_adaptive_timestep = false;
    TransientSolver solver(*parsed.circuit, config);
    return solver.solve(0.0, tstop, tstep);
}

constexpr Real kPi = 3.14159265358979323846;

}  // namespace

TEST(SourceWaveforms, DcValueAcInputAndPulse) {
    const auto parsed = parse(
        "dc ac pulse\nV1 in 0 DC 5 AC 1 PULSE(2 3 1m 1u 1u 1m 2m)\nR1 in out 1k\nC1 out 0 1u\n"
        ".dc V1 0 2 1\n.ac lin 1 159.154943 159.154943\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const NodeId in = parsed.circuit->get_node_id("in");

    // The operating point takes the DC value
    EXPECT_NEAR(operating_point(parsed, "in"), 5.0, 1e-12);

    // The transient starts from the waveform at time 0, and then follows it
    const TransientResult tran = transient(parsed, 2e-3, 1e-4);
    ASSERT_EQ(tran.status, ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(tran.timepoints.front().node_voltages[in - 1], 2.0, 1e-12);
    for (const auto& tp : tran.timepoints) {
        const Real expected = (tp.time > 1e-3 + 1e-6 + 1e-9) ? 3.0 : 2.0;
        if (tp.time < 1e-3 - 1e-9 || tp.time > 1e-3 + 2e-6) {
            EXPECT_NEAR(tp.node_voltages[in - 1], expected, 1e-12) << "t=" << tp.time;
        }
    }
    // After the transient the source has its DC value again
    EXPECT_NEAR(operating_point(parsed, "in"), 5.0, 1e-12);

    // .DC sweeps the DC value
    DCSolver dc(*parsed.circuit, parsed.config);
    DCSweepResult sweep;
    ASSERT_EQ(dc.solve_sweep(parsed.config.dc_sweep, sweep), ConvergenceStatus::SUCCESS);
    ASSERT_EQ(sweep.points.size(), 3u);
    for (const auto& point : sweep.points) {
        EXPECT_NEAR(point.node_voltages[in - 1], point.sweep_value, 1e-12);
    }

    // AC takes the AC input: at the corner of the RC, |V(out)| = 1/sqrt(2)
    ACSolver ac(*parsed.circuit, parsed.config);
    const ACResult result = ac.solve();
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.frequency_points.size(), 1u);
    EXPECT_NEAR(std::abs(result.frequency_points[0].solution[parsed.circuit->get_node_id("out") - 1]),
                1.0 / std::sqrt(2.0), 1e-6);
}

TEST(SourceWaveforms, WithoutDcValueTheOperatingPointTakesTimeZero) {
    // PULSE at 0 is V1; SIN before TD is VO + VA sin(PHASE) = 1 + 2 sin 30 deg
    const auto parsed = parse(
        "no dc\nV1 a 0 PULSE(2 3 1m 1u 1u 1m 2m)\nV2 b 0 SIN(1 2 1k 0.5m 100 30)\n"
        "V3 c 0 EXP(1 3 0.2m 0.1m 0.6m 0.2m)\nV4 e 0 PWL(0 1 0.5m 2 1m 0)\n"
        "R1 a 0 1k\nR2 b 0 1k\nR3 c 0 1k\nR4 e 0 1k\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    EXPECT_NEAR(operating_point(parsed, "a"), 2.0, 1e-12);
    EXPECT_NEAR(operating_point(parsed, "b"), 2.0, 1e-12);
    EXPECT_NEAR(operating_point(parsed, "c"), 1.0, 1e-12);
    EXPECT_NEAR(operating_point(parsed, "e"), 1.0, 1e-12);
}

TEST(SourceWaveforms, SinExpPwlSffmFollowTheirFormulas) {
    const auto parsed = parse(
        "waveforms\nV2 b 0 SIN(1 2 1k 0.5m 100 30)\nV3 c 0 EXP(1 3 0.2m 0.1m 0.6m 0.2m)\n"
        "V4 d 0 SFFM(1 2 10k 3 1k)\nV5 e 0 PWL(0 1 0.5m 2 1m 0)\n"
        "R2 b 0 1k\nR3 c 0 1k\nR4 d 0 1k\nR5 e 0 1k\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const std::function<Real(Real)> sin_wave = [](Real t) {
        const Real phase = 30.0 * kPi / 180.0;
        if (t < 0.5e-3) return 1.0 + 2.0 * std::sin(phase);
        return 1.0 + 2.0 * std::exp(-(t - 0.5e-3) * 100.0) *
                         std::sin(2.0 * kPi * 1e3 * (t - 0.5e-3) + phase);
    };
    const std::function<Real(Real)> exp_wave = [](Real t) {
        if (t < 0.2e-3) return 1.0;
        Real v = 1.0 + 2.0 * (1.0 - std::exp(-(t - 0.2e-3) / 0.1e-3));
        if (t >= 0.6e-3) v += -2.0 * (1.0 - std::exp(-(t - 0.6e-3) / 0.2e-3));
        return v;
    };
    const std::function<Real(Real)> sffm_wave = [](Real t) {
        return 1.0 + 2.0 * std::sin(2.0 * kPi * 1e4 * t + 3.0 * std::sin(2.0 * kPi * 1e3 * t));
    };
    const std::function<Real(Real)> pwl_wave = [](Real t) {
        return (t <= 0.5e-3) ? 1.0 + t / 0.5e-3 : 2.0 - 2.0 * (t - 0.5e-3) / 0.5e-3;
    };
    const TransientResult tran = transient(parsed, 1e-3, 1e-5);
    ASSERT_EQ(tran.status, ConvergenceStatus::SUCCESS);
    for (const auto& [node, wave] : {std::pair{"b", sin_wave}, std::pair{"c", exp_wave},
                                     std::pair{"d", sffm_wave}, std::pair{"e", pwl_wave}}) {
        const NodeId id = parsed.circuit->get_node_id(node);
        for (const auto& tp : tran.timepoints) {
            if (tp.time > 0.0) {
                EXPECT_NEAR(tp.node_voltages[id - 1], wave(tp.time), 1e-9)
                    << node << " t=" << tp.time;
            }
        }
    }
}

TEST(SourceWaveforms, CurrentSourceWithPulse) {
    // 1 mA from 0.2 ms on into 1 kOhm, rising over 1 us: at every point,
    // the steps landing on the corners 0.2 and 0.201 ms too, V = R I(t)
    const auto parsed =
        parse("i pulse\nI1 0 f PULSE(0 1m 0.2m 1u 1u 1 2)\nR1 f 0 1k\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    const NodeId f = parsed.circuit->get_node_id("f");
    const TransientResult tran = transient(parsed, 0.5e-3, 1e-4);
    ASSERT_EQ(tran.status, ConvergenceStatus::SUCCESS);
    auto expected = [](double t) {
        if (t <= 0.2e-3) return 0.0;
        if (t < 0.201e-3) return (t - 0.2e-3) / 1e-6;
        return 1.0;
    };
    for (const auto& tp : tran.timepoints) {
        EXPECT_NEAR(tp.node_voltages[f - 1], expected(tp.time), 1e-9) << "t=" << tp.time;
    }
}

TEST(SourceWaveforms, ArgumentSyntax) {
    // Commas without spaces, expressions in braces with spaces, and a
    // waveform without parentheses give the same source
    for (const char* line : {"V1 a 0 PULSE(0,1,0,1u,1u,1m,2m)",
                             "V1 a 0 PULSE({0 * 2} {a / 2} 0 1u 1u 1m 2m)",
                             "V1 a 0 PULSE 0 1 0 1u 1u 1m 2m", "V1 a 0 DC 0 PULSE (0 1 0 1u 1u 1m 2m)"}) {
        const auto parsed = parse(std::string("syntax\n.param a=2\n") + line + "\nR1 a 0 1k\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr) << line;
        const TransientResult tran = transient(parsed, 0.5e-3, 1e-4);
        ASSERT_EQ(tran.status, ConvergenceStatus::SUCCESS) << line;
        EXPECT_NEAR(tran.timepoints.back().node_voltages[parsed.circuit->get_node_id("a") - 1], 1.0,
                    1e-12)
            << line;
    }
    // AC without a DC value is 0 V DC
    EXPECT_NEAR(operating_point(parse("ac only\nV1 a 0 AC 1\nR1 a 0 1k\n.end\n"), "a"), 0.0, 1e-12);
}

TEST(SourceWaveforms, MalformedWaveformsAreErrors) {
    for (const char* line : {"V1 a 0 EXP(1 2 0)", "V1 a 0 PWL(0 1 1m)", "V1 a 0 PWL(1m 1 0 2)",
                             "V1 a 0 SIN(0 1)", "V1 a 0 PULSE(0 1 0", "V1 a 0 SFFM(0 1 1k 2)",
                             "V1 a 0 1 2"}) {
        EXPECT_THROW(parse(std::string("bad\n") + line + "\nR1 a 0 1k\n.end\n"),
                     parser::ParseError)
            << line;
    }
}

// A DC sweep puts the swept source back as it was: a waveform source written
// without a DC value takes the waveform's value at time 0 again (0.7 V for
// SIN(0.7 0.1 1k)), not the last swept value or 0. ngspice-47 gives the same
// AC result with and without the sweep.
TEST(SourceWaveforms, DcSweepRestoresTheSourceSetting) {
    const auto parsed = parse("sweep restore\nV1 in 0 SIN(0.7 0.1 1k) AC 1\nR1 in 0 1k\n"
                              ".dc V1 0 1 0.5\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    DCSolver dc(*parsed.circuit, parsed.config);
    DCSweepResult sweep;
    ASSERT_EQ(dc.solve_sweep(parsed.config.dc_sweep, sweep), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(operating_point(parsed, "in"), 0.7, 1e-12);
}

// The arguments SPICE defaults from .TRAN: PULSE's TD 0, TR and TF TSTEP,
// PW and PER TSTOP; SIN's FREQ 1/TSTOP; EXP's TD1, TAU1 and TAU2 TSTEP and
// TD2 TD1 + TSTEP; SFFM's FC and FS 1/TSTOP. ngspice-47 gives these
// waveforms for PULSE, SIN and EXP (run 2026-11-21). A source before the
// .TRAN line takes them as well. Before, the arguments had to be given.
TEST(SourceWaveforms, OmittedArgumentsTakeTheDefaultsOfTran) {
    auto value_at = [](const std::string& source, const std::string& tran, double t) {
        auto parsed = parse("defaults\nV1 a 0 " + source + "\nR1 a 0 1k\n" + tran + "\n.end\n");
        TransientSolver solver(*parsed.circuit, parsed.config);
        const auto result = solver.solve(0.0, parsed.config.transient.end_time,
                                         parsed.config.transient.initial_step);
        EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS) << source;
        const NodeId a = parsed.circuit->get_node_id("a");
        for (std::size_t i = 1; i < result.timepoints.size(); ++i) {
            const auto& p = result.timepoints[i - 1];
            const auto& q = result.timepoints[i];
            if (q.time >= t) {
                const double w = (t - p.time) / (q.time - p.time);
                return p.node_voltages[a - 1] + w * (q.node_voltages[a - 1] - p.node_voltages[a - 1]);
            }
        }
        return result.timepoints.back().node_voltages[a - 1];
    };
    // TR = TSTEP: halfway at TSTEP / 2; PW = TSTOP: still high at TSTOP
    EXPECT_NEAR(value_at("PULSE(0 1)", ".tran 0.1u 2u", 0.1e-6), 1.0, 1e-12);
    EXPECT_NEAR(value_at("PULSE(0 1)", ".tran 0.1u 2u", 2e-6), 1.0, 1e-12);
    // FREQ = 1/TSTOP: the peak at TSTOP / 4
    EXPECT_NEAR(value_at("SIN(0 1)", ".tran 1u 1m", 0.25e-3), 1.0, 1e-9);
    // TD1 = TAU1 = TAU2 = TSTEP, TD2 = 2 TSTEP
    EXPECT_NEAR(value_at("EXP(0 1)", ".tran 0.1u 2u", 0.4e-6), std::exp(-2.0) - std::exp(-3.0), 1e-9);
    EXPECT_NEAR(value_at("EXP(0 1)", ".tran 0.1u 2u", 0.1e-6), 0.0, 1e-12);
    // FC = 1/TSTOP, MDI = 0
    EXPECT_NEAR(value_at("SFFM(0 1)", ".tran 1u 1m", 0.25e-3), 1.0, 1e-9);
    // The .TRAN line after the source: the line waits for the end of the
    // netlist, and takes the same FREQ
    auto parsed = parse("after\nV1 a 0 SIN(0 1)\nR1 a 0 1k\n.tran 1u 1m\n.end\n");
    EXPECT_EQ(parsed.circuit->get_device("V1")->type(), DeviceType::VSOURCE);
}

TEST(SourceWaveforms, OmittedArgumentsWithoutTran) {
    // Without .TRAN, TSTEP is 0 and TSTOP unbounded: the operating point
    // takes the value at time 0; EXP's time constants, which default to
    // TSTEP, cannot be 0
    EXPECT_NEAR(operating_point(parse("op\nV1 a 0 PULSE(0.5 1)\nR1 a 0 1k\n.end\n"), "a"), 0.5, 1e-12);
    EXPECT_NEAR(operating_point(parse("op\nV1 a 0 SIN(0.25 1)\nR1 a 0 1k\n.end\n"), "a"), 0.25, 1e-12);
    EXPECT_THROW(parse("op\nV1 a 0 EXP(0 1)\nR1 a 0 1k\n.end\n"), parser::ParseError);
}
