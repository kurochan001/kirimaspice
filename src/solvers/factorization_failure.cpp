// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "kirimaspice/solvers/factorization_failure.hpp"

namespace kirimaspice::solvers {

std::string factorization_failure(const circuit::CircuitGraph& circuit,
                                  const ISparseLinearSolver& solver) {
    std::string message = solver.last_error();
    const auto column = solver.singular_column();
    const std::uint32_t unknowns = circuit.num_unknowns();
    if (column && unknowns > 0) {
        message += "; singular matrix: check " + circuit.unknown_name(*column % unknowns);
    }
    return message;
}

}  // namespace kirimaspice::solvers
