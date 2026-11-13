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

std::pair<TransientResult, NodeId> run_bdf2_solver() {
    CircuitGraph circuit;
    circuit.add_voltage_source("VIN", "vin", "0", 5.0);
    circuit.add_resistor("R1", "vin", "n1", 1.0);
    circuit.add_inductor("L1", "n1", "out", 1e-3);
    circuit.add_capacitor("C1", "out", "0", 10e-6);

    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = false;
    config.transient.method = IntegrationMethod::GEAR2;
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

TEST(TransientHighQ, KirimaspiceBdf2MatchesNgspiceBdf2Reference) {
    auto reference = load_reference("validation/rlc_resonance_bdf2_ngspice_ref.txt");
    ASSERT_FALSE(reference.empty()) << "missing validation/rlc_resonance_bdf2_ngspice_ref.txt";

    auto [result_bdf2, node_out] = run_bdf2_solver();
    std::cout << "node_out=" << node_out << std::endl;

    auto max_error = Real(0.0);
    Real final_error = 0.0;
    Real kirimaspice_peak = 0.0;
    Real ref_peak = 0.0;
    Real kirimaspice_final = result_bdf2.timepoints.back().node_voltages[node_out - 1];
    Real ref_final = interpolate_reference(reference, result_bdf2.timepoints.back().time);

    for (const auto& tp : result_bdf2.timepoints) {
        Real v_sim = tp.node_voltages[node_out - 1];
        Real v_ref = interpolate_reference(reference, tp.time);
        Real err = std::abs(v_sim - v_ref);
        max_error = std::max(max_error, err);
        kirimaspice_peak = std::max(kirimaspice_peak, v_sim);
        ref_peak = std::max(ref_peak, v_ref);
        if (tp.time >= 1e-3) {
            final_error = err;
        }
    }

    std::cout << "BDF2 vs ngspice Gear2: max_error=" << max_error
              << ", final_error=" << final_error
              << ", kirimaspice_peak=" << kirimaspice_peak
              << ", ref_peak=" << ref_peak
              << ", kirimaspice_final=" << kirimaspice_final
              << ", ref_final=" << ref_final
              << ", rejects=" << result_bdf2.stats.timestep_rejects << std::endl;

    EXPECT_LT(max_error, 1e-4) << "max_abs_error=" << max_error;
}
