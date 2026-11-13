// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/core/matrix_types.cpp
#include "kirimaspice/core/matrix_types.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <fmt/format.h>

// OpenMP for sparse matrix operations
#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice {

void SparseMatrix::add_entry(std::uint32_t row, std::uint32_t col, Real value) {
    // IMPORTANT: Do NOT skip zero entries here
    // Rationale: BJT models (both NPN and PNP) can produce exact zero from
    // conductance cancellation (e.g., g_cb + g_cc = 0 in Ebers-Moll model).
    // Skipping zeros causes sparse pattern to oscillate during Newton iterations,
    // which breaks KLU solver's assumption of fixed sparsity pattern.
    // Zero filtering is handled later in to_csc() if needed.

    rows.push_back(row);
    cols.push_back(col);
    values.push_back(value);

    // Update dimensions
    n_rows = std::max(n_rows, row + 1);
    n_cols = std::max(n_cols, col + 1);
}

void SparseMatrix::to_csc(std::vector<std::uint32_t>& col_ptr,
                          std::vector<std::uint32_t>& row_idx,
                          std::vector<Real>& values_out) const {
    // Validate COO array consistency
    if (rows.size() != cols.size() || rows.size() != values.size()) {
        throw std::runtime_error(fmt::format(
            "SparseMatrix::to_csc: Inconsistent COO dimensions (rows={}, cols={}, values={})",
            rows.size(), cols.size(), values.size()));
    }

    // COO to CSC conversion
    // Note: The map accumulation loop (lines 33-36) is inherently sequential
    // due to std::map's red-black tree structure. Parallelizing it would require
    // thread-local maps and merging, which adds complexity for limited benefit
    // since to_csc() is called less frequently than zero().

    // Create a map to accumulate values for duplicate (row, col) pairs
    std::map<std::pair<std::uint32_t, std::uint32_t>, Real> entries;

    for (std::size_t i = 0; i < values.size(); ++i) {
        auto key = std::make_pair(rows[i], cols[i]);
        entries[key] += values[i];
    }

    // Count entries per column (including zeros)
    // IMPORTANT: Do NOT filter by numerical value here!
    // The sparsity pattern must be fixed throughout Newton iterations,
    // regardless of whether accumulated values happen to be zero.
    std::vector<std::uint32_t> col_counts(n_cols, 0);
    for (const auto& [key, val] : entries) {
        col_counts[key.second]++;
    }

    // Build col_ptr (column pointers)
    // Note: This is a prefix sum operation, which is inherently sequential
    // Parallel prefix sum algorithms exist but add complexity for minimal benefit
    col_ptr.resize(n_cols + 1);
    col_ptr[0] = 0;
    for (std::uint32_t j = 0; j < n_cols; ++j) {
        col_ptr[j + 1] = col_ptr[j] + col_counts[j];
    }

    // Allocate output arrays
    std::size_t total_nnz = col_ptr[n_cols];
    row_idx.resize(total_nnz);
    values_out.resize(total_nnz);

    // Fill in row_idx and values_out in column-major order
    // IMPORTANT: Include ALL entries, even if value is zero.
    // This maintains fixed sparsity pattern required by KLU solver.
    std::vector<std::uint32_t> col_offsets = col_ptr;
    for (const auto& [key, val] : entries) {
        std::uint32_t col = key.second;
        std::uint32_t row = key.first;
        std::uint32_t idx = col_offsets[col]++;
        row_idx[idx] = row;
        values_out[idx] = val;
    }

    // Note: padding is applied in reserve() for memory allocation
    // to_csc() maintains exact size for API compatibility with existing code
}

void SparseMatrix::reserve(std::size_t estimated_nnz) {
    // Round up to multiple of 8 for SIMD-friendly allocation
    std::size_t padded_size = (estimated_nnz + 7) & ~std::size_t(7);
    rows.reserve(padded_size);
    cols.reserve(padded_size);
    values.reserve(padded_size);
}

void SparseMatrix::clear() {
    rows.clear();
    cols.clear();
    values.clear();
    n_rows = 0;
    n_cols = 0;
}

void MNASystem::resize(std::uint32_t nodes, std::uint32_t branches) {
    num_nodes = nodes;
    num_branches = branches;
    n_nodes = nodes;      // Backward compatibility
    n_branches = branches; // Backward compatibility
    std::uint32_t total_size = nodes + branches;

    b.resize(total_size, 0.0);
    x.resize(total_size, 0.0);

    G.n_rows = total_size;
    G.n_cols = total_size;
}

void MNASystem::zero() {
    // Parallelize vector zeroing for large systems
    // Threshold: 1000 elements (balance between parallelization benefit and overhead)
    const std::size_t parallel_threshold = 1000;
    std::size_t vec_size = b.size();

    if (vec_size >= parallel_threshold) {
        #ifdef _OPENMP
        #pragma omp parallel
        {
            #pragma omp for schedule(static) nowait
            for (std::size_t i = 0; i < vec_size; ++i) {
                b[i] = 0.0;
            }
            #pragma omp for schedule(static) nowait
            for (std::size_t i = 0; i < vec_size; ++i) {
                x[i] = 0.0;
            }
        }
        #else
        std::fill(b.begin(), b.end(), 0.0);
        std::fill(x.begin(), x.end(), 0.0);
        #endif
    } else {
        // Small systems: sequential is faster (avoid parallelization overhead)
        std::fill(b.begin(), b.end(), 0.0);
        std::fill(x.begin(), x.end(), 0.0);
    }

    G.clear();

    // Reserve capacity based on typical circuit connectivity
    // Estimation: num_nodes * average_connections_per_node
    // Typical average: 4-6 connections per node in analog circuits
    std::size_t estimated_nnz = size() * 5;
    G.reserve(estimated_nnz);
}

void MNASystem::merge(const MNASystem& other) {
    // Merge thread-local MNA system into global system
    // This is used for parallel device stamping

    // Merge sparse matrix G: concatenate COO vectors
    G.rows.insert(G.rows.end(), other.G.rows.begin(), other.G.rows.end());
    G.cols.insert(G.cols.end(), other.G.cols.begin(), other.G.cols.end());
    G.values.insert(G.values.end(), other.G.values.begin(), other.G.values.end());

    // Update sparse matrix dimensions
    G.n_rows = std::max(G.n_rows, other.G.n_rows);
    G.n_cols = std::max(G.n_cols, other.G.n_cols);

    // Merge RHS vector b: element-wise addition
    std::size_t min_size = std::min(b.size(), other.b.size());
    for (std::size_t i = 0; i < min_size; ++i) {
        b[i] += other.b[i];
    }

    // Update system dimensions (should already match, but ensure consistency)
    num_nodes = std::max(num_nodes, other.num_nodes);
    num_branches = std::max(num_branches, other.num_branches);
    n_nodes = num_nodes;  // Backward compatibility
    n_branches = num_branches;
}

// ============================================================================
// Complex number implementations
// ============================================================================

void ComplexSparseMatrix::add_entry(std::uint32_t row, std::uint32_t col, Complex value) {
    // IMPORTANT: Do NOT skip zero entries here
    // Rationale: Same as SparseMatrix - device models can produce exact zero from
    // admittance cancellation. Skipping zeros causes sparse pattern to oscillate
    // during Newton iterations, which breaks KLU solver's assumption of fixed
    // sparsity pattern. Zero filtering is handled later in to_csc() if needed.

    rows.push_back(row);
    cols.push_back(col);
    values.push_back(value);

    // Update dimensions
    n_rows = std::max(n_rows, row + 1);
    n_cols = std::max(n_cols, col + 1);
}

void ComplexSparseMatrix::to_csc(std::vector<std::uint32_t>& col_ptr,
                                 std::vector<std::uint32_t>& row_idx,
                                 std::vector<Complex>& values_out) const {
    // COO to CSC conversion (same notes as SparseMatrix)
    // Map accumulation and prefix sum are kept sequential for simplicity

    // Create a map to accumulate values for duplicate (row, col) pairs
    std::map<std::pair<std::uint32_t, std::uint32_t>, Complex> entries;

    for (std::size_t i = 0; i < values.size(); ++i) {
        auto key = std::make_pair(rows[i], cols[i]);
        entries[key] += values[i];
    }

    // Count entries per column (including zeros)
    // IMPORTANT: Do NOT filter by numerical value here!
    // The sparsity pattern must be fixed throughout Newton iterations,
    // regardless of whether accumulated values happen to be zero.
    std::vector<std::uint32_t> col_counts(n_cols, 0);
    for (const auto& [key, val] : entries) {
        col_counts[key.second]++;
    }

    // Build col_ptr (column pointers) - sequential prefix sum
    col_ptr.resize(n_cols + 1);
    col_ptr[0] = 0;
    for (std::uint32_t j = 0; j < n_cols; ++j) {
        col_ptr[j + 1] = col_ptr[j] + col_counts[j];
    }

    // Allocate output arrays
    std::size_t total_nnz = col_ptr[n_cols];
    row_idx.resize(total_nnz);
    values_out.resize(total_nnz);

    // Fill in row_idx and values_out in column-major order
    // IMPORTANT: Include ALL entries, even if value is zero.
    // This maintains fixed sparsity pattern required by KLU solver.
    std::vector<std::uint32_t> col_offsets = col_ptr;
    for (const auto& [key, val] : entries) {
        std::uint32_t col = key.second;
        std::uint32_t row = key.first;
        std::uint32_t idx = col_offsets[col]++;
        row_idx[idx] = row;
        values_out[idx] = val;
    }

    // Note: padding is applied in reserve() for memory allocation
    // to_csc() maintains exact size for API compatibility with existing code
}

void ComplexSparseMatrix::reserve(std::size_t estimated_nnz) {
    // Round up to multiple of 8 for SIMD-friendly allocation
    std::size_t padded_size = (estimated_nnz + 7) & ~std::size_t(7);
    rows.reserve(padded_size);
    cols.reserve(padded_size);
    values.reserve(padded_size);
}

void ComplexSparseMatrix::clear() {
    rows.clear();
    cols.clear();
    values.clear();
    n_rows = 0;
    n_cols = 0;
}

void ComplexMNASystem::resize(std::uint32_t nodes, std::uint32_t branches) {
    num_nodes = nodes;
    num_branches = branches;
    n_nodes = nodes;      // Backward compatibility
    n_branches = branches; // Backward compatibility
    std::uint32_t total_size = nodes + branches;

    I.resize(total_size, Complex(0.0, 0.0));
    V.resize(total_size, Complex(0.0, 0.0));

    Y.n_rows = total_size;
    Y.n_cols = total_size;
}

void ComplexMNASystem::zero() {
    // Parallelize vector zeroing for large systems
    // Threshold: 1000 elements (balance between parallelization benefit and overhead)
    const std::size_t parallel_threshold = 1000;
    std::size_t vec_size = I.size();

    if (vec_size >= parallel_threshold) {
        #ifdef _OPENMP
        #pragma omp parallel
        {
            #pragma omp for schedule(static) nowait
            for (std::size_t i = 0; i < vec_size; ++i) {
                I[i] = Complex(0.0, 0.0);
            }
            #pragma omp for schedule(static) nowait
            for (std::size_t i = 0; i < vec_size; ++i) {
                V[i] = Complex(0.0, 0.0);
            }
        }
        #else
        std::fill(I.begin(), I.end(), Complex(0.0, 0.0));
        std::fill(V.begin(), V.end(), Complex(0.0, 0.0));
        #endif
    } else {
        // Small systems: sequential is faster (avoid parallelization overhead)
        std::fill(I.begin(), I.end(), Complex(0.0, 0.0));
        std::fill(V.begin(), V.end(), Complex(0.0, 0.0));
    }

    Y.clear();

    // Reserve capacity for AC analysis
    // Same estimation as DC: size() * 5
    std::size_t estimated_nnz = size() * 5;
    Y.reserve(estimated_nnz);
}

void ComplexMNASystem::merge(const ComplexMNASystem& other) {
    // Merge thread-local complex MNA system into global system
    // This is used for parallel device stamping in AC analysis

    // Merge sparse matrix Y: concatenate COO vectors
    Y.rows.insert(Y.rows.end(), other.Y.rows.begin(), other.Y.rows.end());
    Y.cols.insert(Y.cols.end(), other.Y.cols.begin(), other.Y.cols.end());
    Y.values.insert(Y.values.end(), other.Y.values.begin(), other.Y.values.end());

    // Update sparse matrix dimensions
    Y.n_rows = std::max(Y.n_rows, other.Y.n_rows);
    Y.n_cols = std::max(Y.n_cols, other.Y.n_cols);

    // Merge current source vector I: element-wise addition
    std::size_t min_size = std::min(I.size(), other.I.size());
    for (std::size_t i = 0; i < min_size; ++i) {
        I[i] += other.I[i];
    }

    // Update system dimensions (should already match, but ensure consistency)
    num_nodes = std::max(num_nodes, other.num_nodes);
    num_branches = std::max(num_branches, other.num_branches);
    n_nodes = num_nodes;  // Backward compatibility
    n_branches = num_branches;
}

} // namespace kirimaspice
