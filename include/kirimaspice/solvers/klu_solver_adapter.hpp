// include/kirimaspice/solvers/klu_solver_adapter.hpp
#pragma once

#include "kirimaspice/solvers/isparse_solver.hpp"
#include "kirimaspice/solvers/klu_solver.hpp"

namespace kirimaspice::solvers {

/// Adapter: existing KLUSolver -> ISparseLinearSolver
class KluSolverAdapter final : public ISparseLinearSolver {
public:
    KluSolverAdapter() = default;
    ~KluSolverAdapter() override = default;

    bool analyze_and_factorize(const SparseMatrix& matrix) override {
        return klu_.analyze_and_factorize(matrix);
    }

    bool refactorize(const SparseMatrix& matrix) override {
        return klu_.refactorize(matrix);
    }

    bool solve(const DenseVector& b, DenseVector& x) override {
        return klu_.solve(b, x);
    }

    const std::string& last_error() const override {
        return klu_.last_error();
    }

    void reset() override { klu_.reset(); }

private:
    KLUSolver klu_;
};

} // namespace kirimaspice::solvers

