// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/analysis/monte_carlo_executor.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/output/wav_writer.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"
#include "kirimaspice/solvers/fourier_analyzer.hpp"
#include "kirimaspice/solvers/parameter_sweep.hpp"
#include "kirimaspice/solvers/transfer_function_solver.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>

using namespace kirimaspice;

TEST(ACBoundaryConditions, SinglePointSweepsUseTheStartFrequency) {
    for (auto type : {SimulationConfig::AC::SweepType::LIN,
                      SimulationConfig::AC::SweepType::DEC,
                      SimulationConfig::AC::SweepType::OCT}) {
        circuit::CircuitGraph graph;
        graph.add_voltage_source("V1", "in", "0", 1.0);
        graph.add_resistor("R1", "in", "0", 1000.0);
        SimulationConfig config;
        config.ac.sweep_type = type;
        config.ac.start_freq = 10.0;
        config.ac.stop_freq = type == SimulationConfig::AC::SweepType::LIN ? 100.0 : 10.1;
        config.ac.num_points = 1;
        solvers::ACSolver solver(graph, config);
        const auto result = solver.solve();
        ASSERT_EQ(result.status, ConvergenceStatus::SUCCESS);
        ASSERT_EQ(result.frequency_points.size(), 1u);
        EXPECT_DOUBLE_EQ(result.frequency_points[0].frequency, 10.0);
        EXPECT_NEAR(result.frequency_points[0].solution[0].real(), 1.0, 1e-12);
        EXPECT_EQ(solver.solve().stats.linear_solves, result.stats.linear_solves);
    }
}

TEST(ACBoundaryConditions, SingularFrequencyDoesNotReportSuccess) {
    // A parallel LC tank has zero admittance at omega=1 for L=C=1.
    circuit::CircuitGraph graph;
    graph.add_current_source("I1", "0", "out", 0.0, 1.0);
    graph.add_capacitor("C1", "out", "0", 1.0);
    graph.add_inductor("L1", "out", "0", 1.0);
    SimulationConfig config;
    config.gmin = 0.0;
    config.ac.start_freq = config.ac.stop_freq = 1.0 / (2.0 * std::numbers::pi);
    solvers::ACSolver solver(graph, config);
    EXPECT_NE(solver.solve().status, ConvergenceStatus::SUCCESS);
}

TEST(DCBoundaryConditions, SteppingRequiresPointsAndSupportsOnePoint) {
    for (bool source_stepping : {false, true}) {
        for (unsigned steps : {0u, 1u}) {
            circuit::CircuitGraph graph;
            graph.add_voltage_source("V1", "in", "0", 1.0);
            graph.add_resistor("R1", "in", "0", 1000.0);
            SimulationConfig config;
            config.dc.enable_source_stepping = source_stepping;
            config.dc.enable_gmin_stepping = !source_stepping;
            config.dc.source_stepping_steps = config.dc.gmin_stepping_steps = steps;
            solvers::DCSolver solver(graph, config);
            DenseVector solution;
            SolverStats stats;
            const auto status = solver.solve(solution, stats);
            if (steps == 0) {
                EXPECT_EQ(status, ConvergenceStatus::NUMERICAL_ERROR);
                EXPECT_TRUE(solution.empty());
            } else {
                ASSERT_EQ(status, ConvergenceStatus::SUCCESS);
                ASSERT_FALSE(solution.empty());
                EXPECT_NEAR(solution[0], 1.0, 1e-9);
            }
        }
    }
}

TEST(DCBoundaryConditions, FailedSweepsHaveNoFabricatedSolution) {
    circuit::CircuitGraph graph;
    graph.add_voltage_source("V1", "in", "0", 2.0);
    graph.add_resistor("R1", "in", "0", 1000.0);
    SimulationConfig config;
    config.dc.max_iterations = 0;
    config.temp_sweep.temperatures = {280.0, 300.0};
    solvers::DCSolver solver(graph, config);
    DCSweepConfig sweep;
    sweep.source_name = "V1";
    sweep.start_value = 0.0;
    sweep.stop_value = 1.0;
    sweep.step_value = 0.5;
    DCSweepResult result;
    EXPECT_NE(solver.solve_sweep(sweep, result), ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.points.size(), 3u);
    for (const auto& point : result.points) {
        EXPECT_TRUE(point.node_voltages.empty());
        EXPECT_TRUE(point.branch_currents.empty());
    }
    auto source = std::dynamic_pointer_cast<devices::VoltageSource>(graph.get_device("V1"));
    ASSERT_TRUE(source);
    EXPECT_DOUBLE_EQ(source->dc_value(), 2.0);
    TemperatureSweepResult temperatures;
    EXPECT_NE(solver.solve_temp_sweep(temperatures), ConvergenceStatus::SUCCESS);
    ASSERT_EQ(temperatures.points.size(), 2u);
    for (const auto& point : temperatures.points) {
        EXPECT_TRUE(point.node_voltages.empty());
        EXPECT_TRUE(point.branch_currents.empty());
    }
}

TEST(DCBoundaryConditions, MissingNestedSourceRestoresPrimary) {
    circuit::CircuitGraph graph;
    graph.add_voltage_source("V1", "in", "0", 2.0);
    graph.add_resistor("R1", "in", "0", 1000.0);
    solvers::DCSolver solver(graph, {});
    DCSweepConfig sweep;
    sweep.source_name = "V1";
    sweep.start_value = 0.0;
    sweep.stop_value = 1.0;
    sweep.step_value = 0.5;
    sweep.nested_sweep = std::make_shared<DCSweepConfig>();
    sweep.nested_sweep->source_name = "missing";
    DCSweepResult result;
    EXPECT_NE(solver.solve_sweep(sweep, result), ConvergenceStatus::SUCCESS);
    EXPECT_DOUBLE_EQ(std::dynamic_pointer_cast<devices::VoltageSource>(graph.get_device("V1"))->dc_value(), 2.0);
}

TEST(DCBranchCurrents, IncludesVoltageBehavioralSources) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("B source branches\nV1 in 0 1\nB1 out 0 V=2*V(in)\nR1 out 0 1k\n.end\n");
    solvers::DCSolver solver(*parsed.circuit, parsed.config);
    EXPECT_FALSE(solver.get_branch_current("B1"));
    DenseVector solution;
    SolverStats stats;
    ASSERT_EQ(solver.solve(solution, stats), ConvergenceStatus::SUCCESS);
    const auto current = solver.get_branch_current("B1");
    ASSERT_TRUE(current);
    EXPECT_FALSE(solver.get_branch_current("missing"));
    EXPECT_FALSE(solver.get_branch_current("R1"));
    EXPECT_NEAR(*current, -0.002, 1e-9); // I into positive terminal = -V/R.
    DCSweepConfig sweep;
    sweep.source_name = "V1";
    sweep.start_value = sweep.stop_value = 1.0;
    sweep.step_value = 1.0;
    DCSweepResult result;
    ASSERT_EQ(solver.solve_sweep(sweep, result), ConvergenceStatus::SUCCESS);
    ASSERT_EQ(result.points[0].branch_currents.size(), 2u);
    EXPECT_NEAR(result.points[0].branch_currents[1], -0.002, 1e-9);
}

class AnalysisDeckTest : public ::testing::Test {
protected:
    std::filesystem::path directory;
    void SetUp() override {
        directory = std::filesystem::temp_directory_path() /
            ("kirimaspice_analysis_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
    }
    void TearDown() override { std::filesystem::remove_all(directory); }
    std::string write(const std::string& name, const std::string& text) {
        const auto path = directory / name;
        std::ofstream(path) << text;
        return path.string();
    }
};

TEST_F(AnalysisDeckTest, ParameterSweepsExpandSubcircuitsAndRelativeIncludes) {
    write("parts.inc", ".subckt divider in out\nR1 in out {rv}\nR2 out 0 {rl}\n.ends divider\n");
    for (const std::string steps : {"", ".step param rv 1k 2k 1k\n",
             ".step param rv 1k 2k 1k\n.step param rl 1k 2k 1k\n"}) {
        const auto deck = write("sweep.cir", "Sweep include\n.param rv=1k rl=1k\n" + steps +
            ".include parts.inc\nV1 in 0 3\nX1 in out divider\n.op\n.end\n");
        parser::NetlistParser parser;
        auto parsed = parser.parse_file(deck);
        solvers::ParameterSweepExecutor executor(parsed.config);
        const auto result = executor.execute_dc_sweep(deck);
        ASSERT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
        EXPECT_EQ(result.num_success, executor.get_total_sweep_points());
        parsed.circuit->expand_all_subcircuits();
        const auto output = parsed.circuit->get_node_id("out") - 1;
        for (const auto& point : result.sweep_points) {
            const auto& values = point.param_values;
            const Real rv = values.contains("rv") ? values.at("rv") : 1000.0;
            const Real rl = values.contains("rl") ? values.at("rl") : 1000.0;
            ASSERT_GT(point.solution.size(), output);
            EXPECT_NEAR(point.solution[output], 3.0 * rl / (rv + rl), 1e-8);
        }
    }
}

TEST_F(AnalysisDeckTest, SweepParseErrorsAreReturnedToTheCaller) {
    SimulationConfig config;
    StepConfig step{};
    step.sweep_type = StepType::LIN;
    step.param_name = "rv";
    step.start_value = 1.0;
    step.stop_value = 2.0;
    step.step_value = 1.0;
    config.step_configs.push_back(step);
    solvers::ParameterSweepExecutor executor(config);
    EXPECT_THROW(executor.execute_dc_sweep((directory / "missing.cir").string()), std::exception);
}

TEST_F(AnalysisDeckTest, MonteCarloUsesTheSampledDeviceValueInBothPaths) {
    // The 3 V divider gives 1 V for R1=2k, R2=1k (nominal R1=1k gives 1.5 V).
    const auto deck = write("mc.cir", "MC divider\n.subckt divider in out\nR1 in out 1k\nR2 out 0 1k\n.ends\nV1 in 0 3\nX1 in out divider\n.op\n.end\n");
    SimulationConfig config;
    config.monte_carlo.num_runs = 65;
    config.monte_carlo.seed = 42;
    MonteCarloVariation variation{};
    variation.device_name = "X1.R1";
    variation.parameter_name = "resistance";
    variation.distribution = DistributionType::UNIFORM;
    variation.min_value = variation.max_value = 2000.0;
    config.monte_carlo.variations.push_back(variation);
    analysis::MonteCarloExecutor executor(config);
    for (bool fast : {false, true}) {
        const auto result = fast ? executor.execute_dc_monte_carlo_fast(deck, "out") :
                                   executor.execute_dc_monte_carlo(deck, "out");
        ASSERT_TRUE(result.success);
        EXPECT_EQ(result.num_converged, config.monte_carlo.num_runs);
        for (Real value : result.all_values) EXPECT_NEAR(value, 1.0, 1e-8);
    }
}

TEST_F(AnalysisDeckTest, MonteCarloErrorsReachTheCaller) {
    const auto deck = write("mc.cir", "MC errors\nV1 in 0 1\nR1 in out 1k\nR2 out 0 1k\n.end\n");
    SimulationConfig config;
    config.monte_carlo.num_runs = 4;
    config.monte_carlo.seed = 42;
    MonteCarloVariation variation{};
    variation.device_name = "R1";
    variation.parameter_name = "resistance";
    variation.distribution = DistributionType::UNIFORM;
    variation.min_value = variation.max_value = 2000.0;
    config.monte_carlo.variations.push_back(variation);
    for (bool fast : {false, true}) {
        analysis::MonteCarloExecutor executor(config);
        auto run = [&](const std::string& path, const std::string& output) {
            return fast ? executor.execute_dc_monte_carlo_fast(path, output) :
                          executor.execute_dc_monte_carlo(path, output);
        };
        EXPECT_THROW(run(deck, "missing"), std::exception);
        EXPECT_THROW(run((directory / "missing.cir").string(), "out"), std::exception);
    }
    config.monte_carlo.variations[0].parameter_name = "invalid_parameter";
    analysis::MonteCarloExecutor executor(config);
    EXPECT_THROW(executor.execute_dc_monte_carlo(deck, "out"), std::exception);
    EXPECT_THROW(executor.execute_dc_monte_carlo_fast(deck, "out"), std::exception);
}

TEST(FourierBoundaryConditions, InterpolatesTheStartOfTheLastPeriod) {
    // The average of the constant 2 over a complete period is exactly 2,
    // including when the beginning of that period lies between samples.
    const auto result = FourierAnalyzer::analyze({0.0, 0.3, 0.7, 1.1}, {2.0, 2.0, 2.0, 2.0}, 1.0, "v(out)");
    ASSERT_TRUE(result.success);
    EXPECT_NEAR(result.dc_component, 2.0, 1e-12);
    EXPECT_FALSE(FourierAnalyzer::analyze({0.0, 0.5, 0.5, 1.0}, {1.0, 1.0, 1.0, 1.0}, 1.0, "v(out)").success);
}

TEST(TransferFunctionBoundaryConditions, InvalidOutputDoesNotIndexTheSolution) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("TF output\nV1 in 0 1\nR1 in 0 1k\n.end\n");
    for (const std::string output : {"v(0)", "v(missing)"}) {
        parsed.config.tf.output_signal = output;
        parsed.config.tf.input_source = "V1";
        TransferFunctionSolver solver(*parsed.circuit, parsed.config);
        EXPECT_FALSE(solver.analyze().success);
    }
}

TEST(TransferFunctionBoundaryConditions, RestoresWaveformOnlySource) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string("TF waveform\nV1 in 0 SIN(2 1 1k)\nR1 in out 1k\nR2 out 0 1k\n.tf v(out) V1\n.end\n");
    auto source = std::dynamic_pointer_cast<devices::VoltageSource>(parsed.circuit->get_device("V1"));
    ASSERT_TRUE(source);
    const auto before = source->dc_setting();
    TransferFunctionSolver solver(*parsed.circuit, parsed.config);
    const auto result = solver.analyze();
    ASSERT_TRUE(result.success);
    EXPECT_NEAR(result.transfer_gain, 0.5, 1e-7);
    EXPECT_NEAR(result.input_resistance, 2000.0, 1e-5);
    EXPECT_EQ(source->dc_setting().given, before.given);
    EXPECT_DOUBLE_EQ(source->dc_setting().value, before.value);
    EXPECT_DOUBLE_EQ(source->present_value(), 2.0);
}

TEST(WavBoundaryConditions, RejectsZeroSampleRate) {
    EXPECT_THROW(output::WavWriter("unused.wav", 0), std::invalid_argument);
}
