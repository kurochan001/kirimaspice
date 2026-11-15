// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/core/types.hpp"
#include <numbers>

namespace kirimaspice::util {

/// A named constant of the expressions in braces
struct ExpressionConstant {
    const char* name;
    Real value;
};

/// The constants that an expression in braces can use, at the top level and
/// in a subcircuit alike (REFERENCE_MANUAL, Expressions). The parser's
/// ExpressionEvaluator and SubcircuitDefinition::substitute_parameters both
/// define these, and no others.
inline constexpr ExpressionConstant kExpressionConstants[] = {
    {"pi", std::numbers::pi},
    {"e", std::numbers::e},
};

}  // namespace kirimaspice::util
