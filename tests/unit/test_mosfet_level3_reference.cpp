// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The level 3 MOSFET is SPICE's level 3, written from its published
// equations (A. Vladimirescu and S. Liu, UCB/ERL M80/7, 1980). Its drain
// current against reference runs of ngspice-47 (`show m1 : id`, six digits,
// M1 d g 0 b m W=10u L=2u), and its channel-length modulation and NSUB
// defaults against those equations.
//
// The reference cards set KAPPA = 0 or give no NSUB: ngspice-47 smooths the
// channel-length modulation around VDSAT, which the published equations do
// not (KNOWN_ISSUES), and they give PHI, as ngspice-47 computes it from NSUB
// with another intrinsic density.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/mosfet_level3.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::devices;

namespace {

struct Reference {
    const char* type;
    const char* card;
    Real vg, vd, vb, id;
};

// ngspice-47
const Reference kReference[] = {
    {"nmos", "vto=0.7 kp=2e-5 theta=0.1 kappa=0", 3, 0.5, 0, 8.33333e-05},
    {"nmos", "vto=0.7 kp=2e-5 theta=0.1 kappa=0", 3, 4, 0, 0.000215041},
    {"nmos", "vto=0.7 kp=2e-5 theta=0.1 kappa=0", 2, 1, -2, 7.07965e-05},
    {"nmos", "vto=0.7 kp=2e-5 theta=0.1 kappa=0", 3, -1, -1, -0.000210526},
    {"nmos", "vto=0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5", 3, 0.5, 0, 0.000465177},
    {"nmos", "vto=0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5", 3, 4, 0, 0.000896165},
    {"nmos", "vto=0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5", 2, 1, -2, 0.000263233},
    {"nmos", "vto=0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5", 1.2, 0.1, -3, 1.11741e-05},
    {"nmos", "vto=0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5", 2, 0.5, 0.3, 0.000258806},
    {"nmos", "vto=0.7 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 nfs=1e11", 0.7, 1, 0,
     9.68614e-08},
    {"nmos", "vto=0.7 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 nfs=1e11", 1, 1, 0,
     2.11882e-05},
    {"nmos", "vto=0.7 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 nfs=1e11", 2, 0.5, 0,
     0.000265455},
    {"pmos", "vto=-0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5 uo=250", -3, -0.5, 0, 0.0002097},
    {"pmos", "vto=-0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5 uo=250", -3, -4, 0, 0.000477684},
    {"pmos", "vto=-0.7 nsub=1e16 tox=2e-8 phi=0.694857 gamma=0.333698 kappa=0 xj=3e-7 ld=1e-7 "
             "vmax=1e5 theta=0.05 eta=0.5 delta=0.5 uo=250", -2, -1, 2, 0.000127541},
    // VTO from NSUB, TPG and NSS
    {"nmos", "nsub=1e16 tox=2e-8 kappa=0 tpg=-1 nss=1e11 phi=0.694857", 2, 0.5, 0, 0.000164362},
    {"nmos", "nsub=1e16 tox=2e-8 kappa=0 tpg=-1 nss=1e11 phi=0.694857", 2, 0.5, -2, 9.77203e-05},
    {"pmos", "nsub=1e16 tox=2e-8 kappa=0 tpg=-1 nss=1e11 phi=0.694857", -2, -0.5, 0, 0.000116297},
    {"pmos", "nsub=1e16 tox=2e-8 kappa=0 tpg=-1 nss=1e11 phi=0.694857", -2, -0.5, 2, 5.08965e-05},
    {"nmos", "nsub=1e16 tox=2e-8 kappa=0 tpg=0 phi=0.694857", 2, 0.5, 0, 0.000442071},
};

}  // namespace

// The current from drain to source (for PMOS from source to drain, as
// ngspice reports it), including reverse mode (VD below VS, VB at VD) and
// weak inversion with NFS.
TEST(Level3AgainstNgspice, DrainCurrent) {
    for (const auto& r : kReference) {
        char netlist[512];
        std::snprintf(netlist, sizeof netlist,
                      "level3\nVD d 0 DC %g\nVG g 0 DC %g\nVB b 0 DC %g\n"
                      "M1 d g 0 b m W=10u L=2u\n.model m %s level=3 %s\n.end\n",
                      r.vd, r.vg, r.vb, r.type, r.card);
        parser::NetlistParser parser;
        auto parsed = parser.parse_string(netlist);
        ASSERT_NE(parsed.circuit, nullptr);
        solvers::DCSolver solver(*parsed.circuit, parsed.config);
        DenseVector solution;
        SolverStats stats;
        ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS) << netlist;
        const Real i_vd = solver.get_branch_current("VD").value();
        const Real id = (std::string(r.type) == "nmos") ? -i_vd : i_vd;
        EXPECT_NEAR(id, r.id, 2e-5 * std::abs(r.id)) << netlist;
    }
}

// Above VDSAT the channel shortens by dL = Xd sqrt(KAPPA (VDS - VDSAT)),
// Xd = sqrt(2 epsSi / (q NSUB)), and the current is the one at VDSAT divided
// by 1 - dL / Leff; at and below VDSAT there is no such term.
TEST(Level3Equations, ChannelLengthModulationAboveVdsat) {
    MOSFETLevel3Params p;
    p.vto = 0.7;
    p.nsub = 1e16;
    p.phi = 0.7;
    p.gamma = 0.0;
    p.tox = 2e-8;
    p.kappa = 0.2;
    p.theta = 0.0;
    p.vmax = 0.0;
    p.w = 10e-6;
    p.l = 2e-6;
    MOSFET_Level3_N with_clm(1, "M1", 1, 2, 0, 0, p);
    p.kappa = 0.0;
    MOSFET_Level3_N without(2, "M2", 1, 2, 0, 0, p);

    const Real vgs = 3.0, vdsat = vgs - 0.7;  // no body factor: Vdsat = Vov
    const Real xd = std::sqrt(2.0 * MOSFET_Level3_N::eps_si /
                              (MOSFET_Level3_N::q_electron * 1e16 * 1e6));
    for (Real vds : {2.5, 4.0, 10.0}) {
        const Real dl = xd * std::sqrt(0.2 * (vds - vdsat));
        const Real expected = without.get_drain_current(vgs, vdsat, 0.0) / (1.0 - dl / 2e-6);
        EXPECT_NEAR(with_clm.get_drain_current(vgs, vds, 0.0), expected, 1e-12 * expected)
            << "VDS=" << vds;
    }
    for (Real vds : {0.5, vdsat}) {
        EXPECT_DOUBLE_EQ(with_clm.get_drain_current(vgs, vds, 0.0),
                         without.get_drain_current(vgs, vds, 0.0))
            << "VDS=" << vds;
    }
}

// A card without PHI and GAMMA takes PHI = 2 Vt ln(NSUB / ni), ni = 1.45e10
// cm^-3, and GAMMA = sqrt(2 q epsSi NSUB) / Cox, which set the body effect:
// Vth(VBS) - VTO = GAMMA (sqrt(PHI - VBS) - sqrt(PHI)).
TEST(Level3Equations, PhiAndGammaFollowNsub) {
    MOSFETLevel3Params p;
    p.vto = 0.7;
    p.nsub = 1e16;
    p.tox = 2e-8;
    p.phi_given = false;
    p.gamma_given = false;
    p.w = 10e-6;
    p.l = 2e-6;
    MOSFET_Level3_N m(1, "M1", 1, 2, 0, 0, p);
    const Real vt = MOSFET_Level3_N::k_boltzmann * p.tnom / MOSFET_Level3_N::q_electron;
    const Real phi = 2.0 * vt * std::log(1e16 / 1.45e10);
    const Real cox = MOSFET_Level3_N::eps_ox / 2e-8;
    const Real gamma =
        std::sqrt(2.0 * MOSFET_Level3_N::q_electron * MOSFET_Level3_N::eps_si * 1e22) / cox;
    EXPECT_NEAR(m.get_threshold_voltage(0.0), 0.7, 1e-12);
    EXPECT_NEAR(m.get_threshold_voltage(-2.0) - 0.7,
                gamma * (std::sqrt(phi + 2.0) - std::sqrt(phi)), 1e-12);
}
