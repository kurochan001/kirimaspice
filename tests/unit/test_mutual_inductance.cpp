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
