// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Where tests write files: a directory of their own under the build tree
// (KIRIMASPICE_TEST_OUTPUT_DIR, which tests/CMakeLists.txt sets), never the
// source tree or a fixed name in /tmp, so runs of different tests, build
// trees or users do not share files. A directory is emptied the first time
// a process asks for it, so a file in it is one this run wrote.

#pragma once

#include <filesystem>
#include <mutex>
#include <set>
#include <string>

namespace kirimaspice::test {

inline std::filesystem::path output_dir(std::string name) {
    for (char& c : name) {
        if (c == '/' || c == '\\') {
            c = '_';
        }
    }
    const auto dir = std::filesystem::path(KIRIMASPICE_TEST_OUTPUT_DIR) / name;
    static std::mutex mutex;
    static std::set<std::string> emptied;
    const std::lock_guard<std::mutex> lock(mutex);
    if (emptied.insert(name).second) {
        std::filesystem::remove_all(dir);
    }
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace kirimaspice::test
