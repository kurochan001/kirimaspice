#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

class DCConvergenceHelpersTest : public ::testing::Test {
protected:
    std::unique_ptr<circuit::CircuitGraph> make_simple_bjt_circuit() {
        std::string netlist = R"(
* simple diode/resistor circuit
V1 in 0 DC 5
R1 in out 1k
D1 out 0 default
.MODEL default D
.end
)";
        parser::NetlistParser parser;
        auto result = parser.parse_string(netlist);
        return std::move(result.circuit);
    }
};

TEST_F(DCConvergenceHelpersTest, PseudoTransientRecoversFromIterationLimit) {
    auto circuit = make_simple_bjt_circuit();
    SimulationConfig config;
    config.dc.max_iterations = 0; // force immediate failure
    config.dc.enable_pseudo_transient = true;
    config.dc.pseudo_transient_steps = 5;

    solvers::DCSolver solver(*circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);
}

TEST_F(DCConvergenceHelpersTest, ContinuationMethodBehavesLikeSourceStepping) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(R"(
* voltage divider
V1 in 0 DC 10
R1 in mid 1k
R2 mid 0 1k
.end
)");
    SimulationConfig config;
    config.dc.enable_continuation_method = true;
    config.dc.continuation_steps = 4;
    config.dc.continuation_start = 0.2;

    solvers::DCSolver solver(*parsed.circuit, config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);
    ASSERT_EQ(status, ConvergenceStatus::SUCCESS);
    ASSERT_GE(solution.size(), 1u);
    // Node "in" is V1 positive -> expected 10V; first entry corresponds to node "in"
    EXPECT_NEAR(solution[0], 10.0, 1e-3);
}
