// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_selective_reanalysis.cpp
// Unit tests.2: Selective reanalysis based on pattern difference

#include <gtest/gtest.h>
#include "kirimaspice/solvers/klu_solver.hpp"
#include "kirimaspice/core/matrix_types.hpp"

using namespace kirimaspice;
using namespace kirimaspice::solvers;

// Test fixture for selective reanalysis tests
class SelectiveReanalysisTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize solver with a 20x20 matrix (sparse, ~100 entries)
        // This gives us enough entries to test different change ratios
        matrix_init.n_rows = 20;
        matrix_init.n_cols = 20;

        // Create a tridiagonal matrix pattern
        for (uint32_t i = 0; i < 20; ++i) {
            matrix_init.add_entry(i, i, 2.0);  // Diagonal
            if (i > 0) {
                matrix_init.add_entry(i, i - 1, -1.0);  // Lower diagonal
            }
            if (i < 19) {
                matrix_init.add_entry(i, i + 1, -1.0);  // Upper diagonal
            }
        }

        // Add some extra entries (total ~100 entries)
        for (uint32_t i = 0; i < 20; i += 2) {
            for (uint32_t j = 0; j < 20; j += 2) {
                if (i != j && std::abs((int)i - (int)j) > 1) {
                    matrix_init.add_entry(i, j, 0.1);
                }
            }
        }

        solver.analyze_and_factorize(matrix_init);
    }

    KLUSolver solver;
    SparseMatrix matrix_init;
};

TEST_F(SelectiveReanalysisTest, NoChange_Refactorize) {
    // Same pattern, different values - should succeed without warnings
    SparseMatrix matrix_same = matrix_init;
    // Scale all values by 1.5
    for (auto& val : matrix_same.values) {
        val *= 1.5;
    }

    EXPECT_TRUE(solver.refactorize(matrix_same));
}

TEST_F(SelectiveReanalysisTest, SmallChange_LessThan5Percent) {
    // Add 2-3 entries to a ~100-entry matrix (~2-3% change)
    SparseMatrix matrix_small_change = matrix_init;

    // Add 2 new entries
    matrix_small_change.add_entry(0, 10, 0.5);  // NEW
    matrix_small_change.add_entry(5, 15, 0.5);  // NEW

    // Should succeed with INFO message (small change <5%)
    EXPECT_TRUE(solver.refactorize(matrix_small_change));
}

TEST_F(SelectiveReanalysisTest, ModerateChange_5To20Percent) {
    // Add 10 entries to a ~100-entry matrix (~10% change)
    SparseMatrix matrix_moderate_change = matrix_init;

    // Add 10 new entries
    for (uint32_t i = 0; i < 10; ++i) {
        matrix_moderate_change.add_entry(i, 19 - i, 0.5);  // NEW
    }

    // Should succeed with WARN message (moderate change 5-20%)
    EXPECT_TRUE(solver.refactorize(matrix_moderate_change));
}

TEST_F(SelectiveReanalysisTest, LargeChange_GreaterThan20Percent) {
    // Add 30 entries to a ~148-entry matrix (~20% change)
    // Note: Original matrix has ~148 entries (20 diagonal + 19 lower + 19 upper + ~90 extra)
    SparseMatrix matrix_large_change = matrix_init;

    // Add truly new entries at odd row/odd column positions (guaranteed not to exist)
    // Original extra entries only use even rows and even columns
    uint32_t count = 0;
    for (uint32_t row = 1; row < 20 && count < 30; row += 2) {
        for (uint32_t col = 1; col < 20 && count < 30; col += 2) {
            // Skip if on diagonal or adjacent to diagonal (tridiagonal entries exist)
            if (row != col && std::abs((int)row - (int)col) > 1) {
                matrix_large_change.add_entry(row, col, 0.5);  // NEW
                count++;
            }
        }
    }

    // Should FAIL with error (large change >20%)
    EXPECT_FALSE(solver.refactorize(matrix_large_change));
    EXPECT_NE(solver.last_error().find("significantly"), std::string::npos);
}

TEST_F(SelectiveReanalysisTest, RemovalOnlyChange) {
    // Remove ~10% of entries
    SparseMatrix matrix_removal;
    matrix_removal.n_rows = 20;
    matrix_removal.n_cols = 20;

    // Copy most entries except ~10%
    size_t skip_count = 0;
    for (size_t i = 0; i < matrix_init.rows.size(); ++i) {
        if (i % 10 == 0) {
            skip_count++;  // Skip this entry
        } else {
            matrix_removal.add_entry(matrix_init.rows[i], matrix_init.cols[i], matrix_init.values[i]);
        }
    }

    // Should succeed with warning (moderate change)
    EXPECT_TRUE(solver.refactorize(matrix_removal));
}

TEST_F(SelectiveReanalysisTest, MixedChangeBelowThreshold) {
    // Mix of additions and removals, total <5%
    SparseMatrix matrix_mixed = matrix_init;

    // Remove 1 entry (implicitly by not including it in new matrix)
    SparseMatrix matrix_clean;
    matrix_clean.n_rows = 20;
    matrix_clean.n_cols = 20;

    // Copy all but one entry
    for (size_t i = 1; i < matrix_init.rows.size(); ++i) {  // Skip first entry
        matrix_clean.add_entry(matrix_init.rows[i], matrix_init.cols[i], matrix_init.values[i]);
    }

    // Add 2 new entries (net change: 3 entries)
    matrix_clean.add_entry(0, 19, 0.5);
    matrix_clean.add_entry(19, 0, 0.5);

    // Should succeed with INFO (small change <5%)
    EXPECT_TRUE(solver.refactorize(matrix_clean));
}

TEST_F(SelectiveReanalysisTest, EdgeCase_ExactThreshold5Percent) {
    // Calculate exactly 5% change
    size_t current_nnz = matrix_init.values.size();
    size_t changes_for_5_percent = static_cast<size_t>(current_nnz * 0.05);

    SparseMatrix matrix_threshold = matrix_init;

    // Add exactly 5% new entries
    for (size_t i = 0; i < changes_for_5_percent; ++i) {
        uint32_t row = static_cast<uint32_t>(i % 20);
        uint32_t col = static_cast<uint32_t>((i + 5) % 20);
        if (row != col) {
            matrix_threshold.add_entry(row, col, 0.5);
        }
    }

    // At 5%, should trigger moderate change warning
    EXPECT_TRUE(solver.refactorize(matrix_threshold));
}

TEST_F(SelectiveReanalysisTest, EdgeCase_ExactThreshold20Percent) {
    // Calculate exactly 20% change
    size_t current_nnz = matrix_init.values.size();
    size_t changes_for_20_percent = static_cast<size_t>(current_nnz * 0.20);

    SparseMatrix matrix_threshold = matrix_init;

    // Add exactly 20% new entries
    for (size_t i = 0; i < changes_for_20_percent; ++i) {
        uint32_t row = static_cast<uint32_t>(i % 20);
        uint32_t col = static_cast<uint32_t>((i + 7) % 20);
        if (row != col) {
            matrix_threshold.add_entry(row, col, 0.5);
        }
    }

    // At 20%, should trigger moderate change warning (not error)
    EXPECT_TRUE(solver.refactorize(matrix_threshold));
}
