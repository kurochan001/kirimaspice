// Dump fully assembled matrix (after COO aggregation)
// Show complete matrix structure for debugging

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <fstream>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

void dump_assembled_matrix(const std::string& name, bool with_rb, const std::string& filename) {
    std::cout << "\n=== " << name << " ===\n";

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

    // Run DC analysis
    SimulationConfig config;
    DCSolver dc_solver(circuit, config);
    DenseVector dc_solution;
    SolverStats stats;

    if (dc_solver.solve(dc_solution, stats) != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC analysis failed\n";
        return;
    }

    // Build complex AC system
    std::size_t num_nodes = dc_solution.size();
    std::size_t num_branches = 2;

    ComplexMNASystem sys;
    sys.resize(num_nodes, num_branches);
    sys.zero();

    Real omega = 2.0 * M_PI * 1000.0;

    for (const auto& device : circuit.get_devices()) {
        device->stamp_ac(sys, omega, dc_solution);
    }

    // Aggregate complex matrix manually
    std::map<std::pair<std::uint32_t, std::uint32_t>, Complex> complex_agg;
    for (std::size_t k = 0; k < sys.Y.values.size(); ++k) {
        auto key = std::make_pair(sys.Y.rows[k], sys.Y.cols[k]);
        complex_agg[key] += sys.Y.values[k];
    }

    std::cout << "Complex matrix: " << sys.Y.values.size() << " raw entries, "
              << complex_agg.size() << " unique after aggregation\n";
    std::cout << "Matrix size: " << sys.Y.n_rows << " x " << sys.Y.n_cols << "\n";

    // Convert to real system (with aggregation)
    std::uint32_t n = sys.Y.n_rows;
    std::map<std::pair<std::uint32_t, std::uint32_t>, Real> real_agg;

    for (const auto& [key, value] : complex_agg) {
        std::uint32_t row = key.first;
        std::uint32_t col = key.second;
        Real re = std::real(value);
        Real im = std::imag(value);

        // Top-left block: Re(Y)
        real_agg[{row, col}] += re;
        // Top-right block: -Im(Y)
        real_agg[{row, col + n}] += -im;
        // Bottom-left block: Im(Y)
        real_agg[{row + n, col}] += im;
        // Bottom-right block: Re(Y)
        real_agg[{row + n, col + n}] += re;
    }

    // Aggregate RHS
    std::vector<Real> rhs_real(2 * n, 0.0);
    for (std::size_t i = 0; i < sys.I.size(); ++i) {
        rhs_real[i] = std::real(sys.I[i]);
        rhs_real[i + n] = std::imag(sys.I[i]);
    }

    std::cout << "Real matrix: " << real_agg.size() << " non-zero entries, size " << (2*n) << " x " << (2*n) << "\n";

    // Write to file
    std::ofstream out(filename);
    out << std::scientific << std::setprecision(10);

    out << "# Assembled Real Matrix (after aggregation)\n";
    out << "# Size: " << (2*n) << " x " << (2*n) << "\n";
    out << "# Non-zeros: " << real_agg.size() << "\n";
    out << "\n";

    out << "# Matrix entries (row col value):\n";
    for (const auto& [key, val] : real_agg) {
        if (std::abs(val) > 1e-15) {
            out << key.first << " " << key.second << " " << val << "\n";
        }
    }

    out << "\n# RHS vector:\n";
    for (std::size_t i = 0; i < rhs_real.size(); ++i) {
        if (std::abs(rhs_real[i]) > 1e-15) {
            out << i << " " << rhs_real[i] << "\n";
        }
    }

    out.close();
    std::cout << "Written to " << filename << "\n";

    // Print key entries for debugging
    NodeId in_node = circuit.get_node_id("in");
    NodeId out_node = circuit.get_node_id("out");
    NodeId base_node = with_rb ? circuit.get_node_id("base") : in_node;

    std::uint32_t in_idx = in_node - 1;
    std::uint32_t base_idx = base_node - 1;
    std::uint32_t out_idx = out_node - 1;

    std::cout << "\nNode indices: in=" << in_idx << ", base=" << base_idx << ", out=" << out_idx << "\n";

    std::cout << "\nKey entries (top-left block, real part):\n";
    std::cout << "  Y_real[" << out_idx << "," << base_idx << "] = ";
    auto key1 = std::make_pair(out_idx, base_idx);
    if (real_agg.find(key1) != real_agg.end()) {
        std::cout << real_agg[key1] << "\n";
    } else {
        std::cout << "0 (no entry)\n";
    }

    if (with_rb && in_idx != base_idx) {
        std::cout << "  Y_real[" << base_idx << "," << in_idx << "] = ";
        auto key2 = std::make_pair(base_idx, in_idx);
        if (real_agg.find(key2) != real_agg.end()) {
            std::cout << real_agg[key2] << "\n";
        } else {
            std::cout << "0 (no entry)\n";
        }

        std::cout << "  Y_real[" << base_idx << "," << base_idx << "] = ";
        auto key3 = std::make_pair(base_idx, base_idx);
        if (real_agg.find(key3) != real_agg.end()) {
            std::cout << real_agg[key3] << "\n";
        } else {
            std::cout << "0 (no entry)\n";
        }
    }
}

int main() {
    std::cout << "=== Dumping Fully Assembled Matrices ===\n";
    std::cout << "Goal: Check complete matrix structure after aggregation\n";

    dump_assembled_matrix("BJT without Rb (CORRECT phase)", false, "matrix_no_rb.txt");
    dump_assembled_matrix("BJT with Rb (WRONG phase)", true, "matrix_with_rb.txt");

    std::cout << "\n✅ Matrix dumps complete. Compare matrix_no_rb.txt and matrix_with_rb.txt\n";

    return 0;
}
