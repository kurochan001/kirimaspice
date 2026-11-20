// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/isparse_solver.hpp"

namespace kirimaspice::solvers {

/// The message for a factorization that failed: the solver's error and,
/// when the solver found the matrix singular at a column, the unknown of
/// that column (CircuitGraph::unknown_name), as "singular matrix: check
/// V(a)". The systems of every analysis hold the circuit's unknowns in
/// their order, AC's real form once for the real and once for the
/// imaginary parts, so the column is taken modulo num_unknowns().
std::string factorization_failure(const circuit::CircuitGraph& circuit,
                                  const ISparseLinearSolver& solver);

}  // namespace kirimaspice::solvers
