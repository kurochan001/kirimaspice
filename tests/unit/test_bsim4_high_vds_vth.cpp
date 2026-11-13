// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include <cmath>
#include "kirimaspice/devices/bsim4_core.hpp"
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"

using namespace kirimaspice::devices;
using Real = kirimaspice::Real;

class BSIM4HighVdsVthTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize BSIM4 parameters (matching typical 45nm process)
        params_.w = 10e-6;
        params_.l = 45e-9;
        params_.vth0 = 0.4;
        params_.k1 = 0.5;
        params_.k2 = 0.01;
        params_.dvt0 = 0.1;
        params_.dvt1 = 0.1;
        params_.eta0 = 0.01;
        params_.dsub = 0.56;
        params_.kt1 = -0.11;
        params_.kt2 = 0.022;
        params_.ute = -1.5;
        params_.tnom = 300.15;
        
        params_.compute_derived_params(params_.tnom);
    }
    
    Real compute_theta0vb0() const {
        const Real leff = params_.get_leff();
        Real theta0vb0 = 0.0;
        if (params_.Xdep0 > 0.0 && params_.factor1 > 0.0 && leff > 0.0) {
            const Real tmp = params_.factor1 * std::sqrt(params_.Xdep0);
            const Real EXP_THRESHOLD = 60.0;
            const Real MIN_EXP = 1.0e-20;
            const Real T0 = params_.dsub * leff / tmp;
            if (T0 < EXP_THRESHOLD) {
                const Real T1 = std::exp(T0);
                const Real T2 = T1 - 1.0;
                const Real T3 = T2 * T2;
                const Real T4 = T3 + 2.0 * T1 * MIN_EXP;
                theta0vb0 = T1 / T4;
            } else {
                theta0vb0 = 1.0 / (EXP_THRESHOLD - 2.0);
            }
        }
        return theta0vb0;
    }
    
    BSIM4Params params_;
};

// Test Vth at high VDS and high temperature
TEST_F(BSIM4HighVdsVthTest, VthAtHighVdsHighTemp) {
    BSIM4Core core(params_, false);  // N-channel
    const Real theta0vb0 = compute_theta0vb0();
    
    // Test condition: VGS=2V, VDS=2V, T=125°C
    const double vgs = 2.0;
    const double vds = 2.0;
    const double vbs = 0.0;
    const double temp_k = 125.0 + 273.15;
    
    auto op = core.evaluate(vgs, vds, 0.0, vbs, temp_k, theta0vb0);
    
    std::cout << "\n=== BSIM4 Vth Check at High VDS and High Temperature ===" << std::endl;
    std::cout << "Temperature: 125°C (" << temp_k << " K)" << std::endl;
    std::cout << "Bias: VGS=" << vgs << "V, VDS=" << vds << "V, VBS=" << vbs << "V" << std::endl;
    std::cout << "\nResults:" << std::endl;
    std::cout << "  Vth   = " << op.vth << " V" << std::endl;
    std::cout << "  Vdsat = " << op.vdsat << " V" << std::endl;
    std::cout << "  Ids   = " << op.ids << " A" << std::endl;
    std::cout << "  gm    = " << op.gm << " S" << std::endl;
    std::cout << "  gds   = " << op.gds << " S" << std::endl;
    
    // Vth should be positive for a properly functioning MOSFET model
    EXPECT_GT(op.vth, 0.0) << "Vth should be positive at VGS=2V, VDS=2V, T=125°C";
    
    // Also check that Vth is reasonable (within physical bounds)
    EXPECT_LT(op.vth, 1.0) << "Vth should be less than 1V for 45nm process";
}

// Temperature sweep test
TEST_F(BSIM4HighVdsVthTest, VthTemperatureSweep) {
    BSIM4Core core(params_, false);
    const Real theta0vb0 = compute_theta0vb0();
    
    const double vgs = 2.0;
    const double vds = 2.0;
    const double vbs = 0.0;
    
    std::cout << "\n=== Temperature Sweep at VGS=2V, VDS=2V ===" << std::endl;
    std::cout << "  T[°C]     Vth[V]    Vdsat[V]      Ids[A]" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
    
    bool all_vth_positive = true;
    for (double t_c : {27.0, 50.0, 75.0, 100.0, 125.0, 150.0}) {
        double t_k = t_c + 273.15;
        auto op = core.evaluate(vgs, vds, 0.0, vbs, t_k, theta0vb0);
        
        printf("  %6.1f   %8.5f   %8.5f   %12.6e\n", 
               t_c, op.vth, op.vdsat, op.ids);
        
        if (op.vth < 0.0) {
            all_vth_positive = false;
        }
    }
    
    EXPECT_TRUE(all_vth_positive) << "Vth should remain positive across all temperatures";
}

// VDS sweep test at high temperature
TEST_F(BSIM4HighVdsVthTest, VthVdsSweepAt125C) {
    BSIM4Core core(params_, false);
    const Real theta0vb0 = compute_theta0vb0();
    
    const double vgs = 2.0;
    const double vbs = 0.0;
    const double temp_k = 125.0 + 273.15;
    
    std::cout << "\n=== VDS Sweep at VGS=2V, T=125°C ===" << std::endl;
    std::cout << "  VDS[V]    Vth[V]    Vdsat[V]      Ids[A]" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
    
    bool all_vth_positive = true;
    for (double vds : {0.1, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0}) {
        auto op = core.evaluate(vgs, vds, 0.0, vbs, temp_k, theta0vb0);
        
        printf("  %6.2f   %8.5f   %8.5f   %12.6e\n", 
               vds, op.vth, op.vdsat, op.ids);
        
        if (op.vth < 0.0) {
            all_vth_positive = false;
        }
    }
    
    EXPECT_TRUE(all_vth_positive) << "Vth should remain positive across all VDS values";
}

