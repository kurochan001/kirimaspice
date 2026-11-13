/**
 * @file parallel_stamper.hpp
 * @brief Unified parallel device stamping for DC and Transient solvers
 *
 * This header-only template class consolidates the parallel device stamping
 * logic that was duplicated in DCSolver and TransientSolver.
 *
 * @author Claude (Anthropic)
 * @date 2025-12-05
 */

#pragma once

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/solvers/thread_local_mna.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>
#include <string_view>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace kirimaspice::solvers {

/**
 * @brief Configuration for parallel device stamping
 */
struct ParallelStampConfig {
    std::size_t threshold = 10000;      ///< Device count threshold for parallel execution
    bool enable_profiling = true;       ///< Enable profiling output
    std::string_view name = "Solver";   ///< Name for profiling logs (e.g., "DC", "Transient")
};

/**
 * @brief Unified parallel device stamping executor
 *
 * Provides a common implementation for parallel device stamping used by
 * both DC and Transient solvers. The stamp function is provided as a
 * template parameter to allow different stamping strategies.
 *
 * Usage:
 * @code
 * // DC stamping
 * ParallelDeviceStamper::execute(
 *     devices, mna, thread_local_mgr,
 *     [&](const Device& dev, MNASystem& mna) {
 *         if (dev.is_linear()) {
 *             dev.stamp_dc_linear(mna, voltages);
 *         } else {
 *             dev.stamp_gmin(mna, voltages);
 *             dev.stamp_dc_nonlinear(mna, voltages);
 *         }
 *     },
 *     {.threshold = 10000, .name = "DC"}
 * );
 *
 * // Transient stamping
 * ParallelDeviceStamper::execute(
 *     devices, mna, thread_local_mgr,
 *     [&](const Device& dev, MNASystem& mna) {
 *         dev.stamp_transient(mna, voltages, dt, method, bdf, tr_bdf2);
 *     },
 *     {.threshold = 1000000, .name = "Transient"}
 * );
 * @endcode
 */
class ParallelDeviceStamper {
public:
    /**
     * @brief Execute device stamping (parallel or sequential based on device count)
     *
     * @tparam StampFunc Callable with signature void(const Device&, MNASystem&)
     * @param devices Vector of device pointers
     * @param global_mna Global MNA system to stamp into
     * @param thread_local_mgr Thread-local MNA manager for parallel execution
     * @param stamp_func Function to call for each device
     * @param config Parallel stamping configuration
     */
    template<typename StampFunc>
    static void execute(
        const std::vector<std::shared_ptr<devices::Device>>& devices,
        MNASystem& global_mna,
        ThreadLocalMNAManager& thread_local_mgr,
        StampFunc&& stamp_func,
        const ParallelStampConfig& config = {})
    {
        const std::size_t num_devices = devices.size();

        if (num_devices < config.threshold) {
            // Small circuits: sequential is faster (avoid parallelization overhead)
            execute_sequential(devices, global_mna, std::forward<StampFunc>(stamp_func));
        } else {
            // Large circuits: use parallel device stamping
            execute_parallel(devices, global_mna, thread_local_mgr,
                           std::forward<StampFunc>(stamp_func), config);
        }
    }

private:
    /**
     * @brief Sequential device stamping (for small circuits or no OpenMP)
     */
    template<typename StampFunc>
    static void execute_sequential(
        const std::vector<std::shared_ptr<devices::Device>>& devices,
        MNASystem& global_mna,
        StampFunc&& stamp_func)
    {
        for (const auto& device : devices) {
            stamp_func(*device, global_mna);
        }
    }

    /**
     * @brief Parallel device stamping with thread-local MNA systems
     */
    template<typename StampFunc>
    static void execute_parallel(
        const std::vector<std::shared_ptr<devices::Device>>& devices,
        MNASystem& global_mna,
        ThreadLocalMNAManager& thread_local_mgr,
        StampFunc&& stamp_func,
        const ParallelStampConfig& config)
    {
#ifdef _OPENMP
        const std::size_t num_devices = devices.size();

        // Prepare thread-local systems
        auto t_start_init = std::chrono::high_resolution_clock::now();

        thread_local_mgr.prepare(global_mna);
        int num_threads = thread_local_mgr.num_threads();

        auto t_end_init = std::chrono::high_resolution_clock::now();
        double init_time_ms = std::chrono::duration<double, std::milli>(t_end_init - t_start_init).count();

        // Parallel device stamping
        auto t_start_stamp = std::chrono::high_resolution_clock::now();

        #pragma omp parallel
        {
            MNASystem& local_mna = thread_local_mgr.get_local();

            #pragma omp for schedule(static) nowait
            for (std::size_t i = 0; i < num_devices; ++i) {
                stamp_func(*devices[i], local_mna);
            }
        }

        auto t_end_stamp = std::chrono::high_resolution_clock::now();
        double stamp_time_ms = std::chrono::duration<double, std::milli>(t_end_stamp - t_start_stamp).count();

        // Merge thread-local systems into global
        auto t_start_merge = std::chrono::high_resolution_clock::now();

        thread_local_mgr.merge_into(global_mna);

        auto t_end_merge = std::chrono::high_resolution_clock::now();
        double merge_time_ms = std::chrono::duration<double, std::milli>(t_end_merge - t_start_merge).count();

        // Log profiling data (only at info level or above)
        if (config.enable_profiling && spdlog::get_level() <= spdlog::level::info) {
            spdlog::info("Profiling ({}) | devices={} threads={} | Init: {:.3f}ms | Stamp: {:.3f}ms | Merge: {:.3f}ms | Total: {:.3f}ms",
                         config.name, num_devices, num_threads,
                         init_time_ms, stamp_time_ms, merge_time_ms,
                         init_time_ms + stamp_time_ms + merge_time_ms);
        }
#else
        // OpenMP not available: fallback to sequential
        (void)thread_local_mgr;
        (void)config;
        execute_sequential(devices, global_mna, std::forward<StampFunc>(stamp_func));
#endif
    }
};

}  // namespace kirimaspice::solvers
