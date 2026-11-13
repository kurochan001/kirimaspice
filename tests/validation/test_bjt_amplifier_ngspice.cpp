// tests/validation/test_bjt_amplifier_ngspice.cpp
// BJT NPN Common-Emitter Amplifier - transient analysis comparison with ngspice
// Simplified circuit: no emitter resistor for better convergence

#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <cmath>
#include <vector>
#include <string>

using namespace kirimaspice;

// Parse ngspice output file
struct NgspiceData {
    std::vector<double> time;
    std::vector<double> v_base;
    std::vector<double> v_b;
    std::vector<double> v_coll;
    std::vector<double> i_vcc;
};

NgspiceData load_ngspice_data(const std::string& filename) {
    NgspiceData data;
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open ngspice data file: " + filename);
    }

    std::string line;
    // Skip header line
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::istringstream iss(line);
        double t1, t2, vbase, vb, vcoll, ivcc;
        // ngspice output has duplicate time column
        if (iss >> t1 >> t2 >> vbase >> vb >> vcoll >> ivcc) {
            data.time.push_back(t2);
            data.v_base.push_back(vbase);
            data.v_b.push_back(vb);
            data.v_coll.push_back(vcoll);
            data.i_vcc.push_back(ivcc);
        }
    }

    return data;
}

// Find closest voltage in kirimaspice result
double find_closest_voltage(const solvers::TransientResult& result,
                            NodeId node_id, double target_time) {
    double closest_voltage = 0.0;
    double min_diff = 1e100;

    for (const auto& tp : result.timepoints) {
        double diff = std::abs(tp.time - target_time);
        if (diff < min_diff) {
            min_diff = diff;
            closest_voltage = tp.node_voltages[node_id - 1];  // NodeId starts from 1
        }
    }

    return closest_voltage;
}

int main() {
    std::cout << "=== BJT NPN Common-Emitter Amplifier: Transient Analysis ===" << std::endl;
    std::cout << "Circuit: SIN(0.75V 5mV 1kHz) -> Rb(100Ω) -> BJT (Emitter=GND)" << std::endl;
    std::cout << "Expected: Amplified output at collector" << std::endl;
    std::cout << std::endl;

    // Load ngspice reference data
    std::cout << "Loading ngspice reference data..." << std::endl;
    NgspiceData ngspice_data;
    try {
        ngspice_data = load_ngspice_data("validation/bjt_amplifier_ngspice.txt");
        std::cout << "  Loaded " << ngspice_data.time.size() << " datapoints from ngspice" << std::endl;
        std::cout << "  Time range: " << ngspice_data.time.front() << "s to "
                  << ngspice_data.time.back() << "s" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    // Build circuit with kirimaspice (matching ngspice simplified circuit)
    std::cout << "\nBuilding BJT amplifier circuit..." << std::endl;
    circuit::CircuitGraph circuit;

    // Power supply
    circuit.add_voltage_source("Vcc", "vcc", "0", 5.0);

    // Base voltage source: SIN(0.75V, 5mV, 1kHz)
    circuit.add_sin_voltage_source("Vbase", "base", "0",
                                   0.75,     // offset (DC bias)
                                   0.005,    // amplitude (5mV)
                                   1000.0,   // frequency (1kHz)
                                   0.0,      // delay
                                   0.0,      // damping
                                   0.0);     // phase

    // Base resistor: base -> b (100 Ohm)
    circuit.add_resistor("Rb", "base", "b", 100.0);

    // Collector resistor: vcc -> coll (1k Ohm)
    circuit.add_resistor("Rc", "vcc", "coll", 1000.0);

    // BJT transistor: Q1 coll b emitter
    // Emitter directly connected to GND (0)
    circuit.add_bjt_npn("Q1", "coll", "b", "0", 1e-14, 100.0, 1.0);

    NodeId node_base = circuit.get_node_id("base");
    NodeId node_b = circuit.get_node_id("b");
    NodeId node_coll = circuit.get_node_id("coll");

    std::cout << "  Circuit nodes: base=" << node_base
              << ", b=" << node_b
              << ", coll=" << node_coll << std::endl;

    // Simulation configuration
    SimulationConfig config;

    // DC solver settings
    config.dc.max_iterations = 100;
    config.dc.abstol = 1e-10;
    config.dc.reltol = 1e-6;
    config.dc.vntol = 1e-6;

    // Transient settings
    config.transient.use_initial_conditions = false;  // Compute DC operating point
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.adaptive_order = true;
    config.transient.max_order = 2;
    config.transient.initial_step = 1e-8;
    config.transient.max_step = 1e-6;
    config.transient.min_step = 1e-10;
    config.transient.lte_abstol = 1e-8;
    config.transient.lte_reltol = 1e-5;

    // Run transient simulation (10ms, matching ngspice)
    std::cout << "\nRunning transient analysis..." << std::endl;
    double t_stop = 10e-3;  // 10ms
    double initial_dt = 1e-7;

    solvers::TransientSolver solver(circuit, config);
    auto result = solver.solve(0.0, t_stop, initial_dt);

    if (result.status != ConvergenceStatus::SUCCESS) {
        std::cerr << "ERROR: Transient simulation failed" << std::endl;
        return 1;
    }

    std::cout << "  Simulation completed: " << result.timepoints.size() << " timepoints" << std::endl;

    // Compare with ngspice at selected timepoints
    std::cout << "\n=== Waveform Comparison (kirimaspice vs ngspice) ===" << std::endl;
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Time(ms) | V_b_ngsp | V_b_suki | V_coll_ngsp | V_coll_suki | Err_coll(V)" << std::endl;
    std::cout << "---------|----------|----------|-------------|-------------|-------------" << std::endl;

    // Sample at regular intervals (every 200th point)
    double max_error_coll = 0.0;
    double max_error_b = 0.0;
    int comparison_count = 0;

    for (size_t i = 0; i < ngspice_data.time.size(); i += 200) {
        double t = ngspice_data.time[i];
        double v_b_ng = ngspice_data.v_b[i];
        double v_coll_ng = ngspice_data.v_coll[i];

        // Find closest voltage in kirimaspice result
        double v_b_sk = find_closest_voltage(result, node_b, t);
        double v_coll_sk = find_closest_voltage(result, node_coll, t);

        double error_coll = std::abs(v_coll_sk - v_coll_ng);
        double error_b = std::abs(v_b_sk - v_b_ng);

        max_error_coll = std::max(max_error_coll, error_coll);
        max_error_b = std::max(max_error_b, error_b);

        std::cout << std::setw(8) << t * 1e3 << " | ";
        std::cout << std::setw(8) << v_b_ng << " | ";
        std::cout << std::setw(8) << v_b_sk << " | ";
        std::cout << std::setw(11) << v_coll_ng << " | ";
        std::cout << std::setw(11) << v_coll_sk << " | ";
        std::cout << std::setw(11) << error_coll << std::endl;

        comparison_count++;
    }

    std::cout << "\n=== Statistics ===" << std::endl;
    std::cout << "Comparison points: " << comparison_count << std::endl;
    std::cout << "Maximum absolute error (V_coll): " << max_error_coll << " V" << std::endl;
    std::cout << "Maximum absolute error (V_b): " << max_error_b << " V" << std::endl;

    // Success criteria: collector voltage error < 0.2V (relaxed for BJT)
    bool test_passed = (max_error_coll < 0.2);

    std::cout << "\n=== Test Result ===" << std::endl;
    if (test_passed) {
        std::cout << "✓ Test PASSED - BJT amplifier waveforms match within tolerance" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Test FAILED - Error exceeds tolerance" << std::endl;
        return 1;
    }
}
