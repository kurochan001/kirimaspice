// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Solve AC system and dump complete solution vector
// Check if KLU returns correct solution

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/klu_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <iostream>
#include <iomanip>
#include <map>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

void solve_and_dump(const std::string& name, bool with_rb) {
    std::cout << "\n=== " << name << " ===\n";

    // Build circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0, 0.0);
    // Use 0.7V DC (test_bjt_with_rb_phase uses this)
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

    // Build complex AC system
    std::uint32_t num_nodes = circuit.num_nodes() - 1; // Exclude GND
    std::uint32_t num_branches = 2; // Vcc and Vin

    ComplexMNASystem sys;
    sys.resize(num_nodes, num_branches);
    sys.zero();

    Real omega = 2.0 * M_PI * 1000.0;

    for (const auto& device : circuit.get_devices()) {
        device->stamp_ac(sys, omega, dc_solution);
    }

    // Convert to real system (DIRECTLY from COO, as ACSolver does)
    std::uint32_t n = sys.Y.n_rows;
    SparseMatrix real_matrix;
    real_matrix.n_rows = 2 * n;
    real_matrix.n_cols = 2 * n;

    for (std::size_t k = 0; k < sys.Y.values.size(); ++k) {
        std::uint32_t row = sys.Y.rows[k];
        std::uint32_t col = sys.Y.cols[k];
        Complex value = sys.Y.values[k];

        Real re = std::real(value);
        Real im = std::imag(value);

        // Same as ACSolver::convert_to_real_system
        real_matrix.add_entry(row, col, re);
        real_matrix.add_entry(row, col + n, -im);
        real_matrix.add_entry(row + n, col, im);
        real_matrix.add_entry(row + n, col + n, re);
    }

    // Build RHS
    DenseVector rhs(2 * n, 0.0);
    for (std::size_t i = 0; i < sys.I.size(); ++i) {
        rhs[i] = std::real(sys.I[i]);
        rhs[i + n] = std::imag(sys.I[i]);
    }

    std::cout << "System size: " << (2*n) << " x " << (2*n) << "\n";
    std::cout << "Complex I size: " << sys.I.size() << "\n";
    std::cout << "Non-zero entries: " << real_matrix.values.size() << "\n";

    std::cout << "\nComplex RHS (I vector):\n";
    for (std::size_t i = 0; i < sys.I.size(); ++i) {
        if (std::abs(sys.I[i]) > 1e-15) {
            std::cout << "  I[" << i << "] = "
                      << std::scientific << std::setprecision(6)
                      << std::real(sys.I[i]) << " + j" << std::imag(sys.I[i]) << "\n";
        }
    }

    std::cout << "\nReal RHS (b vector):\n";
    for (std::size_t i = 0; i < rhs.size(); ++i) {
        if (std::abs(rhs[i]) > 1e-15) {
            std::cout << "  b[" << i << "] = "
                      << std::scientific << std::setprecision(6)
                      << rhs[i];
            if (i < n) {
                std::cout << " (real part)";
            } else {
                std::cout << " (imag part)";
            }
            std::cout << "\n";
        }
    }

    // Aggregate and print key matrix entries
    std::uint32_t in_idx = in_node - 1;
    std::uint32_t base_idx = base_node - 1;
    std::uint32_t out_idx = out_node - 1;

    std::cout << "\nNode indices: in=" << in_idx << ", base=" << base_idx << ", out=" << out_idx << "\n";

    // Aggregate matrix entries
    std::map<std::pair<std::uint32_t, std::uint32_t>, Real> matrix_agg;
    for (std::size_t k = 0; k < real_matrix.values.size(); ++k) {
        auto key = std::make_pair(real_matrix.rows[k], real_matrix.cols[k]);
        matrix_agg[key] += real_matrix.values[k];
    }

    std::cout << "\nKey matrix entries (top-left block, Re(Y)):\n";

    // Print entire out row
    std::cout << "  Row " << out_idx << " (out node):\n";
    for (const auto& [key, val] : matrix_agg) {
        if (key.first == out_idx && key.second < n) {  // Only real part block
            std::cout << "    Y_real[" << out_idx << "," << key.second << "] = "
                      << std::scientific << std::setprecision(6) << val;
            if (key.second == base_idx) std::cout << " (base)";
            else if (key.second == in_idx) std::cout << " (in)";
            else if (key.second == out_idx) std::cout << " (out diagonal)";
            std::cout << "\n";
        }
    }

    auto key_out_base = std::make_pair(out_idx, base_idx);
    if (matrix_agg.find(key_out_base) != matrix_agg.end()) {
        std::cout << "  Specifically: Y_real[" << out_idx << "," << base_idx << "] = "
                  << std::scientific << std::setprecision(6)
                  << matrix_agg[key_out_base] << " (out←base, should be -gm)\n";
    } else {
        std::cout << "  Specifically: Y_real[" << out_idx << "," << base_idx << "] = 0 (no entry)\n";
    }

    if (with_rb && in_idx != base_idx) {
        std::cout << "\n  Row " << base_idx << " (base node):\n";
        for (const auto& [key, val] : matrix_agg) {
            if (key.first == base_idx && key.second < n) {
                std::cout << "    Y_real[" << base_idx << "," << key.second << "] = "
                          << std::scientific << std::setprecision(6) << val;
                if (key.second == base_idx) std::cout << " (base diagonal)";
                else if (key.second == in_idx) std::cout << " (in)";
                else if (key.second == out_idx) std::cout << " (out)";
                std::cout << "\n";
            }
        }
    }

    // Solve using KLU
    KLUSolver klu;
    if (!klu.analyze_and_factorize(real_matrix)) {
        std::cerr << "KLU factorization failed: " << klu.last_error() << "\n";
        std::cerr << "Matrix dimensions: " << real_matrix.n_rows << " x " << real_matrix.n_cols << "\n";
        std::cerr << "Number of COO entries: " << real_matrix.values.size() << "\n";
        return;
    }

    DenseVector solution;
    if (!klu.solve(rhs, solution)) {
        std::cerr << "KLU solve failed: " << klu.last_error() << "\n";
        return;
    }

    std::cout << "\nSolution vector (first " << n << " = real part, next " << n << " = imag part):\n";
    std::cout << std::scientific << std::setprecision(6);

    for (std::size_t i = 0; i < solution.size(); ++i) {
        if (std::abs(solution[i]) > 1e-10) {
            std::cout << "  V[" << i << "] = " << solution[i];
            if (i < n) {
                std::cout << " (node " << i << ", real)";
            } else {
                std::cout << " (node " << (i - n) << ", imag)";
            }
            std::cout << "\n";
        }
    }

    // Convert to complex
    std::cout << "\nComplex voltages:\n";

    Complex V_in(solution[in_idx], solution[in_idx + n]);
    Complex V_base(solution[base_idx], solution[base_idx + n]);
    Complex V_out(solution[out_idx], solution[out_idx + n]);

    std::cout << "  V(in)   [node " << in_node << ", idx " << in_idx << "] = "
              << std::real(V_in) << " + j" << std::imag(V_in) << "\n";
    if (with_rb && base_node != in_node) {
        std::cout << "  V(base) [node " << base_node << ", idx " << base_idx << "] = "
                  << std::real(V_base) << " + j" << std::imag(V_base) << "\n";
    }
    std::cout << "  V(out)  [node " << out_node << ", idx " << out_idx << "] = "
              << std::real(V_out) << " + j" << std::imag(V_out) << "\n";

    Real phase_out = std::arg(V_out) * 180.0 / M_PI;
    std::cout << "\nPhase of V(out): " << phase_out << "°\n";

    if (std::real(V_out) > 0) {
        std::cout << "⚠️  V(out) is POSITIVE (0° phase) - WRONG!\n";
    } else {
        std::cout << "✓  V(out) is NEGATIVE (180° phase) - CORRECT!\n";
    }
}

int main() {
    std::cout << "=== Solving AC System and Dumping Solution ===\n";

    solve_and_dump("BJT without Rb (CORRECT phase)", false);
    solve_and_dump("BJT with Rb (WRONG phase)", true);

    return 0;
}
