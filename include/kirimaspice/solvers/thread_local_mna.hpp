// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

/**
 * @file thread_local_mna.hpp
 * @brief Thread-local MNA system manager for parallel device stamping
 *
 * This header-only class consolidates thread-local MNA management that was
 * duplicated in DCSolver and TransientSolver.
 *
 * @author Claude (Anthropic)
 * @date 2025-12-05
 */

#pragma once

#include "kirimaspice/core/matrix_types.hpp"

#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice::solvers {

/**
 * @brief Manager for thread-local MNA systems in parallel device stamping
 *
 * Provides memory reuse across iterations/timesteps by maintaining a pool
 * of thread-local MNA systems that are zeroed and reused instead of
 * reallocated.
 *
 * Usage (within OpenMP parallel region):
 * @code
 * ThreadLocalMNAManager tlm;
 *
 * // Before parallel region: prepare systems
 * tlm.prepare(global_mna);
 *
 * #pragma omp parallel
 * {
 *     MNASystem& local = tlm.get_local();
 *     #pragma omp for
 *     for (auto& device : devices) {
 *         device->stamp(local, ...);
 *     }
 * }
 *
 * // After parallel region: merge into global
 * tlm.merge_into(global_mna);
 * @endcode
 */
class ThreadLocalMNAManager {
public:
    ThreadLocalMNAManager() = default;

    // Non-copyable (contains vector of MNASystems)
    ThreadLocalMNAManager(const ThreadLocalMNAManager&) = delete;
    ThreadLocalMNAManager& operator=(const ThreadLocalMNAManager&) = delete;

    // Movable
    ThreadLocalMNAManager(ThreadLocalMNAManager&&) noexcept = default;
    ThreadLocalMNAManager& operator=(ThreadLocalMNAManager&&) noexcept = default;

    /**
     * @brief Prepare thread-local MNA systems for parallel stamping
     *
     * On first call: allocates thread-local systems matching global_mna size.
     * On subsequent calls: zeros existing systems (memory reuse).
     *
     * @param global_mna Reference MNA system for size information
     */
    void prepare(const MNASystem& global_mna) {
#ifdef _OPENMP
        const int nthreads = omp_get_max_threads();

        if (!initialized_) {
            // First call: allocate thread-local systems
            local_systems_.resize(nthreads);
            for (int t = 0; t < nthreads; ++t) {
                local_systems_[t].resize(global_mna.num_nodes, global_mna.num_branches);
                local_systems_[t].zero();
            }
            initialized_ = true;
        } else {
            // Subsequent calls: zero existing systems (reuse memory)
            // Handle case where thread count changed
            if (static_cast<int>(local_systems_.size()) != nthreads) {
                local_systems_.resize(nthreads);
            }

            for (int t = 0; t < nthreads; ++t) {
                // Resize if dimensions changed
                if (local_systems_[t].num_nodes != global_mna.num_nodes ||
                    local_systems_[t].num_branches != global_mna.num_branches) {
                    local_systems_[t].resize(global_mna.num_nodes, global_mna.num_branches);
                }
                local_systems_[t].zero();
            }
        }
#else
        // Without OpenMP, nothing to prepare
        (void)global_mna;
#endif
    }

    /**
     * @brief Get thread-local MNA system for current thread
     *
     * Must be called from within an OpenMP parallel region.
     *
     * @return Reference to current thread's MNA system
     */
    MNASystem& get_local() {
#ifdef _OPENMP
        return local_systems_[omp_get_thread_num()];
#else
        // Should not be called without OpenMP
        static MNASystem dummy;
        return dummy;
#endif
    }

    /**
     * @brief Get thread-local MNA system by thread ID
     *
     * @param thread_id Thread index (0 to num_threads()-1)
     * @return Reference to specified thread's MNA system
     */
    MNASystem& get_local(int thread_id) {
#ifdef _OPENMP
        return local_systems_[thread_id];
#else
        (void)thread_id;
        static MNASystem dummy;
        return dummy;
#endif
    }

    /**
     * @brief Merge all thread-local systems into global MNA
     *
     * Sequentially merges each thread's contributions into the global system.
     *
     * @param global_mna Target MNA system to merge into
     */
    void merge_into(MNASystem& global_mna) {
#ifdef _OPENMP
        for (auto& local : local_systems_) {
            global_mna.merge(local);
        }
#else
        (void)global_mna;
#endif
    }

    /**
     * @brief Get number of thread-local systems
     * @return Number of threads (0 if not initialized or no OpenMP)
     */
    int num_threads() const {
#ifdef _OPENMP
        return static_cast<int>(local_systems_.size());
#else
        return 0;
#endif
    }

    /**
     * @brief Check if manager has been initialized
     * @return true if prepare() has been called at least once
     */
    bool is_initialized() const { return initialized_; }

    /**
     * @brief Reset manager state (forces reallocation on next prepare())
     */
    void reset() {
#ifdef _OPENMP
        local_systems_.clear();
#endif
        initialized_ = false;
    }

private:
#ifdef _OPENMP
    std::vector<MNASystem> local_systems_;  ///< Thread-local MNA systems
#endif
    bool initialized_ = false;  ///< Whether systems have been allocated
};

}  // namespace kirimaspice::solvers
