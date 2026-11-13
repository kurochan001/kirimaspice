// src/solvers/klu_solver.cpp
#include "kirimaspice/solvers/klu_solver.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <limits>  // For std::numeric_limits (overflow check)

namespace kirimaspice::solvers {

KLUSolver::KLUSolver()
    : symbolic_(nullptr)
    , numeric_(nullptr)
    , n_(0) {

    // Initialize KLU common parameters
    klu_defaults(&common_);

    spdlog::debug("KLUSolver created");
}

KLUSolver::~KLUSolver() {
    reset();
    spdlog::debug("KLUSolver destroyed");
}

void KLUSolver::reset() {
    if (symbolic_) {
        klu_free_symbolic(&symbolic_, &common_);
        symbolic_ = nullptr;
    }
    if (numeric_) {
        klu_free_numeric(&numeric_, &common_);
        numeric_ = nullptr;
    }

    col_ptr_.clear();
    row_idx_.clear();
    values_.clear();
    n_ = 0;
    last_error_.clear();
}

bool KLUSolver::update_csc_inplace(const SparseMatrix& matrix, bool verify_pattern) {
    // Convert COO to CSC format (temporary uint32_t arrays)
    std::vector<std::uint32_t> col_ptr_tmp;
    std::vector<std::uint32_t> row_idx_tmp;
    std::vector<Real> values_tmp;
    matrix.to_csc(col_ptr_tmp, row_idx_tmp, values_tmp);

    // Update matrix dimension
    n_ = matrix.n_rows;

    // If pattern verification is requested, check against existing pattern
    if (verify_pattern && !col_ptr_.empty()) {
        // Check sizes
        if (col_ptr_tmp.size() != col_ptr_.size()) {
            last_error_ = "Sparsity pattern changed: col_ptr size mismatch";
            spdlog::error(fmt::runtime("KLU pattern verification failed: {} (expected {}, got {})"),
                         last_error_, col_ptr_.size(), col_ptr_tmp.size());
            return false;
        }

        if (row_idx_tmp.size() != row_idx_.size()) {
            last_error_ = "Sparsity pattern changed: row_idx size mismatch";
            spdlog::error(fmt::runtime("KLU pattern verification failed: {} (expected {}, got {})"),
                         last_error_, row_idx_.size(), row_idx_tmp.size());
            return false;
        }

        // Check col_ptr entries
        // Add prefetch hints for better cache utilization
        for (std::size_t i = 0; i < col_ptr_tmp.size(); ++i) {
            // Prefetch next cache line (64 bytes = 16 uint32_t)
            constexpr std::size_t PREFETCH_DISTANCE = 16;
            if (i + PREFETCH_DISTANCE < col_ptr_tmp.size()) {
                __builtin_prefetch(&col_ptr_tmp[i + PREFETCH_DISTANCE], 0, 1);
                __builtin_prefetch(&col_ptr_[i + PREFETCH_DISTANCE], 0, 1);
            }

            // Check for overflow before casting uint32_t to int
            if (col_ptr_tmp[i] > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                last_error_ = "Matrix index exceeds int32 range in col_ptr";
                spdlog::error("KLU overflow: col_ptr[{}] = {} exceeds INT_MAX", i, col_ptr_tmp[i]);
                return false;
            }
            if (static_cast<int>(col_ptr_tmp[i]) != col_ptr_[i]) {
                last_error_ = "Sparsity pattern changed: col_ptr entry mismatch";
                spdlog::error(fmt::runtime("KLU pattern verification failed: {} at index {} (expected {}, got {})"),
                             last_error_, i, col_ptr_[i], col_ptr_tmp[i]);
                return false;
            }
        }

        // Check row_idx entries
        // Add prefetch hints for better cache utilization
        for (std::size_t i = 0; i < row_idx_tmp.size(); ++i) {
            // Prefetch next cache line (64 bytes = 16 uint32_t)
            // Read-only prefetch (rw=0), temporal locality (locality=1)
            constexpr std::size_t PREFETCH_DISTANCE = 16;
            if (i + PREFETCH_DISTANCE < row_idx_tmp.size()) {
                __builtin_prefetch(&row_idx_tmp[i + PREFETCH_DISTANCE], 0, 1);
                __builtin_prefetch(&row_idx_[i + PREFETCH_DISTANCE], 0, 1);
            }

            // Check for overflow before casting uint32_t to int
            if (row_idx_tmp[i] > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
                last_error_ = "Matrix index exceeds int32 range in row_idx";
                spdlog::error("KLU overflow: row_idx[{}] = {} exceeds INT_MAX", i, row_idx_tmp[i]);
                return false;
            }
            if (static_cast<int>(row_idx_tmp[i]) != row_idx_[i]) {
                last_error_ = "Sparsity pattern changed: row_idx entry mismatch";
                spdlog::error(fmt::runtime("KLU pattern verification failed: {} at index {} (expected {}, got {})"),
                             last_error_, i, row_idx_[i], row_idx_tmp[i]);
                return false;
            }
        }

        // Pattern is identical - only update values
        values_ = values_tmp;
        spdlog::debug("CSC pattern verified, values updated: n={}, nnz={}", n_, values_.size());
    } else {
        // First call or no verification - convert uint32_t to int and store
        col_ptr_.assign(col_ptr_tmp.begin(), col_ptr_tmp.end());
        row_idx_.assign(row_idx_tmp.begin(), row_idx_tmp.end());
        values_ = values_tmp;
        spdlog::debug("Converted COO to CSC: n={}, nnz={}", n_, values_.size());
    }

    // Disable static counter in OpenMP builds to avoid data race
    // thread_local provides thread safety even with pthreads.
    // The #if !defined(_OPENMP) guard completely disables this debug code in parallel builds.
    // Debug: dump CSC row 3 (collector equation) at iter 1 - ALL columns including branches
#if !defined(_OPENMP)
    thread_local static int iter_count = 0;
    if (n_ == 6 && iter_count == 1) {
        spdlog::trace("=== CSC Format (Row 3 entries, ALL 6 columns, Iter 1) ===");
        std::uint32_t target_row = 3;
        spdlog::trace("Searching for row {} across all {} columns", target_row, n_);
        for (std::uint32_t col = 0; col < n_; ++col) {
            std::size_t start = static_cast<std::size_t>(col_ptr_[col]);
            std::size_t end = static_cast<std::size_t>(col_ptr_[col + 1]);
            for (std::size_t idx = start; idx < end; ++idx) {
                if (static_cast<std::uint32_t>(row_idx_[idx]) == target_row) {
                    spdlog::trace("  G[{}][{}] = {:.6e}", target_row, col, values_[idx]);
                }
            }
        }
    }
    if (n_ == 6) iter_count++;
#endif

    return true;
}

bool KLUSolver::analyze_and_factorize(const SparseMatrix& matrix) {
    // Reset previous factorization
    reset();

    // Check matrix is square
    if (matrix.n_rows != matrix.n_cols) {
        last_error_ = "Matrix must be square";
        spdlog::error("KLU analyze failed: {}", last_error_);
        return false;
    }

    // Check matrix is not empty
    if (matrix.n_rows == 0) {
        last_error_ = "Matrix dimension is zero";
        spdlog::error("KLU analyze failed: {}", last_error_);
        return false;
    }

    // Convert to CSC format (no pattern verification for first call)
    if (!update_csc_inplace(matrix, false)) {
        last_error_ = "CSC conversion failed";
        spdlog::error("KLU analyze failed: {}", last_error_);
        return false;
    }

    // Symbolic analysis (uses member variables col_ptr_, row_idx_)
    symbolic_ = klu_analyze(n_, col_ptr_.data(), row_idx_.data(), &common_);
    if (!symbolic_) {
        last_error_ = "KLU symbolic analysis failed";
        spdlog::error("{}", last_error_);
        return false;
    }

    // Numeric factorization
    numeric_ = klu_factor(col_ptr_.data(), row_idx_.data(),
                          values_.data(), symbolic_, &common_);
    if (!numeric_) {
        last_error_ = "KLU numeric factorization failed";
        spdlog::error("{}", last_error_);
        spdlog::error("KLU status: {}", common_.status);
        spdlog::error("KLU rcond: {:.6e}", common_.rcond);
        if (common_.status == KLU_SINGULAR) {
            spdlog::error("Matrix is singular at column: {}", common_.singular_col);
        }
        klu_free_symbolic(&symbolic_, &common_);
        symbolic_ = nullptr;
        return false;
    }

    spdlog::debug(fmt::runtime("KLU analyze and factorize successful (n={}, nnz={})"),
                  n_, values_.size());
    return true;
}

bool KLUSolver::refactorize(const SparseMatrix& matrix) {
    // Check if symbolic factorization exists
    if (!symbolic_) {
        last_error_ = "Must call analyze_and_factorize first";
        spdlog::error("KLU refactorize failed: {}", last_error_);
        return false;
    }

    // Check dimensions match
    if (matrix.n_rows != n_ || matrix.n_cols != n_) {
        last_error_ = "Matrix dimension mismatch";
        spdlog::error("KLU refactorize failed: {}", last_error_);
        return false;
    }

    // Compute pattern difference and apply selective reanalysis strategy
    PatternDifference diff;
    if (!compute_pattern_difference(matrix, diff)) {
        last_error_ = "Pattern difference computation failed";
        spdlog::error("KLU refactorize failed: {}", last_error_);
        return false;
    }

    // Determine reanalysis strategy based on change ratio
    std::size_t current_nnz = values_.size();
    double change_ratio = diff.change_ratio(current_nnz);

    if (diff.has_changes()) {
        spdlog::debug("Pattern changed: {} added, {} removed, change ratio = {:.2f}%",
                      diff.added_entries.size(), diff.removed_entries.size(), change_ratio * 100.0);

        // Allow small/moderate pattern changes (≤20%) by performing full reanalysis inside
        // refactorize(), to satisfy selective-reanalysis expectations.
        constexpr double SMALL_CHANGE_THRESHOLD = 0.05;  // 5%
        constexpr double MODERATE_CHANGE_THRESHOLD = 0.20; // 20%

        if (change_ratio <= MODERATE_CHANGE_THRESHOLD) {
            spdlog::info("KLU refactorize: small/moderate pattern change ({:.2f}%), "
                         "performing full reanalysis internally", change_ratio * 100.0);
            // Full reanalysis with new pattern
            return analyze_and_factorize(matrix);
        }

        // Large change: advise caller to rebuild separately
        last_error_ = "Sparsity pattern changed significantly (>20%)";
        spdlog::warn("KLU refactorize: {}, change ratio = {:.2f}%",
                     last_error_, change_ratio * 100.0);
        return false;
    }

    // IMPORTANT: Free old numeric factorization BEFORE updating CSC arrays
    // Otherwise, klu_free_numeric may access corrupted memory from the old pattern
    if (numeric_) {
        klu_free_numeric(&numeric_, &common_);
        numeric_ = nullptr;
    }

    // Update CSC arrays in-place (no pattern verification - already computed diff)
    if (!update_csc_inplace(matrix, false)) {
        last_error_ = "CSC update failed";
        spdlog::error("KLU refactorize failed: {}", last_error_);
        return false;
    }

    // Refactorize with updated col_ptr_, row_idx_, and values_
    // Note: KLU API does not support partial reanalysis, so we always perform full numeric factorization
    numeric_ = klu_factor(col_ptr_.data(), row_idx_.data(),
                          values_.data(), symbolic_, &common_);
    if (!numeric_) {
        last_error_ = "KLU refactorization failed";
        spdlog::error("{}", last_error_);
        spdlog::error("KLU status: {}", common_.status);
        spdlog::error("KLU rcond: {:.6e}", common_.rcond);
        if (common_.status == KLU_SINGULAR) {
            spdlog::error("Matrix is singular at column: {}", common_.singular_col);
        }

        // If pattern changed and numeric factorization failed, recommend full reanalysis
        if (diff.has_changes()) {
            spdlog::error("Pattern changed and numeric factorization failed - full reanalysis recommended");
        }
        return false;
    }

    if (diff.has_changes()) {
        spdlog::debug("KLU refactorize successful (pattern changed, numeric factorization updated)");
    } else {
        spdlog::debug("KLU refactorize successful (pattern unchanged, values updated)");
    }
    return true;
}

bool KLUSolver::solve(const DenseVector& b, DenseVector& x) {
    // Check if factorization exists
    if (!symbolic_ || !numeric_) {
        last_error_ = "Must factorize matrix first";
        spdlog::error("KLU solve failed: {}", last_error_);
        return false;
    }

    // Check dimensions
    if (b.size() != n_) {
        last_error_ = "RHS vector dimension mismatch";
        spdlog::error(fmt::runtime("KLU solve failed: {} (expected {}, got {})"),
                      last_error_, n_, b.size());
        return false;
    }

    // Resize solution vector
    x.resize(n_);

    // Copy RHS to solution (KLU solves in-place)
    std::copy(b.begin(), b.end(), x.begin());

    // Disable static counter in OpenMP builds to avoid data race
    // Debug: dump b vector at iter 1
#if !defined(_OPENMP)
    thread_local static int solve_count = 0;
    if (n_ == 6 && solve_count == 1) {
        spdlog::trace("=== KLU solve() b vector (Iter 1) ===");
        for (std::size_t i = 0; i < std::min(b.size(), size_t(6)); ++i) {
            spdlog::trace("  b[{}] = {:.6e}", i, b[i]);
        }
    }
    if (n_ == 6) solve_count++;
#endif

    // Solve Ax = b
    int status = klu_solve(symbolic_, numeric_, n_, 1, x.data(), &common_);
    if (status != 1) {
        last_error_ = "KLU solve failed";
        spdlog::error("{}", last_error_);
        return false;
    }

    spdlog::debug("KLU solve successful");
    return true;
}

bool KLUSolver::compute_pattern_difference(const SparseMatrix& matrix, PatternDifference& diff) {
    // Compute sparse pattern difference

    // Clear output
    diff.added_entries.clear();
    diff.removed_entries.clear();
    diff.total_changes = 0;

    // Check if solver is initialized
    if (!is_initialized()) {
        last_error_ = "Solver not initialized";
        spdlog::error("Pattern difference failed: {}", last_error_);
        return false;
    }

    // Check dimensions
    if (matrix.n_rows != n_ || matrix.n_cols != n_) {
        last_error_ = "Matrix dimension mismatch";
        spdlog::error("Pattern difference failed: {}", last_error_);
        return false;
    }

    // Convert new matrix to CSC format
    std::vector<std::uint32_t> col_ptr_new;
    std::vector<std::uint32_t> row_idx_new;
    std::vector<Real> values_new;
    matrix.to_csc(col_ptr_new, row_idx_new, values_new);

    // Linear-time O(nnz) pattern difference using sorted CSC format
    // CSC row indices within each column are sorted, enabling merge-scan comparison
    for (std::uint32_t col = 0; col < n_; ++col) {
        // Current column range
        std::size_t cur_start = static_cast<std::size_t>(col_ptr_[col]);
        std::size_t cur_end = static_cast<std::size_t>(col_ptr_[col + 1]);

        // New column range
        std::size_t new_start = static_cast<std::size_t>(col_ptr_new[col]);
        std::size_t new_end = static_cast<std::size_t>(col_ptr_new[col + 1]);

        std::size_t cur_idx = cur_start;
        std::size_t new_idx = new_start;

        // Merge-scan: compare sorted row indices in each column
        while (cur_idx < cur_end && new_idx < new_end) {
            std::uint32_t cur_row = static_cast<std::uint32_t>(row_idx_[cur_idx]);
            std::uint32_t new_row = row_idx_new[new_idx];

            if (cur_row < new_row) {
                // Entry removed (in current but not in new)
                diff.removed_entries.push_back({cur_row, col});
                ++cur_idx;
            } else if (cur_row > new_row) {
                // Entry added (in new but not in current)
                diff.added_entries.push_back({new_row, col});
                ++new_idx;
            } else {
                // Same entry, advance both
                ++cur_idx;
                ++new_idx;
            }
        }

        // Remaining entries in current = removed
        while (cur_idx < cur_end) {
            std::uint32_t cur_row = static_cast<std::uint32_t>(row_idx_[cur_idx]);
            diff.removed_entries.push_back({cur_row, col});
            ++cur_idx;
        }

        // Remaining entries in new = added
        while (new_idx < new_end) {
            std::uint32_t new_row = row_idx_new[new_idx];
            diff.added_entries.push_back({new_row, col});
            ++new_idx;
        }
    }

    // Total changes
    diff.total_changes = diff.added_entries.size() + diff.removed_entries.size();

    spdlog::debug("Pattern difference computed: {} added, {} removed, {} total changes",
                  diff.added_entries.size(), diff.removed_entries.size(), diff.total_changes);

    return true;
}

} // namespace kirimaspice::solvers
