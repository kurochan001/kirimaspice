// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A file of the source tree, for the data that tests read (reference
// results, netlists): KIRIMASPICE_SOURCE_DIR, which tests/CMakeLists.txt
// sets, joined to the path relative to the source root. So a test finds its
// data wherever the build directory is and whatever directory it runs in.

#pragma once

#include <filesystem>
#include <string>

namespace kirimaspice::test {

inline std::filesystem::path source_path(const std::string& relative) {
    return std::filesystem::path(KIRIMASPICE_SOURCE_DIR) / relative;
}

}  // namespace kirimaspice::test
