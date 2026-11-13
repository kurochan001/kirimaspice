#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using kirimaspice::solvers::TransientSolver;

// Simple sanity check: coupled inductors should exchange energy without blow-up
TEST(MutualInductanceTransient, PulseEnergyStable) {
    CircuitGraph circuit;
    // Nodes: 0=GND, 1=Vin, 2=mid, 3=out
    circuit.add_node("1");
    circuit.add_node("2");
    circuit.add_node("3");

    // Elements: Vpulse(1,0), L1(1,2)=1uH, L2(3,2)=1uH, K1 L1 L2 k=0.99, Rload(3,0)=1ohm
    circuit.add_voltage_source("V1", "1", "0", 1.0);
    circuit.add_inductor("L1", "1", "2", 1e-6);
    circuit.add_inductor("L2", "3", "2", 1e-6);
    circuit.add_mutual_inductance("K1", "L1", "L2", 0.99);
    circuit.add_resistor("R1", "3", "0", 1.0);

    // Resolve mutual inductance references after all inductors are added
    circuit.resolve_mutual_inductance_references();

    // Excite with constant 1 V step from t=0 (good enough to detect instability)
    SimulationConfig cfg{};
    cfg.transient.start_time = 0.0;
    cfg.transient.end_time = 2e-6;
    cfg.transient.initial_step = 1e-9;
    cfg.transient.max_step = 1e-8;
    cfg.transient.method = IntegrationMethod::GEAR2;

    // Use fixed steps for determinism
    cfg.transient.use_adaptive_timestep = false;

    TransientSolver solver(circuit, cfg);
    auto result = solver.solve(0.0, cfg.transient.end_time, cfg.transient.initial_step);

    ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
    // Check that inductor currents remain finite and bounded
    double max_i = 0.0;
    for (const auto& tp : result.timepoints) {
        if (tp.branch_currents.empty()) continue;
        for (double i : tp.branch_currents) {
            max_i = std::max(max_i, std::abs(i));
        }
    }
    EXPECT_LT(max_i, 50.0); // arbitrary bound to detect blow-up
}
