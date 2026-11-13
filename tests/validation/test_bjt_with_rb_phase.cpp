// BJT with base resistor phase test - check if Rb causes phase error
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;
using namespace kirimaspice::solvers;

TEST(BJTWithRbPhase, CompareWithNgspice) {
    // Build BJT circuit WITH base resistor (same as bjt_with_rb_test.cir)
    circuit::CircuitGraph circuit;

    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0, 0.0);       // DC only
    circuit.add_voltage_source("Vin", "in", "0", 0.7, 0.01, 0.0);  // DC=0.7V, AC=10mV∠0°
    circuit.add_resistor("Rb", "in", "base", 1000.0);              // BASE RESISTOR
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
    config.ac.stop_freq = 1001.0;
    config.ac.num_points = 2;

    // Run AC analysis
    ACSolver ac_solver(circuit, config);
    ACResult ac_result;
    try {
        ac_result = ac_solver.solve();
    } catch (const std::exception& e) {
        std::cerr << "AC analysis failed: " << e.what() << "\n";
        FAIL() << "AC analysis threw exception";
    }

    std::cout << "\n=== BJT with Base Resistor - AC Analysis @ 1 kHz ===\n";
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

    std::cout << "\n=== Comparison with ngspice ===\n";
    std::cout << "ngspice: V(in)   = +1.00e-02  (0° phase)\n";
    std::cout << "ngspice: V(base) = +5.28e-03  (0° phase)\n";
    std::cout << "ngspice: V(out)  = -4.71e-01  (180° phase - CORRECT)\n\n";

    std::cout << "kirimaspice:  V(in)   = " << std::real(v_in_ac)
              << (std::real(v_in_ac) > 0 ? " ✓ POSITIVE (0°)" : " ✗ NEGATIVE (180°)") << "\n";
    std::cout << "kirimaspice:  V(base) = " << std::real(v_base_ac)
              << (std::real(v_base_ac) > 0 ? " ✓ POSITIVE (0°)" : " ✗ NEGATIVE (180°)") << "\n";
    std::cout << "kirimaspice:  V(out)  = " << std::real(v_out_ac);

    if (std::real(v_out_ac) < 0) {
        std::cout << "  ✓ NEGATIVE (180° phase) - CORRECT!\n";
        std::cout << "\n🎉 Phase error FIXED! Base resistor does NOT cause phase error.\n";
    } else {
        std::cout << "  ⚠️ POSITIVE (0° phase) - ERROR!\n";
        std::cout << "\n🔴 Phase error detected with base resistor present.\n";
    }

    // Expected from ngspice
    double expected_out_real = -0.471;  // Negative real = 180°

    // Check if sign matches (allow magnitude difference)
    bool sign_correct = (expected_out_real < 0 && std::real(v_out_ac) < 0) ||
                       (expected_out_real > 0 && std::real(v_out_ac) > 0);

    EXPECT_TRUE(sign_correct) << "Phase sign mismatch: expected negative, got positive";

    // Magnitude should be within reasonable range
    EXPECT_NEAR(std::abs(v_out_ac), std::abs(expected_out_real), 0.1)
        << "Magnitude error too large";
}
