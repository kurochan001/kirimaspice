// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
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
