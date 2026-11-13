// tests/unit/test_regression_2025_11_29.cpp
// Regression tests for fixes verified on 2025-11-29
//
// Issue: an earlier investigation note made incorrect claims:
// 1. "BSIM4 set_temperature doesn't recalculate temperature-dependent params" - FALSE
// 2. "Noise adjoint transfer function returns constant 1.0" - FALSE
//
// These tests verify the correct behavior to prevent regression.

#include <gtest/gtest.h>
#include "kirimaspice/devices/mosfet_bsim4.hpp"  // Step 5.7: Unified BSIM4 template
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include "kirimaspice/solvers/noise_solver.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::devices;
using namespace kirimaspice::parser;
using namespace kirimaspice::solvers;

// ============================================================================
// BSIM4 Temperature Recalculation Tests
// ============================================================================
// Verifies that set_temperature() properly propagates to compute_derived_params()
// Chain: MOSFET_BSIM4_N::set_temperature()
//     -> BSIM4Core::set_temperature()
//     -> BSIM4Params::compute_derived_params()

class BSIM4TemperatureRecalculationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Standard 180nm parameters
        params_.w = 10e-6;
        params_.l = 180e-9;
        params_.toxe = 4.5e-9;
        params_.vth0 = 0.4;
        params_.k1 = 0.5;
        params_.k2 = 0.01;
        params_.phi = 0.7;
        params_.u0 = 0.05;
        params_.vsat = 1e5;
        params_.kt1 = -0.11;  // Temperature coefficient
        params_.kt2 = 0.022;
        params_.ute = -1.5;
        params_.tnom = 300.15;
    }

    BSIM4Params params_;
};

// Test 1: Verify MOSFET_BSIM4_N::set_temperature propagates to core
// (Tests the full chain: MOSFET_BSIM4_N -> BSIM4Core -> BSIM4Params)
TEST_F(BSIM4TemperatureRecalculationTest, DeviceSetTemperaturePropagates) {
    params_.compute_derived_params(300.15);
    MOSFET_BSIM4_N device(0, "M1", 1, 2, 0, 0, params_);

    // Get Ids at room temperature
    Real vgs = 1.0, vds = 1.0, vbs = 0.0;
    Real vth1 = device.compute_threshold_voltage(vds, vbs);
    Real ids1 = device.compute_drain_current(vgs, vds, vbs, vth1);

    // Change temperature via device interface
    device.set_temperature(400.0);

    // Get Ids at elevated temperature
    Real vth2 = device.compute_threshold_voltage(vds, vbs);
    Real ids2 = device.compute_drain_current(vgs, vds, vbs, vth2);

    // Both Vth and Ids should change
    EXPECT_NE(vth1, vth2) << "Vth should change after device.set_temperature()";
    EXPECT_NE(ids1, ids2) << "Ids should change after device.set_temperature()";

    // Ids typically increases with temperature in weak inversion (due to Vth decrease)
    // but decreases in strong inversion (due to mobility degradation)
    // Just verify it changes, direction depends on operating region
}

// Test 2: Verify P-channel also works
TEST_F(BSIM4TemperatureRecalculationTest, PChannelTemperatureRecalculation) {
    params_.compute_derived_params(300.15);
    MOSFET_BSIM4_P device(0, "M1", 1, 2, 0, 0, params_);

    Real vsg = 1.0, vsd = 1.0, vsb = 0.0;
    Real vth1 = device.compute_threshold_voltage(vsd, vsb);

    device.set_temperature(400.0);

    Real vth2 = device.compute_threshold_voltage(vsd, vsb);

    EXPECT_NE(vth1, vth2) << "P-channel Vth should change after set_temperature()";
}

// ============================================================================
// Noise Adjoint Transfer Function Tests
// ============================================================================
// Verifies that calculate_transfer_function() returns proper values, not 1.0

class NoiseAdjointTransferFunctionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Simple RC lowpass filter
        // Transfer function from input to output varies with frequency
        netlist_ = R"(Noise Adjoint Regression Test
Vin 1 0 DC 0 AC 1.0
R1 1 2 1k
C1 2 0 1n
.ac dec 10 1k 10Meg
.end
)";
    }

    std::string netlist_;
};

// Test 1: Transfer function should NOT be constant across frequencies
TEST_F(NoiseAdjointTransferFunctionTest, TransferFunctionVariesWithFrequency) {
    NetlistParser parser;
    auto result = parser.parse_string(netlist_);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    ASSERT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(noise_result.frequency_points.size(), 1);

    // For RC lowpass, output noise should decrease at higher frequencies
    // because the transfer function |H(f)| = 1/sqrt(1 + (f/fc)²) decreases
    // fc = 1/(2*pi*R*C) = 1/(2*pi*1k*1n) ≈ 159 kHz

    Real first_noise = noise_result.output_noise.front();
    Real last_noise = noise_result.output_noise.back();

    // Output noise should be different at different frequencies
    // (This proves transfer function is NOT returning constant 1.0)
    EXPECT_NE(first_noise, last_noise)
        << "Output noise should vary with frequency (transfer function NOT constant)";

    // For RC lowpass, high frequency noise should be lower
    // (1 kHz is below fc, 10 MHz is above fc)
    EXPECT_GT(first_noise, last_noise)
        << "Lowpass filter should attenuate high frequency noise";
}

// Test 2: Transfer function for different node pairs should differ
TEST_F(NoiseAdjointTransferFunctionTest, DifferentNodesGiveDifferentTransfer) {
    // Two-stage RC filter
    std::string netlist2 = R"(Two Stage RC Filter
Vin 1 0 DC 0 AC 1.0
R1 1 2 1k
C1 2 0 1n
R2 2 3 1k
C2 3 0 1n
.ac dec 10 1k 10Meg
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist2);
    auto& circuit = result.circuit;

    NodeId input_node = circuit->get_node_id("1");
    NodeId mid_node = circuit->get_node_id("2");
    NodeId output_node = circuit->get_node_id("3");

    // Measure noise at middle node
    NoiseSolver solver1(*circuit, result.config);
    NoiseResult result1 = solver1.solve(input_node, mid_node);

    // Measure noise at output node
    NoiseSolver solver2(*circuit, result.config);
    NoiseResult result2 = solver2.solve(input_node, output_node);

    ASSERT_EQ(result1.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result2.status, ConvergenceStatus::SUCCESS);

    // Noise at output (2-stage) should be lower than at middle (1-stage)
    // because of additional filtering
    Real mid_noise_1k = result1.output_noise.front();
    Real out_noise_1k = result2.output_noise.front();

    // At high frequency, 2-stage filter has more attenuation
    Real mid_noise_10M = result1.output_noise.back();
    Real out_noise_10M = result2.output_noise.back();

    // The noise values should be different (proves node-dependent transfer)
    // Note: At low frequency both might be similar, but at high frequency should differ
    EXPECT_NE(mid_noise_10M, out_noise_10M)
        << "Noise at different nodes should differ (node-dependent transfer function)";
}

// Test 3: Verify resistor contribution uses proper transfer function
TEST_F(NoiseAdjointTransferFunctionTest, ResistorNoiseUsesTransferFunction) {
    NetlistParser parser;
    auto result = parser.parse_string(netlist_);
    auto& circuit = result.circuit;

    NoiseSolver noise_solver(*circuit, result.config);
    NodeId input_node = circuit->get_node_id("1");
    NodeId output_node = circuit->get_node_id("2");

    NoiseResult noise_result = noise_solver.solve(input_node, output_node);

    ASSERT_EQ(noise_result.status, ConvergenceStatus::SUCCESS);

    // Check that R1 thermal noise contribution exists
    auto it = noise_result.contributions.find("R1_thermal");
    ASSERT_TRUE(it != noise_result.contributions.end())
        << "R1 thermal noise should be tracked";

    // The contribution should vary with frequency (not constant)
    const auto& r1_contribution = it->second;
    ASSERT_GT(r1_contribution.size(), 1);

    Real r1_noise_low = r1_contribution.front();
    Real r1_noise_high = r1_contribution.back();

    // R1 noise contribution should change with frequency
    // (due to changing transfer function through the RC filter)
    EXPECT_NE(r1_noise_low, r1_noise_high)
        << "R1 noise contribution should vary with frequency";
}
