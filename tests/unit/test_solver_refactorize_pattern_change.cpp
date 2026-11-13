// tests/unit/test_solver_refactorize_pattern_change.cpp
// Ensure refactorize detects sparsity pattern change and fallback works

#include <gtest/gtest.h>
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"

using namespace kirimaspice;

TEST(SolverRefactorize, DetectsPatternChange_SuperLUMT) {
    // Force SuperLU_MT backend
    kirimaspice::solvers::set_cli_backend_override(std::make_optional<std::string>("SUPERLU_MT"));

    // Build a simple 3x3 diagonal matrix A0
    MNASystem sys0; sys0.resize(3, 0);
    sys0.G.add_entry(0,0, 2.0);
    sys0.G.add_entry(1,1, 3.0);
    sys0.G.add_entry(2,2, 4.0);
    sys0.b = {1.0, 1.0, 1.0};

    // First factorization
    auto solver = kirimaspice::solvers::make_solver(kirimaspice::solvers::choose_backend());
    ASSERT_TRUE(solver->analyze_and_factorize(sys0.G));

    // Change only values but keep pattern -> refactorize OK
    MNASystem sys1 = sys0;
    sys1.G.clear();
    sys1.G.n_rows = 3; sys1.G.n_cols = 3;
    sys1.G.add_entry(0,0, 5.0);
    sys1.G.add_entry(1,1, 6.0);
    sys1.G.add_entry(2,2, 7.0);
    EXPECT_TRUE(solver->refactorize(sys1.G));

    // Alter sparsity pattern (add off-diagonal entry) -> refactorize should fail
    MNASystem sys2 = sys1;
    sys2.G.add_entry(0,1, 0.1); // new non-zero, pattern changed
    EXPECT_FALSE(solver->refactorize(sys2.G));

    // Fallback: analyze full again should succeed
    EXPECT_TRUE(solver->analyze_and_factorize(sys2.G));

    kirimaspice::solvers::set_cli_backend_override(std::nullopt);
}
