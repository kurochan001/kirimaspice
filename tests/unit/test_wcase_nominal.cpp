// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// tests/unit/test_wcase_nominal.cpp
// The nominal value of a worst-case analysis is the output with every
// parameter at its nominal value, and the analysis leaves the circuit's
// parameters as it found them.
//
// The circuit is a divider from 5 V, R1 from in to out and R2 from out to
// ground, both 1k and both varied by 10 %: v(out) = 5 R2 / (R1 + R2), 2.5 V
// at the nominal values (analytic).

#include <gtest/gtest.h>
#include "kirimaspice/analysis/wcase_analyzer.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"

using namespace kirimaspice;
using namespace kirimaspice::analysis;

namespace {

parser::NetlistParseResult divider() {
    parser::NetlistParser parser;
    return parser.parse_string(
        "* divider worst case\n"
        "R1 in out 1k\n"
        "R2 out 0 1k\n"
        "Vin in 0 DC 5\n"
        ".WCASE DC v(out) MAX VAR=R1.resistance:10% VAR=R2.resistance:10%\n"
        ".END\n");
}

}  // namespace

TEST(WCaseNominal, NominalIsTheCaseWithEveryParameterNominal) {
    auto parsed = divider();
    WCaseAnalyzer analyzer(*parsed.circuit, parsed.config);
    const auto result = analyzer.analyze();

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.all_cases.size(), 9u);
    EXPECT_NEAR(result.nominal_value, 2.5, 1e-9);
    // The corners: R1 min with R2 max gives the most, 5 * 1100 / 2000
    EXPECT_NEAR(result.worst_value, 2.75, 1e-9);
    EXPECT_NEAR(result.best_value, 2.25, 1e-9);
}

TEST(WCaseNominal, TheParametersAreRestored) {
    auto parsed = divider();
    WCaseAnalyzer analyzer(*parsed.circuit, parsed.config);
    const auto first = analyzer.analyze();

    for (const char* name : {"R1", "R2"}) {
        const auto value = parsed.circuit->get_device(name)->get_parameter("resistance");
        ASSERT_TRUE(value.has_value()) << name;
        EXPECT_DOUBLE_EQ(*value, 1000.0) << name;
    }

    // A second analysis starts from the same nominal values
    const auto second = analyzer.analyze();
    ASSERT_EQ(first.all_cases.size(), second.all_cases.size());
    for (std::size_t k = 0; k < first.all_cases.size(); ++k) {
        EXPECT_EQ(first.all_cases[k].parameter_values, second.all_cases[k].parameter_values)
            << first.all_cases[k].case_name;
    }
}
