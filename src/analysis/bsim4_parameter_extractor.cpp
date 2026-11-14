// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/analysis/bsim4_parameter_extractor.cpp
#include "kirimaspice/analysis/bsim4_parameter_extractor.hpp"
#include "kirimaspice/devices/mosfet_bsim4.hpp"  // Unified BSIM4 template
#include <spdlog/spdlog.h>
#include <nlopt.hpp>
#include <cmath>
#include <limits>
#include <algorithm>

namespace kirimaspice::analysis {

BSIM4ParameterExtractor::BSIM4ParameterExtractor(
    const BSIM4MeasurementData& measurement_data,
    const devices::BSIM4Params& initial_params)
    : measurement_data_(measurement_data),
      params_(initial_params) {

    spdlog::info("BSIM4ParameterExtractor: Initialized with {} measurement points",
                 measurement_data_.iv_data.size());
}

BSIM4ParameterExtractionResult BSIM4ParameterExtractor::extract(
    const std::vector<std::string>& params_to_extract) {

    params_to_extract_ = params_to_extract;
    num_evaluations_ = 0;

    spdlog::info("BSIM4ParameterExtractor: Extracting {} parameters", params_to_extract.size());
    for (const auto& param_name : params_to_extract) {
        spdlog::debug("  - {}", param_name);
    }

    // Get initial parameter values
    std::vector<Real> initial_values;
    std::vector<Real> lower_bounds;
    std::vector<Real> upper_bounds;

    for (const auto& param_name : params_to_extract) {
        Real init_val = 0.0;
        Real lb = -std::numeric_limits<Real>::infinity();
        Real ub = std::numeric_limits<Real>::infinity();

        // Set initial values and bounds for common parameters
        if (param_name == "VTH0" || param_name == "vth0") {
            init_val = params_.vth0;
            lb = -2.0;  // Reasonable range for Vth
            ub = 2.0;
        } else if (param_name == "U0" || param_name == "u0") {
            init_val = params_.u0;
            lb = 0.001;  // Mobility must be positive
            ub = 0.1;    // m^2/V-s
        } else if (param_name == "K1" || param_name == "k1") {
            init_val = params_.k1;
            lb = 0.0;
            ub = 5.0;
        } else if (param_name == "VSAT" || param_name == "vsat") {
            init_val = params_.vsat;
            lb = 1e4;    // m/s
            ub = 2e5;
        } else if (param_name == "PCLM" || param_name == "pclm") {
            init_val = params_.pclm;
            lb = 0.0;
            ub = 5.0;
        } else {
            spdlog::warn("Unknown parameter: {}, using default bounds", param_name);
            init_val = 1.0;
            lb = 0.0;
            ub = 10.0;
        }

        initial_values.push_back(init_val);
        lower_bounds.push_back(lb);
        upper_bounds.push_back(ub);
    }

    // Create NLopt optimizer (COBYLA: derivative-free, constraint-capable)
    nlopt::opt opt(nlopt::LN_COBYLA, params_to_extract.size());
    opt.set_lower_bounds(lower_bounds);
    opt.set_upper_bounds(upper_bounds);
    opt.set_min_objective(nlopt_objective_wrapper, this);

    // Convergence criteria
    opt.set_xtol_rel(1e-6);
    opt.set_ftol_rel(1e-8);
    opt.set_maxeval(1000);

    // Execute optimization
    std::vector<double> x = initial_values;
    double minf;

    BSIM4ParameterExtractionResult result;

    try {
        nlopt::result nlopt_result = opt.optimize(x, minf);
        result.success = (nlopt_result >= 0);  // Positive values indicate success
        result.rms_error = minf;
        result.num_evaluations = num_evaluations_;

        // Extract final parameter values
        for (size_t i = 0; i < params_to_extract.size(); ++i) {
            result.extracted_params[params_to_extract[i]] = x[i];
        }

        // Compute max error
        apply_parameters(x);
        Real max_err = 0.0;
        for (const auto& point : measurement_data_.iv_data) {
            Real ids_sim = simulate_ids(point.vgs, point.vds, point.vbs);
            Real error = std::abs(ids_sim - point.ids_measured);
            max_err = std::max(max_err, error);
        }
        result.max_error = max_err;

        result.message = "Optimization converged successfully";
        spdlog::info("BSIM4ParameterExtractor: Extraction completed");
        spdlog::info("  RMS error: {:.6e} A", result.rms_error);
        spdlog::info("  Max error: {:.6e} A", result.max_error);
        spdlog::info("  Evaluations: {}", result.num_evaluations);

    } catch (std::exception& e) {
        result.success = false;
        result.message = std::string("Optimization failed: ") + e.what();
        spdlog::error("BSIM4ParameterExtractor: {}", result.message);
    }

    return result;
}

Real BSIM4ParameterExtractor::compute_objective(const std::vector<Real>& param_values) {
    apply_parameters(param_values);

    Real sum_squared_error = 0.0;
    for (const auto& point : measurement_data_.iv_data) {
        Real ids_sim = simulate_ids(point.vgs, point.vds, point.vbs);
        Real error = ids_sim - point.ids_measured;
        sum_squared_error += error * error;
    }

    Real rms_error = std::sqrt(sum_squared_error / measurement_data_.iv_data.size());
    return rms_error;
}

void BSIM4ParameterExtractor::apply_parameters(const std::vector<Real>& param_values) {
    for (size_t i = 0; i < params_to_extract_.size(); ++i) {
        const std::string& param_name = params_to_extract_[i];
        Real value = param_values[i];

        // Apply parameter to BSIM4Params structure
        if (param_name == "VTH0" || param_name == "vth0") {
            params_.vth0 = value;
        } else if (param_name == "U0" || param_name == "u0") {
            params_.u0 = value;
        } else if (param_name == "K1" || param_name == "k1") {
            params_.k1 = value;
        } else if (param_name == "VSAT" || param_name == "vsat") {
            params_.vsat = value;
        } else if (param_name == "PCLM" || param_name == "pclm") {
            params_.pclm = value;
        }
    }
}

Real BSIM4ParameterExtractor::simulate_ids(Real vgs, Real vds, Real vbs) {
    // Create temporary BSIM4 device to simulate I_ds
    // Use dummy node IDs (we only need the DC current calculation)

    if (measurement_data_.is_nmos) {
        devices::MOSFET_BSIM4_N device(0, "temp_device", 1, 2, 0, 0, params_);

        // Compute threshold voltage
        Real vth = device.compute_threshold_voltage(vds, vbs);

        // Compute drain current
        Real ids = device.compute_drain_current(vgs, vds, vbs, vth);

        return ids;
    } else {
        // P-channel: use vsg/vsd convention
        devices::MOSFET_BSIM4_P device(0, "temp_device", 1, 2, 0, 0, params_);

        Real vsg = -vgs;  // Convert to P-channel convention
        Real vsd = -vds;
        Real vth = device.compute_threshold_voltage(vsd, vbs);
        Real ids = device.compute_drain_current(vsg, vsd, vbs, vth);

        return ids;  // Already positive for P-channel
    }
}

double BSIM4ParameterExtractor::nlopt_objective_wrapper(
    const std::vector<double>& x,
    std::vector<double>& grad,
    void* data) {

    auto* extractor = static_cast<BSIM4ParameterExtractor*>(data);
    extractor->num_evaluations_++;

    std::vector<Real> params(x.begin(), x.end());
    Real objective = extractor->compute_objective(params);

    return static_cast<double>(objective);
}

} // namespace kirimaspice::analysis
