// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The IGBT integrates CGE and CCE as capacitors are (device_utils::
// capacitor_companion), for every method. With the gate driven below the
// threshold the device carries no current, so the circuit is the same as
// one with two capacitors in place of the IGBT: a 1 kohm gate resistor
// with CGE = 1 nF (1 us) driven by a ramp, and a 1 kohm collector resistor
// with CCE = 1 nF from a step. The P device, driven the other way, mirrors.

#include <gtest/gtest.h>

#include <string>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

double final_voltage(const std::string& netlist, const std::string& node) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    solvers::TransientSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.solve(0.0, 2e-6, 2e-8);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    return result.timepoints.back().node_voltages.at(parsed.circuit->get_node_id(node) - 1);
}

std::string drive(double sign) {
    const std::string s = sign > 0 ? "" : "-";
    return "VG in 0 PWL(0 0 1u " + s + "1)\nRG in g 1k\nVC vc 0 PULSE(0 " + s +
           "1 0 1n 1n 1 2)\nRC vc c 1k\n";
}

}  // namespace

TEST(IgbtCapacitanceTransient, IntegratesAsCapacitorsForEveryMethod) {
    for (const std::string method : {"trap", "gear", "gear3", "euler", "tr_bdf2"}) {
        for (const double sign : {-1.0, 1.0}) {  // N driven negative, P positive: both off
            const std::string type = sign < 0 ? "NMOS" : "PMOS";
            const std::string options = ".options method=" + method + "\n.end\n";
            const std::string igbt = "t\n" + drive(sign) + "M1 c g 0 IG\n.model IG " + type +
                                     " LEVEL=21 CGE=1n CCE=1n\n" + options;
            const std::string caps = "t\n" + drive(sign) + "C1 g 0 1n\nC2 c 0 1n\n" + options;
            EXPECT_NEAR(final_voltage(igbt, "g"), final_voltage(caps, "g"), 1e-6) << method << " " << type;
            EXPECT_NEAR(final_voltage(igbt, "c"), final_voltage(caps, "c"), 1e-6) << method << " " << type;
        }
    }
}
