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

struct RlcCase {
    Real resistance;
    Real timestep;
    const char* reference_path;
    const char* description;
};

const RlcCase kCases[] = {
    {1.0, 1e-7, "validation/rlc_resonance_bdf2_ngspice_ref.txt", "High Q (R=1Ω)"},
    {5.0, 1e-7, "validation/rlc_resonance_R5_bdf2_ngspice_ref.txt", "Moderate Q (R=5Ω)"},
    {20.0, 5e-7, "validation/rlc_resonance_R20_bdf2_ngspice_ref.txt", "Low Q (R=20Ω)"},
};

} // namespace

class RLCBdf2QSweep : public ::testing::TestWithParam<RlcCase> {};

TEST_P(RLCBdf2QSweep, FixedStepMatchesNgspiceWithinTolerance) {
    const auto& param = GetParam();
    auto reference = load_reference(param.reference_path);
    ASSERT_FALSE(reference.empty()) << "missing " << param.reference_path;

    CircuitGraph circuit;
    circuit.add_voltage_source("VIN", "vin", "0", 5.0);
    circuit.add_resistor("R1", "vin", "n1", param.resistance);
    circuit.add_inductor("L1", "n1", "out", 1e-3);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = false;
    config.transient.method = IntegrationMethod::GEAR2;
    config.transient.adaptive_order = false;
    config.transient.initial_step = param.timestep;
    config.transient.min_step = param.timestep;
    config.transient.max_step = param.timestep;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 1e-3, param.timestep);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS) << "case: " << param.description;
    ASSERT_GT(result.timepoints.size(), 10u);

    NodeId node_out = circuit.get_node_id("out");
    Real max_error = 0.0;
    Real final_error = 0.0;

    for (const auto& tp : result.timepoints) {
        Real v_sim = tp.node_voltages[node_out - 1];
        Real v_ref = interpolate_reference(reference, tp.time);
        Real err = std::abs(v_sim - v_ref);
        max_error = std::max(max_error, err);
        if (tp.time >= 1e-3) {
            final_error = err;
        }
    }

    std::cout << "[Gear2 sweep] " << param.description
              << " steps=" << result.timepoints.size()
              << " max_err=" << max_error
              << " final_err=" << final_error << std::endl;

    EXPECT_LT(max_error, 1e-3) << "max_abs_error=" << max_error << " case=" << param.description;
    EXPECT_LT(final_error, 1e-3) << "final_error=" << final_error << " case=" << param.description;
}

INSTANTIATE_TEST_SUITE_P(Bdf2RSweep,
                         RLCBdf2QSweep,
                         ::testing::ValuesIn(kCases));
