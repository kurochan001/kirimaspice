// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The gate capacitances of the three-terminal FETs charge through a gate
// resistor with the time constant R (CGS + CGD) and then carry no current.
// With drain and source held by voltage sources, a gate fed through R from a
// step of 1 V follows 1 - exp(-t / tau); backward Euler, the devices' own
// integration, stays within a few percent of it at dt = tau / 20, and the
// gate settles at the step.

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

namespace {

// The gate node g of the circuit charges from vs towards vs + 1 V with the
// time constant tau.
void expect_gate_follows(const CircuitGraph& circuit, Real vs, Real tau) {
    SimulationConfig config;
    config.transient.use_adaptive_timestep = false;
    config.transient.method = IntegrationMethod::BACKWARD_EULER;
    TransientSolver solver(circuit, config);
    const TransientResult result = solver.solve(0.0, 10.0 * tau, tau / 20.0);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    const NodeId g = circuit.get_node_id("g");
    for (const auto& tp : result.timepoints) {
        if (tp.time > 0.0) {
            EXPECT_NEAR(tp.node_voltages[g - 1] - vs, 1.0 - std::exp(-tp.time / tau), 0.03)
                << "t=" << tp.time;
        }
    }
    EXPECT_NEAR(result.timepoints.back().node_voltages[g - 1], vs + 1.0, 1e-3);
}

// The gate steps from vs to vs + 1 V through rg, the source sits at vs and
// the drain at vd.
void expect_gate_charges(const std::function<void(CircuitGraph&)>& add, Real vs, Real vd,
                         Real rg, Real tau) {
    CircuitGraph circuit;
    circuit.add_voltage_source("VS", "s", "0", vs, 0.0);
    circuit.add_voltage_source("VD", "d", "0", vd, 0.0);
    circuit.add_pulse_voltage_source("VG", "in", "0", vs, vs + 1.0, 0.0, 1e-15, 1e-15, 1.0, 2.0);
    circuit.add_resistor("RG", "in", "g", rg);
    add(circuit);
    expect_gate_follows(circuit, vs, tau);
}

}  // namespace

TEST(GateCapacitances, GaNHemtChargesThroughTheGateResistor) {
    // CGS = 1 nF and CGD = 0.1 nF, the defaults; CDS sits between two sources
    expect_gate_charges([](CircuitGraph& c) { c.add_gan_hemt_n("M1", "d", "g", "s"); }, 0.0, 5.0,
                        1e3, 1e3 * 1.1e-9);
}

TEST(GateCapacitances, VdmosChargesThroughTheGateResistor) {
    // Source at 1 V, VDS = 4 V = VTO: CGD = CGDMIN + (CGDMAX - CGDMIN) / 2
    const Real cgs = 3e-9, cgdmax = 1e-9, cgdmin = 1e-11;
    const Real cgd = cgdmin + (cgdmax - cgdmin) / 2.0;
    expect_gate_charges(
        [&](CircuitGraph& c) {
            c.add_vdmos_n("M1", "d", "g", "s", 4.0, 20.0, 0.1, 600.0, 0.01, cgs, cgdmax, cgdmin);
        },
        1.0, 5.0, 1e3, 1e3 * (cgs + cgd));
}

TEST(GateCapacitances, SicMosfetChargesThroughTheGateResistor) {
    // CGS = CGSO W and CGD = CGDO W, W = 10 um: 10 nF each, so tau = 1k * 20 nF;
    // below the threshold of 2.5 V the channel carries no current
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "sic gate\nVD d 0 DC 5\nVG in 0 DC 0 PULSE(0 1 0 1f 1f 1 2)\nRG in g 1k\n"
        "M1 d g 0 m\n.model m nmos level=10 cgso=1e-3 cgdo=1e-3\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    expect_gate_follows(*parsed.circuit, 0.0, 1e3 * 2e-8);
}

TEST(GateCapacitances, MesfetChargesThroughTheGateResistor) {
    // The defaults CGS = 1 pF and CGD = 0.5 pF, for both polarities; CDS sits
    // between two sources
    for (const char* type : {"NMF", "PMF"}) {
        parser::NetlistParser parser;
        auto parsed = parser.parse_string(
            std::string("mesfet gate\nVD d 0 DC 2\nVG in 0 DC 0 PULSE(0 1 0 1f 1f 1 2)\n"
                        "RG in g 1k\nZ1 d g 0 m\n.model m ") +
            type + "\n.end\n");
        ASSERT_NE(parsed.circuit, nullptr);
        SCOPED_TRACE(type);
        expect_gate_follows(*parsed.circuit, 0.0, 1e3 * 1.5e-12);
    }
}
