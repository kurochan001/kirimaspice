// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Trace BJT AC stamping step-by-step
// Goal: Identify exact source of each matrix entry

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <iostream>
#include <iomanip>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

// Custom MNA system that logs every add_entry call
struct TracingComplexMNASystem : public ComplexMNASystem {
    void trace_add(int row, int col, Complex value, const char* source) {
        std::cout << "  ADD Y[" << row << "," << col << "] = "
                  << std::real(value) << " + j" << std::imag(value)
                  << "  ← " << source << "\n";
    }
};

int main() {
    std::cout << "=== Tracing BJT AC Stamping (Without Base Resistor) ===\n\n";

    // Build minimal BJT circuit (NO base resistor)
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0, 0.0);
    circuit.add_voltage_source("Vin", "in", "0", 0.7, 0.01, 0.0);
    circuit.add_resistor("Rc", "vcc", "out", 1000.0);
    circuit.add_bjt_npn("Q1", "out", "in", "0", 1e-14, 100.0, 1.0);

    NodeId in_node = circuit.get_node_id("in");   // Base node
    NodeId out_node = circuit.get_node_id("out"); // Collector node

    std::cout << "Node mapping:\n";
    std::cout << "  in (base) = " << in_node << " → matrix idx " << (in_node - 1) << "\n";
    std::cout << "  out (coll) = " << out_node << " → matrix idx " << (out_node - 1) << "\n";
    std::cout << "  emitter = GND (matrix idx -1, skipped)\n\n";

    // Run DC analysis
    SimulationConfig config;
    DCSolver dc_solver(circuit, config);
    DenseVector dc_solution;
    SolverStats stats;

    auto dc_status = dc_solver.solve(dc_solution, stats);
    if (dc_status != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC analysis failed\n";
        return 1;
    }

    std::cout << "DC operating point:\n";
    std::cout << "  V(in) = " << dc_solution[in_node - 1] << " V\n";
    std::cout << "  V(out) = " << dc_solution[out_node - 1] << " V\n\n";

    // Get BJT device
    auto& devices = circuit.get_devices();
    auto bjt_it = std::find_if(devices.begin(), devices.end(),
        [](const auto& dev) { return dev->type() == DeviceType::BJT_NPN; });

    if (bjt_it == devices.end()) {
        std::cerr << "BJT not found\n";
        return 1;
    }

    auto& bjt = *bjt_it;

    // Calculate small-signal parameters manually
    Real v_in = dc_solution[in_node - 1];
    Real v_out = dc_solution[out_node - 1];
    Real vbe = v_in - 0.0;  // Emitter at GND
    Real vbc = v_in - v_out;

    // Use BJT equations directly
    Real IS = 1e-14;
    Real BF = 100.0;
    Real VT = 0.025851;  // Thermal voltage at 27°C

    Real exp_vbe = std::exp(vbe / VT);
    Real ic = IS * exp_vbe;
    Real gm = ic / VT;  // Transconductance

    // Simple model: gpi = gm / BF
    Real gpi = gm / BF;

    // Assume gmu is small for now (TODO: get actual value)
    Real gmu = 1e-12;

    // Capacitances (simplified, TODO: get actual values)
    Real cpi = 0.0;  // Ignore for now
    Real cmu = 0.0;

    std::cout << "BJT small-signal parameters (manual calculation):\n";
    std::cout << "  Vbe = " << vbe << " V\n";
    std::cout << "  Vbc = " << vbc << " V\n";
    std::cout << "  Ic = " << ic << " A\n";
    std::cout << "  gm = " << gm << " S\n";
    std::cout << "  gπ = " << gpi << " S\n";
    std::cout << "  gμ = " << gmu << " S\n\n";

    Real omega = 2.0 * M_PI * 1000.0;  // 1 kHz
    Complex ypi(gpi, omega * cpi);
    Complex ymu(gmu, omega * cmu);
    Complex gmvbe(gm, 0.0);

    std::cout << "Complex admittances:\n";
    std::cout << "  yπ = " << std::real(ypi) << " + j" << std::imag(ypi) << " S\n";
    std::cout << "  yμ = " << std::real(ymu) << " + j" << std::imag(ymu) << " S\n";
    std::cout << "  gm = " << std::real(gmvbe) << " + j" << std::imag(gmvbe) << " S\n\n";

    // Manually trace stamping
    int nb_idx = in_node - 1;   // Base index (1)
    int nc_idx = out_node - 1;  // Collector index (2)
    int ne_idx = -1;             // Emitter index (GND, skipped)

    std::cout << "=== Manual Stamping Trace ===\n\n";

    std::cout << "1. B-E admittance (yπ = gπ + jωCπ):\n";
    if (nb_idx >= 0) {
        std::cout << "  Y[" << nb_idx << "," << nb_idx << "] += " << std::real(ypi) << " + j" << std::imag(ypi) << "\n";
        if (ne_idx >= 0) {
            std::cout << "  Y[" << nb_idx << "," << ne_idx << "] += " << -std::real(ypi) << " + j" << -std::imag(ypi) << "\n";
        } else {
            std::cout << "  Y[" << nb_idx << ",GND] = (skipped)\n";
        }
    }
    if (ne_idx >= 0) {
        std::cout << "  Y[" << ne_idx << "," << ne_idx << "] += " << std::real(ypi) << " + j" << std::imag(ypi) << "\n";
        if (nb_idx >= 0) {
            std::cout << "  Y[" << ne_idx << "," << nb_idx << "] += " << -std::real(ypi) << " + j" << -std::imag(ypi) << "\n";
        }
    } else {
        std::cout << "  Y[GND,*] = (all skipped)\n";
    }

    std::cout << "\n2. B-C admittance (yμ = gμ + jωCμ):\n";
    if (nb_idx >= 0) {
        std::cout << "  Y[" << nb_idx << "," << nb_idx << "] += " << std::real(ymu) << " + j" << std::imag(ymu) << "\n";
        if (nc_idx >= 0) {
            std::cout << "  Y[" << nb_idx << "," << nc_idx << "] += " << -std::real(ymu) << " + j" << -std::imag(ymu) << "\n";
        }
    }
    if (nc_idx >= 0) {
        std::cout << "  Y[" << nc_idx << "," << nc_idx << "] += " << std::real(ymu) << " + j" << std::imag(ymu) << "\n";
        if (nb_idx >= 0) {
            std::cout << "  🔴 Y[" << nc_idx << "," << nb_idx << "] += " << -std::real(ymu) << " + j" << -std::imag(ymu)
                      << " ← B-C feedback\n";
        }
    }

    std::cout << "\n3. Transconductance (gm × Vbe):\n";
    std::cout << "   Current source: Ic = gm × Vbe (collector current)\n";
    if (nc_idx >= 0) {
        if (nb_idx >= 0) {
            std::cout << "  🔴 Y[" << nc_idx << "," << nb_idx << "] += " << -std::real(gmvbe) << " + j" << -std::imag(gmvbe)
                      << " ← Transconductance (-gm × Vb)\n";
        }
        if (ne_idx >= 0) {
            std::cout << "  Y[" << nc_idx << "," << ne_idx << "] += " << +std::real(gmvbe) << " + j" << +std::imag(gmvbe) << "\n";
        } else {
            std::cout << "  Y[" << nc_idx << ",GND] = (skipped)\n";
        }
    }
    if (ne_idx >= 0) {
        if (nb_idx >= 0) {
            std::cout << "  Y[" << ne_idx << "," << nb_idx << "] += " << +std::real(gmvbe) << " + j" << +std::imag(gmvbe) << "\n";
        }
        std::cout << "  Y[" << ne_idx << "," << ne_idx << "] += " << -std::real(gmvbe) << " + j" << -std::imag(gmvbe) << "\n";
    } else {
        std::cout << "  Y[GND,*] = (all skipped)\n";
    }

    std::cout << "\n=== Summary ===\n";
    std::cout << "Y[" << nc_idx << "," << nb_idx << "] (collector, base) receives TWO contributions:\n";
    std::cout << "  1. -yμ = " << -std::real(ymu) << " S  (B-C feedback admittance)\n";
    std::cout << "  2. -gm = " << -std::real(gmvbe) << " S  (Transconductance)\n";
    std::cout << "  Total: " << (-std::real(ymu) - std::real(gmvbe)) << " S\n\n";

    std::cout << "Expected: Both negative, so total should be NEGATIVE\n";
    std::cout << "But compare_bjt_matrices showed +0.028 and -0.219...\n";
    std::cout << "This suggests one entry has WRONG SIGN in actual stamping!\n";

    return 0;
}
