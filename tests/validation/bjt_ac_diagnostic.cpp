// BJT AC Analysis Diagnostic - Compare DC operating points and small-signal parameters

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/devices/bjt_npn.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>

using namespace kirimaspice;

int main() {
    std::cout << "=== BJT Common-Emitter Amplifier Diagnostic ===" << std::endl;
    std::cout << std::fixed << std::setprecision(6);

    // Build circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);
    circuit.add_voltage_source("Vbase", "base", "0", 0.75, 0.005);  // DC=0.75V, AC=5mV
    circuit.add_resistor("Rb", "base", "b", 1000.0);
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    // Get node IDs
    const NodeId node_b = circuit.get_node_id("b");
    const NodeId node_coll = circuit.get_node_id("coll");
    const NodeId node_base = circuit.get_node_id("base");
    const NodeId node_vcc = circuit.get_node_id("vcc");

    // Step 1: Run DC analysis
    std::cout << "\n=== Step 1: DC Operating Point ===" << std::endl;
    SimulationConfig config;
    solvers::DCSolver dc_solver(circuit, config);
    
    DenseVector dc_solution;
    SolverStats dc_stats;
    auto dc_status = dc_solver.solve(dc_solution, dc_stats);

    if (dc_status != ConvergenceStatus::SUCCESS) {
        std::cerr << "DC solve failed!" << std::endl;
        return 1;
    }

    std::cout << "V(base) = " << dc_solution[node_base - 1] << " V" << std::endl;
    std::cout << "V(b)    = " << dc_solution[node_b - 1] << " V" << std::endl;
    std::cout << "V(coll) = " << dc_solution[node_coll - 1] << " V" << std::endl;
    std::cout << "V(vcc)  = " << dc_solution[node_vcc - 1] << " V" << std::endl;

    Real v_b = dc_solution[node_b - 1];
    Real v_coll = dc_solution[node_coll - 1];
    Real v_e = 0.0;
    Real vbe = v_b - v_e;
    Real vbc = v_b - v_coll;

    std::cout << "\nV(be)   = " << vbe << " V" << std::endl;
    std::cout << "V(bc)   = " << vbc << " V" << std::endl;

    // Step 2: Extract BJT and compute small-signal parameters
    std::cout << "\n=== Step 2: Small-Signal Parameters ===" << std::endl;
    auto devices = circuit.get_devices();
    devices::BJT_NPN* bjt = nullptr;
    for (auto& dev : devices) {
        if (dev->name() == "Q1" && dev->type() == DeviceType::BJT_NPN) {
            bjt = dynamic_cast<devices::BJT_NPN*>(dev.get());
            break;
        }
    }

    if (!bjt) {
        std::cerr << "Error: BJT not found!" << std::endl;
        return 1;
    }

    std::cout << std::scientific << std::setprecision(6);
    Real ib = bjt->get_base_current(vbe, vbc);
    Real ic = bjt->get_collector_current(vbe, vbc);
    std::cout << "Ib      = " << ib << " A" << std::endl;
    std::cout << "Ic      = " << ic << " A" << std::endl;
    std::cout << "Ie      = " << -(ib + ic) << " A" << std::endl;

    Real gm = bjt->get_transconductance(vbe, vbc);
    Real gpi = bjt->get_input_conductance(vbe, vbc);
    Real gmu = bjt->get_feedback_conductance(vbe, vbc);
    std::cout << "\nGm      = " << gm << " S (transconductance)" << std::endl;
    std::cout << "Gπ      = " << gpi << " S (input conductance)" << std::endl;
    std::cout << "Gμ      = " << gmu << " S (feedback conductance)" << std::endl;

    // Step 3: Compare with ngspice
    std::cout << "\n=== Step 3: Comparison with ngspice ===" << std::endl;
    std::cout << std::fixed << std::setprecision(6);
    
    std::cout << "\nDC Voltages:" << std::endl;
    std::cout << "          ngspice      kirimaspice       diff" << std::endl;
    std::cout << "V(b)    = 0.695902    " << v_b << "   " << (v_b - 0.695902) << std::endl;
    std::cout << "V(coll) = 0.174422    " << v_coll << "   " << (v_coll - 0.174422) << std::endl;
    std::cout << "Vbe     = 0.695902    " << vbe << "   " << (vbe - 0.695902) << std::endl;
    std::cout << "Vbc     = 0.521480    " << vbc << "   " << (vbc - 0.521480) << std::endl;

    std::cout << std::scientific;
    std::cout << "\nDC Currents:" << std::endl;
    std::cout << "          ngspice      kirimaspice       diff" << std::endl;
    std::cout << "Ib      = 5.409799e-05    " << ib << "   " << (ib - 5.409799e-05) << std::endl;
    std::cout << "Ic      = 4.825578e-03    " << ic << "   " << (ic - 4.825578e-03) << std::endl;

    std::cout << "\nSmall-Signal Parameters:" << std::endl;
    std::cout << "          ngspice      kirimaspice       diff" << std::endl;
    std::cout << "Gm      = 1.867841e-01    " << gm << "   " << (gm - 1.867841e-01) << std::endl;
    std::cout << "Gπ      = 1.871069e-03    " << gpi << "   " << (gpi - 1.871069e-03) << std::endl;
    std::cout << "Gμ      = 2.204890e-04    " << gmu << "   " << (gmu - 2.204890e-04) << std::endl;

    // Step 4: Calculate theoretical gain
    std::cout << "\n=== Step 4: Theoretical Gain Analysis ===" << std::endl;
    Real rc = 1000.0;
    Real rb = 1000.0;
    Real rpi = 1.0 / gpi;  // Input resistance = 1/Gπ
    
    std::cout << "Rc      = " << rc << " Ω (collector resistor)" << std::endl;
    std::cout << "Rb      = " << rb << " Ω (base resistor)" << std::endl;
    std::cout << "rπ      = " << rpi << " Ω (BJT input resistance = 1/Gπ)" << std::endl;

    // Gain WITHOUT considering input voltage division
    Real av_simple = gm * rc;
    std::cout << "\nSimple gain (Av = Gm × Rc):" << std::endl;
    std::cout << "  Av_simple = " << av_simple << " (linear)" << std::endl;
    std::cout << "  Av_simple = " << 20.0 * std::log10(av_simple) << " dB" << std::endl;

    // Gain WITH input voltage division
    // v_be = v_in × (rπ / (Rb + rπ))
    // v_out = -gm × v_be × Rc = -gm × Rc × (rπ / (Rb + rπ)) × v_in
    Real voltage_division = rpi / (rb + rpi);
    Real av_full = gm * rc * voltage_division;
    
    std::cout << "\nFull gain (with input voltage division):" << std::endl;
    std::cout << "  Voltage division = rπ/(Rb+rπ) = " << voltage_division << std::endl;
    std::cout << "  Av_full = Gm × Rc × (rπ/(Rb+rπ)) = " << av_full << " (linear)" << std::endl;
    std::cout << "  Av_full = " << 20.0 * std::log10(av_full) << " dB" << std::endl;

    // Step 5: Run AC analysis and check gain
    std::cout << "\n=== Step 5: AC Analysis at 1kHz ===" << std::endl;
    config.ac.sweep_type = SimulationConfig::AC::SweepType::LIN;
    config.ac.start_freq = 1000.0;
    config.ac.stop_freq = 1000.0;
    config.ac.num_points = 1;

    solvers::ACSolver ac_solver(circuit, config);
    auto ac_result = ac_solver.solve();

    if (!ac_result.frequency_points.empty()) {
        const auto& point = ac_result.frequency_points[0];
        Complex v_coll_ac = point.solution[node_coll - 1];
        Real mag = std::abs(v_coll_ac);
        Real phase_deg = std::arg(v_coll_ac) * 180.0 / M_PI;
        Real gain_linear = mag / 0.005;  // v_out / v_in (v_in = 5mV AC)
        Real gain_db = 20.0 * std::log10(gain_linear);

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "v(coll) = " << mag << " V (magnitude)" << std::endl;
        std::cout << "Phase   = " << phase_deg << " deg" << std::endl;
        std::cout << "Gain    = " << gain_linear << " (v_out / v_in)" << std::endl;
        std::cout << "Gain    = " << gain_db << " dB" << std::endl;

        std::cout << "\nngspice gain (from CSV): |v(coll)| = 0.020344 V" << std::endl;
        std::cout << "ngspice gain = " << (0.020344 / 0.005) << " (linear)" << std::endl;
        std::cout << "ngspice gain = " << 20.0 * std::log10(0.020344 / 0.005) << " dB" << std::endl;

        std::cout << "\nGain error = " << (gain_db - 20.0 * std::log10(0.020344 / 0.005)) << " dB" << std::endl;
        std::cout << "Ratio = kirimaspice/ngspice = " << (gain_linear / (0.020344 / 0.005)) << "x" << std::endl;
    }

    // Step 6: Hypothesis testing
    std::cout << "\n=== Step 6: Hypothesis Testing ===" << std::endl;
    std::cout << std::fixed << std::setprecision(6);
    
    std::cout << "\nHypothesis 1: DC Operating Point Difference" << std::endl;
    Real vbe_error = std::abs(vbe - 0.695902);
    Real vbc_error = std::abs(vbc - 0.521480);
    std::cout << "  Vbe error = " << vbe_error << " V" << std::endl;
    std::cout << "  Vbc error = " << vbc_error << " V" << std::endl;
    if (vbe_error < 1e-6 && vbc_error < 1e-6) {
        std::cout << "  ✓ DC voltages MATCH (error < 1μV)" << std::endl;
    } else {
        std::cout << "  ✗ DC voltages DIFFER" << std::endl;
    }

    std::cout << "\nHypothesis 2: Small-Signal Parameter Calculation" << std::endl;
    Real gm_error = std::abs((gm - 1.867841e-01) / 1.867841e-01);
    Real gpi_error = std::abs((gpi - 1.871069e-03) / 1.871069e-03);
    std::cout << "  Gm  error = " << (gm_error * 100.0) << " %" << std::endl;
    std::cout << "  Gπ  error = " << (gpi_error * 100.0) << " %" << std::endl;
    if (gm_error < 0.01 && gpi_error < 0.01) {
        std::cout << "  ✓ Small-signal parameters MATCH (error < 1%)" << std::endl;
    } else {
        std::cout << "  ✗ Small-signal parameters DIFFER" << std::endl;
    }

    std::cout << "\nHypothesis 3: Input Voltage Division" << std::endl;
    Real expected_ratio = voltage_division;  // rπ/(Rb+rπ)
    std::cout << "  Expected voltage division: " << expected_ratio << std::endl;
    std::cout << "  This should account for ~2x gain reduction" << std::endl;
    
    Real ngspice_gain = 0.020344 / 0.005;  // 4.0688
    Real kirimaspice_gain = av_full;  // theoretical gain with voltage division
    Real ratio = kirimaspice_gain / ngspice_gain;
    std::cout << "  Theoretical gain (with division): " << kirimaspice_gain << std::endl;
    std::cout << "  ngspice gain: " << ngspice_gain << std::endl;
    std::cout << "  Ratio (theoretical/ngspice): " << ratio << "x" << std::endl;

    return 0;
}
