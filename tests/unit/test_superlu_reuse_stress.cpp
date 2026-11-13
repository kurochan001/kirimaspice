// Stress test: SuperLU_MT reuse path across repeated AC solves

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/solver_factory.hpp"

using namespace kirimaspice;

static std::string rc_lowpass_netlist(int stages) {
    std::string nl;
    nl += "* RC ladder AC\n";
    nl += "V1 in 0 DC 0 AC 1\n";
    for (int i = 1; i <= stages; ++i) {
        std::string a = (i == 1) ? "in" : ("n" + std::to_string(i-1));
        std::string b = (i == stages) ? "out" : ("n" + std::to_string(i));
        nl += "R" + std::to_string(i) + " " + a + " " + b + " 1k\n";
        nl += "C" + std::to_string(i) + " " + b + " 0 1n\n";
    }
    nl += ".ac dec 5 1e3 1e6\n.end\n";
    return nl;
}

#if defined(KIRIMASPICE_WITH_SUPERLUMT) && (KIRIMASPICE_HAVE_SUPERLUMT==1)
TEST(SuperLUMTReuse, RepeatedACSolves_Stable) {
    // Force backend to SuperLU_MT
    solvers::set_cli_backend_override(std::make_optional<std::string>("SUPERLU_MT"));

    parser::NetlistParser p;
    auto parsed = p.parse_string(rc_lowpass_netlist(10));

    // Run many times to stress reuse path
    for (int i = 0; i < 200; ++i) {
        solvers::ACSolver ac(*parsed.circuit, parsed.config);
        auto res = ac.solve();
        ASSERT_EQ(res.status, ConvergenceStatus::SUCCESS) << "iter=" << i;
    }

    solvers::set_cli_backend_override(std::nullopt);
}
#else
TEST(SuperLUMTReuse, RepeatedACSolves_Stable) {
    GTEST_SKIP() << "SuperLU_MT not available in this build";
}
#endif

