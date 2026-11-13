// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/analysis/bsim4_parameter_extractor.hpp
// BSIM4 Parameter Extraction Engine
// Extracts BSIM4 model parameters from measurement data
#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include <vector>
#include <string>
#include <map>
#include <memory>

namespace kirimaspice::analysis {

/// BSIM4 parameter extractor
/// Extracts BSIM4 model parameters from I-V measurement data using optimization
class BSIM4ParameterExtractor {
public:
    /// Constructor
    /// @param measurement_data Measured I-V data points
    /// @param initial_params Initial parameter guesses (optional)
    BSIM4ParameterExtractor(const BSIM4MeasurementData& measurement_data,
                            const devices::BSIM4Params& initial_params);

    /// Execute parameter extraction
    /// @param params_to_extract List of parameter names to extract (e.g., {"VTH0", "U0", "K1"})
    /// @return Extraction result with fitted parameters
    BSIM4ParameterExtractionResult extract(const std::vector<std::string>& params_to_extract);

private:
    /// Objective function: RMS error between measured and simulated I_ds
    /// @param param_values Current parameter values (in order of params_to_extract_)
    /// @return RMS error [A]
    Real compute_objective(const std::vector<Real>& param_values);

    /// Apply parameter values to BSIM4 model
    /// @param param_values Parameter values to apply
    void apply_parameters(const std::vector<Real>& param_values);

    /// Compute drain current for given operating point using current parameters
    /// @param vgs Gate-source voltage [V]
    /// @param vds Drain-source voltage [V]
    /// @param vbs Bulk-source voltage [V]
    /// @return Simulated drain current [A]
    Real simulate_ids(Real vgs, Real vds, Real vbs);

    /// NLopt objective function wrapper (static callback)
    /// @param x Parameter values
    /// @param grad Gradient (not used for gradient-free methods)
    /// @param data User data (BSIM4ParameterExtractor*)
    /// @return Objective function value
    static double nlopt_objective_wrapper(const std::vector<double>& x,
                                         std::vector<double>& grad,
                                         void* data);

    const BSIM4MeasurementData& measurement_data_;
    devices::BSIM4Params params_;                  ///< Current BSIM4 parameters
    std::vector<std::string> params_to_extract_;   ///< Parameter names being extracted
    std::uint32_t num_evaluations_ = 0;            ///< Evaluation counter
};

} // namespace kirimaspice::analysis
