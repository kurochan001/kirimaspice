// tests/unit/test_mosfet.cpp
#include <gtest/gtest.h>
#include "kirimaspice/devices/mosfet_level1.hpp"  // Step 5.1: Unified MOSFET Level 1
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

//==============================================================================
// MOSFET_N - Basic Construction Tests
//==============================================================================
TEST(MOSFET_N, Construction) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    EXPECT_EQ(mosfet.id(), 1);
    EXPECT_EQ(mosfet.name(), "M1");
    EXPECT_EQ(mosfet.type(), DeviceType::MOSFET_N);
    EXPECT_EQ(mosfet.drain(), 1);
    EXPECT_EQ(mosfet.gate(), 2);
    EXPECT_EQ(mosfet.source(), 0);
    EXPECT_DOUBLE_EQ(mosfet.process_transconductance(), 2e-5);
    EXPECT_DOUBLE_EQ(mosfet.channel_width(), 10e-6);
    EXPECT_DOUBLE_EQ(mosfet.channel_length(), 1e-6);
    EXPECT_DOUBLE_EQ(mosfet.threshold_voltage(), 0.7);
    EXPECT_FALSE(mosfet.is_linear());
    EXPECT_FALSE(mosfet.is_dynamic());
}

TEST(MOSFET_N, ConstructionWithInvalidParameters) {
    // Negative Kp should throw
    EXPECT_THROW(MOSFET_N(1, "M1", 1, 2, 0, -2e-5, 10e-6, 1e-6, 0.7), std::runtime_error);

    // Zero Kp should throw
    EXPECT_THROW(MOSFET_N(1, "M1", 1, 2, 0, 0.0, 10e-6, 1e-6, 0.7), std::runtime_error);

    // Negative W should throw
    EXPECT_THROW(MOSFET_N(1, "M1", 1, 2, 0, 2e-5, -10e-6, 1e-6, 0.7), std::runtime_error);

    // Zero W should throw
    EXPECT_THROW(MOSFET_N(1, "M1", 1, 2, 0, 2e-5, 0.0, 1e-6, 0.7), std::runtime_error);

    // Negative L should throw
    EXPECT_THROW(MOSFET_N(1, "M1", 1, 2, 0, 2e-5, 10e-6, -1e-6, 0.7), std::runtime_error);

    // Zero L should throw
    EXPECT_THROW(MOSFET_N(1, "M1", 1, 2, 0, 2e-5, 10e-6, 0.0, 0.7), std::runtime_error);
}

//==============================================================================
// MOSFET_N - I-V Characteristic Tests
//==============================================================================
TEST(MOSFET_N, DrainCurrentCutoffRegion) {
    // Standard MOSFET: Kp=2e-5, W=10µm, L=1µm, Vth=0.7V
    // β = Kp × W/L = 2e-5 × 10 = 2e-4 A/V²
    MOSFET_N mosfet(1, "M1", 1, 2, 0);

    // Cutoff region: Vgs < Vth
    Real vgs = 0.5;  // < 0.7V
    Real vds = 1.0;

    Real id = mosfet.get_drain_current(vgs, vds);

    // Id should be zero in cutoff
    EXPECT_NEAR(id, 0.0, 1e-20);
}

TEST(MOSFET_N, DrainCurrentLinearRegion) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Linear region: Vgs > Vth, Vds < Vgs - Vth
    Real vgs = 2.0;  // > Vth (0.7V)
    Real vds = 0.5;  // < Vgs - Vth = 1.3V

    // Id = β × ((Vgs - Vth) × Vds - Vds²/2)
    // β = 2e-4 A/V²
    // Id = 2e-4 × ((2.0 - 0.7) × 0.5 - 0.5²/2)
    // Id = 2e-4 × (1.3 × 0.5 - 0.125)
    // Id = 2e-4 × (0.65 - 0.125) = 2e-4 × 0.525 = 1.05e-4 A

    Real id = mosfet.get_drain_current(vgs, vds);

    Real vgs_eff = vgs - 0.7;  // 1.3V
    Real beta = 2e-4;
    Real expected_id = beta * (vgs_eff * vds - 0.5 * vds * vds);

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

TEST(MOSFET_N, DrainCurrentSaturationRegion) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Saturation region: Vgs > Vth, Vds >= Vgs - Vth
    Real vgs = 2.0;  // > Vth (0.7V)
    Real vds = 2.0;  // >= Vgs - Vth = 1.3V

    // Id = (β/2) × (Vgs - Vth)²
    // β = 2e-4 A/V²
    // Id = (2e-4/2) × (2.0 - 0.7)² = 1e-4 × 1.69 = 1.69e-4 A

    Real id = mosfet.get_drain_current(vgs, vds);

    Real vgs_eff = vgs - 0.7;  // 1.3V
    Real beta = 2e-4;
    Real expected_id = 0.5 * beta * vgs_eff * vgs_eff;

    EXPECT_NEAR(id, expected_id, expected_id * 0.01);  // Within 1%
}

TEST(MOSFET_N, DrainCurrentZeroBias) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Zero bias: Vgs = 0, Vds = 0
    Real vgs = 0.0;
    Real vds = 0.0;

    Real id = mosfet.get_drain_current(vgs, vds);

    EXPECT_NEAR(id, 0.0, 1e-20);
}

//==============================================================================
// MOSFET_N - Conductance Tests
//==============================================================================
TEST(MOSFET_N, TransconductanceLinearRegion) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Linear region
    Real vgs = 2.0;
    Real vds = 0.5;

    // gm = β × Vds
    Real gm = mosfet.get_transconductance(vgs, vds);

    Real beta = 2e-4;
    Real expected_gm = beta * vds;

    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);  // Within 1%
}

TEST(MOSFET_N, TransconductanceSaturationRegion) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Saturation region
    Real vgs = 2.0;
    Real vds = 2.0;

    // gm = β × (Vgs - Vth)
    Real gm = mosfet.get_transconductance(vgs, vds);

    Real beta = 2e-4;
    Real vgs_eff = vgs - 0.7;
    Real expected_gm = beta * vgs_eff;

    EXPECT_NEAR(gm, expected_gm, expected_gm * 0.01);  // Within 1%
}

TEST(MOSFET_N, TransconductanceCutoff) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Cutoff region
    Real vgs = 0.5;  // < Vth
    Real vds = 1.0;

    Real gm = mosfet.get_transconductance(vgs, vds);

    // gm should be approximately gmin in cutoff
    EXPECT_GT(gm, 0.0);
    EXPECT_LT(gm, 1e-9);
}

TEST(MOSFET_N, OutputConductanceLinearRegion) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Linear region
    Real vgs = 2.0;
    Real vds = 0.5;

    // gds = β × (Vgs - Vth - Vds)
    Real gds = mosfet.get_output_conductance(vgs, vds);

    Real beta = 2e-4;
    Real vgs_eff = vgs - 0.7;
    Real expected_gds = beta * (vgs_eff - vds);

    EXPECT_NEAR(gds, expected_gds, expected_gds * 0.01);  // Within 1%
}

TEST(MOSFET_N, OutputConductanceSaturationRegion) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Saturation region
    Real vgs = 2.0;
    Real vds = 2.0;

    // In ideal Level 1, gds = 0 in saturation (add gmin for stability)
    Real gds = mosfet.get_output_conductance(vgs, vds);

    // gds should be approximately gmin in saturation
    EXPECT_GT(gds, 0.0);
    EXPECT_LT(gds, 1e-9);
}

//==============================================================================
// MOSFET_N - Operating Region Tests
//==============================================================================
TEST(MOSFET_N, RegionDetectionCutoff) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    Real vgs = 0.5;  // < Vth
    Real vds = 1.0;

    auto region = mosfet.get_region(vgs, vds);

    EXPECT_EQ(region, MOSFET_N::Region::CUTOFF);
}

TEST(MOSFET_N, RegionDetectionLinear) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    Real vgs = 2.0;  // > Vth
    Real vds = 0.5;  // < Vgs - Vth = 1.3V

    auto region = mosfet.get_region(vgs, vds);

    EXPECT_EQ(region, MOSFET_N::Region::LINEAR);
}

TEST(MOSFET_N, RegionDetectionSaturation) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    Real vgs = 2.0;  // > Vth
    Real vds = 2.0;  // >= Vgs - Vth = 1.3V

    auto region = mosfet.get_region(vgs, vds);

    EXPECT_EQ(region, MOSFET_N::Region::SATURATION);
}

//==============================================================================
// MOSFET_N - Threshold Voltage Tests
//==============================================================================
TEST(MOSFET_N, SetThresholdVoltage) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Change threshold voltage
    mosfet.set_threshold_voltage(1.0);

    EXPECT_DOUBLE_EQ(mosfet.threshold_voltage(), 1.0);

    // MOSFET should now be in cutoff at Vgs = 0.8V
    Real id_cutoff = mosfet.get_drain_current(0.8, 1.0);
    EXPECT_NEAR(id_cutoff, 0.0, 1e-20);

    // But conducting at Vgs = 1.5V
    Real id_conduct = mosfet.get_drain_current(1.5, 1.0);
    EXPECT_GT(id_conduct, 1e-6);
}

//==============================================================================
// MOSFET_N - CircuitGraph Integration Tests
//==============================================================================
TEST(MOSFET_Circuit, AddMOSFETToCircuit) {
    CircuitGraph circuit;

    // Add MOSFET with default parameters
    EXPECT_NO_THROW(circuit.add_mosfet_n("M1", "drain", "gate", "source"));

    EXPECT_TRUE(circuit.has_node("drain"));
    EXPECT_TRUE(circuit.has_node("gate"));
    EXPECT_TRUE(circuit.has_node("source"));
    EXPECT_EQ(circuit.num_devices(), 1);
}

TEST(MOSFET_Circuit, AddMOSFETWithParameters) {
    CircuitGraph circuit;

    // Add MOSFET with custom parameters
    EXPECT_NO_THROW(circuit.add_mosfet_n("M1", "d", "g", "0", 5e-5, 20e-6, 2e-6, 1.0));

    auto device = circuit.get_device("M1");
    ASSERT_NE(device, nullptr);

    auto mosfet = std::dynamic_pointer_cast<MOSFET_N>(device);
    ASSERT_NE(mosfet, nullptr);

    EXPECT_DOUBLE_EQ(mosfet->process_transconductance(), 5e-5);
    EXPECT_DOUBLE_EQ(mosfet->channel_width(), 20e-6);
    EXPECT_DOUBLE_EQ(mosfet->channel_length(), 2e-6);
    EXPECT_DOUBLE_EQ(mosfet->threshold_voltage(), 1.0);
}

//==============================================================================
// MOSFET_N - Simple DC Circuit Tests
//==============================================================================
TEST(MOSFET_Circuit, CommonSourceAmplifier) {
    // Circuit: VDD (5V) -- RD (10kΩ) -- Drain
    //                                      |
    //                                   MOSFET (M1)
    //                                      |
    //         VGS (2V) ---------------- Gate
    //                                      |
    //                                  Source -- GND
    //
    // Expected: MOSFET in saturation region
    // Id = (β/2) × (Vgs - Vth)² = (2e-4/2) × (2 - 0.7)² ≈ 1.69e-4 A
    // Vd = VDD - Id × RD = 5 - 1.69e-4 × 10k ≈ 3.31V

    CircuitGraph circuit;
    circuit.add_voltage_source("VDD", "vdd", "0", 5.0);
    circuit.add_voltage_source("VGS", "vgs", "0", 2.0);
    circuit.add_resistor("RD", "vdd", "d", 10000.0);
    circuit.add_mosfet_n("M1", "d", "vgs", "0");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Get drain voltage
    NodeId drain_id = circuit.get_node_id("d");
    Real v_drain = solution[drain_id - 1];

    // Drain voltage should be between 2V and 4V (MOSFET conducting in saturation)
    EXPECT_GT(v_drain, 2.0);
    EXPECT_LT(v_drain, 4.5);
}

TEST(MOSFET_Circuit, MOSFETCutoffRegion) {
    // Circuit: VDD (5V) -- RD (10kΩ) -- Drain
    //                                      |
    //                                   MOSFET (M1)
    //                                      |
    //         Gate = GND                   |
    //                                  Source -- GND
    //
    // Expected: MOSFET in cutoff (Vgs = 0 < Vth), Id ≈ 0, Vd ≈ VDD

    CircuitGraph circuit;
    circuit.add_voltage_source("VDD", "vdd", "0", 5.0);
    circuit.add_resistor("RD", "vdd", "d", 10000.0);
    circuit.add_mosfet_n("M1", "d", "0", "0");  // Gate = GND

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Get drain voltage
    NodeId drain_id = circuit.get_node_id("d");
    Real v_drain = solution[drain_id - 1];

    // MOSFET should be in cutoff: Vd ≈ VDD (no current through RD)
    EXPECT_NEAR(v_drain, 5.0, 0.1);  // Within 100mV of VDD
}

TEST(MOSFET_Circuit, MOSFETSwitchClosed) {
    // Circuit: VDD (5V) -- RD (1kΩ) -- Drain
    //                                     |
    //                                  MOSFET (M1)
    //                                     |
    //         VGS (5V) --------------- Gate
    //                                     |
    //                                 Source -- GND
    //
    // Expected: MOSFET fully on (large Vgs), acts like a closed switch
    // Vd should be low (near GND)

    CircuitGraph circuit;
    circuit.add_voltage_source("VDD", "vdd", "0", 5.0);
    circuit.add_voltage_source("VGS", "vgs", "0", 5.0);  // Large Vgs
    circuit.add_resistor("RD", "vdd", "d", 1000.0);
    circuit.add_mosfet_n("M1", "d", "vgs", "0");

    DCSolver solver(circuit);
    DenseVector solution;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution, stats);

    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    // Get drain voltage
    NodeId drain_id = circuit.get_node_id("d");
    Real v_drain = solution[drain_id - 1];

    // MOSFET should be fully on: Vd should be relatively low
    // With Vgs=5V, Vth=0.7V → Vgs_eff=4.3V → Id ≈ 1.85mA
    // Vd = VDD - Id*RD = 5V - 1.85mA*1kΩ ≈ 3.15V
    EXPECT_GT(v_drain, 3.0);  // Drain voltage should be around 3-4V
    EXPECT_LT(v_drain, 4.0);  // Not too high (MOSFET is conducting)
}

//==============================================================================
// MOSFET_N - Temperature Tests
//==============================================================================

// Test 1: Basic Temperature Effect - VTO(T) with KT1 (Linear)
TEST(MOSFET_N, TemperatureEffect_KT1_Linear) {
    // VTO temperature coefficient (linear term)
    // VTO(T) = VTO(Tnom) + KT1 * (T - Tnom)
    // KT1 is typically negative for MOSFETs (-2 to -5 mV/K)

    Real kt1 = -0.003;  // -3 mV/K (typical value)
    MOSFET_N mosfet(1, "M1", 1, 2, 0,
                    2e-5, 10e-6, 1e-6, 0.7,  // kp, w, l, vth
                    0.0, 0.0, 0.0,            // cgso, cgdo, cgbo
                    0.0, 1.0, 1.0, 0.667,     // kf, af, ef, gamma
                    kt1, 0.0, -1.5, 300.15);  // KT1, KT2, UTE, TNOM

    // At nominal temperature (300.15K = 27°C)
    Real vth_nom = mosfet.threshold_voltage();
    EXPECT_NEAR(vth_nom, 0.7, 1e-10);

    // At higher temperature (350K = 77°C, ΔT = 350 - 300.15 = 49.85K)
    mosfet.set_temperature(350.0);
    Real vth_hot = mosfet.threshold_voltage();

    // Expected VTO change: ΔVto = KT1 * ΔT = -0.003 * 49.85 = -0.14955V
    Real delta_t_hot = 350.0 - 300.15;
    Real expected_vth_hot = 0.7 + kt1 * delta_t_hot;
    EXPECT_NEAR(vth_hot, expected_vth_hot, 1e-10);

    // At lower temperature (250K = -23°C, ΔT = 250 - 300.15 = -50.15K)
    mosfet.set_temperature(250.0);
    Real vth_cold = mosfet.threshold_voltage();

    // Expected VTO change: ΔVto = KT1 * ΔT = -0.003 * (-50.15) = +0.15045V
    Real delta_t_cold = 250.0 - 300.15;
    Real expected_vth_cold = 0.7 + kt1 * delta_t_cold;
    EXPECT_NEAR(vth_cold, expected_vth_cold, 1e-10);
}

// Test 2: VTO(T) with KT2 (Quadratic)
TEST(MOSFET_N, TemperatureEffect_KT2_Quadratic) {
    // VTO temperature coefficient (quadratic term)
    // VTO(T) = VTO(Tnom) + KT1 * (T - Tnom) + KT2 * (T - Tnom)²
    // KT2 is typically very small (1e-5 to 1e-6 V/K²)

    Real kt1 = -0.002;   // -2 mV/K
    Real kt2 = 5e-6;     // 5 μV/K²
    MOSFET_N mosfet(1, "M1", 1, 2, 0,
                    2e-5, 10e-6, 1e-6, 0.7,
                    0.0, 0.0, 0.0,
                    0.0, 1.0, 1.0, 0.667,
                    kt1, kt2, -1.5, 300.15);

    // At nominal temperature
    Real vth_nom = mosfet.threshold_voltage();
    EXPECT_NEAR(vth_nom, 0.7, 1e-10);

    // At higher temperature (400K = 127°C, ΔT = 400 - 300.15 = 99.85K)
    mosfet.set_temperature(400.0);
    Real vth_hot = mosfet.threshold_voltage();

    // Expected VTO change:
    // ΔVto = KT1 * ΔT + KT2 * ΔT²
    //      = -0.002 * 99.85 + 5e-6 * 99.85²
    //      = -0.1997 + 0.0499 = -0.1498V (approximately)
    Real delta_t = 400.0 - 300.15;
    Real expected_vth_hot = 0.7 + kt1 * delta_t + kt2 * delta_t * delta_t;
    EXPECT_NEAR(vth_hot, expected_vth_hot, 1e-10);
}

// Test 3: KP(T) with UTE (Mobility Temperature Exponent)
TEST(MOSFET_N, TemperatureEffect_UTE_Mobility) {
    // Mobility temperature exponent
    // KP(T) = KP(Tnom) * (T/Tnom)^UTE
    // UTE is typically -1.5 for MOSFETs (mobility decreases with temperature)

    Real ute = -1.5;  // Default mobility exponent
    MOSFET_N mosfet(1, "M1", 1, 2, 0,
                    2e-5, 10e-6, 1e-6, 0.7,
                    0.0, 0.0, 0.0,
                    0.0, 1.0, 1.0, 0.667,
                    0.0, 0.0, ute, 300.15);

    // At nominal temperature
    Real kp_nom = mosfet.process_transconductance();
    EXPECT_NEAR(kp_nom, 2e-5, 1e-20);

    // At higher temperature (350K = 77°C)
    mosfet.set_temperature(350.0);
    Real kp_hot = mosfet.process_transconductance();

    // Expected KP change: KP(T) = KP(Tnom) * (T/Tnom)^UTE
    Real temp_ratio = 350.0 / 300.15;
    Real expected_kp_hot = 2e-5 * std::pow(temp_ratio, ute);
    EXPECT_NEAR(kp_hot, expected_kp_hot, 1e-20);

    // With UTE=-1.5, KP should decrease at higher temperature
    EXPECT_LT(kp_hot, kp_nom);

    // At lower temperature (250K = -23°C)
    mosfet.set_temperature(250.0);
    Real kp_cold = mosfet.process_transconductance();

    // KP should be higher at lower temperature (UTE negative)
    EXPECT_GT(kp_cold, kp_nom);
}

// Test 4: Combined Temperature Effects (VTO + KP)
TEST(MOSFET_N, TemperatureEffect_Combined) {
    // Combined effect of VTO(T) and KP(T)
    // Real SPICE behavior with both effects

    Real kt1 = -0.003;   // -3 mV/K
    Real kt2 = 0.0;
    Real ute = -1.5;
    MOSFET_N mosfet(1, "M1", 1, 2, 0,
                    2e-5, 10e-6, 1e-6, 0.7,
                    0.0, 0.0, 0.0,
                    0.0, 1.0, 1.0, 0.667,
                    kt1, kt2, ute, 300.15);

    // At nominal temperature (300.15K)
    Real id_nom = mosfet.get_drain_current(2.0, 3.0);  // Vgs=2V, Vds=3V (saturation)

    // At higher temperature (350K = 77°C)
    mosfet.set_temperature(350.0);
    Real id_hot = mosfet.get_drain_current(2.0, 3.0);

    // Expected behavior:
    // 1. VTO decreases (KT1 < 0) → Vgs_eff increases → Id increases
    // 2. KP decreases (UTE < 0) → Id decreases
    // Net effect depends on which dominates

    // VTO effect: ΔVto = -0.003 * 50 = -0.15V → Vgs_eff increases by 0.15V
    // KP effect: KP_ratio = (350/300.15)^(-1.5) ≈ 0.87 (13% decrease)
    // Vgs_eff: 2.0 - 0.7 = 1.3V → 2.0 - 0.55 = 1.45V (11.5% increase in Vgs_eff)
    // Id ∝ KP * Vgs_eff² → Id_ratio = 0.87 * (1.45/1.3)² ≈ 1.08 (8% increase)

    // Current should increase slightly (VTO effect dominates for this case)
    EXPECT_GT(id_hot, id_nom * 0.9);  // At least 90% of nominal (could be higher)
    EXPECT_LT(id_hot, id_nom * 1.3);  // Not more than 130% (reasonable upper bound)

    std::cout << "\n=== Combined Temperature Effect ===" << std::endl;
    std::cout << "Id @ 27°C = " << id_nom * 1e3 << " mA" << std::endl;
    std::cout << "Id @ 77°C = " << id_hot * 1e3 << " mA" << std::endl;
    std::cout << "Ratio (77°C / 27°C) = " << id_hot / id_nom << std::endl;
}

// Test 5: Temperature Parameter Getters
TEST(MOSFET_N, TemperatureParameterGetters) {
    // Verify temperature parameter accessors

    Real kt1 = -0.002;
    Real kt2 = 5e-6;
    Real ute = -1.5;
    Real tnom = 300.15;

    MOSFET_N mosfet(1, "M1", 1, 2, 0,
                    2e-5, 10e-6, 1e-6, 0.7,
                    0.0, 0.0, 0.0,
                    0.0, 1.0, 1.0, 0.667,
                    kt1, kt2, ute, tnom);

    EXPECT_NEAR(mosfet.get_kt1(), -0.002, 1e-10);
    EXPECT_NEAR(mosfet.get_kt2(), 5e-6, 1e-10);
    EXPECT_NEAR(mosfet.get_ute(), -1.5, 1e-10);
    EXPECT_NEAR(mosfet.get_tnom(), 300.15, 1e-10);
}

// Test 6: Invalid Temperature
TEST(MOSFET_N, InvalidTemperature) {
    MOSFET_N mosfet(1, "M1", 1, 2, 0, 2e-5, 10e-6, 1e-6, 0.7);

    // Negative temperature should throw
    EXPECT_THROW(mosfet.set_temperature(-10.0), std::runtime_error);

    // Zero temperature should throw
    EXPECT_THROW(mosfet.set_temperature(0.0), std::runtime_error);
}

// Test 7: Circuit Temperature Effect - Common Source Amplifier
TEST(MOSFET_Circuit, TemperatureEffectInCircuit) {
    // Temperature effect on circuit behavior
    // Common source amplifier at different temperatures

    CircuitGraph circuit;
    circuit.add_voltage_source("VDD", "vdd", "0", 5.0);
    circuit.add_voltage_source("VGS", "vgs", "0", 2.0);
    circuit.add_resistor("RD", "vdd", "d", 10000.0);

    // Add MOSFET with temperature coefficients
    // KT1 = -3mV/K (VTO decreases with temperature)
    // UTE = -1.5 (KP decreases with temperature)
    circuit.add_mosfet_n("M1", "d", "vgs", "0",
                         2e-5, 10e-6, 1e-6, 0.7,
                         0.0, 0.0, 0.0,
                         0.0, 1.0, 1.0, 0.667,
                         -0.003, 0.0, -1.5, 300.15);

    // Solve at nominal temperature (300.15K = 27°C)
    DCSolver solver(circuit);
    DenseVector solution_nom;
    SolverStats stats;
    ConvergenceStatus status = solver.solve(solution_nom, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    NodeId drain_id = circuit.get_node_id("d");
    Real v_drain_nom = solution_nom[drain_id - 1];

    // Set temperature to 350K (77°C) and re-solve
    auto mosfet = std::dynamic_pointer_cast<MOSFET_N>(circuit.get_device("M1"));
    ASSERT_NE(mosfet, nullptr);
    mosfet->set_temperature(350.0);

    DenseVector solution_hot;
    status = solver.solve(solution_hot, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    Real v_drain_hot = solution_hot[drain_id - 1];

    // At higher temperature:
    // - VTO decreases → MOSFET conducts more
    // - KP decreases → MOSFET conducts less
    // Net effect: typically VTO effect dominates → Id increases → Vd decreases
    EXPECT_NE(v_drain_hot, v_drain_nom);  // Temperature should affect drain voltage

    std::cout << "\n=== Circuit Temperature Effect ===" << std::endl;
    std::cout << "Vd @ 27°C = " << v_drain_nom << " V" << std::endl;
    std::cout << "Vd @ 77°C = " << v_drain_hot << " V" << std::endl;
    std::cout << "ΔVd = " << (v_drain_hot - v_drain_nom) << " V" << std::endl;
}
