// tests/validation/test_diode_rectifier_ngspice.cpp
// Diode half-wave rectifier circuit - transient analysis comparison with ngspice

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
    std::vector<double> v_in;
    std::vector<double> v_out;
    std::vector<double> i_source;
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
        double t, vin, vout, isrc;
        if (iss >> t >> vin >> vout >> isrc) {
            data.time.push_back(t);
            data.v_in.push_back(vin);
            data.v_out.push_back(vout);
            data.i_source.push_back(isrc);
        }
    }

    return data;
}

// Find closest timepoint in kirimaspice result
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
    std::cout << "=== Diode Half-Wave Rectifier: Transient Analysis ===" << std::endl;
    std::cout << "Circuit: SIN(0V 10V 60Hz) -> Diode -> R 1k" << std::endl;
    std::cout << std::endl;

    // Load ngspice reference data
    std::cout << "Loading ngspice reference data..." << std::endl;
    NgspiceData ngspice_data;
    try {
        ngspice_data = load_ngspice_data("validation/diode_rectifier_ngspice.txt");
        std::cout << "  Loaded " << ngspice_data.time.size() << " datapoints from ngspice" << std::endl;
        std::cout << "  Time range: " << ngspice_data.time.front() << "s to "
                  << ngspice_data.time.back() << "s" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    // Build circuit with kirimaspice
    std::cout << "\nBuilding circuit..." << std::endl;
    circuit::CircuitGraph circuit;

    // SIN voltage source: V(t) = offset + amplitude * sin(2*pi*freq*t)
    circuit.add_sin_voltage_source("V1", "in", "0",
                                   0.0,    // offset
                                   10.0,   // amplitude
                                   60.0,   // frequency (Hz)
                                   0.0,    // delay
                                   0.0,    // damping
                                   0.0);   // phase
    circuit.add_diode("D1", "in", "out", 1e-14, 1.0);  // IS=1e-14A, N=1.0
    circuit.add_resistor("R1", "out", "0", 1000.0);    // 1k load resistor

    NodeId node_in = circuit.get_node_id("in");
    NodeId node_out = circuit.get_node_id("out");

    std::cout << "  Circuit nodes: in=" << node_in << ", out=" << node_out << std::endl;

    // Simulation configuration (adaptive timestep for accuracy)
    SimulationConfig config;
    config.transient.use_initial_conditions = true;
    config.transient.use_adaptive_timestep = true;
    config.transient.method = IntegrationMethod::TRAPEZOIDAL;
    config.transient.adaptive_order = true;
    config.transient.max_order = 2;
    config.transient.initial_step = 1e-7;
    config.transient.max_step = 1e-4;
    config.transient.min_step = 1e-9;
    config.transient.lte_abstol = 1e-7;
    config.transient.lte_reltol = 1e-4;

    // Run transient simulation (50ms, matching ngspice)
    std::cout << "\nRunning transient analysis..." << std::endl;
    double t_stop = 50e-3;  // 50ms
    double initial_dt = 1e-6;

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
    std::cout << "Time(ms) | V_in_ngspice | V_in_suki | V_out_ngspice | V_out_suki | Error_out(V) | Error(%)" << std::endl;
    std::cout << "---------|--------------|-----------|---------------|------------|--------------|----------" << std::endl;

    // Sample at regular intervals (every 100th point, ~50 comparisons)
    std::vector<double> max_errors;
    double max_error_out = 0.0;
    double max_relative_error = 0.0;
    int comparison_count = 0;

    for (size_t i = 0; i < ngspice_data.time.size(); i += 100) {
        double t = ngspice_data.time[i];
        double v_in_ngspice = ngspice_data.v_in[i];
        double v_out_ngspice = ngspice_data.v_out[i];

        // Find closest voltage in kirimaspice result
        double v_in_suki = find_closest_voltage(result, node_in, t);
        double v_out_suki = find_closest_voltage(result, node_out, t);

        double error_out = std::abs(v_out_suki - v_out_ngspice);
        double relative_error = (v_out_ngspice > 0.1) ? (error_out / v_out_ngspice * 100.0) : 0.0;

        max_error_out = std::max(max_error_out, error_out);
        max_relative_error = std::max(max_relative_error, relative_error);

        std::cout << std::setw(8) << t * 1e3 << " | ";
        std::cout << std::setw(12) << v_in_ngspice << " | ";
        std::cout << std::setw(9) << v_in_suki << " | ";
        std::cout << std::setw(13) << v_out_ngspice << " | ";
        std::cout << std::setw(10) << v_out_suki << " | ";
        std::cout << std::setw(12) << error_out << " | ";
        std::cout << std::setw(8) << relative_error << std::endl;

        comparison_count++;
    }

    std::cout << "\n=== Statistics ===" << std::endl;
    std::cout << "Comparison points: " << comparison_count << std::endl;
    std::cout << "Maximum absolute error (V_out): " << max_error_out << " V" << std::endl;
    std::cout << "Maximum relative error (V_out): " << max_relative_error << " %" << std::endl;

    // Success criteria: error < 1% or < 0.1V (whichever is larger)
    bool test_passed = (max_error_out < 0.1) || (max_relative_error < 1.0);

    std::cout << "\n=== Test Result ===" << std::endl;
    if (test_passed) {
        std::cout << "✓ Test PASSED - Waveforms match within tolerance" << std::endl;
        return 0;
    } else {
        std::cout << "✗ Test FAILED - Error exceeds tolerance" << std::endl;
        return 1;
    }
}
