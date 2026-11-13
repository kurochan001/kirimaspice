// tests/unit/test_types.cpp
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"

#include <gtest/gtest.h>

using namespace kirimaspice;

// Test basic type definitions
TEST(TypesTest, BasicTypes) {
    EXPECT_EQ(sizeof(Real), 8); // double = 8 bytes
    EXPECT_EQ(NODE_GND, 0u);
    EXPECT_EQ(NODE_INVALID, UINT32_MAX);
}

// Test SimulationConfig default values
TEST(TypesTest, SimulationConfigDefaults) {
    SimulationConfig config;

    EXPECT_DOUBLE_EQ(config.temperature, 300.15);
    EXPECT_DOUBLE_EQ(config.gmin, 1e-12);

    EXPECT_EQ(config.dc.max_iterations, 100u);
    EXPECT_DOUBLE_EQ(config.dc.abstol, 1e-12);
    EXPECT_DOUBLE_EQ(config.dc.reltol, 1e-3);
    EXPECT_DOUBLE_EQ(config.dc.vntol, 1e-6);

    EXPECT_DOUBLE_EQ(config.transient.start_time, 0.0);
    EXPECT_DOUBLE_EQ(config.transient.end_time, 1e-3);
    EXPECT_DOUBLE_EQ(config.transient.max_step, 1e-6);
    EXPECT_DOUBLE_EQ(config.transient.min_step, 1e-15);
    EXPECT_DOUBLE_EQ(config.transient.initial_step, 1e-9);

    EXPECT_EQ(config.transient.method, IntegrationMethod::TRAPEZOIDAL);
    EXPECT_EQ(config.transient.error_est, ErrorEstimation::PHYSICAL);
}

// Test SolverStats initialization
TEST(TypesTest, SolverStatsInitialization) {
    SolverStats stats;

    EXPECT_EQ(stats.total_iterations, 0u);
    EXPECT_EQ(stats.function_evals, 0u);
    EXPECT_EQ(stats.jacobian_evals, 0u);
    EXPECT_EQ(stats.linear_solves, 0u);
    EXPECT_EQ(stats.timestep_rejects, 0u);
    EXPECT_DOUBLE_EQ(stats.total_time_seconds, 0.0);
}

// Test SparseMatrix basic operations
TEST(MatrixTypesTest, SparseMatrixAddEntry) {
    SparseMatrix mat;

    mat.add_entry(0, 0, 1.0);
    mat.add_entry(0, 1, 2.0);
    mat.add_entry(1, 1, 3.0);

    EXPECT_EQ(mat.nnz(), 3u);
    EXPECT_EQ(mat.n_rows, 2u);
    EXPECT_EQ(mat.n_cols, 2u);
}

// Test SparseMatrix keeps zero entries (required for KLU fixed sparsity pattern)
// See matrix_types.cpp:10-16 for detailed rationale
TEST(MatrixTypesTest, SparseMatrixKeepsZeros) {
    SparseMatrix mat;

    mat.add_entry(0, 0, 1.0);
    mat.add_entry(0, 1, 0.0); // Intentionally kept (not ignored)
    mat.add_entry(1, 1, 2.0);

    // Zero entries are kept to maintain fixed sparsity pattern for KLU solver
    EXPECT_EQ(mat.nnz(), 3u);
}

// Test SparseMatrix clear
TEST(MatrixTypesTest, SparseMatrixClear) {
    SparseMatrix mat;

    mat.add_entry(0, 0, 1.0);
    mat.add_entry(1, 1, 2.0);
    EXPECT_EQ(mat.nnz(), 2u);

    mat.clear();
    EXPECT_EQ(mat.nnz(), 0u);
    EXPECT_EQ(mat.n_rows, 0u);
    EXPECT_EQ(mat.n_cols, 0u);
}

// Test SparseMatrix to CSC conversion
TEST(MatrixTypesTest, SparseMatrixToCSC) {
    SparseMatrix mat;

    // Build a 3x3 matrix:
    // [1  2  0]
    // [0  3  4]
    // [5  0  6]
    mat.add_entry(0, 0, 1.0);
    mat.add_entry(0, 1, 2.0);
    mat.add_entry(1, 1, 3.0);
    mat.add_entry(1, 2, 4.0);
    mat.add_entry(2, 0, 5.0);
    mat.add_entry(2, 2, 6.0);

    std::vector<std::uint32_t> col_ptr, row_idx;
    std::vector<Real> values;

    mat.to_csc(col_ptr, row_idx, values);

    // Expected CSC format:
    // col_ptr = [0, 2, 4, 6]
    // row_idx = [0, 2, 0, 1, 1, 2]
    // values  = [1, 5, 2, 3, 4, 6]

    ASSERT_EQ(col_ptr.size(), 4u);
    EXPECT_EQ(col_ptr[0], 0u);
    EXPECT_EQ(col_ptr[1], 2u);
    EXPECT_EQ(col_ptr[2], 4u);
    EXPECT_EQ(col_ptr[3], 6u);

    ASSERT_EQ(row_idx.size(), 6u);
    ASSERT_EQ(values.size(), 6u);

    // Column 0: entries (0,0)=1, (2,0)=5
    EXPECT_EQ(row_idx[0], 0u);
    EXPECT_DOUBLE_EQ(values[0], 1.0);
    EXPECT_EQ(row_idx[1], 2u);
    EXPECT_DOUBLE_EQ(values[1], 5.0);

    // Column 1: entries (0,1)=2, (1,1)=3
    EXPECT_EQ(row_idx[2], 0u);
    EXPECT_DOUBLE_EQ(values[2], 2.0);
    EXPECT_EQ(row_idx[3], 1u);
    EXPECT_DOUBLE_EQ(values[3], 3.0);

    // Column 2: entries (1,2)=4, (2,2)=6
    EXPECT_EQ(row_idx[4], 1u);
    EXPECT_DOUBLE_EQ(values[4], 4.0);
    EXPECT_EQ(row_idx[5], 2u);
    EXPECT_DOUBLE_EQ(values[5], 6.0);
}

// Test MNASystem resize
TEST(MatrixTypesTest, MNASystemResize) {
    MNASystem mna;

    mna.resize(10, 2);

    EXPECT_EQ(mna.n_nodes, 10u);
    EXPECT_EQ(mna.n_branches, 2u);
    EXPECT_EQ(mna.size(), 12u);
    EXPECT_EQ(mna.b.size(), 12u);
    EXPECT_EQ(mna.x.size(), 12u);
}

// Test MNASystem zero
TEST(MatrixTypesTest, MNASystemZero) {
    MNASystem mna;
    mna.resize(5, 1);

    // Set some values
    mna.b[0] = 1.0;
    mna.x[0] = 2.0;
    mna.G.add_entry(0, 0, 3.0);

    EXPECT_EQ(mna.G.nnz(), 1u);

    // Zero the system
    mna.zero();

    EXPECT_DOUBLE_EQ(mna.b[0], 0.0);
    EXPECT_DOUBLE_EQ(mna.x[0], 0.0);
    EXPECT_EQ(mna.G.nnz(), 0u);
}
