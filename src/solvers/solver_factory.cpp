// src/solvers/solver_factory.cpp
#include "kirimaspice/solvers/solver_factory.hpp"
#include "kirimaspice/solvers/klu_solver_adapter.hpp"
#include "kirimaspice/solvers/superlu_mt_solver.hpp"
#include "kirimaspice/solvers/superlu_dist_solver.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <spdlog/spdlog.h>

namespace kirimaspice::solvers {

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

std::optional<SolverBackend> parse_backend_string(const std::string& s) {
    const std::string u = to_upper(s);
    if (u == "KLU") return SolverBackend::KLU;
    if (u == "KLU_SUN") return SolverBackend::KLU; // KLU_SUN is alias for KLU (native SUNDIALS KLU integration)
    if (u == "SUPERLU_MT") return SolverBackend::SUPERLU_MT;
    if (u == "SUPERLU_DIST") return SolverBackend::SUPERLU_DIST;
    return std::nullopt;
}

// CLI overrides (optional)
static std::optional<std::string> g_cli_backend;
static std::vector<std::string> g_cli_fallback_order; // lower-case names
static bool g_cli_strict = false;
static std::string g_cli_sld_topology;
static std::string g_cli_sld_grid;

void set_cli_backend_override(const std::optional<std::string>& backend_name) {
    g_cli_backend = backend_name;
}

void set_cli_fallback_order(const std::vector<std::string>& order) {
    g_cli_fallback_order.clear();
    g_cli_fallback_order.reserve(order.size());
    for (auto s : order) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        g_cli_fallback_order.push_back(std::move(s));
    }
}

void set_cli_strict_backend(bool strict) { g_cli_strict = strict; }

void set_cli_superludist_topology(const std::string& topo) { g_cli_sld_topology = topo; }
void set_cli_superludist_grid(const std::string& grid) { g_cli_sld_grid = grid; }

// helper: available at build-time?
static bool is_backend_built(SolverBackend b) {
    switch (b) {
        case SolverBackend::KLU: return true;
        case SolverBackend::SUPERLU_MT:
#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
            return true;
#else
            return false;
#endif
        case SolverBackend::SUPERLU_DIST:
#if defined(KIRIMASPICE_WITH_SUPERLUDIST) && (KIRIMASPICE_WITH_SUPERLUDIST)
            // 実装・検出は将来対応
            return false;
#else
            return false;
#endif
        default:
            return false;
    }
}

static std::string backend_to_string(SolverBackend b) {
    switch (b) {
        case SolverBackend::KLU: return "klu";
        case SolverBackend::SUPERLU_MT: return "superlu_mt";
        case SolverBackend::SUPERLU_DIST: return "superlu_dist";
        default: return "unknown";
    }
}

SolverBackend choose_backend() {
    // 1) CLI override
    if (g_cli_backend.has_value()) {
        auto b = parse_backend_string(*g_cli_backend);
        if (b && is_backend_built(*b)) {
            spdlog::info("Solver backend (CLI): {}", *g_cli_backend);
            if (*b == SolverBackend::SUPERLU_DIST) {
                if (!g_cli_sld_topology.empty()) spdlog::info("SUPERLU_DIST topology: {}", g_cli_sld_topology);
                if (!g_cli_sld_grid.empty()) spdlog::info("SUPERLU_DIST grid: {}", g_cli_sld_grid);
            }
            return *b;
        }
        if (g_cli_strict) {
            throw std::runtime_error("Requested backend not available (strict mode): " + *g_cli_backend);
        }
        // fallback chain from CLI setting
        std::vector<std::string> order = g_cli_fallback_order;
        if (order.empty()) order = {"superlu_mt", "klu"};
        for (const auto& name : order) {
            auto fb = parse_backend_string(name);
            if (fb && is_backend_built(*fb)) {
                spdlog::warn("Falling back to {} due to unavailable backend {} (CLI)", name, *g_cli_backend);
                return *fb;
            }
        }
        spdlog::warn("No available backend in CLI fallback order; using KLU");
        return SolverBackend::KLU;
    }

    // 2) ENV
    if (const char* env = std::getenv("KIRIMASPICE_SOLVER_BACKEND")) {
        if (auto b = parse_backend_string(env)) {
            if (is_backend_built(*b)) {
                spdlog::info("Solver backend from ENV: {}", env);
                return *b;
            }
            // ENV fallback order
            std::vector<std::string> order;
            if (const char* ord = std::getenv("KIRIMASPICE_SOLVER_FALLBACK_ORDER")) {
                std::string s = ord;
                std::stringstream ss(s);
                std::string item;
                while (std::getline(ss, item, ',')) order.push_back(item);
            } else {
                order = {"superlu_mt", "klu"};
            }
            const bool env_strict = [](){
                if (const char* sv = std::getenv("KIRIMASPICE_SOLVER_STRICT")) return std::string(sv) == "1";
                return false;
            }();
            if (env_strict) {
                throw std::runtime_error(std::string("Requested backend not available (strict mode): ") + env);
            }
            for (const auto& name : order) {
                auto fb = parse_backend_string(name);
                if (fb && is_backend_built(*fb)) {
                    spdlog::warn("Falling back to {} due to unavailable backend {} (ENV)", name, env);
                    return *fb;
                }
            }
        } else {
            spdlog::warn("Unknown solver backend in ENV: {} (falling back)", env);
        }
    }

    // 3) Build default
#ifdef KIRIMASPICE_BUILD_DEFAULT_SOLVER
    {
        const std::string def = KIRIMASPICE_BUILD_DEFAULT_SOLVER;
        if (auto b = parse_backend_string(def)) {
            if (is_backend_built(*b)) {
                spdlog::info("Solver backend: default={} (build)", def);
                return *b;
            }
        }
    }
#endif
    spdlog::info("Solver backend: default=KLU");
    return SolverBackend::KLU;
}

std::unique_ptr<ISparseLinearSolver> make_solver(SolverBackend backend) {
    switch (backend) {
        case SolverBackend::KLU:
        default:
            return std::make_unique<KluSolverAdapter>();
        case SolverBackend::SUPERLU_MT:
            return std::make_unique<SuperLUMTSolver>();
        case SolverBackend::SUPERLU_DIST:
            return std::make_unique<SuperLUDISTSolver>();
    }
}

} // namespace kirimaspice::solvers
