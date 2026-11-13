// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include <spdlog/spdlog.h>

namespace kirimaspice::utils {

/// Simple thread-safe object pool for frequently reused objects.
/// Objects stay allocated for the process lifetime; acquire() returns a unique_ptr
/// with a custom deleter that returns the object to the pool on destruction.
template <typename T>
class MemoryPool {
public:
    MemoryPool() = default;

    ~MemoryPool() {
        // Ensure all acquired objects are returned before destruction
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_list_.size() != storage_.size()) {
            spdlog::warn("MemoryPool<{}> destroyed with {} unreturned objects",
                        typeid(T).name(),
                        storage_.size() - free_list_.size());
        }
    }

    // Delete copy and move operations (mutex cannot be copied/moved)
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    struct Releaser {
        MemoryPool* pool = nullptr;
        void operator()(T* ptr) const noexcept {
            if (pool && ptr) {
                pool->release(ptr);
            }
        }
    };

    using Ptr = std::unique_ptr<T, Releaser>;

    /// Acquire an object from the pool (or create a new one if none are available).
    Ptr acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        T* ptr = nullptr;
        if (!free_list_.empty()) {
            ptr = free_list_.back();
            free_list_.pop_back();
        } else {
            storage_.emplace_back(std::make_unique<T>());
            ptr = storage_.back().get();
        }
        return Ptr(ptr, Releaser{this});
    }

    std::size_t allocated() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return storage_.size();
    }

    std::size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return free_list_.size();
    }

private:
    void release(T* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Validate pointer before releasing
        // Ensure the pointer belongs to this pool
        bool found = false;
        for (const auto& stored : storage_) {
            if (stored.get() == ptr) {
                found = true;
                break;
            }
        }
        if (!found) {
            spdlog::error("MemoryPool<{}>: Attempt to release pointer not owned by this pool",
                         typeid(T).name());
            return;  // Don't add invalid pointer to free list
        }
        // Check for double-release
        for (const auto& free_ptr : free_list_) {
            if (free_ptr == ptr) {
                spdlog::error("MemoryPool<{}>: Double release detected",
                             typeid(T).name());
                return;  // Don't add duplicate to free list
            }
        }
        free_list_.push_back(ptr);
    }

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<T>> storage_;
    std::vector<T*> free_list_;
};

} // namespace kirimaspice::utils
