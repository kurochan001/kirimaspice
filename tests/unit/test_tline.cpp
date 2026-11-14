// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_tline.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/tline_lossless.hpp"
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
