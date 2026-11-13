// include/kirimaspice/solvers/superlu_dist_solver.hpp
#pragma once

#include "kirimaspice/solvers/isparse_solver.hpp"
#include <string>

namespace kirimaspice::solvers {

/// SuperLU_DIST バックエンド（スケルトン）。未実装ビルドでは常に失敗を返す。
class SuperLUDISTSolver final : public ISparseLinearSolver {
public:
    bool analyze_and_factorize(const SparseMatrix&) override {
        last_error_ = "SuperLU_DIST backend not available in this build";
        return false;
    }
    bool refactorize(const SparseMatrix&) override {
        last_error_ = "SuperLU_DIST backend not available in this build";
        return false;
    }
    bool solve(const DenseVector&, DenseVector&) override {
        last_error_ = "SuperLU_DIST backend not available in this build";
        return false;
    }
    const std::string& last_error() const override { return last_error_; }
    void reset() override { last_error_.clear(); }

private:
    std::string last_error_;
};

} // namespace kirimaspice::solvers

