// Validation test: Compare kirimaspice MOSFET results with ngspice
// Test multiple operating points

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::circuit;
using namespace kirimaspice::solvers;

int main() {
    std::cout << "=== MOSFET ngspice Comparison Test ===" << std::endl;
    std::cout << std::endl;

    // Test points: VGS values from 0V to 3V
    const double vgs_values[] = {0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0};

    // ngspice reference values (v(d) in volts)
    const double ngspice_vd[] = {5.000, 5.000, 4.910, 4.360, 3.310, 1.760, 1.115};

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "VGS(V)  v(d)_ngspice  v(d)_kirimaspice  Error(%)  Status" << std::endl;
    std::cout << "------  ------------  -----------  --------  ------" << std::endl;

    int passed = 0;
    int total = sizeof(vgs_values) / sizeof(vgs_values[0]);

    for (int i = 0; i < total; i++) {
        double vgs = vgs_values[i];
        double vd_ref = ngspice_vd[i];

        // Build circuit
        CircuitGraph circuit;
        circuit.add_voltage_source("VDD", "vdd", "0", 5.0);
        circuit.add_voltage_source("VGS", "vgs", "0", vgs);
        circuit.add_resistor("RD", "vdd", "d", 10000.0);  // 10kΩ
        circuit.add_mosfet_n("M1", "d", "vgs", "0", 2e-5, 10e-6, 1e-6, 0.7);

        // Solve DC operating point
        DCSolver solver(circuit);
        DenseVector solution;
        SolverStats stats;
        ConvergenceStatus status = solver.solve(solution, stats);

        if (status != ConvergenceStatus::SUCCESS) {
            std::cout << vgs << "   " << vd_ref << "       FAILED    ";
            std::cout << "Convergence failed" << std::endl;
            continue;
        }

        // Get drain voltage
        NodeId drain_id = circuit.get_node_id("d");
        double vd_kirimaspice = solution[drain_id - 1];

        // Calculate error
        double error_pct = std::abs((vd_kirimaspice - vd_ref) / vd_ref * 100.0);

        // Check tolerance (1% error)
        bool pass = error_pct < 1.0;
        if (pass) passed++;

        std::cout << vgs << "       " << vd_ref << "       "
                  << vd_kirimaspice << "       " << error_pct << "%   "
                  << (pass ? "PASS" : "FAIL") << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== Summary ===" << std::endl;
    std::cout << "Passed: " << passed << "/" << total << std::endl;
    std::cout << "Success rate: " << (passed * 100.0 / total) << "%" << std::endl;

    return (passed == total) ? 0 : 1;
}
