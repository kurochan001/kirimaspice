// src/solvers/superlu_mt_solver.cpp
#include "kirimaspice/solvers/superlu_mt_solver.hpp"
#include <spdlog/spdlog.h>

#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
  // Use different headers depending on source vs system installation
  #if defined(KIRIMASPICE_SUPERLUMT_FROM_SOURCE) && (KIRIMASPICE_SUPERLUMT_FROM_SOURCE==1)
    // FetchContent SuperLU_MT headers
    #include <slu_mt_ddefs.h>  // SuperLU_MT double precision definitions
    #include <slu_mt_util.h>   // SuperLU_MT utility functions
  #else
    // System-installed SuperLU headers
    #include <slu_ddefs.h>     // Serial/MT common double precision definitions
    #include <slu_util.h>
  #endif
  #ifdef _OPENMP
    #include <omp.h>
  #endif
  #include <cctype>
  #include <memory>   // For std::unique_ptr (exception-safe memory management)
  #include <string>
#endif

namespace kirimaspice::solvers {

// Helpers: parse SuperLU options from ENV
// Thread-safe environment variable access
// These functions use C++11 static local variable initialization which is
// guaranteed to be thread-safe. Environment variables are read once and cached.
#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
static colperm_t get_colperm_from_env() {
    static const colperm_t cached = [] {
        const char* env = std::getenv("KIRIMASPICE_SUPERLU_COLPERM");
        if (!env) return COLAMD;
        std::string s(env);
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (s == "COLAMD") return COLAMD;
        if (s == "MMD_AT_PLUS_A" || s == "MMD" || s == "MMDATA+AT") return MMD_AT_PLUS_A;
        if (s == "MMD_ATA") return MMD_ATA;
        if (s == "NATURAL") return NATURAL;
        return COLAMD; // default
    }();
    return cached;
}

static yes_no_t get_equil_from_env() {
    static const yes_no_t cached = [] {
        const char* env = std::getenv("KIRIMASPICE_SUPERLU_EQUIL");
        if (!env) return NO;
        std::string s(env);
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (s == "1" || s == "ON" || s == "YES" || s == "TRUE") return YES;
        return NO;
    }();
    return cached;
}

// Thread-safe cached getters for runtime options
static bool get_reuse_disabled_from_env() {
    static const bool cached = [] {
        if (const char* env = std::getenv("KIRIMASPICE_SUPERLU_REUSE")) {
            std::string s(env);
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return (s == "0" || s == "off" || s == "false");
        }
        return false;  // default: reuse enabled
    }();
    return cached;
}

static int get_nprocs_from_env() {
    static const int cached = [] {
        if (const char* env = std::getenv("KIRIMASPICE_INNER_SOLVER_THREADS")) {
            try { return std::max(1, std::stoi(env)); } catch (...) { return 1; }
        }
        return 1;  // default: 1 thread
    }();
    return cached;
}
#endif

SuperLUMTSolver::SuperLUMTSolver() = default;
SuperLUMTSolver::~SuperLUMTSolver() {
#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
    if (state_.have_factors) {
        SuperMatrix* L = static_cast<SuperMatrix*>(state_.L);
        SuperMatrix* U = static_cast<SuperMatrix*>(state_.U);
        // Two-step cleanup is intentional:
        // 1. Destroy_*_Matrix() deallocates internal data (Store, nzval, rowind, etc.)
        // 2. delete frees the SuperMatrix wrapper struct itself (allocated with new)
        if (L) Destroy_SuperNode_Matrix(L);
        if (U) Destroy_CompCol_Matrix(U);
        delete L;
        delete U;
        state_.L = nullptr;
        state_.U = nullptr;
        state_.have_factors = false;
    }
#endif
}

void SuperLUMTSolver::reset() {
    colptr_.clear();
    rowind_.clear();
    values_.clear();
    n_ = 0;
    last_error_.clear();
#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
    if (state_.have_factors) {
        SuperMatrix* L = static_cast<SuperMatrix*>(state_.L);
        SuperMatrix* U = static_cast<SuperMatrix*>(state_.U);
        // Two-step cleanup is intentional:
        // 1. Destroy_*_Matrix() deallocates internal data (Store, nzval, rowind, etc.)
        // 2. delete frees the SuperMatrix wrapper struct itself (allocated with new)
        if (L) Destroy_SuperNode_Matrix(L);
        if (U) Destroy_CompCol_Matrix(U);
        delete L;
        delete U;
        state_.L = nullptr;
        state_.U = nullptr;
        state_.have_factors = false;
    }
    state_.perm_c.clear();
    state_.perm_r.clear();
    state_.R.clear();
    state_.C.clear();
#endif
}

bool SuperLUMTSolver::analyze_and_factorize(const SparseMatrix& matrix) {
    reset();
    if (matrix.n_rows != matrix.n_cols) {
        last_error_ = "Matrix must be square";
        return false;
    }
    if (matrix.n_rows == 0) {
        last_error_ = "Matrix dimension is zero";
        return false;
    }

    // COO -> CSC（0-based）
    std::vector<std::uint32_t> colptr_u32, rowind_u32;
    std::vector<Real> values;
    matrix.to_csc(colptr_u32, rowind_u32, values);

    // 保存（int32 に詰め替え）
    colptr_.assign(colptr_u32.begin(), colptr_u32.end());
    rowind_.assign(rowind_u32.begin(), rowind_u32.end());
    values_ = std::move(values);
    n_ = matrix.n_rows;

#if !(defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1))
    (void)colptr_;
    (void)rowind_;
    (void)values_;
    spdlog::warn("SuperLU_MT backend requested but not available at build time; will fallback at solve()");
    return true;
#else
    // Optional runtime disable of reuse path (fallback to one-shot):
    // Use thread-safe cached getter
    if (get_reuse_disabled_from_env()) {
        state_.have_factors = false; state_.perm_c.clear(); state_.perm_r.clear();
        state_.R.clear(); state_.C.clear(); state_.equed = 'N';
        return true;
    }

    // Get number of threads from cached environment variable
    // Use thread-safe cached getter
    int nprocs = get_nprocs_from_env();
    state_.inner_threads = nprocs;
#ifdef _OPENMP
    if (nprocs > 0) omp_set_num_threads(nprocs);
#endif

    state_.perm_c.assign(n_, 0);
    state_.perm_r.assign(n_, 0);

    SuperMatrix A;
    dCreate_CompCol_Matrix(&A,
        static_cast<int>(n_), static_cast<int>(n_),
        static_cast<int>(values_.size()),
        const_cast<double*>(reinterpret_cast<const double*>(values_.data())),
        rowind_.data(), colptr_.data(), SLU_NC, SLU_D, SLU_GE);

    // Use unique_ptr for exception safety - will auto-delete on early return
    auto matrix_deleter = [](SuperMatrix* m) { delete m; };
    std::unique_ptr<SuperMatrix, decltype(matrix_deleter)> L_ptr(new SuperMatrix(), matrix_deleter);
    std::unique_ptr<SuperMatrix, decltype(matrix_deleter)> U_ptr(new SuperMatrix(), matrix_deleter);
    SuperMatrix* L = L_ptr.get();
    SuperMatrix* U = U_ptr.get();
    int_t info = 0;

#if defined(KIRIMASPICE_SUPERLUMT_FROM_SOURCE) && (KIRIMASPICE_SUPERLUMT_FROM_SOURCE==1)
    // Use parallel SuperLU_MT API (source-built version)
    const int panel_size = sp_ienv(1);
    const int relax = sp_ienv(2);
    const double diag_pivot_thresh = 1.0;
    const yes_no_t refact = NO;
    const yes_no_t usepr = NO;
    const double drop_tol = 0.0;
    void* work = nullptr;
    int lwork = 0;

    SuperMatrix AC;
    superlumt_options_t pdgstrf_options;
    Gstat_t Gstat;

    // Allocate statistics
    StatAlloc(static_cast<int>(n_), nprocs, panel_size, relax, &Gstat);
    StatInit(static_cast<int>(n_), nprocs, &Gstat);

    // Initialize options and perform column ordering via pdgstrf_init
    pdgstrf_init(nprocs, DOFACT, NOTRANS, refact,
                 panel_size, relax, diag_pivot_thresh, usepr, drop_tol,
                 state_.perm_c.data(), state_.perm_r.data(),
                 work, lwork, &A, &AC, &pdgstrf_options, &Gstat);

    // Perform parallel factorization
    pdgstrf(&pdgstrf_options, &AC, state_.perm_r.data(), L, U, &Gstat, &info);

    // Cleanup
    pdgstrf_finalize(&pdgstrf_options, &AC);
    Destroy_SuperMatrix_Store(&A);
    StatFree(&Gstat);
#else
    // Use serial SuperLU API (system-installed version)
    superlu_options_t options; set_default_options(&options);
    options.Fact = DOFACT;
    options.Equil = get_equil_from_env();
    options.ColPerm = get_colperm_from_env();

    std::vector<int> etree(n_, 0);

    // Column ordering
    if (options.ColPerm != MY_PERMC)
        get_perm_c(options.ColPerm, &A, state_.perm_c.data());

    // Preorder
    SuperMatrix AC;
    sp_preorder(&options, &A, state_.perm_c.data(), etree.data(), &AC);

    // Factorization parameters
    const int panel_size = sp_ienv(1);
    const int relax = sp_ienv(2);

    SuperLUStat_t stat; StatInit(&stat);
    GlobalLU_t glu;

    dgstrf(&options, &AC, relax, panel_size,
           etree.data(), nullptr, 0, state_.perm_c.data(), state_.perm_r.data(),
           L, U, &glu, &stat, &info);

    Destroy_CompCol_Permuted(&AC);
    Destroy_SuperMatrix_Store(&A);
    StatFree(&stat);
#endif

    if (info != 0) {
        // unique_ptr will auto-delete L and U on return
        last_error_ = "SuperLU factorization failed (info=" + std::to_string((long long)info) + ")";
        return false;
    }

    // Transfer ownership from unique_ptr to state_
    state_.L = L_ptr.release();
    state_.U = U_ptr.release();
    state_.have_factors = true;
    return true;
#endif
}

bool SuperLUMTSolver::refactorize(const SparseMatrix& matrix) {
    if (matrix.n_rows != n_ || matrix.n_cols != n_) {
        last_error_ = "Matrix dimension mismatch";
        return false;
    }

    // 再因子化用: パターン固定か確認しつつ値のみ更新
    std::vector<std::uint32_t> colptr_u32, rowind_u32;
    std::vector<Real> values;
    matrix.to_csc(colptr_u32, rowind_u32, values);

    if (colptr_u32.size() != colptr_.size() || rowind_u32.size() != rowind_.size()) {
        last_error_ = "Sparsity pattern changed (size)";
        return false;
    }
    for (size_t i = 0; i < colptr_u32.size(); ++i) {
        if (static_cast<int>(colptr_u32[i]) != colptr_[i]) {
            last_error_ = "Sparsity pattern changed (colptr)";
            return false;
        }
    }
    for (size_t i = 0; i < rowind_u32.size(); ++i) {
        if (static_cast<int>(rowind_u32[i]) != rowind_[i]) {
            last_error_ = "Sparsity pattern changed (rowind)";
            return false;
        }
    }
    values_ = std::move(values);

#if !(defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1))
    return true;
#else
    if (colptr_.empty() || rowind_.empty()) {
        last_error_ = "Pattern not initialized";
        return false;
    }
    if (!state_.have_factors || state_.perm_c.size() != n_ || state_.perm_r.size() != n_) {
        // No prior analysis; do full
        return analyze_and_factorize(matrix);
    }

    // Re-factorize with same column permutation
    // Free previous factors
    if (state_.have_factors) {
        SuperMatrix* Lold = static_cast<SuperMatrix*>(state_.L);
        SuperMatrix* Uold = static_cast<SuperMatrix*>(state_.U);
        if (Lold) Destroy_SuperNode_Matrix(Lold);
        if (Uold) Destroy_CompCol_Matrix(Uold);
        delete Lold; delete Uold;
        state_.L = nullptr; state_.U = nullptr; state_.have_factors = false;
    }

    int nprocs = state_.inner_threads > 0 ? state_.inner_threads : 1;
#ifdef _OPENMP
    if (nprocs > 0) omp_set_num_threads(nprocs);
#endif

    SuperMatrix A;
    dCreate_CompCol_Matrix(&A,
        static_cast<int>(n_), static_cast<int>(n_),
        static_cast<int>(values_.size()),
        const_cast<double*>(reinterpret_cast<const double*>(values_.data())),
        rowind_.data(), colptr_.data(), SLU_NC, SLU_D, SLU_GE);

    // Use unique_ptr for exception safety - will auto-delete on early return
    auto matrix_deleter = [](SuperMatrix* m) { delete m; };
    std::unique_ptr<SuperMatrix, decltype(matrix_deleter)> L_ptr(new SuperMatrix(), matrix_deleter);
    std::unique_ptr<SuperMatrix, decltype(matrix_deleter)> U_ptr(new SuperMatrix(), matrix_deleter);
    SuperMatrix* L = L_ptr.get();
    SuperMatrix* U = U_ptr.get();
    int_t info = 0;

#if defined(KIRIMASPICE_SUPERLUMT_FROM_SOURCE) && (KIRIMASPICE_SUPERLUMT_FROM_SOURCE==1)
    // Use parallel SuperLU_MT API (source-built version)
    const int panel_size = sp_ienv(1);
    const int relax = sp_ienv(2);
    const double diag_pivot_thresh = 1.0;
    const yes_no_t refact = YES;  // We are refactorizing
    const yes_no_t usepr = YES;   // Reuse perm_r
    const double drop_tol = 0.0;
    void* work = nullptr;
    int lwork = 0;

    SuperMatrix AC;
    superlumt_options_t pdgstrf_options;
    Gstat_t Gstat;

    // Allocate statistics
    StatAlloc(static_cast<int>(n_), nprocs, panel_size, relax, &Gstat);
    StatInit(static_cast<int>(n_), nprocs, &Gstat);

    // Initialize options with refact=YES to reuse permutations
    pdgstrf_init(nprocs, DOFACT, NOTRANS, refact,
                 panel_size, relax, diag_pivot_thresh, usepr, drop_tol,
                 state_.perm_c.data(), state_.perm_r.data(),
                 work, lwork, &A, &AC, &pdgstrf_options, &Gstat);

    // Perform parallel factorization
    pdgstrf(&pdgstrf_options, &AC, state_.perm_r.data(), L, U, &Gstat, &info);

    // Cleanup
    pdgstrf_finalize(&pdgstrf_options, &AC);
    Destroy_SuperMatrix_Store(&A);
    StatFree(&Gstat);
#else
    // Use serial SuperLU API (system-installed version)
    superlu_options_t options; set_default_options(&options);
    options.Fact = DOFACT; // we'll reuse perm_c manually
    options.Equil = get_equil_from_env();
    options.ColPerm = get_colperm_from_env();

    std::vector<int> etree(n_, 0);
    SuperMatrix AC;
    sp_preorder(&options, &A, state_.perm_c.data(), etree.data(), &AC);

    const int panel_size = sp_ienv(1);
    const int relax = sp_ienv(2);

    SuperLUStat_t stat; StatInit(&stat);
    GlobalLU_t glu;

    dgstrf(&options, &AC, relax, panel_size,
           etree.data(), nullptr, 0, state_.perm_c.data(), state_.perm_r.data(),
           L, U, &glu, &stat, &info);

    Destroy_CompCol_Permuted(&AC);
    Destroy_SuperMatrix_Store(&A);
    StatFree(&stat);
#endif

    if (info != 0) {
        // unique_ptr will auto-delete L and U on return
        last_error_ = "SuperLU refactorization failed (info=" + std::to_string((long long)info) + ")";
        return false;
    }

    // Transfer ownership from unique_ptr to state_
    state_.L = L_ptr.release();
    state_.U = U_ptr.release();
    state_.have_factors = true;
    return true;
#endif
}

bool SuperLUMTSolver::solve(const DenseVector& b, DenseVector& x) {
    if (n_ == 0 || colptr_.empty()) {
        last_error_ = "Call analyze_and_factorize() first";
        return false;
    }
    if (b.size() != n_) {
        last_error_ = "RHS size mismatch";
        return false;
    }

#if !(defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1))
    last_error_ = "SuperLU_MT backend not available in this build";
    return false;
#else
    if (state_.have_factors) {
        // Use factored L and U to solve
        int info = 0;
        std::vector<double> rhs(b.begin(), b.end());
        SuperMatrix B;
        dCreate_Dense_Matrix(&B, static_cast<int>(n_), 1, rhs.data(),
                           static_cast<int>(n_), SLU_DN, SLU_D, SLU_GE);

        SuperMatrix* L = static_cast<SuperMatrix*>(state_.L);
        SuperMatrix* U = static_cast<SuperMatrix*>(state_.U);

#if defined(KIRIMASPICE_SUPERLUMT_FROM_SOURCE) && (KIRIMASPICE_SUPERLUMT_FROM_SOURCE==1)
        // Parallel SuperLU_MT API
        Gstat_t Gstat;
        const int nprocs = state_.inner_threads > 0 ? state_.inner_threads : 1;
        const int panel_size = sp_ienv(1);
        const int relax = sp_ienv(2);

        StatAlloc(static_cast<int>(n_), nprocs, panel_size, relax, &Gstat);
        StatInit(static_cast<int>(n_), nprocs, &Gstat);

        dgstrs(NOTRANS, L, U, state_.perm_r.data(), state_.perm_c.data(),
               &B, &Gstat, &info);

        StatFree(&Gstat);
#else
        // Serial SuperLU API
        SuperLUStat_t stat;
        StatInit(&stat);

        dgstrs(NOTRANS, L, U, state_.perm_c.data(), state_.perm_r.data(),
               &B, &stat, &info);

        StatFree(&stat);
#endif

        if (info != 0) {
            Destroy_SuperMatrix_Store(&B);
            last_error_ = "SuperLU dgstrs failed (info=" + std::to_string(info) + ")";
            return false;
        }

        double* sol = static_cast<double*>(static_cast<void*>(
            B.Store ? ((DNformat*)B.Store)->nzval : nullptr));
        if (!sol) {
            Destroy_SuperMatrix_Store(&B);
            last_error_ = "Failed to access solution vector";
            return false;
        }

        x.resize(n_);
        for (std::uint32_t i = 0; i < n_; ++i) {
            x[i] = sol[i];
        }

        Destroy_SuperMatrix_Store(&B);
        return true;
    }

    // One-shot fallback (no prior factorization)
    SuperMatrix A, B;
    dCreate_CompCol_Matrix(&A,
        static_cast<int>(n_), static_cast<int>(n_),
        static_cast<int>(values_.size()),
        const_cast<double*>(reinterpret_cast<const double*>(values_.data())),
        rowind_.data(), colptr_.data(), SLU_NC, SLU_D, SLU_GE);

    std::vector<double> rhs(b.begin(), b.end());
    dCreate_Dense_Matrix(&B, static_cast<int>(n_), 1, rhs.data(),
                        static_cast<int>(n_), SLU_DN, SLU_D, SLU_GE);

    std::vector<int> perm_c(n_, 0), perm_r(n_, 0);
    SuperMatrix L, U;
    int info = 0;

#if defined(KIRIMASPICE_SUPERLUMT_FROM_SOURCE) && (KIRIMASPICE_SUPERLUMT_FROM_SOURCE==1)
    // Parallel SuperLU_MT API - one-shot solve
    const int nprocs = state_.inner_threads > 0 ? state_.inner_threads : 1;

    pdgssv(nprocs, &A, perm_c.data(), perm_r.data(), &L, &U, &B, &info);
#else
    // Serial SuperLU API - one-shot solve
    superlu_options_t options;
    set_default_options(&options);
    options.ColPerm = COLAMD;

    SuperLUStat_t stat;
    StatInit(&stat);

    dgssv(&options, &A, perm_c.data(), perm_r.data(), &L, &U, &B, &stat, &info);

    StatFree(&stat);
#endif

    if (info != 0) {
        Destroy_SuperNode_Matrix(&L);
        Destroy_CompCol_Matrix(&U);
        Destroy_SuperMatrix_Store(&B);
        Destroy_SuperMatrix_Store(&A);
        last_error_ = "SuperLU one-shot solve failed (info=" + std::to_string(info) + ")";
        return false;
    }

    double* sol = static_cast<double*>(static_cast<void*>(
        B.Store ? ((DNformat*)B.Store)->nzval : nullptr));
    if (!sol) {
        Destroy_SuperNode_Matrix(&L);
        Destroy_CompCol_Matrix(&U);
        Destroy_SuperMatrix_Store(&B);
        Destroy_SuperMatrix_Store(&A);
        last_error_ = "Failed to access solution vector";
        return false;
    }

    x.resize(n_);
    for (std::uint32_t i = 0; i < n_; ++i) {
        x[i] = sol[i];
    }

    Destroy_SuperNode_Matrix(&L);
    Destroy_CompCol_Matrix(&U);
    Destroy_SuperMatrix_Store(&B);
    Destroy_SuperMatrix_Store(&A);
    return true;
#endif
}

} // namespace kirimaspice::solvers
