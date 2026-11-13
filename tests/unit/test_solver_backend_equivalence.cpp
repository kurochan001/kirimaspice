// tests/unit/test_solver_backend_equivalence.cpp
// Backend equivalence: KLU vs SuperLU_MT (AC small-signal)

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"
#include <cmath>

using namespace kirimaspice;

namespace {
std::string rc_lowpass_netlist() {
    return R"(
* RC low-pass (AC)
V1 in 0 DC 0 AC 1
R1 in out 1k
C1 out 0 1n
.ac dec 5 1k 1e6
.end
)";
}

// Extract complex voltage at a named node from ACResult (no gtest asserts inside)
std::vector<Complex> extract_node_waveform_noassert(const solvers::ACResult& res,
                                                    const circuit::CircuitGraph& circuit,
                                                    const std::string& node) {
    std::vector<Complex> v;
    auto id = circuit.get_node_id(node); // 1-based, ground=0 not included in solution
    if (id == 0u) return v;
    const std::size_t idx = static_cast<std::size_t>(id - 1);
    v.reserve(res.frequency_points.size());
    for (const auto& pt : res.frequency_points) {
        if (idx < pt.solution.size()) v.push_back(pt.solution[idx]);
    }
    return v;
}

} // namespace

TEST(SolverBackendEquivalence, AC_RC_Lowpass_KLU_vs_SuperLUMT) {
    parser::NetlistParser p;
    auto parsed = p.parse_string(rc_lowpass_netlist());

    // Run with KLU
    solvers::set_cli_backend_override(std::make_optional<std::string>("KLU"));
    solvers::ACSolver ac_klu(*parsed.circuit, parsed.config);
    auto res_klu = ac_klu.solve();
    ASSERT_EQ(res_klu.status, ConvergenceStatus::SUCCESS);
    ASSERT_FALSE(res_klu.frequency_points.empty());

#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
    // Run with SuperLU_MT
    solvers::set_cli_backend_override(std::make_optional<std::string>("SUPERLU_MT"));
    solvers::ACSolver ac_slumt(*parsed.circuit, parsed.config);
    auto res_slumt = ac_slumt.solve();
    ASSERT_EQ(res_slumt.status, ConvergenceStatus::SUCCESS);
    ASSERT_EQ(res_slumt.frequency_points.size(), res_klu.frequency_points.size());

    auto vout_klu = extract_node_waveform_noassert(res_klu, *parsed.circuit, "out");
    auto vout_slumt = extract_node_waveform_noassert(res_slumt, *parsed.circuit, "out");
    ASSERT_EQ(vout_klu.size(), vout_slumt.size());

    const double mag_tol = 1e-5;      // absolute magnitude tolerance
    const double phase_tol = 1e-3;    // radians

    for (std::size_t i = 0; i < vout_klu.size(); ++i) {
        double m1 = std::abs(vout_klu[i]);
        double m2 = std::abs(vout_slumt[i]);
        EXPECT_NEAR(m1, m2, mag_tol) << "mismatch at i=" << i;

        double ph1 = std::arg(vout_klu[i]);
        double ph2 = std::arg(vout_slumt[i]);
        double dph = ph1 - ph2;
        // normalize to [-pi, pi]
        while (dph > M_PI) dph -= 2*M_PI;
        while (dph < -M_PI) dph += 2*M_PI;
        EXPECT_LE(std::abs(dph), phase_tol) << "phase mismatch at i=" << i;
    }
#else
    GTEST_SKIP() << "SuperLU_MT backend not built; skipping equivalence check";
#endif

    // Clear CLI override
    solvers::set_cli_backend_override(std::nullopt);
}

#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
TEST(SolverBackendEquivalence, SuperLUMT_Options_Colperm_Equil) {
    // Set ENV to exercise option parsing
#if defined(_WIN32)
    _putenv("KIRIMASPICE_SUPERLU_COLPERM=NATURAL");
    _putenv("KIRIMASPICE_SUPERLU_EQUIL=on");
#else
    setenv("KIRIMASPICE_SUPERLU_COLPERM", "NATURAL", 1);
    setenv("KIRIMASPICE_SUPERLU_EQUIL", "on", 1);
#endif

    parser::NetlistParser p;
    auto parsed = p.parse_string(rc_lowpass_netlist());
    solvers::set_cli_backend_override(std::make_optional<std::string>("SUPERLU_MT"));
    solvers::ACSolver ac(*parsed.circuit, parsed.config);
    auto res = ac.solve();
    EXPECT_EQ(res.status, ConvergenceStatus::SUCCESS);
    solvers::set_cli_backend_override(std::nullopt);
}
#endif
