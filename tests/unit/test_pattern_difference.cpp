// tests/unit/test_pattern_difference.cpp
// Unit tests.1: Sparse pattern difference computation

#include <gtest/gtest.h>
#include "kirimaspice/solvers/klu_solver.hpp"
#include "kirimaspice/core/matrix_types.hpp"

using namespace kirimaspice;
using namespace kirimaspice::solvers;

// Test fixture for pattern difference tests
class PatternDifferenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize solver with a simple 3x3 matrix
        //   1  2  0
        //   0  3  0
        //   4  0  5
        SparseMatrix matrix_init;
        matrix_init.n_rows = 3;
        matrix_init.n_cols = 3;
        matrix_init.add_entry(0, 0, 1.0);  // (0,0)
        matrix_init.add_entry(0, 1, 2.0);  // (0,1)
        matrix_init.add_entry(1, 1, 3.0);  // (1,1)
        matrix_init.add_entry(2, 0, 4.0);  // (2,0)
        matrix_init.add_entry(2, 2, 5.0);  // (2,2)

        solver.analyze_and_factorize(matrix_init);
    }

    KLUSolver solver;
};

TEST_F(PatternDifferenceTest, NoChange) {
    // Same matrix - no change
    SparseMatrix matrix_same;
    matrix_same.n_rows = 3;
    matrix_same.n_cols = 3;
    matrix_same.add_entry(0, 0, 1.5);  // Value changed, but pattern same
    matrix_same.add_entry(0, 1, 2.5);
    matrix_same.add_entry(1, 1, 3.5);
    matrix_same.add_entry(2, 0, 4.5);
    matrix_same.add_entry(2, 2, 5.5);

    PatternDifference diff;
    EXPECT_TRUE(solver.compute_pattern_difference(matrix_same, diff));
    EXPECT_FALSE(diff.has_changes());
    EXPECT_EQ(diff.added_entries.size(), 0);
    EXPECT_EQ(diff.removed_entries.size(), 0);
    EXPECT_EQ(diff.total_changes, 0);
}

TEST_F(PatternDifferenceTest, AddedEntries) {
    // Matrix with added entries
    SparseMatrix matrix_added;
    matrix_added.n_rows = 3;
    matrix_added.n_cols = 3;
    // Original entries
    matrix_added.add_entry(0, 0, 1.0);
    matrix_added.add_entry(0, 1, 2.0);
    matrix_added.add_entry(1, 1, 3.0);
    matrix_added.add_entry(2, 0, 4.0);
    matrix_added.add_entry(2, 2, 5.0);
    // New entries
    matrix_added.add_entry(0, 2, 6.0);  // NEW (0,2)
    matrix_added.add_entry(1, 0, 7.0);  // NEW (1,0)

    PatternDifference diff;
    EXPECT_TRUE(solver.compute_pattern_difference(matrix_added, diff));
    EXPECT_TRUE(diff.has_changes());
    EXPECT_EQ(diff.added_entries.size(), 2);
    EXPECT_EQ(diff.removed_entries.size(), 0);
    EXPECT_EQ(diff.total_changes, 2);

    // Check added entries
    std::set<std::pair<uint32_t, uint32_t>> added_set(diff.added_entries.begin(), diff.added_entries.end());
    EXPECT_TRUE(added_set.count({0, 2}) > 0);
    EXPECT_TRUE(added_set.count({1, 0}) > 0);
}

TEST_F(PatternDifferenceTest, RemovedEntries) {
    // Matrix with removed entries
    SparseMatrix matrix_removed;
    matrix_removed.n_rows = 3;
    matrix_removed.n_cols = 3;
    // Remove (0,1) and (2,0) from original
    matrix_removed.add_entry(0, 0, 1.0);
    matrix_removed.add_entry(1, 1, 3.0);
    matrix_removed.add_entry(2, 2, 5.0);

    PatternDifference diff;
    EXPECT_TRUE(solver.compute_pattern_difference(matrix_removed, diff));
    EXPECT_TRUE(diff.has_changes());
    EXPECT_EQ(diff.added_entries.size(), 0);
    EXPECT_EQ(diff.removed_entries.size(), 2);
    EXPECT_EQ(diff.total_changes, 2);

    // Check removed entries
    std::set<std::pair<uint32_t, uint32_t>> removed_set(diff.removed_entries.begin(), diff.removed_entries.end());
    EXPECT_TRUE(removed_set.count({0, 1}) > 0);
    EXPECT_TRUE(removed_set.count({2, 0}) > 0);
}

TEST_F(PatternDifferenceTest, MixedChanges) {
    // Matrix with both added and removed entries
    SparseMatrix matrix_mixed;
    matrix_mixed.n_rows = 3;
    matrix_mixed.n_cols = 3;
    // Keep (0,0), (1,1), (2,2)
    matrix_mixed.add_entry(0, 0, 1.0);
    matrix_mixed.add_entry(1, 1, 3.0);
    matrix_mixed.add_entry(2, 2, 5.0);
    // Remove (0,1) and (2,0)
    // Add (1,0) and (1,2)
    matrix_mixed.add_entry(1, 0, 6.0);  // NEW
    matrix_mixed.add_entry(1, 2, 7.0);  // NEW

    PatternDifference diff;
    EXPECT_TRUE(solver.compute_pattern_difference(matrix_mixed, diff));
    EXPECT_TRUE(diff.has_changes());
    EXPECT_EQ(diff.added_entries.size(), 2);
    EXPECT_EQ(diff.removed_entries.size(), 2);
    EXPECT_EQ(diff.total_changes, 4);
}

TEST_F(PatternDifferenceTest, ChangeRatioCalculation) {
    // Matrix with 20% change
    SparseMatrix matrix_changed;
    matrix_changed.n_rows = 3;
    matrix_changed.n_cols = 3;
    // Original: 5 entries
    // Keep 4, remove 1, add 0 → 1 change, 4 total entries
    matrix_changed.add_entry(0, 0, 1.0);
    matrix_changed.add_entry(0, 1, 2.0);
    matrix_changed.add_entry(1, 1, 3.0);
    matrix_changed.add_entry(2, 2, 5.0);
    // Removed (2,0)

    PatternDifference diff;
    EXPECT_TRUE(solver.compute_pattern_difference(matrix_changed, diff));

    // Original had 5 entries
    double ratio = diff.change_ratio(5);
    EXPECT_DOUBLE_EQ(ratio, 0.2);  // 1 change / 5 entries = 20%
}

TEST_F(PatternDifferenceTest, EmptyMatrixDifference) {
    // Completely empty new matrix (all entries removed)
    SparseMatrix matrix_empty;
    matrix_empty.n_rows = 3;
    matrix_empty.n_cols = 3;
    // No entries

    PatternDifference diff;
    EXPECT_TRUE(solver.compute_pattern_difference(matrix_empty, diff));
    EXPECT_TRUE(diff.has_changes());
    EXPECT_EQ(diff.added_entries.size(), 0);
    EXPECT_EQ(diff.removed_entries.size(), 5);  // All 5 original entries removed
    EXPECT_EQ(diff.total_changes, 5);
}

TEST_F(PatternDifferenceTest, DimensionMismatch) {
    // Matrix with wrong dimensions
    SparseMatrix matrix_wrong_size;
    matrix_wrong_size.n_rows = 4;  // Wrong size (should be 3)
    matrix_wrong_size.n_cols = 4;
    matrix_wrong_size.add_entry(0, 0, 1.0);

    PatternDifference diff;
    EXPECT_FALSE(solver.compute_pattern_difference(matrix_wrong_size, diff));
}

TEST(PatternDifferenceStructTest, HasChanges) {
    PatternDifference diff;
    EXPECT_FALSE(diff.has_changes());

    diff.added_entries.push_back({0, 1});
    EXPECT_TRUE(diff.has_changes());

    diff.added_entries.clear();
    diff.removed_entries.push_back({2, 3});
    EXPECT_TRUE(diff.has_changes());
}

TEST(PatternDifferenceStructTest, ChangeRatioEdgeCases) {
    PatternDifference diff;

    // Zero total entries
    EXPECT_DOUBLE_EQ(diff.change_ratio(0), 0.0);

    // 100% change
    diff.total_changes = 10;
    EXPECT_DOUBLE_EQ(diff.change_ratio(10), 1.0);

    // 50% change
    diff.total_changes = 5;
    EXPECT_DOUBLE_EQ(diff.change_ratio(10), 0.5);
}
