#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

namespace {

struct ReferencePoint {
    Real time;
    Real voltage;
};

std::vector<ReferencePoint> load_reference(const std::string& basename) {
    std::vector<std::string> candidates = {
        basename,
        "../" + basename,
        "../../" + basename
    };
    std::vector<ReferencePoint> data;
    std::ifstream file;
    for (const auto& candidate : candidates) {
        file.close();
        file.clear();
        file.open(candidate);
        if (file.is_open()) {
            break;
        }
    }
    if (!file.is_open()) {
        return data;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        ReferencePoint pt{};
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
        [](const ReferencePoint& pt, Real value) { return pt.time < value; });
    auto lower = upper - 1;
    Real fraction = (t - lower->time) / (upper->time - lower->time);
    return lower->voltage + fraction * (upper->voltage - lower->voltage);
}

std::pair<TransientResult, NodeId> run_solver(IntegrationMethod method) {
    CircuitGraph circuit;
    circuit.add_voltage_source("VIN", "vin", "0", 5.0);
    circuit.add_resistor("R1", "vin", "n1", 1.0);
    circuit.add_inductor("L1", "n1", "out", 1e-3);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = false;
    config.transient.method = method;
    config.transient.adaptive_order = false;
    config.transient.initial_step = 1e-7;
    config.transient.min_step = 1e-7;
    config.transient.max_step = 1e-7;

    TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, 1e-3, 100e-9);
    EXPECT_EQ(result.status, ConvergenceStatus::SUCCESS);
    EXPECT_GT(result.timepoints.size(), 10u);
    NodeId node_out = circuit.get_node_id("out");
    return {result, node_out};
}

} // namespace

TEST(TransientHighQ, FinalAmplitudeAndOvershootComparison) {
    auto [result_bdf2, node_out] = run_solver(IntegrationMethod::GEAR2);
    auto [result_trap, node_out_verify] = run_solver(IntegrationMethod::TRAPEZOIDAL);
    ASSERT_EQ(node_out, node_out_verify);

    Real final_bdf2 = result_bdf2.timepoints.back().node_voltages[node_out - 1];
    Real final_trap = result_trap.timepoints.back().node_voltages[node_out - 1];

    Real max_bdf2 = final_bdf2;
    Real max_trap = final_trap;
    for (const auto& tp : result_bdf2.timepoints) {
        max_bdf2 = std::max(max_bdf2, tp.node_voltages[node_out - 1]);
    }
    for (const auto& tp : result_trap.timepoints) {
        max_trap = std::max(max_trap, tp.node_voltages[node_out - 1]);
    }

    auto trap_ref = load_reference("validation/rlc_resonance_ngspice_ref.txt");
    ASSERT_FALSE(trap_ref.empty()) << "missing validation/rlc_resonance_ngspice_ref.txt";
    auto bdf2_ref = load_reference("validation/rlc_resonance_bdf2_ngspice_ref.txt");
    ASSERT_FALSE(bdf2_ref.empty()) << "missing validation/rlc_resonance_bdf2_ngspice_ref.txt";

    Real ref_trap_final = interpolate_reference(trap_ref, 1e-3);
    Real ref_trap_peak = interpolate_reference(trap_ref, 3e-4);
    Real ref_bdf2_final = interpolate_reference(bdf2_ref, 1e-3);
    Real ref_bdf2_peak = interpolate_reference(bdf2_ref, 3e-4);

    std::cout << "Trap final=" << final_trap << " (ref=" << ref_trap_final << ")" << std::endl;
    std::cout << "Trap peak=" << max_trap << " (ref=" << ref_trap_peak << ")" << std::endl;
    std::cout << "BDF2 final=" << final_bdf2 << " (ref=" << ref_bdf2_final << ")" << std::endl;
    std::cout << "BDF2 peak=" << max_bdf2 << " (ref=" << ref_bdf2_peak << ")" << std::endl;

    EXPECT_NEAR(final_bdf2, ref_bdf2_final, 1e-3);
    EXPECT_NEAR(max_bdf2, ref_bdf2_peak, 1e-1);
    EXPECT_NEAR(final_trap, ref_trap_final, 1e-2);
    EXPECT_NEAR(max_trap, ref_trap_peak, 1e-1);
}
