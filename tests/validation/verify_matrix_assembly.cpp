// Verify matrix assembly and conversion process
// Check if duplicate COO entries are properly aggregated

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <iostream>
#include <iomanip>
#include <map>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

void test_matrix_aggregation(const std::string& name, bool with_rb) {
    std::cout << "\n=== Testing: " << name << " ===\n";

    // Build circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0, 0.0);
    circuit.add_voltage_source("Vin", "in", "0", 0.7, 0.01, 0.0);

    if (with_rb) {
        circuit.add_resistor("Rb", "in", "base", 1000.0);
        circuit.add_resistor("Rc", "vcc", "out", 1000.0);
        circuit.add_bjt_npn("Q1", "out", "base", "0", 1e-14, 100.0, 1.0);
    } else {
        circuit.add_resistor("Rc", "vcc", "out", 1000.0);
        circuit.add_bjt_npn("Q1", "out", "in", "0", 1e-14, 100.0, 1.0);
    }

    NodeId in_node = circuit.get_node_id("in");
    NodeId out_node = circuit.get_node_id("out");
    NodeId base_node = with_rb ? circuit.get_node_id("base") : in_node;

    // Run DC analysis
    SimulationConfig config;
    DCSolver dc_solver(circuit, config);
    DenseVector dc_solution;
    SolverStats stats;

    if (dc_solver.solve(dc_solution, stats) != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC analysis failed\n";
        return;
    }

    std::cout << "Node IDs: in=" << in_node << ", base=" << base_node << ", out=" << out_node << "\n";

    // Build complex AC system (COO format)
    std::size_t num_nodes = dc_solution.size();
    std::size_t num_branches = 2;

    ComplexMNASystem sys;
    sys.resize(num_nodes, num_branches);
    sys.zero();

    Real omega = 2.0 * M_PI * 1000.0;

    for (const auto& device : circuit.get_devices()) {
        device->stamp_ac(sys, omega, dc_solution);
    }

    std::cout << "Complex system (COO format): " << sys.Y.values.size() << " entries\n";

    // Aggregate COO entries manually (simulate to_csc behavior)
    std::map<std::pair<std::uint32_t, std::uint32_t>, Complex> aggregated;

    for (std::size_t k = 0; k < sys.Y.values.size(); ++k) {
        auto key = std::make_pair(sys.Y.rows[k], sys.Y.cols[k]);
        aggregated[key] += sys.Y.values[k];
    }

    std::cout << "After aggregation: " << aggregated.size() << " unique entries\n\n";

    // Check specific entry: Y[out-1, base-1]
    std::uint32_t out_idx = out_node - 1;
    std::uint32_t base_idx = base_node - 1;
    auto key = std::make_pair(out_idx, base_idx);

    std::cout << "Y[" << out_idx << "," << base_idx << "] (collector, base):\n";

    // Before aggregation (raw COO)
    std::cout << "  Before aggregation (raw COO entries):\n";
    for (std::size_t k = 0; k < sys.Y.values.size(); ++k) {
        if (sys.Y.rows[k] == out_idx && sys.Y.cols[k] == base_idx) {
            std::cout << "    Entry [" << k << "]: "
                      << std::scientific << std::setprecision(6)
                      << std::real(sys.Y.values[k]) << " + j" << std::imag(sys.Y.values[k]) << "\n";
        }
    }

    // After aggregation
    if (aggregated.find(key) != aggregated.end()) {
        Complex agg_val = aggregated[key];
        std::cout << "  After aggregation (sum):\n";
        std::cout << "    Total: "
                  << std::scientific << std::setprecision(6)
                  << std::real(agg_val) << " + j" << std::imag(agg_val) << "\n";
    } else {
        std::cout << "  After aggregation: (no entry)\n";
    }

    // Now test complex-to-real conversion (as done in ACSolver)
    std::cout << "\n  Complex-to-Real conversion (ACSolver method):\n";

    SparseMatrix real_matrix;
    real_matrix.n_rows = 2 * sys.Y.n_rows;
    real_matrix.n_cols = 2 * sys.Y.n_cols;

    std::uint32_t n = sys.Y.n_rows;

    // This is what ACSolver does (DIRECTLY from COO, no aggregation!)
    for (size_t k = 0; k < sys.Y.values.size(); ++k) {
        std::uint32_t row = sys.Y.rows[k];
        std::uint32_t col = sys.Y.cols[k];
        Complex value = sys.Y.values[k];

        Real re = std::real(value);
        Real im = std::imag(value);

        // Each complex entry creates 4 real entries
        real_matrix.add_entry(row, col, re);           // Re(Y) top-left
        real_matrix.add_entry(row, col + n, -im);      // -Im(Y) top-right
        real_matrix.add_entry(row + n, col, im);       // Im(Y) bottom-left
        real_matrix.add_entry(row + n, col + n, re);   // Re(Y) bottom-right
    }

    std::cout << "    Real matrix (COO): " << real_matrix.values.size() << " entries\n";

    // Check Y_real[out_idx, base_idx] entries (top-left block, real part)
    std::cout << "    Y_real[" << out_idx << "," << base_idx << "] (top-left block, Re(Y)):\n";
    for (std::size_t k = 0; k < real_matrix.values.size(); ++k) {
        if (real_matrix.rows[k] == out_idx && real_matrix.cols[k] == base_idx) {
            std::cout << "      Entry [" << k << "]: "
                      << std::scientific << std::setprecision(6)
                      << real_matrix.values[k] << "\n";
        }
    }

    // Aggregate real matrix to see final value
    std::map<std::pair<std::uint32_t, std::uint32_t>, Real> real_aggregated;
    for (std::size_t k = 0; k < real_matrix.values.size(); ++k) {
        auto rkey = std::make_pair(real_matrix.rows[k], real_matrix.cols[k]);
        real_aggregated[rkey] += real_matrix.values[k];
    }

    auto rkey = std::make_pair(out_idx, base_idx);
    if (real_aggregated.find(rkey) != real_aggregated.end()) {
        std::cout << "    After aggregation: "
                  << std::scientific << std::setprecision(6)
                  << real_aggregated[rkey] << "\n";
    }

    std::cout << "\n🔍 Analysis:\n";
    std::cout << "  - If ACSolver converts BEFORE aggregation → WRONG (duplicates stay)\n";
    std::cout << "  - If ACSolver converts AFTER aggregation → CORRECT (duplicates summed)\n";
    std::cout << "  - Current ACSolver: converts BEFORE aggregation (Line 230 in ac_solver.cpp)\n";
    std::cout << "  - This means duplicate complex entries become duplicate real entries!\n";
}

int main() {
    std::cout << "=== Matrix Assembly and Conversion Verification ===\n";
    std::cout << "Goal: Check if duplicate COO entries are aggregated BEFORE or AFTER\n";
    std::cout << "      complex-to-real conversion\n";

    test_matrix_aggregation("BJT without Rb (CORRECT phase)", false);
    test_matrix_aggregation("BJT with Rb (WRONG phase)", true);

    return 0;
}
