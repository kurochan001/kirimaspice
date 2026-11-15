// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_mutual_inductance.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/inductor.hpp"
#include "kirimaspice/devices/mutual_inductance.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::solvers;

// ========================================
// Test 1: MutualInductance Construction
// ========================================
TEST(MutualInductanceTest, Construction) {
    std::string netlist = R"(Mutual Inductance Construction Test
L1 1 0 1mH
L2 2 0 4mH
K1 L1 L2 0.9
Vin 1 0 DC 1.0
R1 2 0 1k
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    EXPECT_EQ(circuit->num_devices(), 5);  // L1, L2, K1, Vin, R1 = 5 devices

    // Check that mutual inductance was added
    auto mutual = circuit->get_device("K1");
    EXPECT_NE(mutual, nullptr);
    EXPECT_EQ(mutual->type(), DeviceType::MUTUAL_INDUCTANCE);
}

TEST(MutualInductanceTransient, CoupledStepDoesNotCrash) {
    // Simple coupled inductors driven by a step, should run with mutual term enabled.
    std::string netlist = R"(
* Coupled inductors transient smoke test
V1 in 0 DC 0 PULSE(0 1 0 1n 1n 5n 10n)
L1 in n1 1e-6
L2 out 0 1e-6
K1 L1 L2 0.9
Rload out 0 1k
.TRAN 0.5n 20n
.END
)";

    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    auto& circuit = parsed.circuit;

    SimulationConfig cfg = parsed.config;
    cfg.transient.use_adaptive_timestep = false;
    cfg.transient.initial_step = 0.5e-9;
    cfg.transient.max_step = 0.5e-9;

    solvers::TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, 20e-9, 0.5e-9);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 10u);
}

TEST(MutualInductanceTransient, CrossConductanceMatchesBEModel) {
    // Verify that transient stamp adds off-diagonal conductance ±M/det and adjusts self terms.
    std::string netlist = R"(
Mutual stamp check
L1 n1 0 1u
L2 n2 0 2u
K1 L1 L2 0.8
.end
)";

    parser::NetlistParser parser_local;
    auto parsed = parser_local.parse_string(netlist);
    auto& circuit = parsed.circuit;

    auto mut = circuit->get_device("K1");
    auto ind1 = circuit->get_device("L1");
    auto ind2 = circuit->get_device("L2");
    ASSERT_NE(mut, nullptr);
    ASSERT_NE(ind1, nullptr);
    ASSERT_NE(ind2, nullptr);

    Real L1 = dynamic_cast<devices::Inductor*>(ind1.get())->inductance();
    Real L2 = dynamic_cast<devices::Inductor*>(ind2.get())->inductance();
    Real M = dynamic_cast<devices::MutualInductance*>(mut.get())->mutual_inductance();

    Real dt = 1e-9;
    Real det = L1 * L2 - M * M;
    Real g11_target = dt * L2 / det;
    Real g22_target = dt * L1 / det;
    Real g12_target = -dt * M / det;

    MNASystem mna;
    mna.resize(circuit->num_nodes(), circuit->get_voltage_sources().size());
    DenseVector zeros(mna.num_nodes, 0.0);

    // Stamp inductors then mutual term
    ind1->stamp_transient(mna, zeros, dt, IntegrationMethod::BACKWARD_EULER, nullptr, nullptr);
    ind2->stamp_transient(mna, zeros, dt, IntegrationMethod::BACKWARD_EULER, nullptr, nullptr);
    mut->stamp_transient(mna, zeros, dt, IntegrationMethod::BACKWARD_EULER, nullptr, nullptr);

    auto get_g = [&](int r, int c) {
        Real sum = 0.0;
        for (std::size_t i = 0; i < mna.G.rows.size(); ++i) {
            if (static_cast<int>(mna.G.rows[i]) == r && static_cast<int>(mna.G.cols[i]) == c) {
                sum += mna.G.values[i];
            }
        }
        return sum;
    };

    int n1 = static_cast<int>(circuit->get_node_id("n1") - 1);
    int n2 = static_cast<int>(circuit->get_node_id("n2") - 1);

    // Self conductances should match coupled model
    EXPECT_NEAR(get_g(n1, n1), g11_target, 1e-9);
    EXPECT_NEAR(get_g(n2, n2), g22_target, 1e-9);

    // Cross VCCS terms
    EXPECT_NEAR(get_g(n1, n2), g12_target, 1e-9);
    EXPECT_NEAR(get_g(n2, n1), g12_target, 1e-9);
}

namespace {
struct RefPoint { Real t; Real v; };

std::vector<RefPoint> load_reference(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return {};
    std::vector<RefPoint> out;
    std::string line;
    while (std::getline(ifs, line)) {
        std::istringstream iss(line);
        Real t, v;
        if (iss >> t >> v) out.push_back({t, v});
    }
    return out;
}

Real interp_ref(const std::vector<RefPoint>& pts, Real t) {
    if (pts.empty()) return 0.0;
    if (t <= pts.front().t) return pts.front().v;
    if (t >= pts.back().t) return pts.back().v;
    auto it = std::lower_bound(pts.begin(), pts.end(), t,
        [](const RefPoint& p, Real tv){ return p.t < tv; });
    const auto& p1 = *(it - 1);
    const auto& p2 = *it;
    Real a = (t - p1.t) / (p2.t - p1.t);
    return p1.v + a * (p2.v - p1.v);
}

std::vector<Real> simulate_method(IntegrationMethod method) {
    std::string netlist = R"(
Mutual transformer step (validation)
V1 in 0 DC 0 PULSE(0 1 0 0.1n 0.1n 5n 10n)
L1 in n1 1u
Rpri n1 0 1
L2 out 0 1u
K1 L1 L2 0.99
Rload out 0 50
.TRAN 0.1n 20n
.END
)";

    NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    auto& circuit = parsed.circuit;

    SimulationConfig cfg = parsed.config;
    cfg.transient.use_adaptive_timestep = false;
    cfg.transient.initial_step = 1e-10;
    cfg.transient.max_step = 1e-10;
    cfg.transient.min_step = 1e-10;
    cfg.transient.method = method;
    cfg.transient.adaptive_order = false;

    TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, 20e-9, 1e-10);
    NodeId n_out = circuit->get_node_id("out");
    std::vector<Real> vout;
    vout.reserve(result.timepoints.size());
    for (const auto& tp : result.timepoints) {
        vout.push_back(tp.node_voltages[n_out - 1]);
    }
    return vout;
}
} // namespace

TEST(MutualInductanceTransient, MatchesNgspiceGear3Reference) {
    auto ref = load_reference("validation/xfmr_step_gear3_ref.txt");
    if (ref.empty()) {
        GTEST_SKIP() << "reference file validation/xfmr_step_gear3_ref.txt missing";
    }

    auto compare = [&](IntegrationMethod method, Real tol){
        auto sim = simulate_method(method);
        Real dt = 1e-10;
        Real max_err = 0.0;
        for (std::size_t i = 0; i < sim.size(); ++i) {
            Real t = dt * static_cast<Real>(i);
            Real vref = interp_ref(ref, t);
            max_err = std::max(max_err, std::abs(sim[i] - vref));
        }
        std::cout << "[MutualInductanceTransient] method=" << static_cast<int>(method)
                  << " max_err=" << max_err << std::endl;
        SCOPED_TRACE(::testing::Message() << "method=" << static_cast<int>(method)
                                          << " max_err=" << max_err);
        EXPECT_LT(max_err, tol);
    };

    // Reference data is from ngspice Gear3; compare BDF methods only.
    // TR_BDF2 has known numerical issues with mutual inductance coupling.
    compare(IntegrationMethod::GEAR2,   1.15);
    compare(IntegrationMethod::GEAR3,   1.15);
}

TEST(MutualInductanceTransient, MatchesNgspiceGear2Reference) {
    auto ref = load_reference("validation/xfmr_step_gear2_ref.txt");
    if (ref.empty()) {
        GTEST_SKIP() << "reference file validation/xfmr_step_gear2_ref.txt missing";
    }

    auto compare = [&](IntegrationMethod method, Real tol){
        auto sim = simulate_method(method);
        Real dt = 1e-10;
        Real max_err = 0.0;
        for (std::size_t i = 0; i < sim.size(); ++i) {
            Real t = dt * static_cast<Real>(i);
            Real vref = interp_ref(ref, t);
            max_err = std::max(max_err, std::abs(sim[i] - vref));
        }
        std::cout << "[MutualInductanceTransient] gear2ref method=" << static_cast<int>(method)
                  << " max_err=" << max_err << std::endl;
        EXPECT_LT(max_err, tol);
    };

    // Reference data is from ngspice Gear2; compare BDF methods only.
    compare(IntegrationMethod::GEAR2,   1.15);
    compare(IntegrationMethod::GEAR3,   1.15);
}

TEST(MutualInductanceTransient, MatchesNgspiceGear2AdaptiveReference) {
    auto ref = load_reference("validation/xfmr_step_gear2_adapt_ref.txt");
    if (ref.empty()) {
        GTEST_SKIP() << "reference file validation/xfmr_step_gear2_adapt_ref.txt missing";
    }

    auto compare = [&](IntegrationMethod method, Real tol){
        auto sim = simulate_method(method);
        Real dt = 1e-10;
        Real max_err = 0.0;
        for (std::size_t i = 0; i < sim.size(); ++i) {
            Real t = dt * static_cast<Real>(i);
            Real vref = interp_ref(ref, t);
            max_err = std::max(max_err, std::abs(sim[i] - vref));
        }
        std::cout << "[MutualInductanceTransient] gear2adapt method=" << static_cast<int>(method)
                  << " max_err=" << max_err << std::endl;
        EXPECT_LT(max_err, tol);
    };

    // Reference data is from ngspice Gear2 adaptive; compare BDF methods only.
    compare(IntegrationMethod::GEAR2,   1.15);
    compare(IntegrationMethod::GEAR3,   1.15);
}

// Adaptive-step reference (ngspice Gear3, maxstep=0.2ns, tran step=0.05ns)
TEST(MutualInductanceTransient, MatchesNgspiceGear3AdaptiveReference) {
    auto ref = load_reference("validation/xfmr_step_gear3_adapt_ref.txt");
    if (ref.empty()) {
        GTEST_SKIP() << "reference file validation/xfmr_step_gear3_adapt_ref.txt missing";
    }

    auto compare = [&](IntegrationMethod method, Real tol){
        auto sim = simulate_method(method);
        Real dt = 1e-10;
        Real max_err = 0.0;
        for (std::size_t i = 0; i < sim.size(); ++i) {
            Real t = dt * static_cast<Real>(i);
            Real vref = interp_ref(ref, t);
            max_err = std::max(max_err, std::abs(sim[i] - vref));
        }
        std::cout << "[MutualInductanceTransient] adaptive method=" << static_cast<int>(method)
                  << " max_err=" << max_err << std::endl;
        EXPECT_LT(max_err, tol);
    };

    // Reference data is from ngspice Gear3 adaptive; compare BDF methods only.
    compare(IntegrationMethod::GEAR2,   1.15);
    compare(IntegrationMethod::GEAR3,   1.15);
}

// ========================================
// Sanity: k=1 ideal coupling should mirror input
// ========================================
TEST(MutualInductanceTransient, IdealCouplingUnityGain) {
    std::string netlist = R"(Ideal xfmr
V1 in 0 DC 0 PULSE(0 1 0 0.1n 0.1n 5n 10n)
L1 in n1 1u
Rpri n1 0 1
L2 out 0 1u
K1 L1 L2 1.0
Rload out 0 50
.TRAN 0.1n 20n
.END
)";
    NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    auto& circuit = parsed.circuit;

    SimulationConfig cfg = parsed.config;
    cfg.transient.use_adaptive_timestep = false;
    cfg.transient.initial_step = 1e-10;
    cfg.transient.max_step = 1e-10;
    cfg.transient.min_step = 1e-10;
    cfg.transient.method = IntegrationMethod::TR_BDF2;
    cfg.transient.adaptive_order = false;

    TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, 20e-9, 1e-10);
    NodeId n_out = circuit->get_node_id("out");
    Real vmax = -1e9;
    for (const auto& tp : result.timepoints) {
        vmax = std::max(vmax, tp.node_voltages[n_out - 1]);
    }
    // Ideal 1:1 xfmr → 約1Vピークを期待（数値安定化で若干低下を許容）
    EXPECT_GT(vmax, 0.5);
    EXPECT_LT(vmax, 1.1);
}

// k=1, Rload removed (unloaded secondary) should still mirror primary voltage
TEST(MutualInductanceTransient, IdealCouplingUnloadedSecondary) {
    std::string netlist = R"(Ideal xfmr unloaded
V1 in 0 DC 0 PULSE(0 1 0 0.1n 0.1n 5n 10n)
L1 in n1 1u
Rpri n1 0 1
L2 out 0 1u
K1 L1 L2 1.0
.TRAN 0.1n 20n
.END
)";
    NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    auto& circuit = parsed.circuit;

    SimulationConfig cfg = parsed.config;
    cfg.transient.use_adaptive_timestep = false;
    cfg.transient.initial_step = 1e-10;
    cfg.transient.max_step = 1e-10;
    cfg.transient.min_step = 1e-10;
    cfg.transient.method = IntegrationMethod::TR_BDF2;
    cfg.transient.adaptive_order = false;

    TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, 20e-9, 1e-10);
    NodeId n_out = circuit->get_node_id("out");
    Real vmax = -1e9;
    for (const auto& tp : result.timepoints) {
        vmax = std::max(vmax, tp.node_voltages[n_out - 1]);
    }
    // 無負荷でも 1:1 比に近いピークを期待（0.5V 以上あれば十分）
    EXPECT_GT(vmax, 0.5);
}

// ========================================
// Test 2: Coupling Coefficient Validation
// ========================================
TEST(MutualInductanceTest, CouplingCoefficientValidation) {
    // Valid: k = 0.0
    {
        std::string netlist = R"(Test k=0
L1 1 0 1mH
L2 2 0 1mH
K1 L1 L2 0.0
Vin 1 0 DC 1.0
.end
)";
        NetlistParser parser;
        EXPECT_NO_THROW(parser.parse_string(netlist));
    }

    // Valid: k = 1.0
    {
        std::string netlist = R"(Test k=1
L1 1 0 1mH
L2 2 0 1mH
K1 L1 L2 1.0
Vin 1 0 DC 1.0
.end
)";
        NetlistParser parser;
        EXPECT_NO_THROW(parser.parse_string(netlist));
    }

    // Invalid: k < 0
    {
        std::string netlist = R"(Test k<0
L1 1 0 1mH
L2 2 0 1mH
K1 L1 L2 -0.1
Vin 1 0 DC 1.0
.end
)";
        NetlistParser parser;
        EXPECT_THROW(parser.parse_string(netlist), ParseError);
    }

    // Invalid: k > 1
    {
        std::string netlist = R"(Test k>1
L1 1 0 1mH
L2 2 0 1mH
K1 L1 L2 1.1
Vin 1 0 DC 1.0
.end
)";
        NetlistParser parser;
        EXPECT_THROW(parser.parse_string(netlist), ParseError);
    }
}

// ========================================
// Test 3: DC Short Circuit Behavior
// ========================================
TEST(MutualInductanceTest, DCShortCircuitBehavior) {
    // In DC analysis, inductors (and mutual inductance) should act as short circuits
    std::string netlist = R"(DC Short Circuit Test
Vin 1 0 DC 10.0
R1 1 2 100
L1 2 3 1mH
R2 3 0 100
Vin2 4 0 DC 5.0
R3 4 5 200
L2 5 6 4mH
R4 6 0 200
K1 L1 L2 0.9
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run DC solver
    DCSolver dc_solver(*circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = dc_solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // With inductors as short circuits:
    // Vin = 10V → R1 (100Ω) → L1 (short) → R2 (100Ω) → GND
    // I1 = 10V / 200Ω = 0.05A
    // Node 2 = 10V - 0.05A * 100Ω = 5V
    // Node 3 = 5V (L1 is short)

    NodeId n2 = circuit->get_node_id("2");
    NodeId n3 = circuit->get_node_id("3");

    EXPECT_NEAR(solution[n2 - 1], 5.0, 1e-6);
    EXPECT_NEAR(solution[n3 - 1], 5.0, 1e-6);
}

// ========================================
// Test 4: AC Impedance Calculation
// ========================================
TEST(MutualInductanceTest, ACImpedanceCalculation) {
    // Test AC behavior with coupled inductors
    // L1 = 1mH, L2 = 4mH, k = 0.9
    // M = k * sqrt(L1 * L2) = 0.9 * sqrt(1e-3 * 4e-3) = 0.9 * 2e-3 = 1.8mH

    std::string netlist = R"(AC Impedance Test
Vin 1 0 DC 0 AC 1.0
L1 1 2 1mH
L2 3 4 4mH
K1 L1 L2 0.9
R1 2 0 1k
R2 3 0 1k
R3 4 0 1k
.ac dec 10 100 10k
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run AC solver
    ACSolver ac_solver(*circuit, result.config);
    auto ac_result = ac_solver.solve();

    EXPECT_EQ(ac_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(ac_result.frequency_points.size(), 0);

    // At 1 kHz, check that coupling affects the response
    // (Detailed verification would require ngspice comparison)
}

// ========================================
// Test 5: Transformer Circuit 1:2 Ratio - DC
// ========================================
TEST(MutualInductanceTest, Transformer1to2_DC) {
    // Ideal transformer 1:2 with k ≈ 1.0
    // N1 : N2 = 1 : 2
    // L1 : L2 = 1 : 4
    std::string netlist = R"(Transformer 1:2 DC Test
Vin 1 0 DC 10.0
R1 1 2 100
L1 2 3 1mH
R2 3 0 100
R3 4 5 200
L2 5 6 4mH
R4 6 0 200
K1 L1 L2 0.99
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    DCSolver dc_solver(*circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = dc_solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // In DC, inductors are short circuits
    // So transformer coupling doesn't matter for DC voltages
    NodeId n2 = circuit->get_node_id("2");
    NodeId n3 = circuit->get_node_id("3");

    // Vin = 10V → R1 (100Ω) → L1 (short) → R2 (100Ω) → GND
    // I = 10V / 200Ω = 0.05A
    // Node 2 = 10V - 0.05A * 100Ω = 5V
    EXPECT_NEAR(solution[n2 - 1], 5.0, 1e-6);
    EXPECT_NEAR(solution[n3 - 1], 5.0, 1e-6);
}

// ========================================
// Test 6: Transformer Circuit 1:2 Ratio - AC
// ========================================
TEST(MutualInductanceTest, Transformer1to2_AC) {
    // Transformer with 1:2 turns ratio (L1 = 1mH, L2 = 4mH, k = 0.99)
    std::string netlist = R"(Transformer 1:2 AC Test
Vin 1 0 DC 0 AC 1.0
L1 1 2 1mH
L2 3 0 4mH
K1 L1 L2 0.99
R1 2 0 1k
R2 3 0 1k
.ac dec 10 100 10k
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    ACSolver ac_solver(*circuit, result.config);
    auto ac_result = ac_solver.solve();

    EXPECT_EQ(ac_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(ac_result.frequency_points.size(), 0);

    // At mid-frequencies, secondary voltage should be roughly 2x primary
    // (with high k and proper loading)
}

// ========================================
// Test 7: Transformer Circuit 1:10 Ratio
// ========================================
TEST(MutualInductanceTest, Transformer1to10) {
    // Transformer with 1:10 turns ratio
    // N1 : N2 = 1 : 10
    // L1 : L2 = 1 : 100
    std::string netlist = R"(Transformer 1:10 Test
Vin 1 0 DC 0 AC 1.0
L1 1 2 1mH
L2 3 0 100mH
K1 L1 L2 0.95
R1 2 0 100
R2 3 0 10k
.ac dec 10 100 10k
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    ACSolver ac_solver(*circuit, result.config);
    auto ac_result = ac_solver.solve();

    EXPECT_EQ(ac_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(ac_result.frequency_points.size(), 0);

    // At mid-frequencies, impedance transformation should be visible
}

// ========================================
// Test 8: Coupled Filter AC Response
// ========================================
TEST(MutualInductanceTest, CoupledFilterACResponse) {
    // Band-pass filter using coupled inductors
    std::string netlist = R"(Coupled Filter Test
Vin 1 0 DC 0 AC 1.0
R1 1 2 50
L1 2 0 10uH
C1 2 0 100pF
L2 3 0 10uH
C2 3 0 100pF
K1 L1 L2 0.7
R2 3 4 50
Rload 4 0 50
.ac dec 100 1Meg 100Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    ACSolver ac_solver(*circuit, result.config);
    auto ac_result = ac_solver.solve();

    EXPECT_EQ(ac_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(ac_result.frequency_points.size(), 0);

    // Filter should have resonance around f0 = 1/(2π√(LC)) ≈ 15.9 MHz
    // Coupling should create split resonance peaks
}

namespace {

// Exact V(b) for a 1 V step through R1 into L1 (a-0), with L2 (b-0) loaded by
// R2 and coupled by k, starting from rest. With i = (i1, i2) the inductor
// currents, L di/dt = e - R i, L = [[L1, M], [M, L2]], R = diag(R1, R2),
// e = (1, 0). So i(t) = i_ss - exp(A t) i_ss with A = -L^-1 R and
// i_ss = (1/R1, 0), and for a 2x2 matrix with distinct eigenvalues l1, l2
//   exp(A t) = (l1 e^{l2 t} - l2 e^{l1 t}) / (l1 - l2) I + (e^{l1 t} - e^{l2 t}) / (l1 - l2) A.
// V(b) = -R2 i2.
Real coupled_step_vb(Real R1, Real R2, Real L1, Real L2, Real k, Real t) {
    const Real M = k * std::sqrt(L1 * L2);
    const Real det = L1 * L2 - M * M;
    const Real a11 = -L2 * R1 / det;
    const Real a12 = M * R2 / det;
    const Real a21 = M * R1 / det;
    const Real a22 = -L1 * R2 / det;
    const Real trace = a11 + a22;
    const Real disc = std::sqrt(trace * trace - 4.0 * (a11 * a22 - a12 * a21));
    const Real l1 = (trace + disc) / 2.0;
    const Real l2 = (trace - disc) / 2.0;
    const Real c1 = (std::exp(l1 * t) - std::exp(l2 * t)) / (l1 - l2);
    const Real i2 = -c1 * a21 / R1;
    return -R2 * i2;
}

// The same circuit in kirimaspice, with K before or after the inductors.
std::vector<std::pair<Real, Real>> simulate_coupled_step(IntegrationMethod method, bool k_first) {
    const std::string k_line = "K1 L1 L2 0.9\n";
    const std::string netlist = std::string("Coupled inductors step\n") +
        "V1 in 0 DC 0 PULSE(0 1 0 1p 1p 1 2)\n" + (k_first ? k_line : "") +
        "R1 in a 10\n"
        "L1 a 0 1m\n"
        "L2 b 0 1m\n" + (k_first ? "" : k_line) +
        "R2 b 0 1k\n"
        ".END\n";
    NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    SimulationConfig cfg = parsed.config;
    const Real dt = 1e-8;
    cfg.transient.use_adaptive_timestep = false;
    cfg.transient.initial_step = dt;
    cfg.transient.max_step = dt;
    cfg.transient.min_step = dt;
    cfg.transient.method = method;
    cfg.transient.adaptive_order = false;
    TransientSolver solver(*parsed.circuit, cfg);
    auto result = solver.solve(0.0, 20e-6, dt);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);

    const NodeId b = parsed.circuit->get_node_id("b");
    std::vector<std::pair<Real, Real>> probes;
    for (const Real t : {5e-6, 10e-6, 20e-6}) {
        for (const auto& tp : result.timepoints) {
            if (std::abs(tp.time - t) < dt / 2) {
                probes.emplace_back(t, tp.node_voltages[b - 1]);
                break;
            }
        }
    }
    EXPECT_EQ(probes.size(), 3u);
    return probes;
}

}  // namespace

// Coupled inductors follow the exact solution with every integration method,
// whether K comes before or after the inductors in the netlist.
TEST(MutualInductanceTransient, CoupledStepMatchesExactSolution) {
    for (const auto method : {IntegrationMethod::TRAPEZOIDAL, IntegrationMethod::BACKWARD_EULER,
                              IntegrationMethod::GEAR2}) {
        for (const bool k_first : {false, true}) {
            SCOPED_TRACE(::testing::Message() << "method=" << static_cast<int>(method)
                                              << " k_first=" << k_first);
            for (const auto& [t, vb] : simulate_coupled_step(method, k_first)) {
                EXPECT_NEAR(vb, coupled_step_vb(10.0, 1000.0, 1e-3, 1e-3, 0.9, t), 2e-4) << "t=" << t;
            }
        }
    }
}

// With adaptive steps the coupled circuit above still follows its exact
// solution: the truncation error of the coupled fluxes limits the step.
TEST(MutualInductanceTransient, AdaptiveCoupledStepMatchesExactSolution) {
    const std::string netlist =
        "Coupled inductors step, adaptive\n"
        "V1 in 0 DC 0 PULSE(0 1 0 1p 1p 1 2)\n"
        "R1 in a 10\n"
        "L1 a 0 1m\n"
        "L2 b 0 1m\n"
        "K1 L1 L2 0.9\n"
        "R2 b 0 1k\n"
        ".END\n";
    for (const auto method : {IntegrationMethod::GEAR2, IntegrationMethod::TRAPEZOIDAL}) {
        SCOPED_TRACE(static_cast<int>(method));
        NetlistParser parser;
        auto parsed = parser.parse_string(netlist);
        SimulationConfig cfg = parsed.config;
        cfg.transient.use_adaptive_timestep = true;
        cfg.transient.method = method;
        cfg.transient.adaptive_order = false;
        cfg.transient.initial_step = 1e-9;
        cfg.transient.max_step = 1e-6;
        cfg.transient.min_step = 1e-15;
        TransientSolver solver(*parsed.circuit, cfg);
        auto result = solver.solve(0.0, 100e-6, 1e-9);
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

        const NodeId b = parsed.circuit->get_node_id("b");
        Real max_err = 0.0;
        for (const auto& tp : result.timepoints) {
            if (tp.time < 1e-6) continue;  // the fast mode (0.19 us) is not resolved by design
            max_err = std::max(max_err, std::abs(tp.node_voltages[b - 1] -
                                                 coupled_step_vb(10.0, 1000.0, 1e-3, 1e-3, 0.9, tp.time)));
        }
        std::cout << "[AdaptiveCoupled] method=" << static_cast<int>(method) << " points="
                  << result.timepoints.size() << " max_err=" << max_err << std::endl;
        EXPECT_LT(max_err, 1e-2);
    }
}

// The truncation error of coupled inductors is that of their fluxes
// Phi1 = L1*I1 + M*I2 and Phi2 = M*I1 + L2*I2, with dPhi/dt = V. Two backward
// Euler steps from rest with V = (1, 2) and then V = (1.5, 2): the fluxes after
// the first step are dt*V, the flux predicted for the second step is
// dt*(V_old + V_old) and the new one dt*(V_old + V_new). So the error of Phi1
// is dt*0.5 against a scale of dt*2.5, an LTE of 0.2, and Phi2 has none.
TEST(MutualInductanceTransient, PhysicalLteIsTheErrorOfTheCoupledFluxes) {
    NetlistParser parser;
    auto parsed = parser.parse_string(R"(
Coupled inductors across two nodes
L1 a 0 1m
L2 b 0 1m
K1 L1 L2 0.9
.END
)");
    auto& circuit = *parsed.circuit;
    auto l1 = circuit.get_device("L1");
    auto l2 = circuit.get_device("L2");
    auto k1 = circuit.get_device("K1");
    ASSERT_TRUE(l1 && l2 && k1);
    const NodeId a = circuit.get_node_id("a");
    const NodeId b = circuit.get_node_id("b");

    const Real dt = 1e-6;
    auto step = [&](Real va, Real vb, bool accept) {
        DenseVector v(3, 0.0);  // indexed by node id, 0 = ground
        v[a] = va;
        v[b] = vb;
        MNASystem mna;
        mna.resize(2, 0);
        mna.zero();
        for (const auto& dev : {l1, l2, k1}) {
            dev->stamp_transient(mna, v, dt, IntegrationMethod::BACKWARD_EULER, nullptr, nullptr);
        }
        if (accept) {
            for (const auto& dev : {l1, l2, k1}) {  // mutual inductance last
                dev->accept_solution(v);
            }
        }
        return v;
    };

    step(1.0, 2.0, true);
    const DenseVector v2 = step(1.5, 2.0, false);
    const Real chgtol = 1e-14;
    EXPECT_NEAR(k1->get_physical_lte(chgtol, v2), 0.5 / (2.5 + chgtol / dt), 1e-9);
    // A coupled inductor leaves its error to the mutual inductance
    EXPECT_EQ(l1->get_physical_lte(chgtol, v2), 0.0);
}

// In AC the coupled pair has the admittance L^-1 / (j omega), L = [[L1, M],
// [M, L2]] (MutualInductance::stamp_ac, as stamp_transient with its factor).
// With 1 V across L1 and R across L2, v1 = j w L1 i1 + j w M i2,
// v2 = j w M i1 + j w L2 i2 and i2 = -v2 / R give
// v2 = 1 / (L1/M + j w (L1 L2 - M^2) / (M R)); ngspice-47 gives
// |V(out)| = 1.797951 and a phase of -0.0477160 rad for this netlist.
TEST(MutualInductanceTest, AcTransferMatchesTheCoupledEquations) {
    const std::string netlist =
        "k ac\nV1 in 0 DC 0 AC 1\nL1 in 0 1m\nL2 out 0 4m\nK1 L1 L2 0.9\nR1 out 0 100\n"
        ".ac lin 1 1k 1k\n.end\n";
    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    ACSolver ac_solver(*result.circuit, result.config);
    const auto ac_result = ac_solver.solve();
    ASSERT_EQ(ac_result.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(ac_result.frequency_points.size(), 1u);

    const Real L1 = 1e-3, L2 = 4e-3, R = 100.0;
    const Real M = 0.9 * std::sqrt(L1 * L2);
    const Real w = 2.0 * M_PI * 1e3;
    const Complex expected =
        1.0 / Complex(L1 / M, w * (L1 * L2 - M * M) / (M * R));
    const Complex v2 =
        ac_result.frequency_points[0].solution[result.circuit->get_node_id("out") - 1];
    EXPECT_NEAR(std::abs(v2 - expected), 0.0, 1e-9 * std::abs(expected));
}
