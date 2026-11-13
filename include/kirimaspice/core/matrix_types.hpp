// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/core/matrix_types.hpp
#pragma once

#include "types.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kirimaspice {

/// Sparse matrix in COO (Coordinate) format for MNA construction
struct SparseMatrix {
    std::vector<std::uint32_t> rows;   ///< Row indices
    std::vector<std::uint32_t> cols;   ///< Column indices
    std::vector<Real> values;          ///< Non-zero elements
    std::uint32_t n_rows = 0;          ///< Number of rows
    std::uint32_t n_cols = 0;          ///< Number of columns

    /// Add entry (duplicates are automatically summed)
    void add_entry(std::uint32_t row, std::uint32_t col, Real value);

    /// Reserve capacity for estimated number of non-zero elements
    /// This reduces memory allocations during matrix construction
    void reserve(std::size_t estimated_nnz);

    /// Convert to CSC (Compressed Sparse Column) format for KLU
    void to_csc(std::vector<std::uint32_t>& col_ptr,
                std::vector<std::uint32_t>& row_idx,
                std::vector<Real>& values_out) const;

    /// Clear all entries
    void clear();

    /// Get number of non-zero elements
    std::size_t nnz() const { return values.size(); }
};

/// Dense vector
using DenseVector = std::vector<Real>;

/// MNA system (Gx = b)
struct MNASystem {
    SparseMatrix G; ///< Conductance/Jacobian matrix (MNA G matrix)
    DenseVector b;  ///< Right-hand side vector
    DenseVector x;  ///< Solution vector (node voltages + branch currents)

    std::uint32_t num_nodes = 0;   ///< Number of nodes (excluding GND)
    std::uint32_t num_branches = 0; ///< Number of branches (voltage sources, etc.)

    // Backward compatibility aliases
    std::uint32_t n_nodes = 0;    ///< Deprecated: use num_nodes
    std::uint32_t n_branches = 0; ///< Deprecated: use num_branches

    /// Resize system
    void resize(std::uint32_t nodes, std::uint32_t branches);

    /// Zero clear (reset values to 0)
    void zero();

    /// Merge another MNA system into this one (for parallel device stamping)
    /// Used to combine thread-local MNA systems
    void merge(const MNASystem& other);

    /// Get total system size
    std::uint32_t size() const { return num_nodes + num_branches; }
};

// ============================================================================
// Complex number support for AC analysis
// ============================================================================

/// Complex sparse matrix in COO format (for AC analysis)
struct ComplexSparseMatrix {
    std::vector<std::uint32_t> rows;   ///< Row indices
    std::vector<std::uint32_t> cols;   ///< Column indices
    std::vector<Complex> values;       ///< Complex non-zero elements
    std::uint32_t n_rows = 0;          ///< Number of rows
    std::uint32_t n_cols = 0;          ///< Number of columns

    /// Add entry (duplicates are automatically summed)
    void add_entry(std::uint32_t row, std::uint32_t col, Complex value);

    /// Reserve capacity for estimated number of non-zero elements
    /// This reduces memory allocations during matrix construction
    void reserve(std::size_t estimated_nnz);

    /// Convert to CSC (Compressed Sparse Column) format
    void to_csc(std::vector<std::uint32_t>& col_ptr,
                std::vector<std::uint32_t>& row_idx,
                std::vector<Complex>& values_out) const;

    /// Clear all entries
    void clear();

    /// Get number of non-zero elements
    std::size_t nnz() const { return values.size(); }
};

/// Complex dense vector (for AC analysis)
using ComplexDenseVector = std::vector<Complex>;

/// Complex MNA system (Y*V = I) for AC analysis
struct ComplexMNASystem {
    ComplexSparseMatrix Y; ///< Admittance matrix (not G, use Y for AC)
    ComplexDenseVector I;  ///< Current source vector (phasors)
    ComplexDenseVector V;  ///< Solution vector (voltage phasors)

    std::uint32_t num_nodes = 0;   ///< Number of nodes (excluding GND)
    std::uint32_t num_branches = 0; ///< Number of branches (voltage sources, etc.)

    // Backward compatibility aliases
    std::uint32_t n_nodes = 0;    ///< Deprecated: use num_nodes
    std::uint32_t n_branches = 0; ///< Deprecated: use num_branches

    /// Resize system
    void resize(std::uint32_t nodes, std::uint32_t branches);

    /// Zero clear (reset values to 0)
    void zero();

    /// Merge another complex MNA system into this one (for parallel device stamping)
    /// Used to combine thread-local MNA systems
    void merge(const ComplexMNASystem& other);

    /// Get total system size
    std::uint32_t size() const { return num_nodes + num_branches; }
};

// ============================================================================
// DC Sweep analysis structures
// ============================================================================

/// Single point in DC sweep results
struct DCSweepPoint {
    Real sweep_value;                           ///< Primary sweep variable value
    std::optional<Real> nested_sweep_value;     ///< Nested sweep variable value (if 2D sweep)
    DenseVector node_voltages;                  ///< Node voltages at this sweep point
    DenseVector branch_currents;                ///< Branch currents at this sweep point
    ConvergenceStatus status;                   ///< Convergence status for this point
    std::uint32_t iterations;                   ///< Number of Newton-Raphson iterations
};

/// DC sweep analysis results
struct DCSweepResult {
    std::vector<DCSweepPoint> points;  ///< All sweep points
    ConvergenceStatus status;          ///< Overall convergence status
    std::uint64_t total_iterations;    ///< Total iterations across all points

    /// Check if all points converged
    bool all_converged() const {
        return std::all_of(points.begin(), points.end(),
            [](const DCSweepPoint& p) { return p.status == ConvergenceStatus::SUCCESS; });
    }
};

// ============================================================================
// Temperature sweep (.TEMP) results
// ============================================================================

/// Single temperature point in a temperature sweep
struct TemperatureSweepPoint {
    Real temperature;                   ///< Temperature [K]
    DenseVector node_voltages;          ///< Node voltages at this temperature
    DenseVector branch_currents;        ///< Branch currents at this temperature
    ConvergenceStatus status;           ///< Convergence status for this temperature
    std::uint32_t iterations;           ///< Number of Newton-Raphson iterations
};

/// Temperature sweep analysis results
/// Contains results for each temperature point from .TEMP directive
struct TemperatureSweepResult {
    std::vector<TemperatureSweepPoint> points;  ///< All temperature points
    ConvergenceStatus status;                   ///< Overall convergence status
    std::uint64_t total_iterations;             ///< Total iterations across all temperatures

    /// Check if all temperature points converged
    bool all_converged() const {
        return std::all_of(points.begin(), points.end(),
            [](const TemperatureSweepPoint& p) { return p.status == ConvergenceStatus::SUCCESS; });
    }
};

// ============================================================================
// .measure functionality
// ============================================================================

/// Result of a single .measure statement
struct MeasureResult {
    std::string name;             ///< Measurement name (from .measure statement)
    bool success;                 ///< true if measurement succeeded, false if failed
    Real value;                   ///< Measured value (valid only if success=true)
    std::string unit;             ///< Unit of measurement (e.g., "V", "A", "s", "Hz", "dB")

    /// Optional: time/frequency where measurement was taken
    std::optional<Real> at_time;      ///< Time point for TRAN measurements (seconds)
    std::optional<Real> at_frequency; ///< Frequency point for AC measurements (Hz)

    /// Optional: trigger and target times for TRIG_TARG measurements
    std::optional<Real> trig_time;    ///< Trigger time (seconds)
    std::optional<Real> targ_time;    ///< Target time (seconds)

    /// Error message (if success=false)
    std::string error_message;

    /// Helper: Create successful measurement result
    static MeasureResult success_result(const std::string& name, Real value,
                                        const std::string& unit = "") {
        MeasureResult result;
        result.name = name;
        result.success = true;
        result.value = value;
        result.unit = unit;
        return result;
    }

    /// Helper: Create failed measurement result
    static MeasureResult failure_result(const std::string& name,
                                        const std::string& error_msg) {
        MeasureResult result;
        result.name = name;
        result.success = false;
        result.value = 0.0;
        result.error_message = error_msg;
        return result;
    }
};

// ============================================================================
// Sensitivity analysis
// ============================================================================

/// Sensitivity result for a single parameter
/// Represents ∂V_out/∂P for one device parameter P
struct SensitivityResult {
    std::string device_name;        ///< Device name (e.g., "R1", "Q1")
    std::string parameter_name;     ///< Parameter name (e.g., "resistance", "IS", "VTO")
    std::string output_node;        ///< Output node name (e.g., "out")
    Real nominal_value;             ///< Nominal parameter value
    Real nominal_output;            ///< Nominal output voltage V_out(P_nominal)
    Real sensitivity;               ///< Absolute sensitivity ∂V_out/∂P [V/unit]
    Real normalized_sensitivity;    ///< Normalized sensitivity (∂V_out/V_out)/(∂P/P) [dimensionless]
    Real relative_importance;       ///< |normalized_sensitivity| for ranking
};

/// Sensitivity analysis result container
struct SensitivityAnalysisResult {
    std::vector<SensitivityResult> sensitivities;  ///< All sensitivity results
    DenseVector nominal_solution;                  ///< Nominal DC operating point
    bool success = false;                          ///< Overall analysis success
    std::string error_message;                     ///< Error message if failed
};

} // namespace kirimaspice
