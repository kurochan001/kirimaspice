// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A stamp linearizes a device at the present iterate; it does not change the
// device's history, which accept_solution advances once per accepted time
// point (device.hpp). Newton's convergence test stamps the devices again at
// the new solution (kirchhoff_current_law_holds), and a stamp that moved the
// history would make that test, and the result, depend on the number of
// iterations. The lossy line did so with its loss filter.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;

namespace {

// The entries of a stamp, summed per position, and its right-hand side
std::pair<std::vector<std::tuple<std::uint32_t, std::uint32_t, Real>>, DenseVector> stamp_once(
    const devices::Device& device, const DenseVector& x, std::uint32_t nodes,
    std::uint32_t branches, Real dt) {
    MNASystem mna;
    mna.resize(nodes, branches);
    mna.zero();
    device.stamp_transient(mna, x, dt, IntegrationMethod::TRAPEZOIDAL);
    std::vector<std::tuple<std::uint32_t, std::uint32_t, Real>> entries;
    for (std::size_t k = 0; k < mna.G.values.size(); ++k) {
        entries.emplace_back(mna.G.rows[k], mna.G.cols[k], mna.G.values[k]);
    }
    std::sort(entries.begin(), entries.end());
    return {entries, mna.b};
}

}  // namespace

TEST(StampsKeepHistory, TransientStampsDoNotChangeTheDevices) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "history\nVin 1 0 PULSE(0 1 0 1n 1n 5n 10n)\nO1 1 0 2 0 R=1 L=50n C=1p G=1e-6 TD=1e-9\n"
        "Rload 2 0 50\nT1 2 0 3 0 Z0=50 TD=2n\nR3 3 0 50\nC1 3 0 1p\nL1 3 4 1n\nR4 4 0 10\n"
        "D1 2 5 dm\nR5 5 0 1k\n.model dm D IS=1e-14\n.end\n");
    ASSERT_NE(parsed.circuit, nullptr);
    SimulationConfig config = parsed.config;
    config.transient.use_adaptive_timestep = false;
    solvers::TransientSolver solver(*parsed.circuit, config);
    const solvers::TransientResult result = solver.solve(0.0, 3e-9, 1e-10);
    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);

    // The last accepted point, as the stamps read it (node k at k - 1)
    const auto& last = result.timepoints.back();
    DenseVector x(last.node_voltages);
    x.insert(x.end(), last.branch_currents.begin(), last.branch_currents.end());
    const auto nodes = static_cast<std::uint32_t>(last.node_voltages.size());
    const auto branches = static_cast<std::uint32_t>(last.branch_currents.size());

    for (const auto& device : parsed.circuit->get_devices()) {
        const auto first = stamp_once(*device, x, nodes, branches, 1e-10);
        const auto second = stamp_once(*device, x, nodes, branches, 1e-10);
        EXPECT_EQ(first.first, second.first) << device->name();
        EXPECT_EQ(first.second, second.second) << device->name();
    }
}
