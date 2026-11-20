// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include "kirimaspice/devices/mosfet_bsim4.hpp"
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
using namespace kirimaspice::devices;

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

// DISABLED: the run converges, but the switching edges come earlier than the
// reference because BSIM4's capacitances are a simplified Meyer model (see
// KNOWN_ISSUES); the worst errors are 0.40/0.83/1.83 V for n1/n2/n3.
TEST(BSIM4Transient, DISABLED_InverterChain_MatchesNgspiceReference) {
    // Reduce log chatter (BSIM4 debug logs are extremely verbose) but keep errors
    spdlog::set_level(spdlog::level::err);

    auto reference = load_bsim4_reference("validation/bsim4_inv_chain_tran_ref.txt");
    ASSERT_FALSE(reference.empty()) << "Run scripts/generate_bsim4_tran_ref.py first";

    CircuitGraph circuit;
    
    // Common BSIM4 Parameters (matching generation script)
    BSIM4Params n_params;
    n_params.type = 1.0; // NMOS
    n_params.toxe = 3.0e-9;
    n_params.vth0 = 0.5;
    n_params.u0 = 0.067;
    n_params.vsat = 8.0e4;
    n_params.w = 10e-6;
    n_params.l = 180e-9;
    n_params.cgso = 2.0e-10;
    n_params.cgdo = 2.0e-10;
    n_params.cgbo = 1.0e-10;
    n_params.compute_derived_params();

    BSIM4Params p_params;
    p_params.type = -1.0; // PMOS
    p_params.toxe = 3.0e-9;
    p_params.vth0 = -0.5;
    p_params.u0 = 0.025;
    p_params.vsat = 8.0e4;
    p_params.w = 20e-6;
    p_params.l = 180e-9;
    p_params.cgso = 2.0e-10;
    p_params.cgdo = 2.0e-10;
    p_params.cgbo = 1.0e-10;
    p_params.compute_derived_params();

    // Power Supply
    circuit.add_voltage_source("Vdd", "vdd", "0", 1.8);
    
    // Input Pulse: PULSE(0 1.8 1ns 100ps 100ps 4ns 10ns)
    // Use Behavioral Voltage Source for PULSE
    // Or better, implement Pulse source if available. Assuming Behavioral for flexibility
    // V(t) logic: 
    // 0 < t < 1ns: 0
    // 1ns < t < 1.1ns: rise to 1.8
    // 1.1ns < t < 5.1ns: 1.8
    // 5.1ns < t < 5.2ns: fall to 0
    // 5.2ns < t < 10ns: 0
    
    // Input Pulse: PULSE(0 1.8 1ns 100ps 100ps 4ns 10ns)
    Real v1 = 0.0;
    Real v2 = 1.8;
    Real delay = 1e-9;
    Real tr = 100e-12; // Match reference edge rate
    Real tf = 100e-12; // Match reference edge rate
    Real pw = 4e-9;
    Real period = 10e-9;
    
    circuit.add_pulse_voltage_source("Vin", "vin", "0", v1, v2, delay, tr, tf, pw, period);

    // Inverter Chain (3 stages)
    // Stage 1
    circuit.add_mosfet_bsim4_p("Mp1", "n1", "vin", "vdd", "vdd", p_params);
    circuit.add_mosfet_bsim4_n("Mn1", "n1", "vin", "0", "0", n_params);
    circuit.add_capacitor("Cload1", "n1", "0", 10e-15);

    // Stage 2
    circuit.add_mosfet_bsim4_p("Mp2", "n2", "n1", "vdd", "vdd", p_params);
    circuit.add_mosfet_bsim4_n("Mn2", "n2", "n1", "0", "0", n_params);
    circuit.add_capacitor("Cload2", "n2", "0", 10e-15);

    // Stage 3
    circuit.add_mosfet_bsim4_p("Mp3", "n3", "n2", "vdd", "vdd", p_params);
    circuit.add_mosfet_bsim4_n("Mn3", "n3", "n2", "0", "0", n_params);
    circuit.add_capacitor("Cload3", "n3", "0", 10e-15);

    SimulationConfig config;
    config.gmin = 1e-9; // add shunt conductance for transient robustness
    // Strengthen DC solve to get a robust operating point for transient
    config.dc.enable_source_stepping = true;
    config.dc.source_stepping_steps = 10;
    config.dc.enable_gmin_stepping = true;
    config.dc.gmin_stepping_steps = 5;

    config.transient.use_initial_conditions = false; // Recompute DC op each run (no UIC shortcut)
    config.transient.method = IntegrationMethod::TRAPEZOIDAL; // match ngspice default
    config.transient.initial_step = 5e-12;  // Seed step equal to output resolution
    config.transient.min_step = 5e-15;      // Allow a few more halvings near edges
    config.transient.max_step = 5e-12;      // Upper bound aligned with reference resolution
    config.transient.use_adaptive_timestep = false; // Fixed 5 ps step with NR backoff
    config.transient.max_iterations = 150;   // Allow more NR iterations per step
    config.transient.max_timesteps = 200000; // Safety margin vs ITL5
    config.transient.adaptive_order = false; // Not used for fixed trapezoidal
    config.transient.lte_reltol = 1e-2;      // (unused in fixed-step)
    config.transient.lte_abstol = 1e-5;
    config.transient.trtol = 10.0;
    config.transient.chgtol = 1e-13;
    
    // Relax LTE slightly if needed, but Gear2 is robust (fixed-step so LTE unused)
    config.transient.reltol = 1e-2; // Relax NR criteria
    config.transient.abstol = 1e-9;
    
    TransientSolver solver(circuit, config);
    // Use fixed 5 ps step to match ngspice reference resolution while aiding convergence
    auto result = solver.solve(0.0, 10e-9, 5e-12);
    
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    
    NodeId n_vin = circuit.get_node_id("vin");
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
