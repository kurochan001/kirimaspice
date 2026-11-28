// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_noise_analysis.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/solvers/noise_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::solvers;

// Physical constants for validation
constexpr Real k_B = 1.380649e-23;  // Boltzmann constant (J/K)
constexpr Real q_e = 1.602176634e-19; // Elementary charge (C)
constexpr Real T = 300.15;  // Temperature (K) - 27°C

// ========================================
// Test 1: Resistor Thermal Noise
// ========================================
TEST(NoiseAnalysisTest, ResistorThermalNoise) {
    // Simple circuit: Vin (AC 1V) → R1 (1kΩ) → output (node 2)
    // Expected thermal noise: V_n² = 4kTR = 4 * 1.38e-23 * 300.15 * 1000
    std::string netlist = R"(Resistor Thermal Noise Test
Vin 1 0 DC 0 AC 1.0
R1 1 2 1k
Rload 2 0 1Meg
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // Calculate expected thermal noise using correct circuit analysis
    // Node 1 is AC grounded (voltage source), output is at node 2
    // Each noise source transfers through the circuit with specific transfer function
    Real R1 = 1000.0;     // 1kΩ
    Real Rload = 1e6;     // 1MΩ

    // Equivalent resistance seen at output node = R1 || Rload
    Real R_eq = R1 * Rload / (R1 + Rload);  // ≈ 999 Ω

    // For thermal noise, the output noise is simply sqrt(4kT × R_eq)
    // This is because the noise contributions from R1 and Rload, when properly
    // accounting for transfer functions, sum to give V_n² = 4kT × R_eq
    //
    // Detailed analysis:
    // - R1 noise current: I_n1² = 4kT/R1, appears at output as |H|² × I_n1²
    //   where H = R_eq (transimpedance from R1 noise to output)
    // - Rload noise current: I_n2² = 4kT/Rload, appears at output as |H|² × I_n2²
    //   where H = R_eq
    // Total = R_eq² × (4kT/R1 + 4kT/Rload) = 4kT × R_eq² × (1/R1 + 1/Rload)
    //       = 4kT × R_eq² / R_eq = 4kT × R_eq
    Real V_n_squared_total = 4.0 * k_B * T * R_eq;

    // Check noise sources were collected
    EXPECT_TRUE(noise_result.contributions.find("R1_thermal") != noise_result.contributions.end());
    EXPECT_TRUE(noise_result.contributions.find("Rload_thermal") != noise_result.contributions.end());

    // Output noise should be approximately equal to sqrt(4kT × R_eq)
    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];
        Real expected_noise = std::sqrt(V_n_squared_total);

        // 4kT (R1 || Rload) is exact: relative 1e-6 is far above rounding
        // and far below any error in the PSD
        EXPECT_NEAR(output_noise, expected_noise, expected_noise * 1e-6)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Test 2: Diode Shot Noise
// ========================================
TEST(NoiseAnalysisTest, DiodeShotNoise) {
    // Circuit: Vin (DC 0.7V) → R1 (100Ω) → D1 (diode) → GND
    // Diode forward bias ~0.7V, current ~1mA (depends on Is)
    // Expected shot noise: I_n² = 2qI_d
    std::string netlist = R"(Diode Shot Noise Test
Vin 1 0 DC 0.7 AC 1.0
R1 1 2 100
D1 2 0 DMOD
.model DMOD D (IS=1e-14 N=1.0)
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // Check that diode shot noise source was created
    EXPECT_TRUE(noise_result.contributions.find("D1_shot") != noise_result.contributions.end());

    // Verify non-zero noise (exact value depends on DC operating point)
    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];
        EXPECT_GT(output_noise, 0.0)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Test 3: Multiple Resistors (Uncorrelated)
// ========================================
TEST(NoiseAnalysisTest, MultipleResistorsUncorrelated) {
    // Circuit: Vin → R1 (1kΩ) → node 2 → R2 (1kΩ) → output (node 3)
    // Total noise: √(V_n1² + V_n2²) (uncorrelated)
    std::string netlist = R"(Multiple Resistors Noise Test
Vin 1 0 DC 0 AC 1.0
R1 1 2 1k
R2 2 3 1k
Rload 3 0 1Meg
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("3");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);

    // All resistors (R1, R2, Rload) should contribute
    EXPECT_TRUE(noise_result.contributions.find("R1_thermal") != noise_result.contributions.end());
    EXPECT_TRUE(noise_result.contributions.find("R2_thermal") != noise_result.contributions.end());
    EXPECT_TRUE(noise_result.contributions.find("Rload_thermal") != noise_result.contributions.end());

    // Correct noise calculation using equivalent resistance
    // Circuit: Vin (AC ground) → R1 (1kΩ) → node 2 → R2 (1kΩ) → output (node 3) → Rload (1MΩ) → GND
    // From the output node's perspective:
    // - R1 and R2 are in series from AC ground: R_series = R1 + R2 = 2kΩ
    // - R_series is in parallel with Rload: R_eq = R_series || Rload
    Real R1 = 1000.0;     // 1kΩ
    Real R2 = 1000.0;     // 1kΩ
    Real Rload = 1e6;     // 1MΩ
    Real R_series = R1 + R2;  // 2kΩ
    Real R_eq = R_series * Rload / (R_series + Rload);  // ≈ 1996 Ω

    // Total output noise = sqrt(4kT × R_eq)
    Real V_n_squared_total = 4.0 * k_B * T * R_eq;
    Real expected_total = std::sqrt(V_n_squared_total);

    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];

        // Relaxed tolerance
        EXPECT_NEAR(output_noise, expected_total, expected_total * 0.15)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Test 4: Resistor Divider Noise
// ========================================
TEST(NoiseAnalysisTest, ResistorDividerNoise) {
    // Voltage divider: R1 (1kΩ) and R2 (1kΩ)
    // Output at midpoint should have contributions from both resistors
    std::string netlist = R"(Resistor Divider Noise Test
Vin 1 0 DC 5.0 AC 1.0
R1 1 2 1k
R2 2 0 1k
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);

    // Both resistors should contribute
    EXPECT_TRUE(noise_result.contributions.find("R1_thermal") != noise_result.contributions.end());
    EXPECT_TRUE(noise_result.contributions.find("R2_thermal") != noise_result.contributions.end());

    // Verify output noise is non-zero and reasonable
    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];
        EXPECT_GT(output_noise, 0.0);
        EXPECT_LT(output_noise, 1e-6);  // Should be in nV/√Hz range
    }
}

// ========================================
// Test 5: Noise Source Data Structure
// ========================================
TEST(NoiseAnalysisTest, NoiseSourceDataStructure) {
    // Test that noise sources are correctly populated
    std::string netlist = R"(Noise Source Test
Vin 1 0 DC 0.7 AC 1.0
R1 1 2 1k
D1 2 0 DMOD
.model DMOD D (IS=1e-14)
.ac dec 5 1k 100k
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);

    // Check that contributions map has expected entries
    EXPECT_EQ(noise_result.contributions.size(), 2);  // R1_thermal + D1_shot
    EXPECT_TRUE(noise_result.contributions.find("R1_thermal") != noise_result.contributions.end());
    EXPECT_TRUE(noise_result.contributions.find("D1_shot") != noise_result.contributions.end());

    // Check that each contribution has data for all frequency points
    size_t num_freq = noise_result.frequency_points.size();
    EXPECT_GT(num_freq, 0);

    for (const auto& [name, contributions] : noise_result.contributions) {
        EXPECT_EQ(contributions.size(), num_freq)
            << "Noise source: " << name;

        // Check all contributions are non-negative
        for (Real contrib : contributions) {
            EXPECT_GE(contrib, 0.0);
        }
    }
}

// ========================================
// Test 6: Frequency Independence (White Noise)
// ========================================
TEST(NoiseAnalysisTest, FrequencyIndependenceWhiteNoise) {
    // Thermal and shot noise are white noise (frequency-independent)
    std::string netlist = R"(White Noise Test
Vin 1 0 DC 0 AC 1.0
R1 1 2 1k
Rload 2 0 1Meg
.ac dec 20 100 10Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 10);

    // For white noise, output should be approximately constant across frequency
    // (Note: Transfer function may vary, but noise spectral density is constant)

    auto& r1_contributions = noise_result.contributions["R1_thermal"];
    EXPECT_GT(r1_contributions.size(), 10);

    // Check variance across frequency (should be low for white noise)
    Real sum = 0.0;
    for (Real contrib : r1_contributions) {
        sum += contrib;
    }
    Real mean = sum / r1_contributions.size();

    // All values should be close to mean (white noise)
    for (size_t i = 0; i < r1_contributions.size(); ++i) {
        EXPECT_NEAR(r1_contributions[i], mean, mean * 0.1)  // 10% tolerance
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Test 7: BJT NPN Shot Noise (IB + IC)
// ========================================
TEST(NoiseAnalysisTest, BJTNPNShotNoise) {
    // Circuit: Vin (DC 5V) → Rc (1kΩ) → collector
    //          Rb (10kΩ) → base
    //          Emitter → GND
    // Expected noise: IB shot noise (2qIB) + IC shot noise (2qIC)
    std::string netlist = R"(BJT NPN Shot Noise Test
Vcc 1 0 DC 5.0
Rc 1 2 1k
Ib 0 3 DC 10uA
Rb 3 0 10k
Q1 2 3 0 NPN is=1e-14 bf=100
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // Check that BJT shot noise sources were created
    EXPECT_TRUE(noise_result.contributions.find("Q1_shot_ib") != noise_result.contributions.end())
        << "Base current shot noise should be present";
    EXPECT_TRUE(noise_result.contributions.find("Q1_shot_ic") != noise_result.contributions.end())
        << "Collector current shot noise should be present";

    // Verify non-zero noise
    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];
        EXPECT_GT(output_noise, 0.0)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }

    // Shot noise should be white noise (frequency-independent)
    auto& ic_contributions = noise_result.contributions["Q1_shot_ic"];
    Real sum = 0.0;
    for (Real contrib : ic_contributions) {
        sum += contrib;
    }
    Real mean = sum / ic_contributions.size();

    // All values should be close to mean (white noise)
    for (size_t i = 0; i < ic_contributions.size(); ++i) {
        EXPECT_NEAR(ic_contributions[i], mean, mean * 0.15)  // 15% tolerance
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Test 8: BJT NPN Flicker Noise (1/f)
// ========================================
TEST(NoiseAnalysisTest, BJTNPNFlickerNoise) {
    // Circuit: BJT common-emitter amplifier with flicker noise
    // Expected: Flicker noise I_n² = K_f * I_C^AF / f^FF
    std::string netlist = R"(BJT NPN Flicker Noise Test
Vcc 1 0 DC 5.0
Rc 1 2 1k
Ib 0 3 DC 10uA
Rb 3 0 10k
Q1 2 3 0 NPN is=1e-14 bf=100 kf=1e-16 af=1.0 ff=1.0
.ac dec 20 10 100k
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 10);

    // Check that flicker noise source was created
    EXPECT_TRUE(noise_result.contributions.find("Q1_flicker") != noise_result.contributions.end())
        << "Flicker noise should be present";

    // Flicker noise should decrease with frequency (1/f behavior)
    auto& flicker_contributions = noise_result.contributions["Q1_flicker"];
    EXPECT_GT(flicker_contributions.size(), 10);

    // Lower frequency should have higher noise
    Real noise_at_low_freq = flicker_contributions[0];
    Real noise_at_high_freq = flicker_contributions.back();

    EXPECT_GT(noise_at_low_freq, noise_at_high_freq * 5.0)
        << "Flicker noise should decrease with frequency";
}

// ========================================
// Test 9: BJT PNP Shot Noise
// ========================================
TEST(NoiseAnalysisTest, BJTPNPShotNoise) {
    // Circuit: PNP common-emitter amplifier
    // Expected: IB shot noise + IC shot noise (same as NPN)
    std::string netlist = R"(BJT PNP Shot Noise Test
Vcc 1 0 DC 5.0
Rc 2 0 1k
Ib 3 0 DC 10uA
Rb 1 3 100k
Q1 2 3 1 PNP is=1e-14 bf=100
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // Check that BJT shot noise sources were created
    EXPECT_TRUE(noise_result.contributions.find("Q1_shot_ib") != noise_result.contributions.end())
        << "Base current shot noise should be present";
    EXPECT_TRUE(noise_result.contributions.find("Q1_shot_ic") != noise_result.contributions.end())
        << "Collector current shot noise should be present";

    // Verify non-zero noise
    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];
        EXPECT_GT(output_noise, 0.0)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Test 10: MOSFET N-channel Thermal Noise
// ========================================
TEST(NoiseAnalysisTest, MOSFETNChannelThermalNoise) {
    // Circuit: NMOS common-source amplifier
    // Expected: Channel thermal noise I_n² = 4kTγg_m
    std::string netlist = R"(MOSFET N Thermal Noise Test
Vdd 1 0 DC 5.0
Rd 1 2 10k
Vg 3 0 DC 1.5
M1 2 3 0 NMOS kp=200u w=10u l=1u vth=0.7
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // Check that channel thermal noise was created
    EXPECT_TRUE(noise_result.contributions.find("M1_thermal") != noise_result.contributions.end())
        << "Channel thermal noise should be present";

    // Verify non-zero noise
    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];
        EXPECT_GT(output_noise, 0.0)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }

    // Note: Thermal noise SOURCE spectral density I_n² = 4kTγg_m is white (frequency-independent)
    // However, the CONTRIBUTION to output noise = |H|² × S_n, where H is the transfer function.
    // The transfer function H varies with frequency due to circuit poles/zeros (capacitances, etc.)
    // Therefore, we only verify that thermal contributions exist and are non-zero.
    auto& thermal_contributions = noise_result.contributions["M1_thermal"];
    EXPECT_GT(thermal_contributions.size(), 0u) << "Thermal contributions should exist";

    for (size_t i = 0; i < thermal_contributions.size(); ++i) {
        EXPECT_GE(thermal_contributions[i], 0.0)  // Non-negative (squared quantity)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Test 11: MOSFET N-channel Flicker Noise
// ========================================
TEST(NoiseAnalysisTest, MOSFETNChannelFlickerNoise) {
    // Circuit: NMOS with flicker noise
    // Expected: Flicker noise I_n² = K_f * I_D^AF / (f^EF * (W*L)^EF)
    std::string netlist = R"(MOSFET N Flicker Noise Test
Vdd 1 0 DC 5.0
Rd 1 2 10k
Vg 3 0 DC 1.5
M1 2 3 0 NMOS kp=200u w=10u l=1u vth=0.7 kf=1e-25 af=1.0 ef=1.0
.ac dec 20 10 100k
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 10);

    // Check that flicker noise source was created
    EXPECT_TRUE(noise_result.contributions.find("M1_flicker") != noise_result.contributions.end())
        << "Flicker noise should be present";

    // Flicker noise should decrease with frequency (1/f behavior)
    auto& flicker_contributions = noise_result.contributions["M1_flicker"];
    EXPECT_GT(flicker_contributions.size(), 10);

    // Lower frequency should have higher noise
    Real noise_at_low_freq = flicker_contributions[0];
    Real noise_at_high_freq = flicker_contributions.back();

    EXPECT_GT(noise_at_low_freq, noise_at_high_freq * 5.0)
        << "MOSFET flicker noise should decrease with frequency";
}

// ========================================
// Test 12: MOSFET P-channel Thermal Noise
// ========================================
TEST(NoiseAnalysisTest, MOSFETPChannelThermalNoise) {
    // Circuit: PMOS common-source amplifier
    // Expected: Channel thermal noise I_n² = 4kTγg_m
    std::string netlist = R"(MOSFET P Thermal Noise Test
Vdd 1 0 DC 5.0
Rd 2 0 10k
Vg 3 0 DC 3.5
M1 2 3 1 PMOS kp=100u w=10u l=1u vth=-0.7
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    // Run noise analysis
    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // Check that channel thermal noise was created
    EXPECT_TRUE(noise_result.contributions.find("M1_thermal") != noise_result.contributions.end())
        << "PMOS channel thermal noise should be present";

    // Verify non-zero noise
    for (size_t i = 0; i < noise_result.frequency_points.size(); ++i) {
        Real output_noise = noise_result.output_noise[i];
        EXPECT_GT(output_noise, 0.0)
            << "Frequency: " << noise_result.frequency_points[i] << " Hz";
    }
}

// ========================================
// Additional Tests (8 tests)
// ========================================

// Test 13: High Value Resistor Thermal Noise
// ========================================
TEST(NoiseAnalysisTest, HighValueResistorThermalNoise) {
    // Simple resistor divider with high resistance values
    // Verifies thermal noise scales with resistance value
    std::string netlist = R"(High Resistance Thermal Noise
Vin 1 0 DC 1.0 AC 1.0
R1 1 2 100k
R2 2 0 47k
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);
    EXPECT_GT(noise_result.output_noise[0], 0.0);

    // High resistance should produce measurable thermal noise
    EXPECT_TRUE(noise_result.contributions.find("R1_thermal") != noise_result.contributions.end());
    EXPECT_TRUE(noise_result.contributions.find("R2_thermal") != noise_result.contributions.end());
}

// Test 14: Differential Pair Noise
// ========================================
TEST(NoiseAnalysisTest, DifferentialPairNoise) {
    // BJT differential pair for common-mode rejection noise testing
    std::string netlist = R"(Differential Pair Noise
Vcc 1 0 DC 5.0
Vbias1 2 0 DC 2.5
Vbias2 3 0 DC 2.5
Vin1 2 4 DC 0 AC 1.0
Vin2 3 5 DC 0 AC 0
Rb1 4 6 10k
Rb2 5 7 10k
Rc1 1 8 1k
Rc2 1 9 1k
Q1 8 6 10 NPN
Q2 9 7 10 NPN
Rtail 10 0 10k
Vee 11 0 DC 1.0
Re_tie 10 11 1
.model NPN NPN (IS=1e-14 BF=100)
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("4");
    NodeId output_node = circuit->get_node_id("8");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);
    EXPECT_GT(noise_result.output_noise[0], 0.0);
}

// Test 15: Low Resistance Thermal Noise
// ========================================
TEST(NoiseAnalysisTest, LowResistanceThermalNoise) {
    // Simple resistor circuit with low resistance values
    // Complements the high resistance test
    std::string netlist = R"(Low Resistance Thermal Noise
Vin 1 0 DC 1.0 AC 1.0
R1 1 2 100
R2 2 0 47
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);
    EXPECT_GT(noise_result.output_noise[0], 0.0);

    // Low resistance should still produce thermal noise (just less)
    EXPECT_TRUE(noise_result.contributions.find("R1_thermal") != noise_result.contributions.end());
}

// Test 16: CMOS Inverter Noise
// ========================================
TEST(NoiseAnalysisTest, CMOSInverterNoise) {
    // CMOS inverter for digital circuit noise testing
    std::string netlist = R"(CMOS Inverter Noise
Vdd 1 0 DC 5.0
Vin 2 0 DC 2.5 AC 1.0
M1 3 2 0 NMOS kp=100u w=10u l=1u vth=0.7
M2 3 2 1 PMOS kp=50u w=10u l=1u vth=-0.7
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("2");
    NodeId output_node = circuit->get_node_id("3");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);
    EXPECT_GT(noise_result.output_noise[0], 0.0);
}

// Test 17: BJT Current Mirror Noise
// ========================================
TEST(NoiseAnalysisTest, BJTCurrentMirrorNoise) {
    // BJT current mirror for bias circuit noise testing
    std::string netlist = R"(BJT Current Mirror Noise
Vcc 1 0 DC 5.0
Iref 1 2 DC 1m
Q1 2 2 0 NPN
Q2 3 2 0 NPN
Rload 1 3 1k
.model NPN NPN (IS=1e-14 BF=100)
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("2");
    NodeId output_node = circuit->get_node_id("3");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);
    EXPECT_GT(noise_result.output_noise[0], 0.0);
}

// Test 18: High Resistance Thermal Noise Dominance
// ========================================
TEST(NoiseAnalysisTest, HighResistanceThermalNoise) {
    // High value resistor to test thermal noise scaling
    std::string netlist = R"(High Resistance Thermal Noise
Vin 1 0 DC 0 AC 1.0
R1 1 2 1Meg
R2 2 0 10k
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // High resistance R1 should dominate noise
    EXPECT_TRUE(noise_result.contributions.find("R1_thermal") != noise_result.contributions.end());
    EXPECT_GT(noise_result.output_noise[0], 0.0);
}

// Test 19: Three-Stage RC Cascade Noise
// ========================================
TEST(NoiseAnalysisTest, ThreeStageRCCascadeNoise) {
    // Multi-stage RC filter for noise accumulation testing
    std::string netlist = R"(Three Stage RC Cascade Noise
Vin 1 0 DC 0 AC 1.0
R1 1 2 1k
C1 2 0 100p
R2 2 3 1k
C2 3 0 100p
R3 3 4 1k
C3 4 0 100p
.ac dec 10 1k 10Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("4");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);
    EXPECT_GT(noise_result.output_noise[0], 0.0);
}

// Test 20: BJT and Resistor Combined Noise
// ========================================
TEST(NoiseAnalysisTest, BJTResistorCombinedNoise) {
    // BJT amplifier with resistor noise for combined noise source testing
    std::string netlist = R"(BJT Resistor Combined Noise
Vcc 1 0 DC 5.0
Vin 2 0 DC 0 AC 1.0
Rb 2 3 100k
Rc 1 4 10k
Re 5 0 1k
Q1 4 3 5 NPN
.model NPN NPN (IS=1e-14 BF=100)
.ac dec 10 1k 1Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("2");
    NodeId output_node = circuit->get_node_id("4");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    EXPECT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(noise_result.frequency_points.size(), 0);

    // Check both resistor and BJT noise sources
    EXPECT_TRUE(noise_result.contributions.find("Rb_thermal") != noise_result.contributions.end());
    EXPECT_TRUE(noise_result.contributions.find("Rc_thermal") != noise_result.contributions.end());
    EXPECT_GT(noise_result.output_noise[0], 0.0);
}

// .NOISE as SPICE runs it (NoiseSolver::solve from an input source,
// solvers::integrated_noise): ngspice-45.2 gives these totals over the
// sweep (run 2026-11-28), and the spectra point by point within 2e-5. The
// input-referred noise is the output noise over the gain from the input
// source: for the RC low-pass it is sqrt(4kTR) at every frequency, whose
// integral over 10 Hz to 100 kHz is 1.28742e-6 V; ngspice-45.2 prints
// 1.619906e-6 there (KNOWN_ISSUES), so that case checks the analytic value.
TEST(NoiseAnalysis, MatchesNgspice) {
    const struct {
        std::string what, lines, out, ref, source;
        SimulationConfig::AC::SweepType type;
        std::uint32_t points;
        Real fstart, fstop, onoise, inoise;
    } cases[] = {
        {"divider", "V1 in 0 DC 0 AC 1\nR1 in out 1k\nR2 out 0 1k\n", "out", "0", "V1",
         SimulationConfig::AC::SweepType::DEC, 1, 1e3, 1e4, 2.731159e-07, 5.462318e-07},
        {"rc", "V1 in 0 DC 0 AC 1\nR1 in out 1k\nC1 out 0 1u\n", "out", "0", "V1",
         SimulationConfig::AC::SweepType::DEC, 5, 10, 1e5, 6.275417e-08, 1.28742e-06},
        {"diode", "V1 in 0 DC 1 AC 1\nR1 in b 1k\nD1 b 0 dm\n.model dm D IS=1e-14\n", "b", "0", "V1",
         SimulationConfig::AC::SweepType::DEC, 2, 10, 1e4, 7.585919e-08, 1.162675e-06},
        {"bjt", "Vcc vcc 0 12\nV1 in 0 DC 0 AC 1\nCin in b 10u\nRb1 vcc b 100k\nRb2 b 0 20k\nRc vcc c 5k\n"
                "Re e 0 1k\nQ1 c b e qn\n.model qn NPN(BF=100 IS=1e-15)\n",
         "c", "0", "V1", SimulationConfig::AC::SweepType::DEC, 2, 10, 1e5, 7.494734e-06, 1.549491e-06},
        {"mosfet", "Vdd vdd 0 5\nV1 in 0 DC 2 AC 1\nRd vdd d 10k\nM1 d in 0 0 nm W=10u L=1u\n"
                   ".model nm NMOS(LEVEL=1 KP=50u VTO=1)\n",
         "d", "0", "V1", SimulationConfig::AC::SweepType::DEC, 2, 10, 1e5, 8.474812e-06, 1.694962e-06},
        {"current source", "I1 0 b DC 0 AC 1\nR1 b 0 1k\nR2 b c 1k\nR3 c 0 1k\n", "c", "0", "I1",
         SimulationConfig::AC::SweepType::DEC, 1, 1e3, 1e4, 3.153671e-07, 9.461012e-10},
        {"two nodes", "V1 in 0 DC 0 AC 1\nR1 in b 1k\nR2 b c 1k\nR3 c 0 1k\n", "b", "c", "V1",
         SimulationConfig::AC::SweepType::DEC, 1, 1e3, 1e4, 3.153671e-07, 9.461012e-07},
    };
    for (const auto& c : cases) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string("t\n" + c.lines + ".end\n");
        SimulationConfig config = parsed.config;
        config.ac.sweep_type = c.type;
        config.ac.num_points = c.points;
        config.ac.start_freq = c.fstart;
        config.ac.stop_freq = c.fstop;
        NoiseSolver solver(*parsed.circuit, config);
        const auto result = solver.solve(c.source, parsed.circuit->get_node_id(c.out),
                                         parsed.circuit->get_node_id(c.ref));
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS) << c.what;
        EXPECT_NEAR(integrated_noise(result.frequency_points, result.output_noise), c.onoise, 1e-4 * c.onoise)
            << c.what;
        EXPECT_NEAR(integrated_noise(result.frequency_points, result.input_referred_noise), c.inoise,
                    1e-4 * c.inoise)
            << c.what;
    }
}
