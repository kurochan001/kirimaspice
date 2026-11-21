// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <stdexcept>

namespace kirimaspice::output {

/// A file the writers cannot open, write or close: an error of the command
/// line or of the environment, not of an analysis, so the program ends with
/// the exit code of an input error (2)
class OutputFileError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace kirimaspice::output
