// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_tline.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/tline_lossless.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <cmath>

using namespace kirimaspice;

//==============================================================================
// Parser Tests for T-line
//==============================================================================

TEST(TLineParser, BasicSyntax) {
    std::string netlist = R"(T-line Parser Test
T1 1 0 2 0 Z0=50 TD=1e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    EXPECT_EQ(circuit->num_devices(), 1);

    auto dev = circuit->get_device("T1");
    ASSERT_NE(dev, nullptr);
    EXPECT_EQ(dev->type(), DeviceType::TLINE_LOSSLESS);

    auto tline = std::dynamic_pointer_cast<devices::TLineLossless>(dev);
    ASSERT_NE(tline, nullptr);
    EXPECT_DOUBLE_EQ(tline->z0(), 50.0);
    EXPECT_DOUBLE_EQ(tline->td(), 1e-9);
}

TEST(TLineParser, CaseInsensitive) {
    std::string netlist = R"(T-line Case Test
T1 in 0 out 0 z0=75 td=5e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    ASSERT_NE(circuit, nullptr);
    auto tline = std::dynamic_pointer_cast<devices::TLineLossless>(circuit->get_device("T1"));
    ASSERT_NE(tline, nullptr);
    EXPECT_DOUBLE_EQ(tline->z0(), 75.0);
    EXPECT_DOUBLE_EQ(tline->td(), 5e-9);
}

TEST(TLineParser, ScientificNotation) {
    std::string netlist = R"(T-line Scientific Notation Test
T1 1 0 2 0 Z0=50 TD=100e-12
T2 3 0 4 0 Z0=75 TD=10e-9
T3 5 0 6 0 Z0=100 TD=1e-6
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    auto t1 = std::dynamic_pointer_cast<devices::TLineLossless>(circuit->get_device("T1"));
    EXPECT_DOUBLE_EQ(t1->td(), 100e-12);  // 100ps

    auto t2 = std::dynamic_pointer_cast<devices::TLineLossless>(circuit->get_device("T2"));
    EXPECT_DOUBLE_EQ(t2->td(), 10e-9);    // 10ns

    auto t3 = std::dynamic_pointer_cast<devices::TLineLossless>(circuit->get_device("T3"));
    EXPECT_DOUBLE_EQ(t3->td(), 1e-6);     // 1us
}

TEST(TLineParser, ExpressionEvaluation) {
    // Test parameter expressions (when expression support is fully implemented)
    // For now, test with direct computed values
    std::string netlist = R"(T-line Expression Test
T1 1 0 2 0 Z0=100 TD=5e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    auto tline = std::dynamic_pointer_cast<devices::TLineLossless>(circuit->get_device("T1"));
    ASSERT_NE(tline, nullptr);
    EXPECT_DOUBLE_EQ(tline->z0(), 100.0);  // Direct value (future: {Z_BASE*2})
    EXPECT_DOUBLE_EQ(tline->td(), 5e-9);   // Direct value (future: {DELAY*5})
}

TEST(TLineParser, MissingZ0) {
    std::string netlist = R"(T-line Missing Z0
T1 1 0 2 0 TD=1e-9
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string_legacy(netlist), parser::ParseError);
}

TEST(TLineParser, MissingTD) {
    std::string netlist = R"(T-line Missing TD
T1 1 0 2 0 Z0=50
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string_legacy(netlist), parser::ParseError);
}

TEST(TLineParser, NegativeZ0) {
    std::string netlist = R"(T-line Negative Z0
T1 1 0 2 0 Z0=-50 TD=1e-9
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string_legacy(netlist), parser::ParseError);
}

TEST(TLineParser, ZeroTD) {
    std::string netlist = R"(T-line Zero TD
T1 1 0 2 0 Z0=50 TD=0
.END
)";

    parser::NetlistParser parser;
    EXPECT_THROW(parser.parse_string_legacy(netlist), parser::ParseError);
}

//==============================================================================
// Device Behavior Tests
//==============================================================================

TEST(TLineDevice, ACAdmittancePhaseDelay) {
    // Verify AC stamping uses complex phase shift (cot/csc) terms
    const Real z0 = 50.0;
    const Real td = 1e-9;        // 1 ns delay
    const Real freq = 100e6;     // 100 MHz (θ ≈ 0.628 rad)
    const Real omega = 2.0 * M_PI * freq;

    devices::TLineLossless tline(DeviceId(0), "T1", 1, 0, 2, 0, z0, td);

    ComplexMNASystem sys;
    sys.resize(2, 0);  // nodes: 1 (ap), 2 (bp)

    DenseVector dc_solution(2, 0.0);
    tline.stamp_ac(sys, omega, dc_solution);

    auto entry = [&](std::uint32_t r, std::uint32_t c) -> Complex {
        for (std::size_t i = 0; i < sys.Y.values.size(); ++i) {
            if (sys.Y.rows[i] == r && sys.Y.cols[i] == c) {
                return sys.Y.values[i];
            }
        }
        return Complex(0.0, 0.0);
    };

    const Complex Y11 = entry(0, 0);
    const Complex Y12 = entry(0, 1);
    const Complex Y21 = entry(1, 0);
    const Complex Y22 = entry(1, 1);

    const Real theta = omega * td;
    const Real sin_theta = std::sin(theta);
    const Real cos_theta = std::cos(theta);
    const Real cot_theta = cos_theta / sin_theta;
    const Real csc_theta = 1.0 / sin_theta;
    const Complex j(0.0, 1.0);

    const Complex expected_Y11 = -j * ((1.0 / z0) * cot_theta);
    const Complex expected_Y12 =  j * ((1.0 / z0) * csc_theta);

    EXPECT_EQ(sys.Y.nnz(), 4u);
    EXPECT_NEAR(Y11.real(), expected_Y11.real(), 1e-12);
    EXPECT_NEAR(Y11.imag(), expected_Y11.imag(), 1e-12);
    EXPECT_NEAR(Y22.real(), expected_Y11.real(), 1e-12);
    EXPECT_NEAR(Y22.imag(), expected_Y11.imag(), 1e-12);
    EXPECT_NEAR(Y12.real(), expected_Y12.real(), 1e-12);
    EXPECT_NEAR(Y12.imag(), expected_Y12.imag(), 1e-12);
    EXPECT_NEAR(Y21.real(), expected_Y12.real(), 1e-12);
    EXPECT_NEAR(Y21.imag(), expected_Y12.imag(), 1e-12);
}

TEST(TLineDevice, CharacteristicImpedance) {
    // Test that the transmission line acts as Z0 impedance at DC (long term)
    std::string netlist = R"(T-line Impedance Test
Vin 1 0 DC 1
T1 1 0 2 0 Z0=50 TD=1n
Rload 2 0 50
.TRAN 1n 100n
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;

    solvers::TransientSolver solver(*circuit, config);

    Real t_start = 0.0;
    Real t_stop = 100e-9;
    Real dt = 1e-9;

    auto result = solver.solve(t_start, t_stop, dt);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
}

TEST(TLineDevice, PropagationDelay) {
    // Test signal propagation through transmission line
    // DC step input to verify basic delay mechanism
    // (Detailed pulse testing will be added when PULSE waveform is implemented)
    std::string netlist = R"(T-line Delay Test
Vin 1 0 DC 1
Rs 1 2 50
T1 2 0 3 0 Z0=50 TD=5e-9
Rload 3 0 50
.TRAN 1e-10 20e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;

    solvers::TransientSolver solver(*circuit, config);

    Real t_start = 0.0;
    Real t_stop = 20e-9;
    Real dt = 1e-10;

    auto result = solver.solve(t_start, t_stop, dt);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 100); // Should have many time points
}

TEST(TLineDevice, Reflection) {
    // Test impedance mismatch reflection
    // Open-circuit termination test with DC source
    // (Detailed reflection testing will be added when PULSE waveform is implemented)
    std::string netlist = R"(T-line Reflection Test
Vin 1 0 DC 1
Rs 1 2 50
T1 2 0 3 0 Z0=50 TD=5e-9
* Open circuit at load (no termination resistor)
.TRAN 1e-10 30e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;

    solvers::TransientSolver solver(*circuit, config);

    Real t_start = 0.0;
    Real t_stop = 30e-9;
    Real dt = 1e-10;

    auto result = solver.solve(t_start, t_stop, dt);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
}

TEST(TLineDevice, MatchedTermination) {
    // Test matched termination (no reflection)
    // DC source to verify basic matched impedance behavior
    std::string netlist = R"(T-line Matched Test
Vin 1 0 DC 1
Rs 1 2 50
T1 2 0 3 0 Z0=50 TD=5e-9
Rload 3 0 50
.TRAN 1e-10 20e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;

    solvers::TransientSolver solver(*circuit, config);

    Real t_start = 0.0;
    Real t_stop = 20e-9;
    Real dt = 1e-10;

    auto result = solver.solve(t_start, t_stop, dt);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // TODO: Add detailed voltage verification when T-line steady-state behavior is validated
    // With matched termination (Rs=Z0=Rload=50), voltage should approach Vin/2 = 0.5V
    // (Currently testing convergence only)
}

TEST(TLineDevice, HistoryBufferManagement) {
    // Test that history buffer correctly stores and retrieves delayed signals
    // Use very long simulation to ensure old entries are pruned
    std::string netlist = R"(T-line History Test
Vin 1 0 DC 1
Rs 1 2 50
T1 2 0 3 0 Z0=50 TD=2e-9
Rload 3 0 50
.TRAN 1e-10 50e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;

    solvers::TransientSolver solver(*circuit, config);

    Real t_start = 0.0;
    Real t_stop = 50e-9;
    Real dt = 1e-10;

    auto result = solver.solve(t_start, t_stop, dt);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 400);  // Many timesteps

    // Just verify simulation completes without crash
    // (history buffer should automatically prune old entries)
}

TEST(TLineDevice, LossyOlineTransientPasses) {
    // Minimal smoke test: lossy O-line should not crash transient solve
    std::string netlist = R"(Lossy O-line transient test
Vin 1 0 DC 1
O1 1 0 2 0 R=1 L=50n C=1p G=1e-6 TD=1e-9
Rload 2 0 50
.TRAN 1e-10 10e-9
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;

    solvers::TransientSolver solver(*circuit, config);
    auto result = solver.solve(0.0, 10e-9, 1e-10);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 50u);
}

//==============================================================================
// Waves on a lossless line (method of characteristics)
//==============================================================================
namespace {

// A 1 V step through Rs = Z0 = 50 Ohm into T1 (TD = 5 ns) with the given load
// at the far end. Returns V(<node>) at time t from a fixed-step transient.
Real step_response(const std::string& load_line, const std::string& node, Real t) {
    const std::string netlist = "T-line step\n"
        "Vin 1 0 DC 0 PULSE(0 1 0 1p 1p 1 2)\n"
        "Rs 1 2 50\n"
        "T1 2 0 3 0 Z0=50 TD=5n\n" + load_line + "\n.END\n";
    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;
    solvers::TransientSolver solver(*circuit, config);
    const Real dt = 1e-10;
    auto result = solver.solve(0.0, 20e-9, dt);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);

    const NodeId id = circuit->get_node_id(node);
    for (const auto& tp : result.timepoints) {
        if (std::abs(tp.time - t) < dt / 2) {
            return tp.node_voltages[id - 1];
        }
    }
    ADD_FAILURE() << "no time point at " << t;
    return 0.0;
}

}  // namespace

// Matched at both ends: the near end stays at 0.5 V and the far end rises to
// 0.5 V after TD, with no reflection.
TEST(TLineDevice, MatchedLineCarriesHalfTheStepWithoutReflection) {
    const std::string load = "Rload 3 0 50";
    EXPECT_NEAR(step_response(load, "3", 3e-9), 0.0, 1e-9);
    EXPECT_NEAR(step_response(load, "3", 8e-9), 0.5, 1e-9);
    EXPECT_NEAR(step_response(load, "2", 8e-9), 0.5, 1e-9);
    EXPECT_NEAR(step_response(load, "2", 15e-9), 0.5, 1e-9);
}

// Open far end: the wave doubles there after TD, and the reflection brings
// the near end to 1 V after 2*TD.
TEST(TLineDevice, OpenEndReflectsTheWave) {
    const std::string load = "Rload 3 0 1G";
    EXPECT_NEAR(step_response(load, "3", 8e-9), 1.0, 1e-6);
    EXPECT_NEAR(step_response(load, "2", 8e-9), 0.5, 1e-6);
    EXPECT_NEAR(step_response(load, "2", 15e-9), 1.0, 1e-6);
}

// In DC a lossless line is in steady state, and its characteristic
// equations with the delayed values equal to the present ones give
// V_A = V_B and I_A = -I_B: 1 V through 50 Ohm into a line loaded with
// 50 Ohm puts 0.5 V at both ends (each within I / 1e6 S, the DC coupling).
namespace {
const char* kLineInDc = R"(line in DC
V1 in 0 DC 1
R1 in a 50
T1 a 0 b 0 Z0=50 TD=10n
R2 b 0 50
.TRAN 0.1n 50n
.END
)";
}  // namespace

TEST(TLineDevice, OperatingPointConnectsThePorts) {
    parser::NetlistParser parser;
    auto result = parser.parse_string(kLineInDc);
    ASSERT_NE(result.circuit, nullptr);
    solvers::DCSolver dc(*result.circuit, result.config);
    DenseVector op;
    SolverStats stats;
    ASSERT_EQ(dc.solve(op, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(op[result.circuit->get_node_id("a") - 1], 0.5, 1e-7);
    EXPECT_NEAR(op[result.circuit->get_node_id("b") - 1], 0.5, 1e-7);
}

//==============================================================================
// Lossy line: the exact two-port in DC and AC (TLineLossy::two_port)
//==============================================================================

// R, L, C, G are per unit length and TD fixes the length, TD / sqrt(L C):
// with L = 250 nH, C = 100 pF and TD = 5 ns the line is 1 m long, so in DC it
// is 1 Ohm in series. ngspice-47 gives V(b) = 50/101 V for its LTRA with the
// same values and LEN = 1 (run on 2026-11-15).
TEST(TLineDevice, LossyLineInDcIsItsSeriesResistance) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 a 0 1\nR0 a a2 50\nO1 a2 0 b 0 R=1 L=250n C=100p G=0 TD=5n\nR1 b 0 50\n.op\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("b") - 1], 50.0 / 101.0, 1e-9);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("a2") - 1], 51.0 / 101.0, 1e-9);
}

// ... and with G the shunt conductance too: 1 Ohm and 1 mS over 1 m, the
// two-port of the telegrapher's equations at omega = 0
TEST(TLineDevice, LossyLineInDcWithShuntConductance) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 a 0 1\nO1 a 0 b 0 R=1 L=250n C=100p G=1m TD=5n\nR1 b 0 50\n.op\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    // V_B with port A at 1 V: I_B = Y12 + Y11 V_B = -V_B / 50
    const Real g = std::sqrt(1.0 * 1e-3);  // gamma = sqrt(R G), length 1
    const Real zc = std::sqrt(1.0 / 1e-3);
    const Real y11 = 1.0 / (zc * std::tanh(g));
    const Real y12 = -1.0 / (zc * std::sinh(g));
    EXPECT_NEAR(x[parsed.circuit->get_node_id("b") - 1], -y12 / (y11 + 1.0 / 50.0), 1e-9);
}

// In AC the line between 50 Ohm ends: V(b) / V(a) from its ABCD matrix,
// A = D = cosh(gamma l), B = Zc sinh(gamma l), C = sinh(gamma l) / Zc
TEST(TLineDevice, LossyLineInAcIsTheTelegrapherTwoPort) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\nV1 a 0 DC 0 AC 1\nR0 a a2 50\nO1 a2 0 b 0 R=5 L=250n C=100p G=1m TD=5n\nR1 b 0 50\n"
        ".ac lin 1 30Meg 30Meg\n.end\n");
    solvers::ACSolver solver(*parsed.circuit, parsed.config);
    const auto ac = solver.solve();
    ASSERT_EQ(ac.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(ac.frequency_points.size(), 1u);
    const Complex vb = ac.frequency_points[0].solution[parsed.circuit->get_node_id("b") - 1];

    const Real w = 2.0 * M_PI * 30e6;
    const Complex zs(5.0, w * 250e-9), ys(1e-3, w * 100e-12);
    const Complex gl = std::sqrt(zs * ys) * 1.0;
    const Complex zc = std::sqrt(zs / ys);
    const Complex a = std::cosh(gl), b = zc * std::sinh(gl), c = std::sinh(gl) / zc;
    // V2 = 50 I2 at the load; V1 = A V2 + B I2, I1 = C V2 + A I2; 1 = 50 I1 + V1
    const Complex i2 = 1.0 / (a * 50.0 + b + 50.0 * (c * 50.0 + a));
    EXPECT_NEAR(std::abs(vb - 50.0 * i2), 0.0, 1e-9);
}

// A line delays by TD whatever the integration method: the first stage of
// TR-BDF2 ends between two time points, and the line's clock, history and
// filter stay at the last point until the second stage (accept_solution).
// A 1 V step at 1 ns (1 ps rise) through 50 Ohm into a line of TD = 10 ns
// loaded with 50 Ohm: nothing reaches the far end before 11 ns, and the
// lossless line puts 0.5 V there after it, as at its near end.
TEST(TLineDevice, EveryMethodDelaysByTd) {
    for (const std::string line : {"T1 a 0 b 0 Z0=50 TD=10n",
                                   "O1 a 0 b 0 R=0.5 L=500n C=200p G=0 TD=10n"}) {
        for (const std::string method : {"trap", "gear", "tr_bdf2"}) {
            SCOPED_TRACE(line + " " + method);
            parser::NetlistParser parser;
            auto parsed = parser.parse_string("t\nV1 in 0 PULSE(0 1 1n 1p 1p 1 2)\nR1 in a 50\n" +
                                              line + "\nR2 b 0 50\n.options method=" + method +
                                              "\n.end\n");
            solvers::TransientSolver solver(*parsed.circuit, parsed.config);
            const auto result = solver.solve(0.0, 20e-9, 0.25e-9);
            ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
            const NodeId b = parsed.circuit->get_node_id("b");
            for (const auto& tp : result.timepoints) {
                if (tp.time <= 11e-9 * (1 - 1e-9)) {
                    EXPECT_NEAR(tp.node_voltages[b - 1], 0.0, 1e-9) << "t=" << tp.time;
                } else if (line[0] == 'T' && tp.time >= 11.25e-9) {
                    EXPECT_NEAR(tp.node_voltages[b - 1], 0.5, 1e-6) << "t=" << tp.time;
                }
            }
        }
    }
}
