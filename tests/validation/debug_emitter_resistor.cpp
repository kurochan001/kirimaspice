// Debug tool for emitter resistor matrix singularity issue
// Dump DC MNA matrix to investigate column 4 singularity

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/devices/device.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <set>

using namespace kirimaspice;

void print_matrix_structure(const circuit::CircuitGraph& circuit) {
    std::cout << "\n=== Circuit Structure ===\n";
    std::cout << "Number of devices: " << circuit.num_devices() << "\n";
    std::cout << "Number of nodes: " << circuit.num_nodes() << "\n";

    std::cout << "\nDevices:\n";
    for (const auto& device : circuit.get_devices()) {
        std::cout << "  " << device->name() << " (type=" << static_cast<int>(device->type()) << ")\n";
    }
}

void dump_mna_matrix(const MNASystem& sys, const circuit::CircuitGraph& circuit) {
    std::cout << "\n=== MNA Matrix Structure ===\n";
    std::cout << "Matrix size: " << sys.G.n_rows << " x " << sys.G.n_cols << "\n";
    std::cout << "Number of entries: " << sys.G.values.size() << "\n";

    // Aggregate COO entries
    std::map<std::pair<std::uint32_t, std::uint32_t>, Real> agg;
    for (std::size_t k = 0; k < sys.G.values.size(); ++k) {
        auto key = std::make_pair(sys.G.rows[k], sys.G.cols[k]);
        agg[key] += sys.G.values[k];
    }

    std::cout << "After aggregation: " << agg.size() << " unique entries\n";

    // Print matrix in dense format
    std::cout << "\nMatrix G (dense format):\n";
    std::cout << std::scientific << std::setprecision(3);

    std::cout << "       ";
    for (std::uint32_t col = 0; col < sys.G.n_cols; ++col) {
        std::cout << std::setw(12) << col;
    }
    std::cout << "\n";

    for (std::uint32_t row = 0; row < sys.G.n_rows; ++row) {
        std::cout << std::setw(4) << row << " | ";
        for (std::uint32_t col = 0; col < sys.G.n_cols; ++col) {
            auto key = std::make_pair(row, col);
            if (agg.find(key) != agg.end() && std::abs(agg[key]) > 1e-15) {
                std::cout << std::setw(12) << agg[key];
            } else {
                std::cout << std::setw(12) << ".";
            }
        }
        std::cout << "\n";
    }

    // Print RHS vector
    std::cout << "\nRHS vector b:\n";
    for (std::uint32_t i = 0; i < sys.b.size(); ++i) {
        if (std::abs(sys.b[i]) > 1e-15) {
            std::cout << "  b[" << i << "] = " << sys.b[i] << "\n";
        }
    }

    // Check column 4 specifically
    std::cout << "\n=== Analysis of Column 4 ===\n";
    std::cout << "Column 4 entries:\n";
    bool has_nonzero = false;
    for (std::uint32_t row = 0; row < sys.G.n_rows; ++row) {
        auto key = std::make_pair(row, 4);
        if (agg.find(key) != agg.end() && std::abs(agg[key]) > 1e-15) {
            std::cout << "  G[" << row << ",4] = " << agg[key] << "\n";
            has_nonzero = true;
        }
    }
    if (!has_nonzero) {
        std::cout << "  *** Column 4 has NO non-zero entries! ***\n";
    }

    // Check if column 4 corresponds to a node
    if (sys.G.n_cols > sys.G.n_rows - 2) {
        std::cout << "\nColumn 4 corresponds to: ";
        if (4 < circuit.num_nodes()) {
            std::cout << "Node voltage variable (index " << 4 << ")\n";
        } else {
            std::uint32_t branch_idx = 4 - circuit.num_nodes();
            std::cout << "Branch current variable (branch " << branch_idx << ")\n";
        }
    }
}

void analyze_netlist(const std::string& filename, const std::string& description) {
    std::cout << "\n\n";
    std::cout << "====================================================================\n";
    std::cout << "=== " << description << " ===\n";
    std::cout << "=== File: " << filename << " ===\n";
    std::cout << "====================================================================\n\n";

    try {
        // Parse netlist
        parser::NetlistParser parser;
        auto result = parser.parse_file(filename);

        if (!result.circuit) {
            std::cerr << "Failed to parse netlist\n";
            return;
        }

        print_matrix_structure(*result.circuit);

        // Create DC solver
        solvers::DCSolver solver(*result.circuit, result.config);

        // Try to solve once to build the initial MNA system
        DenseVector solution;
        SolverStats stats;

        std::cout << "\n=== Attempting DC solve ===\n";
        auto status = solver.solve(solution, stats);

        // Dump the MNA system
        std::cout << "\n=== Dumping MNA System ===\n";
        const MNASystem& mna = solver.get_mna_system();
        dump_mna_matrix(mna, *result.circuit);

        if (status == ConvergenceStatus::SUCCESS) {
            std::cout << "\n✓ DC solve succeeded\n";
            std::cout << "Solution vector size: " << solution.size() << "\n";
        } else {
            std::cout << "\n✗ DC solve failed with status: " << static_cast<int>(status) << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}

int main() {
    std::cout << "=== Emitter Resistor Matrix Singularity Debug ===\n";
    std::cout << "Comparing working vs. failing configurations\n";

    // Analyze working configuration (emitter直接接地)
    analyze_netlist("tests/integration/test_multi_device_working.cir",
                    "WORKING: Emitter directly grounded");

    // Analyze failing configuration (emitter抵抗経由接地)
    analyze_netlist("tests/integration/test_multi_device.cir",
                    "FAILING: Emitter via resistor");

    std::cout << "\n\n";
    std::cout << "====================================================================\n";
    std::cout << "=== Analysis Complete ===\n";
    std::cout << "====================================================================\n";

    return 0;
}
