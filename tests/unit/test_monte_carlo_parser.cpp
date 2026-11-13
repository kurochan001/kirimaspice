// tests/unit/test_monte_carlo_parser.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

using namespace kirimaspice;

//==============================================================================
// .MC Parser Tests
//==============================================================================

// Test 1: Basic .MC directive parsing
TEST(MonteCarloParser, BasicDirective) {
    std::string netlist = R"(
.title Monte Carlo Test
.MC 100 DC
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_EQ(result.config.monte_carlo.num_runs, 100);
    EXPECT_EQ(result.config.monte_carlo.seed, 0);  // Default seed (use random device)
}

// Test 2: .MC with explicit seed
TEST(MonteCarloParser, ExplicitSeed) {
    std::string netlist = R"(
.title Monte Carlo with Seed
.MC 500 DC SEED=12345
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_EQ(result.config.monte_carlo.num_runs, 500);
    EXPECT_EQ(result.config.monte_carlo.seed, 12345);
}

// Test 3: .MC case insensitive
TEST(MonteCarloParser, CaseInsensitive) {
    std::string netlist = R"(
.title Monte Carlo Case Insensitive
.mc 250 dc seed=99
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_EQ(result.config.monte_carlo.num_runs, 250);
    EXPECT_EQ(result.config.monte_carlo.seed, 99);
}

// Test 4: .MONTECARLO alternate syntax
TEST(MonteCarloParser, AlternateSyntax) {
    std::string netlist = R"(
.title Monte Carlo Alternate Syntax
.MONTECARLO 1000 TRAN
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_EQ(result.config.monte_carlo.num_runs, 1000);
}

// Test 5: .MC with AC analysis
TEST(MonteCarloParser, ACAnalysis) {
    std::string netlist = R"(
.title Monte Carlo AC Analysis
.MC 300 AC SEED=777
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_EQ(result.config.monte_carlo.num_runs, 300);
    EXPECT_EQ(result.config.monte_carlo.seed, 777);
}

// Test 6: .MC error - missing num_runs
TEST(MonteCarloParser, ErrorMissingNumRuns) {
    std::string netlist = R"(
.title Monte Carlo Missing Num Runs
.MC
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 7: .MC error - missing analysis type
TEST(MonteCarloParser, ErrorMissingAnalysisType) {
    std::string netlist = R"(
.title Monte Carlo Missing Analysis Type
.MC 100
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 8: .MC error - invalid analysis type
TEST(MonteCarloParser, ErrorInvalidAnalysisType) {
    std::string netlist = R"(
.title Monte Carlo Invalid Analysis Type
.MC 100 INVALID
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 9: .MC error - zero runs
TEST(MonteCarloParser, ErrorZeroRuns) {
    std::string netlist = R"(
.title Monte Carlo Zero Runs
.MC 0 DC
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    EXPECT_THROW({
        auto result = parser.parse_string(netlist);
    }, parser::ParseError);
}

// Test 10: .MC with very large number of runs
TEST(MonteCarloParser, LargeNumberOfRuns) {
    std::string netlist = R"(
.title Monte Carlo Large Runs
.MC 1000000 DC SEED=42
R1 1 0 1k
.end
)";

    parser::NetlistParser parser;
    auto result = parser.parse_string(netlist);

    EXPECT_EQ(result.config.monte_carlo.num_runs, 1000000);
    EXPECT_EQ(result.config.monte_carlo.seed, 42);
}
