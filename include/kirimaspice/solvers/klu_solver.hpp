// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/klu_solver.hpp
#pragma once

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"

// SuiteSparse 7.6 includes <omp.h> inside an extern "C" block, which breaks
// OpenMP builds with GCC. Including <omp.h> first, with C++ linkage, avoids that.
#if defined(_OPENMP)
#include <omp.h>
#endif
#include <klu.h>
// Undefine KLU's Int-related macros to avoid conflicts with fmt's template parameters
#ifdef Int
#undef Int
#endif
#ifdef Int_id
#undef Int_id
#endif
#ifdef Int_MAX
#undef Int_MAX
#endif
#include <memory>
#include <vector>

namespace kirimaspice::solvers {

/// Sparse pattern difference information
struct PatternDifference {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> added_entries;    ///< Added (row, col) pairs
    std::vector<std::pair<std::uint32_t, std::uint32_t>> removed_entries;  ///< Removed (row, col) pairs
    std::size_t total_changes;                                              ///< Total number of changes

    /// Check if pattern has any changes
    bool has_changes() const {
        return !added_entries.empty() || !removed_entries.empty();
    }

    /// Calculate change ratio relative to total entries
    double change_ratio(std::size_t total_entries) const {
        if (total_entries == 0) return 0.0;
        return static_cast<double>(total_changes) / static_cast<double>(total_entries);
    }
};

/// KLU sparse linear solver (wrapper around SuiteSparse KLU)
/// Solves Ax = b where A is sparse matrix
class KLUSolver {
public:
    KLUSolver();
    ~KLUSolver();

    // Non-copyable
    KLUSolver(const KLUSolver&) = delete;
    KLUSolver& operator=(const KLUSolver&) = delete;

    /// Analyze and factorize matrix (first call)
    /// @param matrix Sparse matrix in COO format
    /// @return true if successful
    bool analyze_and_factorize(const SparseMatrix& matrix);

    /// Refactorize with same sparsity pattern (subsequent calls)
    /// @param matrix Sparse matrix in COO format
    /// @return true if successful
    bool refactorize(const SparseMatrix& matrix);

    /// Solve Ax = b
    /// @param b Right-hand side vector
    /// @param x Solution vector (output)
    /// @return true if successful
    bool solve(const DenseVector& b, DenseVector& x);

    /// Get last error message
    const std::string& last_error() const { return last_error_; }

    /// Check if solver is initialized
    bool is_initialized() const { return symbolic_ != nullptr; }

    /// Reset solver (clears all state)
    void reset();

    /// Compute pattern difference between current and new matrix
    /// @param matrix New sparse matrix in COO format
    /// @param diff Output pattern difference
    /// @return true if computation successful
    bool compute_pattern_difference(const SparseMatrix& matrix, PatternDifference& diff);

private:
    // KLU data structures
    klu_symbolic* symbolic_;   ///< Symbolic factorization
    klu_numeric* numeric_;     ///< Numeric factorization
    klu_common common_;        ///< KLU control parameters

    // CSC storage (owned by this class) - int arrays for KLU API
    std::vector<int> col_ptr_;  ///< Column pointers (int for KLU)
    std::vector<int> row_idx_;  ///< Row indices (int for KLU)
    std::vector<Real> values_;  ///< Non-zero values

    std::uint32_t n_;          ///< Matrix dimension
    std::string last_error_;   ///< Last error message

    /// Convert COO to CSC and update internal storage in-place
    /// Returns false if sparsity pattern has changed
    bool update_csc_inplace(const SparseMatrix& matrix, bool verify_pattern);
};

} // namespace kirimaspice::solvers
