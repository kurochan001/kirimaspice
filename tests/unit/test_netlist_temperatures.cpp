// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A netlist gives TNOM in degrees Celsius, as SPICE does, on a model card and
// on an element line, like .TEMP and .OPTIONS TEMP; the devices hold it in
// kelvin (core::celsius_to_kelvin). The default TNOM of the devices is 27 C,
// so a card with TNOM = 27 means the same as one without it.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/temperature_utils.hpp"

using namespace kirimaspice;
using namespace kirimaspice::solvers;

namespace {

// The current through VD for the elements at .OPTIONS TEMP = celsius;
// "{t}" in the elements is replaced by tnom_text.
Real drain_current(std::string elements, const std::string& tnom_text, Real vd, Real vg,
                   Real celsius) {
    elements.replace(elements.find("{t}"), 3, tnom_text);
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("tnom\nVD d 0 DC " + std::to_string(vd) + "\nVG g 0 DC " +
                                      std::to_string(vg) + "\n" + elements + ".options temp=" +
                                      std::to_string(celsius) + "\n.end\n");
    EXPECT_NE(parsed.circuit, nullptr);
    DCSolver solver(*parsed.circuit, parsed.config);
    DenseVector solution;
    SolverStats stats;
    EXPECT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS) << elements;
    return solver.get_branch_current("VD").value();
}

}  // namespace

// For each device whose current depends on T - TNOM: at 100 C a card with
// TNOM = 27 gives the current of a card without TNOM, and that current differs
// from the one at 27 C (so TNOM acts at all).
TEST(NetlistTemperatures, TnomOnACardIsInCelsius) {
    struct Case {
        const char* what;
        const char* elements;
        Real vd, vg;
    };
    const Case cases[] = {
        {"BSIM3",
         "M1 d g 0 0 m L=0.18u W=1u\n"
         ".model m nmos level=8 tox=3e-9 vth0=0.5 u0=0.067 vsat=8e4{t}\n",
         1.0, 1.0},
        {"SiC MOSFET", "M1 d g 0 m\n.model m nmos level=10{t}\n", 5.0, 15.0},
        {"GaN HEMT", "M1 d g 0 m\n.model m nmos level=11{t}\n", 5.0, 2.0},
        {"FinFET", "M1 d g 0 m\n.model m nmos level=12{t}\n", 0.8, 0.8},
        {"BSIM4",
         "M1 d g 0 0 m L=0.18u W=1u\n"
         ".model m nmos level=14 version=4.8.3 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4{t}\n",
         1.0, 1.0},
        {"VDMOS", "M1 d g 0 m\n.model m nmos level=20{t}\n", 1.0, 4.5},
        {"IGBT", "M1 d g 0 m\n.model m nmos level=21{t}\n", 5.0, 15.0},
    };
    for (const auto& c : cases) {
        const Real without = drain_current(c.elements, "", c.vd, c.vg, 100.0);
        const Real at_27c = drain_current(c.elements, " tnom=27", c.vd, c.vg, 100.0);
        EXPECT_NEAR(at_27c, without, 1e-9 * std::abs(without)) << c.what;
        const Real at_tnom = drain_current(c.elements, "", c.vd, c.vg, 27.0);
        EXPECT_GT(std::abs(without - at_tnom), 1e-3 * std::abs(at_tnom)) << c.what;
    }
}

// TNOM = 27 on a MESFET card, and on the element line of an SCR and a TRIAC
// (which take parameters only there), is 300.15 K in the device.
TEST(NetlistTemperatures, TnomOnAnElementLineIsInCelsius) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "tnom\nV1 a 0 DC 1\n"
        "Z1 a g 0 mz\n.model mz NMF tnom=27\n"
        "S1 a 0 g tnom=27\n"
        "Y1 a 0 g tnom=27\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    for (const char* name : {"Z1", "S1", "Y1"}) {
        auto device = parsed.circuit->get_device(name);
        ASSERT_NE(device, nullptr) << name;
        auto tnom = device->get_parameter("tnom");
        ASSERT_TRUE(tnom.has_value()) << name;
        EXPECT_DOUBLE_EQ(*tnom, 300.15) << name;
    }
}
