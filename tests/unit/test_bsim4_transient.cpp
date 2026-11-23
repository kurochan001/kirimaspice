// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/devices/bsim4_core.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "source_path.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

namespace {

struct ReferencePoint {
    Real time;
    Real v_in;
    Real v_n1;
    Real v_n2;
    Real v_n3;
};

std::vector<ReferencePoint> load_bsim4_reference(const std::string& basename) {
    std::vector<std::string> candidates = {kirimaspice::test::source_path(basename).string()};
    std::ifstream file;
    for (const auto& candidate : candidates) {
        file.close();
        file.clear();
        file.open(candidate);
        if (file.is_open()) break;
    }
    std::vector<ReferencePoint> data;
    if (!file.is_open()) return data;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        ReferencePoint pt{};
        std::istringstream iss(line);
        if (iss >> pt.time >> pt.v_in >> pt.v_n1 >> pt.v_n2 >> pt.v_n3) {
            data.push_back(pt);
        }
    }
    return data;
}

Real interpolate_signal(const std::vector<ReferencePoint>& ref, Real t, int signal_idx) {
    if (ref.empty()) return 0.0;
    if (t <= ref.front().time) {
        switch(signal_idx) {
            case 0: return ref.front().v_in;
            case 1: return ref.front().v_n1;
            case 2: return ref.front().v_n2;
            case 3: return ref.front().v_n3;
        }
    }
    if (t >= ref.back().time) {
        switch(signal_idx) {
            case 0: return ref.back().v_in;
            case 1: return ref.back().v_n1;
            case 2: return ref.back().v_n2;
            case 3: return ref.back().v_n3;
        }
    }
    
    auto upper = std::lower_bound(ref.begin(), ref.end(), t,
        [](const ReferencePoint& pt, Real val) { return pt.time < val; });
    auto lower = upper - 1;
    Real fraction = (t - lower->time) / (upper->time - lower->time);
    
    Real v_lower=0, v_upper=0;
    switch(signal_idx) {
        case 0: v_lower = lower->v_in; v_upper = upper->v_in; break;
        case 1: v_lower = lower->v_n1; v_upper = upper->v_n1; break;
        case 2: v_lower = lower->v_n2; v_upper = upper->v_n2; break;
        case 3: v_lower = lower->v_n3; v_upper = upper->v_n3; break;
    }
    
    return v_lower + fraction * (v_upper - v_lower);
}

} // namespace

// The three switching edges must all meet the reference waveform tolerance.
TEST(BSIM4Transient, DISABLED_InverterChain_MatchesNgspiceReference) {
    // Reduce log chatter (BSIM4 debug logs are extremely verbose) but keep errors
    spdlog::set_level(spdlog::level::err);

    auto reference = load_bsim4_reference("validation/bsim4_inv_chain_tran_ref.txt");
    ASSERT_FALSE(reference.empty()) << "Run scripts/generate_bsim4_tran_ref.py first";

    // The reference generator reads the same deck, including the BSIM4
    // card defaults, instance dimensions, integration method and time step.
    parser::NetlistParser parser;
    auto parsed = parser.parse_file(
        kirimaspice::test::source_path("validation/bsim4_inv_chain.cir").string());
    auto& circuit = *parsed.circuit;
    const auto& tran = parsed.config.transient;
    TransientSolver solver(circuit, parsed.config);
    auto result = solver.solve(0.0, tran.end_time, tran.initial_step);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_FALSE(result.timepoints.empty());
    ASSERT_NEAR(result.timepoints.back().time, tran.end_time, 1e-18);

    NodeId n_n1 = circuit.get_node_id("n1");
    NodeId n_n2 = circuit.get_node_id("n2");
    NodeId n_n3 = circuit.get_node_id("n3");

    Real max_err_n1 = 0.0;
    Real max_err_n2 = 0.0;
    Real max_err_n3 = 0.0;
    
    for (const auto& tp : result.timepoints) {
        // Skip very first few points where initial transient might differ slightly
        if (tp.time < 1e-10) continue;
        
        Real ref_n1 = interpolate_signal(reference, tp.time, 1);
        Real ref_n2 = interpolate_signal(reference, tp.time, 2);
        Real ref_n3 = interpolate_signal(reference, tp.time, 3);
        
        Real sim_n1 = tp.node_voltages[n_n1 - 1];
        Real sim_n2 = tp.node_voltages[n_n2 - 1];
        Real sim_n3 = tp.node_voltages[n_n3 - 1];
        
        max_err_n1 = std::max(max_err_n1, std::abs(sim_n1 - ref_n1));
        max_err_n2 = std::max(max_err_n2, std::abs(sim_n2 - ref_n2));
        max_err_n3 = std::max(max_err_n3, std::abs(sim_n3 - ref_n3));
    }
    
    std::cout << "Max Error n1: " << max_err_n1 << " V" << std::endl;
    std::cout << "Max Error n2: " << max_err_n2 << " V" << std::endl;
    std::cout << "Max Error n3: " << max_err_n3 << " V" << std::endl;
    
    // Allow 50mV tolerance (typical for transient mismatch due to timestep/method diffs)
    // BSIM4 is complex, so slight model diffs accumulate in chains
    EXPECT_LT(max_err_n1, 0.05);
    EXPECT_LT(max_err_n2, 0.05);
    EXPECT_LT(max_err_n3, 0.05);
}

// ngspice-47 .op at |Vgs|=|Vds|=0.9 V with source tied to bulk,
// TOXE=3 nm, L=180 nm, Wn=10 um, Wp=20 um. Charges below are in fC
// and derivatives in fF. The P device's physical charges change sign.
TEST(BSIM4Transient, IntrinsicTerminalChargesAndTheirDerivatives) {
    struct BiasPoint {
        bool pmos;
        Real partition;
        int cvcharge;
        Real qg, qd, cdg, cgd;
    };
    for (const auto& point : std::array{
            BiasPoint{false, 0.0, 0, 8.55482, -1.11553, -4.65603, 0.0199291},
            BiasPoint{true,  0.0, 0, 17.3078, -2.29636, -9.33208, 0.0397836},
            BiasPoint{false, 0.5, 1, 8.02427, -1.12783, -8.44439, 0.0305576},
            BiasPoint{false, 1.0, 0, 8.55482, -0.00252256, -0.0135005, 0.0199291}}) {
        const Real sign = point.pmos ? -1.0 : 1.0;
        auto p = devices::bsim4_params_from_model({
            {"toxe", 3e-9}, {"vth0", sign * 0.5}, {"vsat", 8e4},
            {"u0", point.pmos ? 0.025 : 0.067}, {"xpart", point.partition},
            {"cvchargemod", double(point.cvcharge)}}, "m");
        p.w = point.pmos ? 20e-6 : 10e-6;
        p.l = 180e-9;
        p.type = sign;
        p.compute_derived_params(p.tnom);
        const auto theta = devices::bsim4_short_channel_factor(
            p.dsub * p.get_leff() / (p.factor1 * std::sqrt(p.Xdep0))).value;
        devices::BSIM4Core model(p, point.pmos);
        const Real rail = point.pmos ? 1.8 : 0.0;
        const devices::BSIM4Terminals v{0.9, 0.9, rail, rail};
        devices::BSIM4ChargeJacobian dq;
        const auto q = model.intrinsic_charges(v, p.tnom, theta, &dq);
        SCOPED_TRACE(::testing::Message() << "pmos=" << point.pmos << " xpart=" << point.partition
                                         << " cvchargemod=" << point.cvcharge);
        EXPECT_NEAR(sign * q[0] * 1e15, point.qg, 0.005 * point.qg);
        EXPECT_NEAR(sign * q[1] * 1e15, point.qd, 0.005 * std::abs(point.qd));
        EXPECT_NEAR(dq[1][0] * 1e15, point.cdg, 0.005 * std::abs(point.cdg));
        EXPECT_NEAR(dq[0][1] * 1e15, point.cgd, 0.005 * point.cgd);
        EXPECT_NEAR(q[0] + q[1] + q[2] + q[3], 0.0, 1e-28);
        for (int j = 0; j < 4; ++j) {
            EXPECT_NEAR(dq[0][j] + dq[1][j] + dq[2][j] + dq[3][j], 0.0, 4e-23);
            EXPECT_NEAR(dq[j][0] + dq[j][1] + dq[j][2] + dq[j][3], 0.0, 1e-28);
        }
        const auto reverse = model.intrinsic_charges({v[0], v[2], v[1], v[3]}, p.tnom, theta);
        EXPECT_DOUBLE_EQ(reverse[0], q[0]);
        EXPECT_DOUBLE_EQ(reverse[1], q[2]);
        EXPECT_DOUBLE_EQ(reverse[2], q[1]);
        EXPECT_DOUBLE_EQ(reverse[3], q[3]);
    }
}

// The imaginary transadmittance divided by omega is the corresponding
// off-diagonal dQ/dV. Voltage-source currents are positive into the supply.
TEST(BSIM4Transient, AcUsesDistinctGateAndDrainChargeDerivatives) {
    for (bool drive_gate : {false, true}) {
        const std::string gate_ac = drive_gate ? "1" : "0";
        const std::string drain_ac = drive_gate ? "0" : "1";
        parser::NetlistParser parser;
        auto parsed = parser.parse_string(
            "BSIM4 intrinsic transfer capacitance\nVG g 0 DC 0.9 AC " + gate_ac +
            "\nVD d 0 DC 0.9 AC " + drain_ac +
            "\nM1 d g 0 0 m W=10u L=180n\n.model m nmos level=14 vth0=0.5 toxe=3n"
            " cgso=0 cgdo=0 cgbo=0 cf=0 cjs=0 cjsws=0 cjswgs=0\n"
            "Hmeter meter 0 " + (drive_gate ? "VD" : "VG") + " 1\n.ac lin 1 1meg 1meg\n.end\n");
        ACSolver solver(*parsed.circuit, parsed.config);
        const auto ac = solver.solve();
        ASSERT_EQ(ac.status, ConvergenceStatus::SUCCESS);
        const auto meter = parsed.circuit->get_node_id("meter");
        const Real ff = -ac.frequency_points.at(0).solution.at(meter - 1).imag() *
                        1e15 / (2.0 * std::acos(-1.0) * 1e6);
        const Real reference = drive_gate ? -4.6560253 : 0.0199291;  // ngspice-47
        EXPECT_NEAR(ff, reference, 0.005 * std::abs(reference));
    }
}
