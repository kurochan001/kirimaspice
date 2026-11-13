// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_wcase.cpp
#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/analysis/wcase_analyzer.hpp"

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::analysis;

// ============================================================================
// 1. Parser Tests (3 tests)
// ============================================================================

TEST(WCase, BasicSyntax) {
    std::string netlist = R"(
* Worst-case analysis parser test
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.WCASE DC v(out) MAX
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.wcase.enabled);
    EXPECT_EQ(result.config.wcase.analysis_type, WorstCaseConfig::AnalysisType::DC);
    EXPECT_EQ(result.config.wcase.objective, WorstCaseConfig::Objective::MAXIMIZE);
    EXPECT_EQ(result.config.wcase.output_variable, "v(out)");
}

TEST(WCase, WithParameters) {
    std::string netlist = R"(
* Worst-case with parameter variations
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.WCASE DC v(out) MIN VAR=R1.resistance:10%,R2.resistance:5%
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.wcase.enabled);
    EXPECT_EQ(result.config.wcase.objective, WorstCaseConfig::Objective::MINIMIZE);
    ASSERT_EQ(result.config.wcase.variations.size(), 2);

    EXPECT_EQ(result.config.wcase.variations[0].parameter_name, "R1.resistance");
    EXPECT_DOUBLE_EQ(result.config.wcase.variations[0].deviation_percent, 10.0);

    EXPECT_EQ(result.config.wcase.variations[1].parameter_name, "R2.resistance");
    EXPECT_DOUBLE_EQ(result.config.wcase.variations[1].deviation_percent, 5.0);
}

TEST(WCase, CaseInsensitive) {
    std::string netlist = R"(
* Case-insensitive test
R1 1 0 1k
Vin 1 0 DC 5
.wcase dc v(1) max
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    ASSERT_TRUE(result.config.wcase.enabled);
    EXPECT_EQ(result.config.wcase.analysis_type, WorstCaseConfig::AnalysisType::DC);
}

// ============================================================================
// 2. Simple Integration Test (2 tests)
// ============================================================================

TEST(WCase_Integration, VoltageDivider) {
    // Simple voltage divider: Vin=5V, R1=R2=1k -> Vout=2.5V nominal
    // With R1 variation ±10%, expect Vout range
    std::string netlist = R"(
* Voltage divider worst-case
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.WCASE DC v(out) MAX VAR=R1.resistance:10%
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    WCaseAnalyzer analyzer(*result.circuit, result.config);
    WorstCaseResult wcase_result = analyzer.analyze();

    ASSERT_TRUE(wcase_result.success);

    // With R1 varying ±10%, we have 3^1=3 cases (R1_min, R1_typ, R1_max)
    EXPECT_EQ(wcase_result.all_cases.size(), 3);

    // Worst case (MAX): R1 minimum -> higher current -> higher Vout
    // Vout = 5V * R2 / (R1 + R2)
    // R1_min = 900Ω -> Vout = 5 * 1000 / (900 + 1000) = 2.632V
    EXPECT_GT(wcase_result.worst_value, 2.6);
    EXPECT_LT(wcase_result.worst_value, 2.7);

    // Best case (MAX objective means MIN is best): R1 maximum
    // R1_max = 1100Ω -> Vout = 5 * 1000 / (1100 + 1000) = 2.381V
    EXPECT_GT(wcase_result.best_value, 2.3);
    EXPECT_LT(wcase_result.best_value, 2.4);
}

TEST(WCase_Integration, MinimizeObjective) {
    // Test MIN objective
    std::string netlist = R"(
* Minimize voltage
R1 in out 1k
R2 out 0 1k
Vin in 0 DC 5
.WCASE DC v(out) MIN VAR=R1.resistance:10%
.END
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);

    WCaseAnalyzer analyzer(*result.circuit, result.config);
    WorstCaseResult wcase_result = analyzer.analyze();

    ASSERT_TRUE(wcase_result.success);

    // Worst case (MIN): R1 maximum -> lower Vout
    EXPECT_GT(wcase_result.worst_value, 2.3);
    EXPECT_LT(wcase_result.worst_value, 2.4);

    // Best case (MIN objective means MAX is best): R1 minimum
    EXPECT_GT(wcase_result.best_value, 2.6);
    EXPECT_LT(wcase_result.best_value, 2.7);
}
