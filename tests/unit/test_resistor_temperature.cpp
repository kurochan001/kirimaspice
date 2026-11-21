// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// SPICE's resistor takes TC1 and TC2 on its line: R(T) = R (1 + TC1 dT +
// TC2 dT^2) with dT = T - TNOM and TNOM = 27 C. 1 kohm with TC1 = 0.01 and
// TC2 = 1e-4 at 77 C is 1 kohm (1 + 0.5 + 0.25) = 1750 ohm: 1 V drives
// 0.571429 mA (ngspice-47 gives 0.5714 mA, run 2026-11-21); at 27 C it is
// 1 kohm. A value set by name is the value at TNOM and is scaled too.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "kirimaspice/devices/resistor.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

double source_current(const std::string& temp_line) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("t\nV1 a 0 1\nR1 a 0 1k TC1=0.01 TC2=1e-4\n" + temp_line + ".end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector x;
    SolverStats stats;
    EXPECT_EQ(solver.solve(x, stats), ConvergenceStatus::SUCCESS);
    return x.empty() ? 0.0 : -x.back();  // I(V1) is the last unknown
}

}  // namespace

TEST(ResistorTemperature, CoefficientsScaleTheResistance) {
    EXPECT_NEAR(source_current(".options temp=77\n"), 1.0 / 1750.0, 1e-12);
    EXPECT_NEAR(source_current(""), 1.0 / 1000.0, 1e-12);
}

TEST(ResistorTemperature, ValueSetByNameIsTheValueAtTnom) {
    devices::Resistor r(1, "R1", 1, 0, 1000.0);
    r.set_temperature_coefficients(0.01, 1e-4);
    r.set_temperature(350.15);
    EXPECT_NEAR(r.resistance(), 1750.0, 1e-9);
    ASSERT_TRUE(r.set_parameter("R", 2000.0));
    EXPECT_NEAR(r.resistance(), 3500.0, 1e-9);
    EXPECT_NEAR(*r.get_parameter("R"), 2000.0, 1e-12);
}
