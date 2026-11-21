// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The gate capacitances of the MESFET, the GaN HEMT, the VDMOS, the SiC
// MOSFET and the FinFET are integrated by the transient's method as a
// capacitor is (device_utils::CapacitorHistory), so a gate driven through
// 1 kohm with drain and source grounded (no channel current reaches the
// gate) follows the same voltage as a capacitor of their sum. Before, they
// were integrated by backward Euler whatever the method: with the
// trapezoidal rule, v(g) differed from the capacitor's by the first-order
// error of backward Euler.

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

struct Case {
    const char* name;
    const char* element;   // the device, gate g, drain and source 0
    const char* card;
    const char* capacitance;  // the gate's total capacitance at Vds = 0
};

std::vector<double> gate_voltage(const std::string& netlist, double stop, double step,
                                 std::vector<double>* times = nullptr) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    SimulationConfig config = parsed.config;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.use_adaptive_timestep = false;
    solvers::TransientSolver solver(*parsed.circuit, config);
    const auto result = solver.solve(0.0, stop, step);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS) << netlist;
    const NodeId g = parsed.circuit->get_node_id("g");
    std::vector<double> v;
    for (const auto& tp : result.timepoints) {
        v.push_back(tp.node_voltages[g - 1]);
        if (times) times->push_back(tp.time);
    }
    return v;
}

}  // namespace

TEST(DeviceCapacitorMethods, GateCapacitancesFollowTheMethod) {
    const Case cases[] = {
        {"MESFET", "Z1 0 g 0 mm", ".model mm NMF CGS=1p CGD=0.5p CDS=0.1p", "1.5p"},
        {"GaN HEMT", "M1 0 g 0 mm", ".model mm nmos level=11 cgs=1p cgd=0.5p cds=0.1p", "1.5p"},
        {"VDMOS", "M1 0 g 0 mm", ".model mm nmos level=20 cgs=1p cgdmax=0.5p cgdmin=0.1p cds=0.1p", "1.5p"},
        {"SiC MOSFET", "M1 0 g 0 mm", ".model mm nmos level=10 cgso=1n cgdo=0.5n w=1e-3", "1.5p"},
        {"FinFET", "M1 0 g 0 mm", ".model mm nmos level=12 cgs=1p cgd=0.5p", "1.5p"},
    };
    const double stop = 10e-9, step = 0.1e-9;  // RC = 1.5 ns
    for (const auto& c : cases) {
        const std::string drive = "t\nV1 in 0 PULSE(0 1 0.2n 0.1n 0.1n 1 2)\nR1 in g 1k\n";
        std::vector<double> times;
        const auto device = gate_voltage(drive + c.element + "\n" + c.card + "\n.end\n", stop, step, &times);
        const auto capacitor = gate_voltage(drive + "C1 g 0 " + c.capacitance + "\n.end\n",
                                            stop, step);
        ASSERT_EQ(device.size(), capacitor.size()) << c.name;
        double worst = 0.0;
        for (std::size_t i = 0; i < device.size(); ++i) {
            worst = std::max(worst, std::abs(device[i] - capacitor[i]));
        }
        EXPECT_LT(worst, 1e-6) << c.name;
    }
}
