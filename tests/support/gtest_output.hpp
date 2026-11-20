// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The output directory of the Google Test that is running
// (test_output.hpp)

#pragma once

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "test_output.hpp"

namespace kirimaspice::test {

// file_name in the directory of the running test, named <suite>.<test>
inline std::filesystem::path test_output_path(const std::string& file_name) {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    return output_dir(std::string(info->test_suite_name()) + "." + info->name()) / file_name;
}

}  // namespace kirimaspice::test
