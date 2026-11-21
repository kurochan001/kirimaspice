// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Numbers in expressions take SPICE's scale factors and units, as ngspice-47
// reads them (util::expand_scale_factors, which every expression passes
// before it is compiled): `.param r={2*1k}` is 2000, `{1k+1k}` on an element
// line 2000, and a B source's V(a)*1k 1000 V(a); ngspice-47 gives 1 mA and
// V(b) = 1000 V for the netlist below (run 2026-11-21). Names with digits
// (x1k) stay names. Before, the scale factors were undefined symbols.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/util/units.hpp"

using namespace kirimaspice;

TEST(ExpressionScaleFactors, NumbersAreWrittenAsTheirValues) {
    EXPECT_EQ(util::expand_scale_factors("2*1k"), "2*1000");
    EXPECT_EQ(util::expand_scale_factors("1.5meg+x1k"), "1500000+x1k");
    EXPECT_EQ(util::expand_scale_factors("V(n1)*1e3+2.5"), "V(n1)*1e3+2.5");
    EXPECT_NEAR(std::stod(util::expand_scale_factors("10uF")), 1e-5, 1e-20);
}

TEST(ExpressionScaleFactors, NetlistExpressionsReadThem) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.param r={2*1k} x1k=3\nV1 a 0 1\nR1 a 0 {r}\nB1 b 0 V={V(a)*1k}\nR2 b 0 1meg\n"
        "R3 a 0 {1k+1k}\nR4 c 0 {x1k*1k}\n.end\n");
    EXPECT_DOUBLE_EQ(parsed.config.global_params.at("r"), 2000.0);
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    ASSERT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    EXPECT_NEAR(x[parsed.circuit->get_node_id("b") - 1], 1000.0, 1e-9);
    for (std::uint32_t i = 0; i < x.size(); ++i) {
        if (parsed.circuit->unknown_name(i) == "I(V1)") {
            EXPECT_NEAR(x[i], -1e-3, 1e-12);
        }
    }
}
