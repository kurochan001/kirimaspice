// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/solvers/isparse_solver.hpp
#pragma once

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"

#include <memory>
#include <string>

namespace kirimaspice::solvers {

/// Common interface for sparse linear solvers
/// Provides a minimal API used across DC/AC/TRAN solvers.
class ISparseLinearSolver {
public:
    virtual ~ISparseLinearSolver() = default;

    /// Analyze and factorize matrix (first call)
    /// Implementations may perform both symbolic and numeric factorization.
    virtual bool analyze_and_factorize(const SparseMatrix& matrix) = 0;

    /// Refactorize with same sparsity pattern (subsequent calls)
    /// Implementations must verify pattern invariance and update values.
    virtual bool refactorize(const SparseMatrix& matrix) = 0;

    /// Solve Ax = b using the current factorization
    virtual bool solve(const DenseVector& b, DenseVector& x) = 0;

    /// Error message (empty if the last operation succeeded)
    virtual const std::string& last_error() const = 0;

    /// Reset internal state
    virtual void reset() = 0;
};

} // namespace kirimaspice::solvers

