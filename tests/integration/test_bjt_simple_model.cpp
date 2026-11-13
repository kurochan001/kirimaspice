// tests/integration/test_bjt_simple_model.cpp
// Simple BJT model reference test

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

TEST(BJTModelTest, SimpleCommonEmitter) {
    // Parse netlist with 2N3904 model reference
    parser::NetlistParser parser;
    auto result = parser.parse_file("tests/integration/test_bjt_simple_ce.cir");

    ASSERT_NE(result.circuit, nullptr);

    // Check that model was loaded
    EXPECT_TRUE(result.config.models.find("2N3904") != result.config.models.end());

    // Run DC analysis
    solvers::DCSolver solver(*result.circuit, result.config);
    DenseVector solution;
    SolverStats stats;
    auto status = solver.solve(solution, stats);

    // This should succeed
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS);

    if (status == ConvergenceStatus::SUCCESS) {
        // Check voltages
        NodeId base_id = result.circuit->get_node_id("b");
        NodeId collector_id = result.circuit->get_node_id("c");

        Real V_base = solution[base_id - 1];
        Real V_collector = solution[collector_id - 1];

        // Base voltage should be > 0.5V
        EXPECT_GT(V_base, 0.5);
        EXPECT_LT(V_base, 1.0);

        // Collector voltage should be positive
        EXPECT_GT(V_collector, 0.0);
        EXPECT_LT(V_collector, 5.0);
    }
}
