// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The drain current and the conductances that a BSIM4 transistor stamps,
// against reference runs of ngspice-47.
//
// The card gives only TOXE, VTH0, U0 and VSAT, so every other parameter takes
// its default. The values are ngspice-47's `show m1 : id gm gds gmbs` (six
// digits) for
//
//   VD d 0 DC <vds>
//   VG g 0 DC <vgs>
//   VB b 0 DC <vbs>
//   M1 d g 0 b nch L=0.18u W=1u
//   .model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4
//
// ngspice's `id` is the channel current alone: its i(vd) also holds the
// GMIN currents of the drain and source junctions.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/temperature_utils.hpp"

using namespace kirimaspice;

namespace {

const char* kCard =
    ".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4\n";

struct Reference {
    Real vgs, vds, vbs;
    Real id, gm, gds, gmbs;
};

// ngspice-47
const Reference kReference[] = {
    {0.6, 0.05, 0.0, 1.13795e-07, 3.5163e-06, 8.37322e-07, 4.54219e-07},
    {0.6, 1.0, 0.0, 3.20828e-07, 9.80864e-06, 1.95104e-07, 1.24704e-06},
    {1.0, 0.05, 0.0, 2.97985e-05, 9.22035e-05, 0.000517094, 1.3342e-05},
    {1.0, 1.0, 0.0, 0.00012292, 0.000533238, 2.96913e-05, 6.59131e-05},
    {1.8, 0.3, 0.0, 0.000388301, 0.000328869, 0.00081393, 5.3649e-05},
    {1.8, 1.8, 0.0, 0.000696688, 0.00074506, 7.45275e-05, 8.64832e-05},
    {0.6, 1.0, -1.0, 1.79532e-08, 6.22826e-07, 1.23007e-08, 3.76343e-08},
    {1.0, 0.3, -1.0, 5.81894e-05, 0.00036773, 5.22846e-05, 2.18678e-05},
    {1.8, 0.05, -1.0, 8.01833e-05, 6.15796e-05, 0.00149815, 6.41768e-06},
    {1.8, 1.8, -1.0, 0.000637051, 0.000741189, 7.13974e-05, 4.02567e-05},
    {1.0, 1.0, -0.5, 9.67119e-05, 0.000510096, 2.52199e-05, 4.16814e-05},
};

// What M1 stamps in the row of its drain: gm, gds and gmb are the entries in
// the gate, drain and bulk columns, and the drain current follows from the
// right-hand side, b = -(ids - gm vgs - gds vds - gmb vbs).
struct DrainRow {
    Real ids = 0.0;
    Real gm = 0.0;
    Real gds = 0.0;
    Real gmb = 0.0;
};

// options holds further lines such as ".options temp=100", whose temperature
// is applied to the devices as the DC solver applies it.
DrainRow drain_row_of_m1(const std::string& instance, const std::string& card,
                         Real vd, Real vg, Real vs, Real vb,
                         const std::string& options = "") {
    parser::NetlistParser parser;
    auto parsed =
        parser.parse_string("bias\n" + instance + "\n" + card + options + ".end\n");
    EXPECT_NE(parsed.circuit, nullptr);
    auto& circuit = *parsed.circuit;
    if (parsed.config.temperature_explicitly_set) {
        solvers::apply_temperature_to_devices(circuit, parsed.config.temperature);
    }
    auto device = circuit.get_device("M1");
    EXPECT_NE(device, nullptr);

    const NodeId d = circuit.get_node_id("d");
    const NodeId g = circuit.get_node_id("g");
    const NodeId s = circuit.get_node_id("s");
    const NodeId b = circuit.get_node_id("b");
    const std::size_t size = circuit.num_nodes() + device->extra_node_count();
    DenseVector v(size, 0.0);
    v[d - 1] = vd;
    v[g - 1] = vg;
    v[s - 1] = vs;
    v[b - 1] = vb;

    MNASystem mna;
    mna.resize(size, 0);
    mna.zero();
    device->stamp_dc_nonlinear(mna, v);

    DrainRow row;
    for (std::size_t k = 0; k < mna.G.nnz(); ++k) {
        if (mna.G.rows[k] != static_cast<int>(d - 1)) {
            continue;
        }
        const auto col = mna.G.cols[k];
        if (col == static_cast<int>(g - 1)) row.gm += mna.G.values[k];
        if (col == static_cast<int>(d - 1)) row.gds += mna.G.values[k];
        if (col == static_cast<int>(b - 1)) row.gmb += mna.G.values[k];
    }
    row.ids = -mna.b[d - 1] + row.gm * (vg - vs) + row.gds * (vd - vs) + row.gmb * (vb - vs);
    return row;
}

std::string bias_of(const Reference& ref) {
    char text[64];
    std::snprintf(text, sizeof text, "VGS=%.2f VDS=%.2f VBS=%.2f", ref.vgs, ref.vds, ref.vbs);
    return text;
}

DrainRow nmos_at(Real vgs, Real vds, Real vbs) {
    return drain_row_of_m1("M1 d g s b nch L=0.18u W=1u", kCard, vds, vgs, 0.0, vbs);
}

}  // namespace

// The drain current over the inversion regions, with and without body bias.
TEST(BSIM4AgainstNgspice, DrainCurrent) {
    for (const auto& ref : kReference) {
        const DrainRow row = nmos_at(ref.vgs, ref.vds, ref.vbs);
        EXPECT_NEAR(row.ids, ref.id, 2e-4 * ref.id) << bias_of(ref);
    }
}

// The drain current at other temperatures than TNOM = 27 C: Vth (KT1),
// the mobility (UTE, UA1, UB1) and VSAT (AT) follow the temperature.
// ngspice-47 with ".temp <T>" (here ".options temp=<T>"), VDS = 1 V, VBS = 0.
TEST(BSIM4AgainstNgspice, DrainCurrentAtOtherTemperatures) {
    struct AtTemperature {
        Real celsius, vgs, id;
    };
    const AtTemperature points[] = {
        {100.0, 0.6, 6.04225e-07}, {100.0, 1.0, 1.04236e-04}, {100.0, 1.8, 5.17267e-04},
        {-40.0, 0.6, 1.20355e-07}, {-40.0, 1.0, 1.45518e-04}, {-40.0, 1.8, 7.41321e-04},
    };
    for (const auto& p : points) {
        char options[64];
        std::snprintf(options, sizeof options, ".options temp=%.1f\n", p.celsius);
        const DrainRow row = drain_row_of_m1("M1 d g s b nch L=0.18u W=1u", kCard, 1.0, p.vgs,
                                             0.0, 0.0, options);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << options << "VGS=" << p.vgs;
    }
}

// With body bias the temperature also acts through UC1, whose default for
// MOBMOD = 0 is -0.056e-9 m/V^2. ngspice-47 with ".temp <T>", VDS = 1 V,
// VBS = -1 V.
TEST(BSIM4AgainstNgspice, DrainCurrentAtOtherTemperaturesWithBodyBias) {
    struct AtTemperature {
        Real celsius, vgs, id;
    };
    const AtTemperature points[] = {
        {100.0, 1.0, 7.04314e-05}, {100.0, 1.8, 4.73168e-04},
        {-40.0, 1.0, 8.95418e-05}, {-40.0, 1.8, 6.72150e-04},
    };
    for (const auto& p : points) {
        char options[64];
        std::snprintf(options, sizeof options, ".options temp=%.1f\n", p.celsius);
        const DrainRow row = drain_row_of_m1("M1 d g s b nch L=0.18u W=1u", kCard, 1.0, p.vgs,
                                             0.0, -1.0, options);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << options << "VGS=" << p.vgs;
    }
}

// A PMOS card gives VTH0 negative, as BSIM4 cards do; the model takes
// TYPE * VTH0. ngspice-47's `show m1 : id` for
// .model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 u0=0.025 vsat=8e4,
// M1 d g 0 b pch L=0.18u W=1u, VBS = 0. ngspice gives the magnitude; the
// current that flows from drain to source is negative.
TEST(BSIM4AgainstNgspice, PmosDrainCurrent) {
    const std::string card =
        ".model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 u0=0.025 vsat=8e4\n";
    struct Point {
        Real vgs, vds, id;
    };
    const Point points[] = {
        {-0.6, -0.1, 5.14543e-08}, {-0.6, -1.0, 1.11298e-07}, {-1.0, -0.1, 2.23597e-05},
        {-1.0, -1.0, 5.50758e-05}, {-1.8, -0.1, 7.44459e-05}, {-1.8, -1.0, 3.39023e-04},
    };
    for (const auto& p : points) {
        const DrainRow row =
            drain_row_of_m1("M1 d g s b pch L=0.18u W=1u", card, p.vds, p.vgs, 0.0, 0.0);
        EXPECT_NEAR(-row.ids, p.id, 2e-4 * p.id) << "VGS=" << p.vgs << " VDS=" << p.vds;
    }
}

// The mobility models: MOBMOD 1 and 2, and MOBMOD 0 with Coulomb
// scattering (UD) and the channel-length factor of U0 (UP, LP = 1e-8 m).
// ngspice-47's `show m1 : id` for the card above with the parameters added.
TEST(BSIM4AgainstNgspice, MobilityModels) {
    struct Point {
        const char* extra;
        Real vgs, vds, vbs, id;
    };
    const Point points[] = {
        {"mobmod=1", 1.0, 0.1, 0.0, 5.21294e-05},
        {"mobmod=1", 1.8, 0.1, 0.0, 1.66861e-04},
        {"mobmod=1", 1.0, 1.0, -0.5, 9.66785e-05},
        {"mobmod=2", 1.0, 0.1, 0.0, 6.58641e-05},
        {"mobmod=2", 1.8, 0.1, 0.0, 2.01013e-04},
        {"ud=1e17 up=1e7", 0.6, 1.0, 0.0, 2.32463e-07},
        {"ud=1e17 up=1e7", 1.0, 0.1, 0.0, 4.23974e-05},
        {"ud=1e17 up=1e7", 1.8, 1.0, 0.0, 5.60731e-04},
    };
    for (const auto& p : points) {
        const std::string card =
            std::string(".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 "
                        "vsat=8e4 ") + p.extra + "\n";
        const DrainRow row =
            drain_row_of_m1("M1 d g s b nch L=0.18u W=1u", card, p.vds, p.vgs, 0.0, p.vbs);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id)
            << p.extra << " VGS=" << p.vgs << " VDS=" << p.vds << " VBS=" << p.vbs;
    }
}

// U0 and EU that a card does not give take the defaults of its TYPE: 0.067
// m^2/Vs and 1.67 for NMOS, 0.025 m^2/Vs and 1.0 for PMOS (EU acts for
// MOBMOD 2). ngspice-47's `show m1 : id`, VBS = 0.
TEST(BSIM4AgainstNgspice, MobilityDefaultsOfEachType) {
    struct Point {
        const char* instance;
        const char* card;
        Real vgs, vds, id;
    };
    const Point points[] = {
        {"M1 d g s b nch L=0.18u W=1u",
         ".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 vsat=8e4\n", 1.0, 0.1,
         5.21294e-05},
        {"M1 d g s b pch L=0.18u W=1u",
         ".model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 vsat=8e4\n", -1.0, -0.1,
         2.23597e-05},
        {"M1 d g s b pch L=0.18u W=1u",
         ".model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 vsat=8e4\n", -1.8, -1.0,
         3.39023e-04},
        {"M1 d g s b pch L=0.18u W=1u",
         ".model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 u0=0.025 vsat=8e4 "
         "mobmod=2\n",
         -1.0, -0.1, 3.38160e-05},
        {"M1 d g s b pch L=0.18u W=1u",
         ".model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 u0=0.025 vsat=8e4 "
         "mobmod=2\n",
         -1.8, -1.0, 5.16942e-04},
    };
    for (const auto& p : points) {
        const DrainRow row = drain_row_of_m1(p.instance, p.card, p.vds, p.vgs, 0.0, 0.0);
        EXPECT_NEAR(std::abs(row.ids), p.id, 2e-4 * p.id) << p.card << "VGS=" << p.vgs;
    }
}

// gm, gds and gmb are the derivatives of that drain current.
TEST(BSIM4AgainstNgspice, Conductances) {
    for (const auto& ref : kReference) {
        const DrainRow row = nmos_at(ref.vgs, ref.vds, ref.vbs);
        const std::string at = bias_of(ref);
        EXPECT_NEAR(row.gm, ref.gm, 1e-3 * ref.gm) << at;
        EXPECT_NEAR(row.gds, ref.gds, 1e-3 * ref.gds) << at;
        EXPECT_NEAR(row.gmb, ref.gmbs, 1e-3 * ref.gmbs) << at;
    }
}

// With the drain below the source the device swaps them, and with a P-channel
// card it works on the negated voltages. In both cases the stamped gm, gds and
// gmb are still the derivatives of the stamped drain current with respect to
// the gate, drain and bulk voltages, as Newton's method and the AC analysis
// take them.
TEST(BSIM4Conductances, AreTheDerivativesOfTheStampedCurrent) {
    const std::string pmos_card =
        ".model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 u0=0.025 vsat=8e4\n";
    struct Case {
        const char* what;
        std::string instance;
        std::string card;
        Real vd, vg, vs, vb;
    };
    const Case cases[] = {
        {"NMOS, drain below source", "M1 d g s b nch L=0.18u W=1u", kCard, 0.2, 1.4, 1.0, -0.3},
        {"PMOS", "M1 d g s b pch L=0.18u W=1u", pmos_card, 0.4, 0.3, 1.8, 1.8},
        {"PMOS, drain above source", "M1 d g s b pch L=0.18u W=1u", pmos_card, 1.8, 0.3, 1.2, 1.8},
    };
    const Real h = 1e-4;
    for (const auto& c : cases) {
        const DrainRow row = drain_row_of_m1(c.instance, c.card, c.vd, c.vg, c.vs, c.vb);
        auto ids = [&](Real vd, Real vg, Real vb) {
            return drain_row_of_m1(c.instance, c.card, vd, vg, c.vs, vb).ids;
        };
        const Real gm = (ids(c.vd, c.vg + h, c.vb) - ids(c.vd, c.vg - h, c.vb)) / (2 * h);
        const Real gds = (ids(c.vd + h, c.vg, c.vb) - ids(c.vd - h, c.vg, c.vb)) / (2 * h);
        const Real gmb = (ids(c.vd, c.vg, c.vb + h) - ids(c.vd, c.vg, c.vb - h)) / (2 * h);
        ASSERT_GT(std::abs(row.ids), 1e-6) << c.what;
        EXPECT_NEAR(row.gm, gm, 1e-3 * std::abs(gm)) << c.what;
        EXPECT_NEAR(row.gds, gds, 1e-3 * std::abs(gds)) << c.what;
        EXPECT_NEAR(row.gmb, gmb, 1e-3 * std::abs(gmb)) << c.what;
    }
}

// Long channels: Theta0, the short-channel factor of Vth, takes BSIM4's
// constant 1 / (MAX_EXP - 2), about zero, once DVT1 Leff / lt1 reaches 34
// (BSIM4 4.8.3 b4ld.c), so that Vth tends to its long-channel value. DSUB and
// DROUT are small here so that their factors stay below that limit.
// ngspice-47, run 2026-11-21: `id gm gds gmbs` of m1 for
//
//   .model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4
//   +   dsub=0.05 drout=0.05
//   .model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 u0=0.025 vsat=8e4
//   +   dsub=0.05 drout=0.05
//   M1 d g 0 b nch W=10u L=<L>
TEST(BSIM4AgainstNgspice, LongChannelThreshold) {
    const std::string nmos =
        ".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4 "
        "dsub=0.05 drout=0.05\n";
    const std::string pmos =
        ".model pch pmos level=14 version=4.8.1 toxe=3e-9 vth0=-0.5 u0=0.025 vsat=8e4 "
        "dsub=0.05 drout=0.05\n";
    struct Point {
        const char* instance;
        const std::string& card;
        Real vgs, vds, vbs;
        Real id, gm, gds, gmbs;
    };
    const Point points[] = {
        {"M1 d g s b nch L=3.1u W=10u", nmos, 1.8, 1.8, 0.0,
         9.034393e-04, 1.137719e-03, 2.762677e-05, 1.060951e-04},
        {"M1 d g s b nch L=3.1u W=10u", nmos, 1.8, 1.8, -1.0,
         8.178243e-04, 1.129566e-03, 2.661236e-05, 6.582295e-05},
        {"M1 d g s b nch L=3.1u W=10u", nmos, 0.6, 0.1, 0.0,
         5.300985e-06, 9.942084e-05, 1.480805e-05, 1.369951e-05},
        {"M1 d g s b nch L=10u W=10u", nmos, 1.8, 1.8, 0.0,
         2.944434e-04, 3.808298e-04, 5.489122e-06, 3.366409e-05},
        // ngspice gives the magnitudes; the PMOS current flows from source
        // to drain
        {"M1 d g s b pch L=10u W=10u", pmos, -1.8, -1.8, 0.0,
         1.115056e-04, 1.453695e-04, 1.916612e-06, 1.275424e-05},
    };
    for (const auto& p : points) {
        const DrainRow row = drain_row_of_m1(p.instance, p.card, p.vds, p.vgs, 0.0, p.vbs);
        const std::string at = std::string(p.instance) + " VGS=" + std::to_string(p.vgs) +
                               " VDS=" + std::to_string(p.vds) + " VBS=" + std::to_string(p.vbs);
        EXPECT_NEAR(std::abs(row.ids), p.id, 2e-4 * p.id) << at;
        EXPECT_NEAR(std::abs(row.gm), p.gm, 1e-3 * p.gm) << at;
        EXPECT_NEAR(std::abs(row.gds), p.gds, 1e-3 * p.gds) << at;
        EXPECT_NEAR(std::abs(row.gmb), p.gmbs, 1e-3 * p.gmbs) << at;
    }
}

// The DIBL factors of long channels: theta0vb0, the factor of the DIBL shift
// of Vth, of DSUB Leff / lt0, and the factor of thetaRout of the output
// resistance, of DROUT Leff / lt0, take the same constant 1 / (MAX_EXP - 2)
// as Theta0 once their argument reaches 34 (BSIM4 4.8.3 b4temp.c). At
// L = 1.5 um, DROUT Leff / lt0 is about 31.5 with the reference card; with
// DSUB = 2 at L = 1 um, DSUB Leff / lt0 is about 75. ngspice-47, run
// 2026-11-21: `id gm gds gmbs` of m1 for the cards
//
//   ref:   .model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4
//   short: .model nch nmos level=14 version=4.8.1 toxe=2e-9 vth0=0.4 u0=0.03 vsat=1e5
//   M1 d g 0 b nch W=<W> L=<L>, VBS = 0
TEST(BSIM4AgainstNgspice, LongChannelDibl) {
    const std::string ref =
        ".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4\n";
    const std::string ref_dsub =
        ".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 vsat=8e4 dsub=2\n";
    const std::string short_card =
        ".model nch nmos level=14 version=4.8.1 toxe=2e-9 vth0=0.4 u0=0.03 vsat=1e5\n";
    struct Point {
        const char* instance;
        const std::string& card;
        Real vgs, vds;
        Real id, gm, gds, gmbs;
    };
    const Point points[] = {
        {"M1 d g s b nch L=1.5u W=10u", ref, 1.8, 1.8,
         1.727650e-03, 2.113506e-03, 6.839317e-05, 2.124033e-04},
        {"M1 d g s b nch L=1.5u W=10u", ref, 0.7, 1.0,
         5.309869e-05, 5.825637e-04, 5.659477e-06, 7.897428e-05},
        {"M1 d g s b nch L=3.1u W=10u", ref, 1.8, 1.8,
         9.016743e-04, 1.137933e-03, 2.573890e-05, 1.065600e-04},
        {"M1 d g s b nch L=10u W=10u", ref, 1.8, 1.8,
         2.944434e-04, 3.808298e-04, 5.489120e-06, 3.366409e-05},
        {"M1 d g s b nch L=2u W=1u", short_card, 0.6, 1.2,
         2.117607e-06, 2.616217e-05, 2.027710e-07, 2.366206e-06},
        {"M1 d g s b nch L=2u W=1u", short_card, 1.2, 1.2,
         3.479863e-05, 7.668591e-05, 1.497999e-06, 6.133006e-06},
        {"M1 d g s b nch L=1u W=10u", ref_dsub, 0.5, 1.8,
         1.356353e-06, 3.899271e-05, 3.127132e-07, 5.498242e-06},
        {"M1 d g s b nch L=1u W=10u", ref_dsub, 1.0, 1.8,
         4.870999e-04, 1.749397e-03, 2.818408e-05, 2.276706e-04},
    };
    for (const auto& p : points) {
        const DrainRow row = drain_row_of_m1(p.instance, p.card, p.vds, p.vgs, 0.0, 0.0);
        const std::string at = p.card + p.instance + " VGS=" + std::to_string(p.vgs) +
                               " VDS=" + std::to_string(p.vds);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << at;
        EXPECT_NEAR(row.gm, p.gm, 1e-3 * p.gm) << at;
        EXPECT_NEAR(row.gds, p.gds, 1e-3 * p.gds) << at;
        EXPECT_NEAR(row.gmb, p.gmbs, 1e-3 * p.gmbs) << at;
    }
}

// PRT, the temperature coefficient of the source/drain resistance: for
// TEMPMOD = 0, BSIM4 adds PRT (T / TNOM - 1) to RDSW and to RDSWMIN, and sets
// either to zero where it then is negative (BSIM4 4.8.3 b4temp.c and
// b4check.c; ngspice-47 warns "Rdswmin at current temperature ... is
// negative. Set to zero." at -40 C here). ngspice-47, run 2026-11-21: `id gm
// gds gmbs` of m1 for the reference card with PRT (and WR) added, TNOM = 27 C
// by default, ".options temp=<T>", VBS = 0.
TEST(BSIM4AgainstNgspice, ResistanceTemperatureCoefficient) {
    struct Point {
        const char* extra;
        Real celsius, vgs, vds;
        Real id, gm, gds, gmbs;
    };
    const Point points[] = {
        {"prt=100", 100.0, 1.0, 1.0, 1.031982e-04, 4.386847e-04, 2.515803e-05, 5.098189e-05},
        {"prt=100", 100.0, 1.0, 0.05, 2.305873e-05, 6.817505e-05, 4.089174e-04, 9.456655e-06},
        {"prt=100", 100.0, 1.8, 1.8, 5.712953e-04, 6.116536e-04, 6.336622e-05, 6.613293e-05},
        {"prt=100", -40.0, 1.0, 0.05, 3.901680e-05, 1.282546e-04, 6.573084e-04, 1.905093e-05},
        {"prt=100", -40.0, 1.8, 1.8, 8.339461e-04, 8.773435e-04, 8.510996e-05, 1.070088e-04},
        {"prt=500 wr=0.9", -40.0, 1.8, 1.8,
         8.819032e-04, 9.297548e-04, 9.338121e-05, 1.171777e-04},
        {"prt=500 wr=0.9", 100.0, 1.0, 0.05,
         2.133481e-05, 5.900563e-05, 3.819493e-04, 8.168147e-06},
    };
    for (const auto& p : points) {
        const std::string card =
            std::string(".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 "
                        "vsat=8e4 ") + p.extra + "\n";
        char options[64];
        std::snprintf(options, sizeof options, ".options temp=%.1f\n", p.celsius);
        const DrainRow row =
            drain_row_of_m1("M1 d g s b nch L=0.18u W=1u", card, p.vds, p.vgs, 0.0, 0.0, options);
        const std::string at = std::string(p.extra) + " " + options + "VGS=" +
                               std::to_string(p.vgs) + " VDS=" + std::to_string(p.vds);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << at;
        EXPECT_NEAR(row.gm, p.gm, 1e-3 * p.gm) << at;
        EXPECT_NEAR(row.gds, p.gds, 1e-3 * p.gds) << at;
        EXPECT_NEAR(row.gmb, p.gmbs, 1e-3 * p.gmbs) << at;
    }
}

// DSUB, the DIBL coefficient of the threshold, takes the value of DROUT when
// the card does not give it (BSIM4 4.8.3 b4set.c); a card that gives DSUB
// keeps it. ngspice-47, run 2026-11-21: `id gm gds gmbs` of m1 for
//
//   .model nch nmos level=14 version=4.8.1 toxe=2e-9 vth0=0.4 u0=0.03 vsat=1e5 <extra>
//   M1 d g 0 b nch L=0.09u W=1u, VBS = 0
//
// With DROUT = 0.3 the DIBL shift is large enough to make gmbs negative.
TEST(BSIM4AgainstNgspice, DsubDefaultsToDrout) {
    struct Point {
        const char* extra;
        Real vgs, vds;
        Real id, gm, gds, gmbs;
    };
    const Point points[] = {
        {"drout=0.3", 0.6, 1.2, 1.264765e-04, 1.196847e-03, 1.711610e-04, -1.546362e-05},
        {"drout=0.3", 1.2, 1.2, 7.051427e-04, 8.888970e-04, 4.472779e-04, -1.611683e-05},
        {"drout=0.3", 0.6, 0.05, 4.992666e-06, 9.217409e-05, 7.243528e-05, 3.489092e-06},
        {"drout=1", 0.6, 1.2, 1.407893e-05, 2.880668e-04, 6.125130e-06, 1.069648e-05},
        {"drout=0.3 dsub=0.56", 0.6, 1.2, 7.424805e-05, 1.041407e-03, 7.952337e-05, 2.898417e-05},
    };
    for (const auto& p : points) {
        const std::string card =
            std::string(".model nch nmos level=14 version=4.8.1 toxe=2e-9 vth0=0.4 u0=0.03 "
                        "vsat=1e5 ") + p.extra + "\n";
        const DrainRow row =
            drain_row_of_m1("M1 d g s b nch L=0.09u W=1u", card, p.vds, p.vgs, 0.0, 0.0);
        const std::string at = std::string(p.extra) + " VGS=" + std::to_string(p.vgs) +
                               " VDS=" + std::to_string(p.vds);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << at;
        EXPECT_NEAR(row.gm, p.gm, 1e-3 * std::abs(p.gm)) << at;
        EXPECT_NEAR(row.gds, p.gds, 1e-3 * std::abs(p.gds)) << at;
        EXPECT_NEAR(row.gmb, p.gmbs, 1e-3 * std::abs(p.gmbs)) << at;
    }
}

// XL and XW, the offsets of the drawn length and width due to mask and etch,
// add to L and to the width of one finger before LINT and WINT are taken off
// (BSIM4 4.8.3 b4temp.c: Lnew = L + XL, Wnew = W / NF + XW). ngspice-47, run
// 2026-11-21: `id gm gds gmbs` of m1 for the reference card with XL and XW
// added, VBS = 0.
TEST(BSIM4AgainstNgspice, MaskAndEtchOffsets) {
    struct Point {
        const char* extra;
        const char* instance;
        Real vgs, vds;
        Real id, gm, gds, gmbs;
    };
    const Point points[] = {
        {"xl=-20n", "M1 d g s b nch L=0.18u W=1u", 1.0, 1.0,
         1.334358e-04, 5.740063e-04, 3.692863e-05, 6.702098e-05},
        {"xw=50n", "M1 d g s b nch L=0.18u W=1u", 1.0, 1.0,
         1.295435e-04, 5.603433e-04, 3.126674e-05, 6.926451e-05},
        {"xl=-20n xw=50n", "M1 d g s b nch L=0.18u W=1u", 1.8, 1.8,
         7.837104e-04, 8.193489e-04, 9.333495e-05, 8.931589e-05},
        {"xl=-20n xw=50n", "M1 d g s b nch L=0.18u W=1u", 1.0, 0.05,
         3.361626e-05, 1.063809e-04, 5.789036e-04, 1.457901e-05},
        {"xl=20n xw=-0.2u", "M1 d g s b nch L=0.18u W=1u", 1.0, 1.0,
         9.059279e-05, 3.980001e-04, 1.967083e-05, 5.103348e-05},
        {"xw=50n", "M1 d g s b nch L=0.18u W=2u nf=2", 1.8, 1.8,
         1.464495e-03, 1.564875e-03, 1.566237e-04, 1.816377e-04},
    };
    for (const auto& p : points) {
        const std::string card =
            std::string(".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 "
                        "vsat=8e4 ") + p.extra + "\n";
        const DrainRow row = drain_row_of_m1(p.instance, card, p.vds, p.vgs, 0.0, 0.0);
        const std::string at = std::string(p.extra) + " " + p.instance + " VGS=" +
                               std::to_string(p.vgs) + " VDS=" + std::to_string(p.vds);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << at;
        EXPECT_NEAR(row.gm, p.gm, 1e-3 * p.gm) << at;
        EXPECT_NEAR(row.gds, p.gds, 1e-3 * p.gds) << at;
        EXPECT_NEAR(row.gmb, p.gmbs, 1e-3 * p.gmbs) << at;
    }
}

// PRWB, the body-bias coefficient of Rds, multiplies the rise of sqrt(Phis)
// over sqrt(Phi), so it has no effect at VBS = 0 (BSIM4 4.8.3 b4ld.c). With
// body bias gmbs is here the small difference of the body effects of Vth and
// of Rds, and is compared within 1e-5 gm as well, since the threshold with
// body bias differs from ngspice-47's by some microvolts (KNOWN_ISSUES.md).
// ngspice-47, run 2026-11-21: `id gm gds gmbs` of m1 for
//
//   .model nch nmos level=14 version=4.8.1 toxe=2e-9 vth0=0.4 u0=0.03 vsat=1e5 <extra>
//   M1 d g 0 b nch L=0.09u W=1u
TEST(BSIM4AgainstNgspice, ResistanceBodyBiasCoefficient) {
    struct Point {
        const char* extra;
        Real vgs, vds, vbs;
        Real id, gm, gds, gmbs;
    };
    const Point points[] = {
        {"prwb=0.2", 1.2, 1.2, 0.0, 5.133164e-04, 9.088863e-04, 1.698216e-04, 3.250307e-05},
        {"prwb=0.2", 1.2, 0.05, 0.0, 6.721244e-05, 8.175533e-05, 1.257236e-03, 7.679876e-06},
        {"prwb=0.2", 1.2, 0.05, -0.8, 6.315824e-05, 7.792985e-05, 1.185023e-03, 3.228307e-06},
        {"prwb=0.2 rdsw=500", 1.2, 1.2, -0.8,
         4.432191e-04, 7.514371e-04, 1.543774e-04, 4.082084e-06},
        {"prwb=-0.1 rdsw=500", 1.2, 0.05, -0.8,
         5.287505e-05, 6.109865e-05, 1.002819e-03, 3.276873e-07},
    };
    for (const auto& p : points) {
        const std::string card =
            std::string(".model nch nmos level=14 version=4.8.1 toxe=2e-9 vth0=0.4 u0=0.03 "
                        "vsat=1e5 ") + p.extra + "\n";
        const DrainRow row =
            drain_row_of_m1("M1 d g s b nch L=0.09u W=1u", card, p.vds, p.vgs, 0.0, p.vbs);
        const std::string at = std::string(p.extra) + " VGS=" + std::to_string(p.vgs) +
                               " VDS=" + std::to_string(p.vds) + " VBS=" + std::to_string(p.vbs);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << at;
        EXPECT_NEAR(row.gm, p.gm, 1e-3 * p.gm) << at;
        EXPECT_NEAR(row.gds, p.gds, 1e-3 * p.gds) << at;
        EXPECT_NEAR(row.gmb, p.gmbs, std::max(1e-3 * p.gmbs, 1e-5 * p.gm)) << at;
    }
}

// Strong body bias: Vbseff follows Vbs down to VBSC and then levels off
// (BSIM4 4.8.3 b4ld.c), where VBSC is 0.9 (phi - (K1 / (2 K2))^2) limited to
// -30 V to -3 V for K2 < 0, and at most VBM (b4temp.c). K2 = -0.3 (with
// K1 = 0.53, the default when only K2 is given) gives VBSC = -3 V; the
// reference card gives about -6.7 V. ngspice-47, run 2026-11-21: `id gm gds
// gmbs` of m1 for the reference card with <extra>, L = 0.18 um, W = 1 um.
TEST(BSIM4AgainstNgspice, StrongBodyBias) {
    struct Point {
        const char* extra;
        Real vgs, vds, vbs;
        Real id, gm, gds, gmbs;
    };
    const Point points[] = {
        {"k2=-0.3", 1.0, 1.0, -1.0, 1.503119e-04, 5.796334e-04, 3.663403e-05, -7.845208e-05},
        {"k2=-0.3", 1.0, 1.0, -3.0, 3.732647e-04, 6.935759e-04, 8.200463e-05, -7.217976e-05},
        {"k2=-0.3", 1.0, 1.0, -5.0, 3.809922e-04, 6.958078e-04, 8.357128e-05, -1.100030e-07},
        {"k2=-0.3", 1.8, 1.8, -5.0, 1.071010e-03, 8.101868e-04, 1.108232e-04, -1.381891e-07},
        {"k2=-0.3", 0.6, 1.0, -3.0, 1.106824e-04, 5.977090e-04, 3.141091e-05, -5.790727e-05},
        {"", 1.0, 1.0, -5.0, 4.737332e-05, 4.382350e-04, 1.864956e-05, -2.670049e-06},
        {"", 1.8, 1.8, -5.0, 5.990866e-04, 7.428428e-04, 7.508538e-05, -8.650276e-06},
    };
    for (const auto& p : points) {
        const std::string card =
            std::string(".model nch nmos level=14 version=4.8.1 toxe=3e-9 vth0=0.5 u0=0.067 "
                        "vsat=8e4 ") + p.extra + "\n";
        const DrainRow row =
            drain_row_of_m1("M1 d g s b nch L=0.18u W=1u", card, p.vds, p.vgs, 0.0, p.vbs);
        const std::string at = std::string(p.extra) + " VGS=" + std::to_string(p.vgs) +
                               " VDS=" + std::to_string(p.vds) + " VBS=" + std::to_string(p.vbs);
        EXPECT_NEAR(row.ids, p.id, 2e-4 * p.id) << at;
        EXPECT_NEAR(row.gm, p.gm, 1e-3 * p.gm) << at;
        EXPECT_NEAR(row.gds, p.gds, 1e-3 * p.gds) << at;
        EXPECT_NEAR(row.gmb, p.gmbs, std::max(1e-3 * std::abs(p.gmbs), 1e-5 * p.gm)) << at;
    }
}
