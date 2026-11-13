// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_complex_matrix_types.cpp
// Unit tests for complex number matrix types
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/core/types.hpp"
#include <gtest/gtest.h>
#include <cmath>

using namespace kirimaspice;

// Test fixture for complex matrix types
class ComplexMatrixTypesTest : public ::testing::Test {
protected:
    // Helper function to compare complex numbers with tolerance
    bool complex_near(Complex a, Complex b, Real tol = 1e-10) {
        return std::abs(a - b) < tol;
    }
};

// Test ComplexSparseMatrix::add_entry
TEST_F(ComplexMatrixTypesTest, ComplexSparseMatrix_AddEntry) {
    ComplexSparseMatrix mat;

    // Add some entries
    mat.add_entry(0, 0, Complex(1.0, 2.0));
    mat.add_entry(0, 1, Complex(3.0, 4.0));
    mat.add_entry(1, 0, Complex(5.0, 6.0));
    mat.add_entry(1, 1, Complex(7.0, 8.0));

    EXPECT_EQ(mat.nnz(), 4);
    EXPECT_EQ(mat.n_rows, 2);
    EXPECT_EQ(mat.n_cols, 2);
}

// Test ComplexSparseMatrix::add_entry preserves zeros (for KLU fixed sparsity pattern)
// Zeros are NOT skipped to maintain fixed sparsity pattern
// required by KLU solver. This matches SparseMatrix behavior.
TEST_F(ComplexMatrixTypesTest, ComplexSparseMatrix_PreservesZeros) {
    ComplexSparseMatrix mat;

    // Add near-zero entries (NOT skipped - maintains fixed sparsity pattern)
    mat.add_entry(0, 0, Complex(1e-16, 1e-16));
    mat.add_entry(0, 1, Complex(0.0, 0.0));

    // Add non-zero entry
    mat.add_entry(1, 1, Complex(1.0, 0.0));

    // All 3 entries are stored (zeros not skipped)
    EXPECT_EQ(mat.nnz(), 3);
}

// Test ComplexSparseMatrix::to_csc
TEST_F(ComplexMatrixTypesTest, ComplexSparseMatrix_ToCSC) {
    ComplexSparseMatrix mat;

    // Create a 3x3 matrix:
    // [1+i    0    2+2i]
    // [0    3+3i    0  ]
    // [4+4i   0    5+5i]
    mat.n_rows = 3;
    mat.n_cols = 3;

    mat.add_entry(0, 0, Complex(1.0, 1.0));
    mat.add_entry(0, 2, Complex(2.0, 2.0));
    mat.add_entry(1, 1, Complex(3.0, 3.0));
    mat.add_entry(2, 0, Complex(4.0, 4.0));
    mat.add_entry(2, 2, Complex(5.0, 5.0));

    std::vector<std::uint32_t> col_ptr;
    std::vector<std::uint32_t> row_idx;
    std::vector<Complex> values;

    mat.to_csc(col_ptr, row_idx, values);

    // Verify CSC structure
    EXPECT_EQ(col_ptr.size(), 4);  // n_cols + 1
    EXPECT_EQ(col_ptr[0], 0);
    EXPECT_EQ(col_ptr[1], 2);      // Column 0 has 2 entries
    EXPECT_EQ(col_ptr[2], 3);      // Column 1 has 1 entry
    EXPECT_EQ(col_ptr[3], 5);      // Column 2 has 2 entries

    EXPECT_EQ(row_idx.size(), 5);
    EXPECT_EQ(values.size(), 5);

    // Column 0: rows 0, 2
    EXPECT_EQ(row_idx[0], 0);
    EXPECT_TRUE(complex_near(values[0], Complex(1.0, 1.0)));
    EXPECT_EQ(row_idx[1], 2);
    EXPECT_TRUE(complex_near(values[1], Complex(4.0, 4.0)));

    // Column 1: row 1
    EXPECT_EQ(row_idx[2], 1);
    EXPECT_TRUE(complex_near(values[2], Complex(3.0, 3.0)));

    // Column 2: rows 0, 2
    EXPECT_EQ(row_idx[3], 0);
    EXPECT_TRUE(complex_near(values[3], Complex(2.0, 2.0)));
    EXPECT_EQ(row_idx[4], 2);
    EXPECT_TRUE(complex_near(values[4], Complex(5.0, 5.0)));
}

// Test ComplexSparseMatrix::to_csc with duplicate entries
TEST_F(ComplexMatrixTypesTest, ComplexSparseMatrix_ToCSC_Duplicates) {
    ComplexSparseMatrix mat;
    mat.n_rows = 2;
    mat.n_cols = 2;

    // Add duplicate entries (should be summed)
    mat.add_entry(0, 0, Complex(1.0, 1.0));
    mat.add_entry(0, 0, Complex(2.0, 2.0));
    mat.add_entry(0, 0, Complex(3.0, 3.0));

    std::vector<std::uint32_t> col_ptr;
    std::vector<std::uint32_t> row_idx;
    std::vector<Complex> values;

    mat.to_csc(col_ptr, row_idx, values);

    // Duplicates should be summed: (1+i) + (2+2i) + (3+3i) = (6+6i)
    EXPECT_EQ(values.size(), 1);
    EXPECT_TRUE(complex_near(values[0], Complex(6.0, 6.0)));
}

// Test ComplexSparseMatrix::clear
TEST_F(ComplexMatrixTypesTest, ComplexSparseMatrix_Clear) {
    ComplexSparseMatrix mat;
    mat.add_entry(0, 0, Complex(1.0, 1.0));
    mat.add_entry(0, 1, Complex(2.0, 2.0));

    EXPECT_EQ(mat.nnz(), 2);

    mat.clear();

    EXPECT_EQ(mat.nnz(), 0);
    EXPECT_EQ(mat.n_rows, 0);
    EXPECT_EQ(mat.n_cols, 0);
}

// Test ComplexMNASystem::resize
TEST_F(ComplexMatrixTypesTest, ComplexMNASystem_Resize) {
    ComplexMNASystem sys;

    sys.resize(3, 2);  // 3 nodes, 2 branches

    EXPECT_EQ(sys.num_nodes, 3);
    EXPECT_EQ(sys.num_branches, 2);
    EXPECT_EQ(sys.size(), 5);
    EXPECT_EQ(sys.I.size(), 5);
    EXPECT_EQ(sys.V.size(), 5);
    EXPECT_EQ(sys.Y.n_rows, 5);
    EXPECT_EQ(sys.Y.n_cols, 5);

    // Check backward compatibility aliases
    EXPECT_EQ(sys.n_nodes, 3);
    EXPECT_EQ(sys.n_branches, 2);
}

// Test ComplexMNASystem::zero
TEST_F(ComplexMatrixTypesTest, ComplexMNASystem_Zero) {
    ComplexMNASystem sys;
    sys.resize(2, 1);

    // Add some entries
    sys.Y.add_entry(0, 0, Complex(1.0, 1.0));
    sys.I[0] = Complex(2.0, 2.0);
    sys.V[0] = Complex(3.0, 3.0);

    sys.zero();

    // Check that vectors are zeroed
    for (size_t i = 0; i < sys.I.size(); ++i) {
        EXPECT_TRUE(complex_near(sys.I[i], Complex(0.0, 0.0)));
        EXPECT_TRUE(complex_near(sys.V[i], Complex(0.0, 0.0)));
    }

    // Check that matrix is cleared
    EXPECT_EQ(sys.Y.nnz(), 0);
}

// Test ComplexMNASystem basic usage
TEST_F(ComplexMatrixTypesTest, ComplexMNASystem_BasicUsage) {
    ComplexMNASystem sys;
    sys.resize(2, 0);  // 2 nodes (excluding GND), 0 branches

    // Build a simple admittance matrix for a resistor divider at AC
    // Y[0][0] = 1/R1 + 1/R2 = 1/1k + 1/1k = 0.002 S
    // Y[0][1] = -1/R2 = -0.001 S
    // Y[1][0] = -1/R2 = -0.001 S
    // Y[1][1] = 1/R2 = 0.001 S

    Real G1 = 0.001;  // 1/1k
    Real G2 = 0.001;  // 1/1k

    sys.Y.add_entry(0, 0, Complex(G1 + G2, 0.0));
    sys.Y.add_entry(0, 1, Complex(-G2, 0.0));
    sys.Y.add_entry(1, 0, Complex(-G2, 0.0));
    sys.Y.add_entry(1, 1, Complex(G2, 0.0));

    // Current source at node 0: I[0] = 1A (AC magnitude)
    sys.I[0] = Complex(1.0, 0.0);
    sys.I[1] = Complex(0.0, 0.0);

    // Verify matrix structure
    EXPECT_EQ(sys.Y.nnz(), 4);

    // Note: We don't solve the system here (that's for ac_solver tests)
    // This test just verifies that the data structure works correctly
}

// Test complex number arithmetic in matrix operations
TEST_F(ComplexMatrixTypesTest, ComplexArithmetic) {
    ComplexSparseMatrix mat;

    // Test addition of complex entries
    Complex z1(1.0, 2.0);
    Complex z2(3.0, 4.0);
    Complex z_sum = z1 + z2;

    EXPECT_DOUBLE_EQ(z_sum.real(), 4.0);
    EXPECT_DOUBLE_EQ(z_sum.imag(), 6.0);

    // Test multiplication
    Complex z_prod = z1 * z2;
    // (1+2i) * (3+4i) = 3 + 4i + 6i + 8i^2 = 3 + 10i - 8 = -5 + 10i
    EXPECT_DOUBLE_EQ(z_prod.real(), -5.0);
    EXPECT_DOUBLE_EQ(z_prod.imag(), 10.0);

    // Test magnitude
    EXPECT_DOUBLE_EQ(std::abs(z1), std::sqrt(5.0));  // sqrt(1^2 + 2^2)
}

// Test capacitor admittance Y_C = j*omega*C
TEST_F(ComplexMatrixTypesTest, CapacitorAdmittance) {
    Real C = 1e-6;  // 1 μF
    Real f = 1000.0;  // 1 kHz
    Real omega = 2.0 * M_PI * f;  // rad/s

    // Y_C = j*omega*C
    Complex Y_C(0.0, omega * C);

    EXPECT_DOUBLE_EQ(Y_C.real(), 0.0);
    EXPECT_NEAR(Y_C.imag(), 0.00628, 1e-5);  // ~6.28e-3

    // At 1 kHz, capacitor impedance: |Z_C| = 1/(omega*C) ~ 159 Ω
    Real Z_mag = 1.0 / std::abs(Y_C);
    EXPECT_NEAR(Z_mag, 159.15, 0.1);
}

// Test inductor admittance Y_L = 1/(j*omega*L)
TEST_F(ComplexMatrixTypesTest, InductorAdmittance) {
    Real L = 1e-3;  // 1 mH
    Real f = 1000.0;  // 1 kHz
    Real omega = 2.0 * M_PI * f;  // rad/s

    // Y_L = 1/(j*omega*L) = -j/(omega*L)
    Complex j_omega_L(0.0, omega * L);
    Complex Y_L = Complex(1.0, 0.0) / j_omega_L;

    EXPECT_NEAR(Y_L.real(), 0.0, 1e-10);
    EXPECT_LT(Y_L.imag(), 0.0);  // Negative imaginary part

    // At 1 kHz, inductor impedance: |Z_L| = omega*L ~ 6.28 Ω
    Real Z_mag = 1.0 / std::abs(Y_L);
    EXPECT_NEAR(Z_mag, 6.28, 0.01);
}
