// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
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
    Real voltage;
};

std::vector<ReferencePoint> load_reference(const std::string& basename) {
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
        if (iss >> pt.time >> pt.voltage) {
            data.push_back(pt);
        }
    }
    return data;
}

Real interpolate_reference(const std::vector<ReferencePoint>& ref, Real t) {
    if (ref.empty()) return 0.0;
    if (t <= ref.front().time) return ref.front().voltage;
    if (t >= ref.back().time) return ref.back().voltage;
    auto upper = std::lower_bound(ref.begin(), ref.end(), t,
        [](const ReferencePoint& pt, Real val) { return pt.time < val; });
    auto lower = upper - 1;
    Real fraction = (t - lower->time) / (upper->time - lower->time);
    return lower->voltage + fraction * (upper->voltage - lower->voltage);
}

} // namespace

TEST(TransientHighQ, AdaptiveBdf3MatchesNgspiceReference) {
    auto reference = load_reference("validation/rlc_resonance_bdf3_ngspice_ref.txt");
    ASSERT_FALSE(reference.empty()) << "missing validation/rlc_resonance_bdf3_ngspice_ref.txt";

    CircuitGraph circuit;
    circuit.add_voltage_source("VIN", "vin", "0", 5.0);
    circuit.add_resistor("R1", "vin", "n1", 1.0);
    circuit.add_inductor("L1", "n1", "out", 1e-3);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR3;
    config.transient.adaptive_order = false;
    config.transient.initial_step = 1e-7;
    config.transient.min_step = 5e-9;
    config.transient.max_step = 5e-6;
    config.transient.abstol = 1e-8;
    config.transient.reltol = 1e-5;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 1e-3, 100e-9);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 50u);

    NodeId node_out = circuit.get_node_id("out");
    Real max_error = 0.0;
    Real final_error = 0.0;
    Real peak_error = 0.0;

    for (const auto& tp : result.timepoints) {
        Real v_sim = tp.node_voltages[node_out - 1];
        Real v_ref = interpolate_reference(reference, tp.time);
        Real err = std::abs(v_sim - v_ref);
        max_error = std::max(max_error, err);
        if (tp.time >= 1e-3) {
            final_error = err;
        }
        if (tp.time <= 4e-4) {
            peak_error = std::max(peak_error, err);
        }
    }

    std::cout << "[Adaptive Gear3] steps=" << result.timepoints.size()
              << " rejects=" << result.stats.timestep_rejects
              << " max_err=" << max_error
              << " peak_err=" << peak_error
              << " final_err=" << final_error << std::endl;

    EXPECT_LT(max_error, 5e-3);
    EXPECT_LT(final_error, 5e-3);
    EXPECT_LT(peak_error, 5e-3);
    EXPECT_LE(result.stats.timestep_rejects, 5u);
}

TEST(TransientHighQ, AdaptiveBdf3StrictToleranceMatchesNgspiceReference) {
    auto reference = load_reference("validation/rlc_resonance_bdf3_ngspice_ref.txt");
    ASSERT_FALSE(reference.empty()) << "missing validation/rlc_resonance_bdf3_ngspice_ref.txt";

    CircuitGraph circuit;
    circuit.add_voltage_source("VIN", "vin", "0", 5.0);
    circuit.add_resistor("R1", "vin", "n1", 1.0);
    circuit.add_inductor("L1", "n1", "out", 1e-3);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::GEAR3;
    config.transient.adaptive_order = false;
    config.transient.initial_step = 5e-8;
    config.transient.min_step = 1e-9;
    config.transient.max_step = 1e-6;
    config.transient.abstol = 5e-9;
    config.transient.reltol = 5e-6;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 1e-3, 100e-9);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    ASSERT_GT(result.timepoints.size(), 200u); // 厳しい設定ではステップ数が増加する

    NodeId node_out = circuit.get_node_id("out");
    Real max_error = 0.0;
    Real final_error = 0.0;
    Real peak_error = 0.0;

    for (const auto& tp : result.timepoints) {
        Real v_sim = tp.node_voltages[node_out - 1];
        Real v_ref = interpolate_reference(reference, tp.time);
        Real err = std::abs(v_sim - v_ref);
        max_error = std::max(max_error, err);
        if (tp.time >= 1e-3) {
            final_error = err;
        }
        if (tp.time <= 4e-4) {
            peak_error = std::max(peak_error, err);
        }
    }

    std::cout << "[Adaptive Gear3 strict] steps=" << result.timepoints.size()
              << " rejects=" << result.stats.timestep_rejects
              << " max_err=" << max_error
              << " peak_err=" << peak_error
              << " final_err=" << final_error << std::endl;

    EXPECT_LT(max_error, 2.5e-3);
    EXPECT_LT(final_error, 2.5e-3);
    EXPECT_LT(peak_error, 2.5e-3);
    EXPECT_LE(result.stats.timestep_rejects, 5u);
}
