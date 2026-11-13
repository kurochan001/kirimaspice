// Compare MNA matrices: BJT without Rb vs with Rb
// Goal: Identify where sign inversion occurs

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

void print_complex_matrix(const ComplexSparseMatrix& Y, const ComplexDenseVector& I,
                         std::size_t system_size, const std::string& filename) {
    std::ofstream out(filename);
    out << std::fixed << std::setprecision(6);

    out << "=== Complex MNA System ===\n";
    out << "System size: " << system_size << " x " << system_size << "\n\n";

    // Print Y matrix (dense format for readability)
    out << "Y matrix (Admittance):\n";
    for (std::size_t i = 0; i < system_size; i++) {
        for (std::size_t j = 0; j < system_size; j++) {
            Complex y_ij(0, 0);
            // Find entry in sparse matrix
            for (std::size_t k = 0; k < Y.rows.size(); k++) {
                if (Y.rows[k] == i && Y.cols[k] == j) {
                    y_ij = Y.values[k];
                    break;
                }
            }
            if (std::abs(y_ij) > 1e-12) {
                out << "Y[" << i << "," << j << "] = "
                    << std::real(y_ij) << " + j" << std::imag(y_ij) << "\n";
            }
        }
    }

    // Print I vector (RHS)
    out << "\nI vector (RHS):\n";
    for (std::size_t i = 0; i < I.size(); i++) {
        if (std::abs(I[i]) > 1e-12) {
            out << "I[" << i << "] = "
                << std::real(I[i]) << " + j" << std::imag(I[i]) << "\n";
        }
    }

    out.close();
    std::cout << "Matrix written to " << filename << "\n";
}

void test_circuit(const std::string& name, bool with_rb) {
    std::cout << "\n=== Testing: " << name << " ===\n";

    circuit::CircuitGraph circuit;

    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0, 0.0);
    circuit.add_voltage_source("Vin", "in", "0", 0.7, 0.01, 0.0);

    if (with_rb) {
        // With base resistor
        circuit.add_resistor("Rb", "in", "base", 1000.0);
        circuit.add_resistor("Rc", "vcc", "out", 1000.0);
        circuit.add_bjt_npn("Q1", "out", "base", "0", 1e-14, 100.0, 1.0);
    } else {
        // Without base resistor (direct connection)
        circuit.add_resistor("Rc", "vcc", "out", 1000.0);
        circuit.add_bjt_npn("Q1", "out", "in", "0", 1e-14, 100.0, 1.0);
    }

    // Get node IDs
    NodeId in_node = circuit.get_node_id("in");
    NodeId out_node = circuit.get_node_id("out");
    NodeId base_node = with_rb ? circuit.get_node_id("base") : in_node;

    std::cout << "Node IDs: in=" << in_node << ", out=" << out_node;
    if (with_rb) {
        std::cout << ", base=" << base_node;
    }
    std::cout << "\n";

    // Run DC analysis to get operating point
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1000.0;
    config.ac.num_points = 1;

    DCSolver dc_solver(circuit, config);
    DenseVector dc_solution;
    SolverStats stats;

    auto dc_status = dc_solver.solve(dc_solution, stats);
    if (dc_status != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC analysis failed\n";
        return;
    }

    std::cout << "DC solution:\n";
    std::cout << "  V(in) = " << dc_solution[in_node - 1] << " V\n";
    if (with_rb) {
        std::cout << "  V(base) = " << dc_solution[base_node - 1] << " V\n";
    }
    std::cout << "  V(out) = " << dc_solution[out_node - 1] << " V\n";

    // Build AC system
    std::size_t num_nodes = dc_solution.size();
    std::size_t num_branches = 2;  // Vcc + Vin
    std::size_t system_size = num_nodes + num_branches;

    ComplexMNASystem sys;
    sys.num_nodes = num_nodes;
    sys.num_branches = num_branches;
    sys.Y.rows.reserve(system_size * 10);
    sys.Y.cols.reserve(system_size * 10);
    sys.Y.values.reserve(system_size * 10);
    sys.I.resize(system_size, Complex(0, 0));

    // Stamp all devices
    Real omega = 2.0 * M_PI * 1000.0;  // 1 kHz
    for (const auto& device : circuit.get_devices()) {
        device->stamp_ac(sys, omega, dc_solution);
    }

    // Dump matrix
    std::string filename = with_rb ? "bjt_with_rb_matrix.txt" : "bjt_without_rb_matrix.txt";
    print_complex_matrix(sys.Y, sys.I, system_size, filename);

    // Print key matrix entries for BJT transconductance
    std::cout << "\nNode to matrix index mapping:\n";
    std::cout << "  base_node = " << base_node << " → matrix idx = " << (base_node - 1) << "\n";
    std::cout << "  out_node = " << out_node << " → matrix idx = " << (out_node - 1) << "\n";

    std::cout << "\nAll BJT-related matrix entries:\n";

    std::size_t base_idx = base_node - 1;
    std::size_t out_idx = out_node - 1;

    // Print ALL entries involving base or out nodes
    for (std::size_t k = 0; k < sys.Y.rows.size(); k++) {
        std::size_t i = sys.Y.rows[k];
        std::size_t j = sys.Y.cols[k];

        if (i == base_idx || i == out_idx || j == base_idx || j == out_idx) {
            Complex y = sys.Y.values[k];
            if (std::abs(y) > 1e-12) {
                std::cout << "  Y[" << i << "," << j << "] = "
                         << std::real(y) << " + j" << std::imag(y);

                if (i == out_idx && j == base_idx) {
                    std::cout << "  ← KEY: gm (out→base)";
                } else if (i == base_idx && j == out_idx) {
                    std::cout << "  ← KEY: gm (base→out)";
                }
                std::cout << "\n";
            }
        }
    }
}

int main() {
    std::cout << "=== Comparing BJT Circuits: With and Without Base Resistor ===\n";
    std::cout << "Goal: Identify where sign inversion occurs\n";

    test_circuit("BJT without Rb (CORRECT phase)", false);
    test_circuit("BJT with Rb=1k (WRONG phase)", true);

    std::cout << "\n=== Analysis Complete ===\n";
    std::cout << "Compare bjt_without_rb_matrix.txt and bjt_with_rb_matrix.txt\n";
    std::cout << "Look for sign differences in transconductance entries\n";

    return 0;
}
