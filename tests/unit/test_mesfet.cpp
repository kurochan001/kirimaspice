// tests/unit/test_mesfet.cpp
// MESFET Device Tests

#include <gtest/gtest.h>
#include "kirimaspice/devices/mesfet.hpp"  // Step 4.2: Unified MESFET template
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/core/types.hpp"

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

//==============================================================================
// Test Fixture
//==============================================================================

class MESFETTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

//==============================================================================
// 1. Device Construction Tests (2 tests)
//==============================================================================

TEST_F(MESFETTest, MESFET_N_Construction) {
    // Test N-channel MESFET construction
    MESFET_N mesfet(1, "Z1", 1, 2, 0, MESFETModel::CURTICE,
                    0.1, -2.0, 0.05, 2.0, 0.3, 3.0,
                    1e-12, 0.5e-12, 0.1e-12, 0.0, 1.0, 1.0, 300.15);

    EXPECT_EQ(mesfet.id(), 1);
    EXPECT_EQ(mesfet.name(), "Z1");
    EXPECT_EQ(mesfet.type(), DeviceType::MESFET_N);
    EXPECT_FALSE(mesfet.is_linear());
    EXPECT_TRUE(mesfet.is_dynamic());

    const auto& nodes = mesfet.nodes();
    EXPECT_EQ(nodes.size(), 3);  // Drain, Gate, Source
}

//==============================================================================
// 4. Transient TR-BDF2 coverage (1 test)
//==============================================================================

TEST(MESFET_Transient, TRBDF2StagesCover) {
    // Simple biased MESFET ensures both TR-BDF2 stages run without crash.
    std::string netlist = R"(
* MESFET TR-BDF2 coverage test
Vd d 0 DC 2.5
Vg g 0 DC 1.0
Rload d s 1k
Z1 d g s NMF BETA=1e-3 VTO=0.8 LAMBDA=0.02 CGS=1p CGD=0.5p CDS=0.2p
.TRAN 1n 20n
.OPTION METHOD=trbdf2
.END
)";

    parser::NetlistParser parser;
    auto circuit = parser.parse_string_legacy(netlist);

    SimulationConfig cfg;
    cfg.transient.use_adaptive_timestep = false;
    cfg.transient.initial_step = 1e-9;
    cfg.transient.max_step = 1e-9;
    cfg.transient.method = IntegrationMethod::TR_BDF2;

    solvers::TransientSolver solver(*circuit, cfg);
    auto result = solver.solve(0.0, 20e-9, 1e-9);

    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 5u);
}

TEST_F(MESFETTest, MESFET_P_Construction) {
    // Test P-channel MESFET construction
    MESFET_P mesfet(1, "Z1", 1, 2, 0, MESFETModel::CURTICE,
                    0.1, 2.0, 0.05, 2.0, 0.3, 3.0,
                    1e-12, 0.5e-12, 0.1e-12, 0.0, 1.0, 1.0, 300.15);

    EXPECT_EQ(mesfet.id(), 1);
    EXPECT_EQ(mesfet.name(), "Z1");
    EXPECT_EQ(mesfet.type(), DeviceType::MESFET_P);
    EXPECT_FALSE(mesfet.is_linear());
    EXPECT_TRUE(mesfet.is_dynamic());

    const auto& nodes = mesfet.nodes();
    EXPECT_EQ(nodes.size(), 3);  // Drain, Gate, Source
}

//==============================================================================
// 2. Curtice Model Operation Tests (4 tests)
//==============================================================================

TEST_F(MESFETTest, Curtice_CutoffRegion) {
    // Test cutoff region: Vgs < Vto → Id ≈ 0
    CircuitGraph circuit;

    // Add voltage sources
    circuit.add_voltage_source("Vdd", "drain", "0", 5.0);
    circuit.add_voltage_source("Vg", "gate", "0", -3.0);  // Vgs = -3.0 < Vto = -2.0

    // Add MESFET (model=1 for Curtice)
    circuit.add_mesfet_n("Z1", "drain", "gate", "0", 1,
                         0.1, -2.0, 0.05, 2.0);

    // Solve DC operating point
    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
    // In cutoff, drain current should be near zero (< 1 µA)
    // We cannot directly check current, but we can verify convergence
}

TEST_F(MESFETTest, Curtice_LinearRegion) {
    // Test linear region: Vgs > Vto, small Vds
    CircuitGraph circuit;

    circuit.add_voltage_source("Vdd", "drain", "0", 0.5);  // Small Vds
    circuit.add_voltage_source("Vg", "gate", "0", 0.0);    // Vgs = 0.0 > Vto = -2.0

    circuit.add_mesfet_n("Z1", "drain", "gate", "0", 1,
                         0.1, -2.0, 0.05, 2.0);

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
}

TEST_F(MESFETTest, Curtice_SaturationRegion) {
    // Test saturation region: Vgs > Vto, large Vds
    CircuitGraph circuit;

    circuit.add_voltage_source("Vdd", "drain", "0", 5.0);  // Large Vds
    circuit.add_voltage_source("Vg", "gate", "0", 0.0);    // Vgs = 0.0 > Vto = -2.0

    circuit.add_mesfet_n("Z1", "drain", "gate", "0", 1,
                         0.1, -2.0, 0.05, 2.0);

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
}

TEST_F(MESFETTest, Curtice_ChannelLengthModulation) {
    // Test channel length modulation effect (LAMBDA)
    CircuitGraph circuit1, circuit2;

    // Circuit with channel length modulation (LAMBDA = 0.05)
    circuit1.add_voltage_source("Vdd", "drain", "0", 5.0);
    circuit1.add_voltage_source("Vg", "gate", "0", 0.0);
    circuit1.add_mesfet_n("Z1", "drain", "gate", "0", 1,
                          0.1, -2.0, 0.05, 2.0);  // LAMBDA = 0.05

    // Circuit without channel length modulation (LAMBDA = 0)
    circuit2.add_voltage_source("Vdd", "drain", "0", 5.0);
    circuit2.add_voltage_source("Vg", "gate", "0", 0.0);
    circuit2.add_mesfet_n("Z1", "drain", "gate", "0", 1,
                          0.1, -2.0, 0.0, 2.0);   // LAMBDA = 0

    DCSolver solver1(circuit1);
    DCSolver solver2(circuit2);

    DenseVector solution1, solution2;
    SolverStats stats1, stats2;
    ConvergenceStatus status1 = solver1.solve(solution1, stats1);
    ConvergenceStatus status2 = solver2.solve(solution2, stats2);

    EXPECT_EQ(status1, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(status2, ConvergenceStatus::SUCCESS);
    // Both should converge (cannot easily compare currents without RL)
}

//==============================================================================
// 3. Statz Model Operation Tests (2 tests)
//==============================================================================

TEST_F(MESFETTest, Statz_CutoffRegion) {
    // Test Statz model cutoff region
    CircuitGraph circuit;

    circuit.add_voltage_source("Vdd", "drain", "0", 5.0);
    circuit.add_voltage_source("Vg", "gate", "0", -3.0);  // Vgs < Vto

    circuit.add_mesfet_n("Z1", "drain", "gate", "0", 2,  // model=2 for Statz
                         0.1, -2.0, 0.05, 2.0, 0.3, 3.0);

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
}

TEST_F(MESFETTest, Statz_SaturationRegion) {
    // Test Statz model saturation region
    CircuitGraph circuit;

    circuit.add_voltage_source("Vdd", "drain", "0", 5.0);
    circuit.add_voltage_source("Vg", "gate", "0", 0.0);   // Vgs > Vto

    circuit.add_mesfet_n("Z1", "drain", "gate", "0", 2,  // model=2 for Statz
                         0.1, -2.0, 0.05, 2.0, 0.3, 3.0);

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
}

//==============================================================================
// 4. CircuitGraph Integration Tests (2 tests)
//==============================================================================

TEST_F(MESFETTest, CircuitGraph_AddMESFET_N) {
    // Test adding N-channel MESFET to circuit
    CircuitGraph circuit;

    circuit.add_mesfet_n("Z1", "drain", "gate", "source", 1,  // model=1 for Curtice
                         0.1, -2.0, 0.05, 2.0, 0.3, 3.0,
                         1e-12, 0.5e-12, 0.1e-12, 0.0, 1.0, 1.0, 300.15);

    // Cannot directly access devices() - it's private
    // Just verify circuit was created successfully
    EXPECT_NO_THROW({
        DCSolver solver(circuit);
    });
}

TEST_F(MESFETTest, CircuitGraph_AddMESFET_P) {
    // Test adding P-channel MESFET to circuit
    CircuitGraph circuit;

    circuit.add_mesfet_p("Z1", "drain", "gate", "source", 1,  // model=1 for Curtice
                         0.1, 2.0, 0.05, 2.0, 0.3, 3.0,
                         1e-12, 0.5e-12, 0.1e-12, 0.0, 1.0, 1.0, 300.15);

    EXPECT_NO_THROW({
        DCSolver solver(circuit);
    });
}

//==============================================================================
// 5. Noise Analysis Tests (2 tests)
//==============================================================================

TEST_F(MESFETTest, Noise_ThermalNoise) {
    // Test thermal noise generation
    MESFET_N mesfet(1, "Z1", 1, 2, 0, MESFETModel::CURTICE,
                    0.1, -2.0, 0.05, 2.0, 0.3, 3.0,
                    1e-12, 0.5e-12, 0.1e-12, 0.0, 1.0, 1.0, 300.15);

    DenseVector dc_solution = {5.0, 0.0, 0.0};  // Drain, Gate, Source (active region)

    Real frequency = 1e6;  // 1 MHz
    auto noise_sources = mesfet.get_noise_sources(dc_solution, frequency);

    // Should have at least thermal noise
    EXPECT_GT(noise_sources.size(), 0);

    // Find thermal noise source
    bool found_thermal = false;
    for (const auto& ns : noise_sources) {
        if (ns.name.find("thermal") != std::string::npos) {
            found_thermal = true;
            EXPECT_TRUE(ns.is_current);
            EXPECT_GT(ns.spectral_density, 0.0);
        }
    }
    EXPECT_TRUE(found_thermal);
}

TEST_F(MESFETTest, Noise_FlickerNoise) {
    // Test flicker noise (1/f noise)
    MESFET_N mesfet(1, "Z1", 1, 2, 0, MESFETModel::CURTICE,
                    0.1, -2.0, 0.05, 2.0, 0.3, 3.0,
                    1e-12, 0.5e-12, 0.1e-12,
                    1e-20, 1.0, 1.0, 300.15);  // KF > 0 for flicker noise

    DenseVector dc_solution = {5.0, 0.0, 0.0};

    Real frequency = 1e3;  // 1 kHz (low frequency for flicker noise)
    auto noise_sources = mesfet.get_noise_sources(dc_solution, frequency);

    // Should have both thermal and flicker noise
    EXPECT_GE(noise_sources.size(), 2);

    // Find flicker noise source
    bool found_flicker = false;
    for (const auto& ns : noise_sources) {
        if (ns.name.find("flicker") != std::string::npos) {
            found_flicker = true;
            EXPECT_TRUE(ns.is_current);
            EXPECT_GT(ns.spectral_density, 0.0);
        }
    }
    EXPECT_TRUE(found_flicker);
}
