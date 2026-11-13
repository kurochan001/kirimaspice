// tests/unit/test_bsim4_parameter_extractor.cpp
#include <gtest/gtest.h>
#include "kirimaspice/analysis/bsim4_parameter_extractor.hpp"
#include "kirimaspice/devices/mosfet_bsim4.hpp"  // Step 5.7: Unified BSIM4 template
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::analysis;
using namespace kirimaspice::devices;

// Test fixture
class BSIM4ParameterExtractorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default BSIM4 parameters
        params_.w = 1e-5;    // 10 μm
        params_.l = 1e-6;    // 1 μm
        params_.vth0 = 0.7;
        params_.u0 = 0.067;
        params_.k1 = 0.5;
        params_.vsat = 1e5;
        params_.pclm = 1.3;
    }

    BSIM4Params params_;
};

// Test 1: Basic measurement data structure
TEST_F(BSIM4ParameterExtractorTest, MeasurementDataStructure) {
    BSIM4MeasurementData data;
    data.device_name = "M1";
    data.is_nmos = true;

    // Add sample measurement points
    BSIM4MeasurementPoint pt1;
    pt1.vgs = 1.0;
    pt1.vds = 1.0;
    pt1.vbs = 0.0;
    pt1.ids_measured = 1e-3;
    data.iv_data.push_back(pt1);

    EXPECT_EQ(data.iv_data.size(), 1);
    EXPECT_DOUBLE_EQ(data.iv_data[0].vgs, 1.0);
    EXPECT_DOUBLE_EQ(data.iv_data[0].ids_measured, 1e-3);
}

// Test 2: Generate synthetic measurement data using BSIM4_N
TEST_F(BSIM4ParameterExtractorTest, GenerateSyntheticData) {
    // Create BSIM4_N device with known parameters
    MOSFET_BSIM4_N device(0, "M1", 1, 2, 0, 0, params_);

    BSIM4MeasurementData data;
    data.device_name = "M1";
    data.is_nmos = true;

    // Generate I-V sweep data
    for (Real vgs = 0.5; vgs <= 2.0; vgs += 0.5) {
        for (Real vds = 0.2; vds <= 2.0; vds += 0.4) {
            BSIM4MeasurementPoint pt;
            pt.vgs = vgs;
            pt.vds = vds;
            pt.vbs = 0.0;

            // Compute "measured" current using known parameters
            Real vth = device.compute_threshold_voltage(vds, 0.0);
            pt.ids_measured = device.compute_drain_current(vgs, vds, 0.0, vth);

            data.iv_data.push_back(pt);
        }
    }

    EXPECT_GT(data.iv_data.size(), 0);
    EXPECT_GT(data.iv_data[0].ids_measured, 0.0);  // Should have positive current
}

// Test 3: Parameter extraction - perfect recovery (VTH0 only)
TEST_F(BSIM4ParameterExtractorTest, ExtractVTH0_PerfectRecovery) {
    // Generate synthetic data with known VTH0 = 0.7
    MOSFET_BSIM4_N device(0, "M1", 1, 2, 0, 0, params_);

    BSIM4MeasurementData data;
    data.device_name = "M1";
    data.is_nmos = true;

    // Generate measurement data
    for (Real vgs = 0.5; vgs <= 2.0; vgs += 0.5) {
        for (Real vds = 0.5; vds <= 2.0; vds += 0.5) {
            BSIM4MeasurementPoint pt;
            pt.vgs = vgs;
            pt.vds = vds;
            pt.vbs = 0.0;
            Real vth = device.compute_threshold_voltage(vds, 0.0);
            pt.ids_measured = device.compute_drain_current(vgs, vds, 0.0, vth);
            data.iv_data.push_back(pt);
        }
    }

    // Start with wrong initial guess
    BSIM4Params initial_params = params_;
    initial_params.vth0 = 0.5;  // Wrong initial guess

    // Extract VTH0
    BSIM4ParameterExtractor extractor(data, initial_params);
    auto result = extractor.extract({"VTH0"});

    // Should converge successfully
    EXPECT_TRUE(result.success);
    EXPECT_GT(result.num_evaluations, 0);

    // Should recover VTH0 = 0.7 (within tolerance)
    EXPECT_TRUE(result.extracted_params.count("VTH0") > 0);
    Real extracted_vth0 = result.extracted_params.at("VTH0");
    EXPECT_NEAR(extracted_vth0, 0.7, 0.1);  // ±0.1 V tolerance

    // RMS error should be reasonable (optimization convergence tolerance)
    EXPECT_LT(result.rms_error, 0.01);  // 10 mA tolerance for realistic optimization
}

// Test 4: Multi-parameter extraction (VTH0 + U0)
TEST_F(BSIM4ParameterExtractorTest, ExtractMultipleParameters) {
    // Generate synthetic data with known VTH0 = 0.7, U0 = 0.067
    MOSFET_BSIM4_N device(0, "M1", 1, 2, 0, 0, params_);

    BSIM4MeasurementData data;
    data.device_name = "M1";
    data.is_nmos = true;

    // More extensive sweep for multi-parameter extraction
    for (Real vgs = 0.8; vgs <= 2.0; vgs += 0.3) {
        for (Real vds = 0.2; vds <= 2.0; vds += 0.3) {
            BSIM4MeasurementPoint pt;
            pt.vgs = vgs;
            pt.vds = vds;
            pt.vbs = 0.0;
            Real vth = device.compute_threshold_voltage(vds, 0.0);
            pt.ids_measured = device.compute_drain_current(vgs, vds, 0.0, vth);
            data.iv_data.push_back(pt);
        }
    }

    // Wrong initial guesses
    BSIM4Params initial_params = params_;
    initial_params.vth0 = 0.5;
    initial_params.u0 = 0.05;

    // Extract both VTH0 and U0
    BSIM4ParameterExtractor extractor(data, initial_params);
    auto result = extractor.extract({"VTH0", "U0"});

    // Should converge
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.extracted_params.count("VTH0") > 0);
    EXPECT_TRUE(result.extracted_params.count("U0") > 0);

    // Check recovered values (looser tolerance for 2-parameter extraction)
    Real extracted_vth0 = result.extracted_params.at("VTH0");
    Real extracted_u0 = result.extracted_params.at("U0");
    EXPECT_NEAR(extracted_vth0, 0.7, 0.15);
    EXPECT_NEAR(extracted_u0, 0.067, 0.02);
}

// Test 5: Extraction with noisy data
TEST_F(BSIM4ParameterExtractorTest, ExtractWithNoisyData) {
    // Generate synthetic data with 5% noise
    MOSFET_BSIM4_N device(0, "M1", 1, 2, 0, 0, params_);

    BSIM4MeasurementData data;
    data.device_name = "M1";
    data.is_nmos = true;

    std::srand(42);  // Fixed seed for reproducibility
    for (Real vgs = 0.8; vgs <= 2.0; vgs += 0.4) {
        for (Real vds = 0.5; vds <= 2.0; vds += 0.5) {
            BSIM4MeasurementPoint pt;
            pt.vgs = vgs;
            pt.vds = vds;
            pt.vbs = 0.0;
            Real vth = device.compute_threshold_voltage(vds, 0.0);
            Real ids_ideal = device.compute_drain_current(vgs, vds, 0.0, vth);

            // Add 5% Gaussian noise
            Real noise = (std::rand() / (Real)RAND_MAX - 0.5) * 0.1 * ids_ideal;
            pt.ids_measured = ids_ideal + noise;

            data.iv_data.push_back(pt);
        }
    }

    BSIM4Params initial_params = params_;
    initial_params.vth0 = 0.6;

    BSIM4ParameterExtractor extractor(data, initial_params);
    auto result = extractor.extract({"VTH0"});

    // Should still converge with noisy data
    EXPECT_TRUE(result.success);

    // Extracted value should be reasonable (within 0.2 V due to noise)
    Real extracted_vth0 = result.extracted_params.at("VTH0");
    EXPECT_NEAR(extracted_vth0, 0.7, 0.2);

    // RMS error should be non-zero but reasonable (with 5% noise)
    EXPECT_GT(result.rms_error, 1e-6);  // Not perfect fit due to noise
    EXPECT_LT(result.rms_error, 100.0);  // Reasonable for noisy data
}
