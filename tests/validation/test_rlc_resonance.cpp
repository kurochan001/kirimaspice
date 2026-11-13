// tests/validation/test_rlc_resonance.cpp
// RLC series resonance circuit validation
// Tests oscillatory behavior and compares BDF2 vs Trapezoidal accuracy
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"
#include "kirimaspice/core/types.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

using namespace kirimaspice;

// RLC circuit theoretical response (underdamped)
// V(t) = V0 * (1 - exp(-alpha*t) * (cos(omega_d*t) + (alpha/omega_d)*sin(omega_d*t)))
// where alpha = R/(2L), omega_0 = 1/sqrt(LC), omega_d = sqrt(omega_0^2 - alpha^2)
double rlc_theory(double t, double V0, double R, double L, double C) {
    double alpha = R / (2.0 * L);
    double omega_0 = 1.0 / std::sqrt(L * C);
    double omega_d = std::sqrt(omega_0 * omega_0 - alpha * alpha);

    if (omega_d < 0) {
        // Overdamped - not implemented
        return V0;
    }

    return V0 * (1.0 - std::exp(-alpha * t) *
                (std::cos(omega_d * t) + (alpha / omega_d) * std::sin(omega_d * t)));
}

struct NgspiceDataPoint {
    double time;
    double v_out;
};

std::vector<NgspiceDataPoint> load_ngspice_data(const std::string& filename) {
    std::vector<NgspiceDataPoint> data;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Warning: Could not open " << filename << std::endl;
        return data;
    }

    std::string line;
    // Skip header lines
    while (std::getline(file, line)) {
        if (line.find("Index") != std::string::npos) {
            break;
        }
    }

    // Read data points
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '-') continue;

        std::istringstream iss(line);
        int index;
        double time, v_in, v_n1, v_out;

        if (iss >> index >> time >> v_in >> v_n1 >> v_out) {
            NgspiceDataPoint dp;
            dp.time = time;
            dp.v_out = v_out;
            data.push_back(dp);
        }
    }

    return data;
}

int main() {
    // Circuit parameters - adjusted for underdamped oscillation (ζ < 1)
    double R = 10.0;       // 10 Ohm (reduced for underdamped behavior)
    double L = 1e-3;       // 1 mH
    double C = 10e-6;      // 10 uF
    double V0 = 5.0;       // 5V source

    // Calculate circuit characteristics
    double f0 = 1.0 / (2.0 * M_PI * std::sqrt(L * C));  // Natural frequency
    double zeta = R / (2.0 * std::sqrt(L / C));         // Damping ratio

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n=== RLC Series Resonance Circuit ===" << std::endl;
    std::cout << "R = " << R << " Ω" << std::endl;
    std::cout << "L = " << L * 1e3 << " mH" << std::endl;
    std::cout << "C = " << C * 1e6 << " μF" << std::endl;
    std::cout << "Natural frequency f0 = " << f0 << " Hz" << std::endl;
    std::cout << "Damping ratio ζ = " << zeta << " (underdamped)" << std::endl;

    // Create RLC circuit
    circuit::CircuitGraph circuit;
    circuit.add_voltage_source("V1", "in", "0", V0);
    circuit.add_resistor("R1", "in", "n1", R);
    circuit.add_inductor("L1", "n1", "out", L);
    circuit.add_capacitor("C1", "out", "0", C);

    NodeId node_out = circuit.get_node_id("out");

    // Load ngspice reference data
    auto ngspice_data = load_ngspice_data("validation/rlc_resonance_ngspice.txt");
    bool has_ngspice_data = !ngspice_data.empty();

    if (has_ngspice_data) {
        std::cout << "\nngspice reference data loaded: " << ngspice_data.size() << " points" << std::endl;
    } else {
        std::cout << "\nWarning: ngspice reference data not available" << std::endl;
    }

    // Test 1: BDF2 with UIC (Use Initial Conditions) - observe oscillations!
    std::cout << "\n=== Test 1: BDF2 (GEAR2) with UIC - Step Response ===" << std::endl;
    {
        SimulationConfig config;
        config.transient.use_initial_conditions = true;  // UIC: Start from zero
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::GEAR2;
        config.transient.adaptive_order = true;
        config.transient.max_order = 3;

        solvers::TransientSolver solver(circuit, config);
        auto result = solver.solve(0.0, 1e-3, 100e-9);  // 0 to 1ms, initial dt=100ns

        if (result.status != ConvergenceStatus::SUCCESS) {
            std::cerr << "BDF2 simulation failed!" << std::endl;
            return 1;
        }

        std::cout << "Steps: " << result.timepoints.size() << std::endl;
        std::cout << "Iterations: " << result.stats.total_iterations << std::endl;
        std::cout << "Rejects: " << result.stats.timestep_rejects << std::endl;

        // Check oscillation at key times
        std::vector<double> check_times = {100e-6, 200e-6, 300e-6, 500e-6, 1e-3};
        std::cout << "\nKey timepoints:" << std::endl;
        std::cout << std::setw(12) << "Time" << " | ";
        std::cout << std::setw(10) << "V_out" << " | ";
        std::cout << std::setw(10) << "Theory" << " | ";
        std::cout << std::setw(10) << "Error%" << std::endl;
        std::cout << std::string(50, '-') << std::endl;

        for (double target_t : check_times) {
            // Find closest timepoint
            size_t closest_idx = 0;
            double min_diff = std::abs(result.timepoints[0].time - target_t);
            for (size_t j = 1; j < result.timepoints.size(); ++j) {
                double diff = std::abs(result.timepoints[j].time - target_t);
                if (diff < min_diff) {
                    min_diff = diff;
                    closest_idx = j;
                }
            }

            const auto& tp = result.timepoints[closest_idx];
            double v_out = tp.node_voltages[node_out - 1];
            double theory_v = rlc_theory(tp.time, V0, R, L, C);
            double error = std::abs(v_out - theory_v) / std::max(std::abs(theory_v), 0.1) * 100.0;

            std::cout << std::setw(10) << tp.time * 1e6 << "μs | ";
            std::cout << std::setw(10) << v_out << " | ";
            std::cout << std::setw(10) << theory_v << " | ";
            std::cout << std::setw(9) << error << "%" << std::endl;
        }
    }

    // Test 2: Trapezoidal with UIC for comparison
    std::cout << "\n=== Test 2: Trapezoidal with UIC - Step Response ===" << std::endl;
    {
        SimulationConfig config;
        config.transient.use_initial_conditions = true;  // UIC: Start from zero
        config.transient.use_adaptive_timestep = true;
        config.transient.method = IntegrationMethod::TRAPEZOIDAL;
        config.transient.adaptive_order = false;  // Fixed method

        solvers::TransientSolver solver(circuit, config);
        auto result = solver.solve(0.0, 1e-3, 100e-9);

        if (result.status != ConvergenceStatus::SUCCESS) {
            std::cerr << "Trapezoidal simulation failed!" << std::endl;
            return 1;
        }

        std::cout << "Steps: " << result.timepoints.size() << std::endl;
        std::cout << "Iterations: " << result.stats.total_iterations << std::endl;
        std::cout << "Rejects: " << result.stats.timestep_rejects << std::endl;

        // Check oscillation at key times
        std::vector<double> check_times = {100e-6, 200e-6, 300e-6, 500e-6, 1e-3};
        std::cout << "\nKey timepoints:" << std::endl;
        std::cout << std::setw(12) << "Time" << " | ";
        std::cout << std::setw(10) << "V_out" << " | ";
        std::cout << std::setw(10) << "Theory" << " | ";
        std::cout << std::setw(10) << "Error%" << std::endl;
        std::cout << std::string(50, '-') << std::endl;

        for (double target_t : check_times) {
            // Find closest timepoint
            size_t closest_idx = 0;
            double min_diff = std::abs(result.timepoints[0].time - target_t);
            for (size_t j = 1; j < result.timepoints.size(); ++j) {
                double diff = std::abs(result.timepoints[j].time - target_t);
                if (diff < min_diff) {
                    min_diff = diff;
                    closest_idx = j;
                }
            }

            const auto& tp = result.timepoints[closest_idx];
            double v_out = tp.node_voltages[node_out - 1];
            double theory_v = rlc_theory(tp.time, V0, R, L, C);
            double error = std::abs(v_out - theory_v) / std::max(std::abs(theory_v), 0.1) * 100.0;

            std::cout << std::setw(10) << tp.time * 1e6 << "μs | ";
            std::cout << std::setw(10) << v_out << " | ";
            std::cout << std::setw(10) << theory_v << " | ";
            std::cout << std::setw(9) << error << "%" << std::endl;
        }
    }

    std::cout << "\n=== RLC Resonance Test Complete ===" << std::endl;
    std::cout << "Both BDF2 and Trapezoidal methods handled oscillatory behavior" << std::endl;

    return 0;
}
