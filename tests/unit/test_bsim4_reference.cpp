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
