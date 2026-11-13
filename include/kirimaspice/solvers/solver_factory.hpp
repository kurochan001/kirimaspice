// include/kirimaspice/solvers/solver_factory.hpp
#pragma once

#include "kirimaspice/solvers/isparse_solver.hpp"

#include <memory>
#include <string>
#include <optional>
#include <vector>

namespace kirimaspice::solvers {

/// Supported solver backends (expand as implementations land)
enum class SolverBackend {
    KLU,
    SUPERLU_MT,
    SUPERLU_DIST,
    // KLU_SUN,        // SUNDIALS KLU adapter（将来）
};

/// Choose backend via priority: ENV -> build default (KLU)
SolverBackend choose_backend();

/// Create a solver instance for the given backend
std::unique_ptr<ISparseLinearSolver> make_solver(SolverBackend backend);

/// Parse backend string (e.g., "KLU", "SUPERLU_MT"). Returns std::nullopt on failure.
std::optional<SolverBackend> parse_backend_string(const std::string& s);

// --- CLI overrides (optional) ---
// Set from main() when parsing command-line flags. If unset, ENV/ビルド既定にフォールバック。
void set_cli_backend_override(const std::optional<std::string>& backend_name);
void set_cli_fallback_order(const std::vector<std::string>& order); // e.g., {"superlu_mt","klu"}
void set_cli_strict_backend(bool strict);

// Optional: SUPERLU_DIST parameters (accepted and stored for future use)
void set_cli_superludist_topology(const std::string& topo);
void set_cli_superludist_grid(const std::string& grid);

} // namespace kirimaspice::solvers
