// include/kirimaspice/solvers/superlu_mt_solver.hpp
#pragma once

#include "kirimaspice/solvers/isparse_solver.hpp"

#include <string>
#include <vector>

namespace kirimaspice::solvers {

/// SuperLU_MT backend（最小実装）。
/// 注意: 本最小版は SuperLU(または SuperLU_MT) の dgssv を用いた一括因子化+解法で、
///       analyze/refactorize の再利用は行いません（将来拡張）。
class SuperLUMTSolver final : public ISparseLinearSolver {
public:
    SuperLUMTSolver();
    ~SuperLUMTSolver() override;

    bool analyze_and_factorize(const SparseMatrix& matrix) override;
    bool refactorize(const SparseMatrix& matrix) override;
    bool solve(const DenseVector& b, DenseVector& x) override;

    const std::string& last_error() const override { return last_error_; }
    void reset() override;

private:
    std::vector<int> colptr_;
    std::vector<int> rowind_;
    std::vector<Real> values_;
    std::uint32_t n_ = 0;
    std::string last_error_;

#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
    // Factorization state (opaque in header; real types managed in .cpp)
    struct SuperState {
        void* L = nullptr; // SuperMatrix*
        void* U = nullptr; // SuperMatrix*
        bool have_factors = false;
        // permutations
        std::vector<int> perm_c;
        std::vector<int> perm_r;
        // equilibration (not used when Equil=NO, but required by API)
        char equed = 'N';
        std::vector<double> R, C; // length n when used
        int inner_threads = 0; // from ENV
    } state_;
#endif
};

} // namespace kirimaspice::solvers
