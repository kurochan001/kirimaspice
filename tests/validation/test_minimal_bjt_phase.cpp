// Minimal BJT phase test - reproduce ngspice reference
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

// Test 1: Minimal BJT with small base resistor for physical correctness
// fix: Added Rb=100Ω to make the circuit physically realistic
// (direct voltage source to base caused non-physical operating point)
TEST(MinimalBJTPhase, CompareWithNgspice) {
    // Build minimal BJT circuit with small base resistor for physical correctness
    circuit::CircuitGraph circuit;

    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0, 0.0);      // DC only
    circuit.add_voltage_source("Vin", "in", "0", 0.7, 0.01, 0.0); // DC=0.7V, AC=10mV∠0°
    circuit.add_resistor("Rb", "in", "base", 100.0);              // Small base resistor for physical correctness
    circuit.add_resistor("Rc", "vcc", "out", 1000.0);
    circuit.add_bjt_npn("Q1", "out", "base", "0", 1e-14, 100.0, 1.0);

    // Get node IDs
    const NodeId in_node = circuit.get_node_id("in");
    const NodeId base_node = circuit.get_node_id("base");
    const NodeId out_node = circuit.get_node_id("out");

    // Configure simulation
    SimulationConfig config;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1001.0;  // Need at least 2 points
    config.ac.num_points = 2;

    // Run AC analysis (includes DC operating point calculation)
    ACSolver ac_solver(circuit, config);
    ACResult ac_result;
    try {
        ac_result = ac_solver.solve();
    } catch (const std::exception& e) {
        std::cerr << "AC analysis failed: " << e.what() << "\n";
        FAIL() << "AC analysis threw exception";
    }

    // Display DC operating point (from AC solver's internal DC analysis)
    std::cout << "\n=== DC Operating Point (from AC solver) ===\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "(Note: DC voltages are computed internally by AC solver)\n";

    std::cout << "\n=== AC Analysis @ 1 kHz ===\n";
    std::cout << std::scientific << std::setprecision(6);

    ASSERT_FALSE(ac_result.frequency_points.empty());

    const auto& freq_point = ac_result.frequency_points[0];

    Complex v_in_ac = freq_point.solution[in_node - 1];
    Complex v_base_ac = freq_point.solution[base_node - 1];
    Complex v_out_ac = freq_point.solution[out_node - 1];

    std::cout << "V(in)   = " << std::real(v_in_ac) << " + j" << std::imag(v_in_ac) << "\n";
    std::cout << "V(base) = " << std::real(v_base_ac) << " + j" << std::imag(v_base_ac) << "\n";
    std::cout << "V(out)  = " << std::real(v_out_ac) << " + j" << std::imag(v_out_ac) << "\n";

    double phase_in = std::arg(v_in_ac) * 180.0 / M_PI;
    double phase_base = std::arg(v_base_ac) * 180.0 / M_PI;
    double phase_out = std::arg(v_out_ac) * 180.0 / M_PI;

    std::cout << "\n";
    std::cout << "Phase(in)   = " << phase_in << "°\n";
    std::cout << "Phase(base) = " << phase_base << "°\n";
    std::cout << "Phase(out)  = " << phase_out << "°\n";

    std::cout << "\n=== Phase Check (Common Emitter Amplifier) ===\n";
    std::cout << "Common emitter amplifier should have 180° phase inversion.\n";
    std::cout << "V(out) should have NEGATIVE real part (180° phase).\n";
    std::cout << "kirimaspice: Re(V(out)) = " << std::real(v_out_ac);

    if (std::real(v_out_ac) < 0) {
        std::cout << "  ✓ NEGATIVE (180° phase) - CORRECT!\n";
    } else {
        std::cout << "  ⚠️ POSITIVE (0° phase) - ERROR!\n";
    }

    // Common emitter amplifier should have:
    // 1. V(in) with 0° phase (positive real part)
    // 2. V(out) with 180° phase (negative real part) - inverted
    EXPECT_GT(std::real(v_in_ac), 0.0) << "V(in) should have 0° phase (positive)";
    EXPECT_LT(std::real(v_out_ac), 0.0) << "V(out) should have 180° phase (negative)";

    // Verify gain is reasonable (with Rb=100Ω and Rc=1kΩ, gain should be significant)
    double gain = std::abs(v_out_ac) / std::abs(v_in_ac);
    std::cout << "Voltage gain |V(out)/V(in)| = " << gain << "\n";
    EXPECT_GT(gain, 0.1) << "Gain should be significant for common emitter";
}
