// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/core/types.hpp
#pragma once

#include <complex>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "kirimaspice/util/string_utils.hpp"

namespace kirimaspice {

// Basic type definitions
using Real = double;                 ///< Real number type (default: double)
using Complex = std::complex<Real>;  ///< Complex number type
using NodeId = std::uint32_t;        ///< Node ID (0 = GND)
using DeviceId = std::uint32_t;      ///< Device ID
using BranchId = std::uint32_t;      ///< Branch ID (for voltage sources)
using ScopeId = std::uint32_t;       ///< Scope ID

/// Special node ID values
constexpr NodeId NODE_GND = 0;               ///< Ground node
/// Name of the ground node NODE_GND. At the top level no other name is ground;
/// output that leaves ground out tests for this name only.
inline constexpr std::string_view NODE_GND_NAME = "0";
/// True if a node name in a netlist means ground: "0", or "gnd" in any case,
/// at the top level as inside subcircuits (docs/spec/NETLIST_CONVENTIONS.md,
/// rule 5). Such a node is NODE_GND, named NODE_GND_NAME.
inline bool is_ground_name(std::string_view name) {
    return name == NODE_GND_NAME || util::equals_icase(name, "gnd");
}
constexpr NodeId NODE_INVALID = UINT32_MAX; ///< Invalid node
constexpr BranchId INVALID_BRANCH = UINT32_MAX; ///< Invalid branch

/// Simulation mode
enum class SimMode {
    DC,        ///< DC operating point analysis
    TRANSIENT, ///< Transient analysis
    AC,        ///< AC analysis (+)
    NOISE,     ///< Noise analysis (+)
};

/// Device type (extensible)
enum class DeviceType {
    RESISTOR,
    CAPACITOR,
    INDUCTOR,
    VSOURCE,   ///< Voltage source
    ISOURCE,   ///< Current source
    DIODE,     ///<
    BJT_NPN,   ///< (Ebers-Moll model)
    BJT_PNP,   ///< (Ebers-Moll model)
    BJT_GP_NPN, ///< N-channel BJT Gummel-Poon
    BJT_GP_PNP, ///< P-channel BJT Gummel-Poon
    MOSFET_N,  ///< (Level 1)
    MOSFET_P,  ///< (Level 1)
    MOSFET_LEVEL3_N, ///< N-channel MOSFET Level 3
    MOSFET_LEVEL3_P, ///< P-channel MOSFET Level 3
    JFET_N,    ///< N-channel JFET
    JFET_P,    ///< P-channel JFET
    VCVS,      ///< Voltage-controlled voltage source
    CCCS,      ///< Current-controlled current source
    VCCS,      ///< Voltage-controlled current source
    CCVS,      ///< Current-controlled voltage source
    MUTUAL_INDUCTANCE, ///< Mutual inductance
    MOSFET_BSIM3_N, ///< N-channel MOSFET BSIM3v3
    MOSFET_BSIM3_P, ///< P-channel MOSFET BSIM3v3
    TLINE_LOSSLESS, ///< Lossless transmission line
    TLINE_LOSSY,    ///< Lossy transmission line (O-element)
    MOSFET_BSIM4_N, ///< N-channel MOSFET BSIM4.8
    MOSFET_BSIM4_P, ///< P-channel MOSFET BSIM4.8
    BEHAVIORAL_SOURCE, ///< Behavioral voltage/current source
    MOSFET_SIC_N, ///< N-channel SiC MOSFET
    MOSFET_SIC_P, ///< P-channel SiC MOSFET
    GAN_HEMT_N, ///< N-channel GaN HEMT
    GAN_HEMT_P, ///< P-channel GaN HEMT
    FINFET_N,   ///< N-channel FinFET
    FINFET_P,   ///< P-channel FinFET
    VDMOS_N,    ///< N-channel VDMOS (Vertical DMOS)
    VDMOS_P,    ///< P-channel VDMOS (Vertical DMOS)
    IGBT_N,     ///< N-channel IGBT (Insulated Gate Bipolar Transistor)
    IGBT_P,     ///< P-channel IGBT (Insulated Gate Bipolar Transistor)
    SCR_N,      ///< N-type SCR (Silicon Controlled Rectifier / Thyristor)
    SCR_P,      ///< P-type SCR (Silicon Controlled Rectifier / Thyristor)
    MOSFET_BSIM1_N, ///< N-channel MOSFET BSIM1 (Level 9)
    MOSFET_BSIM1_P, ///< P-channel MOSFET BSIM1 (Level 9)
    MESFET_N,   ///< N-channel MESFET (Metal-Semiconductor FET)
    MESFET_P,   ///< P-channel MESFET (Metal-Semiconductor FET)
    TRIAC,      ///< TRIAC (Triode for Alternating Current) - bidirectional thyristor
};

/// Numerical integration method
enum class IntegrationMethod {
    BACKWARD_EULER, ///< Backward Euler (1st order)
    TRAPEZOIDAL,    ///< Trapezoidal rule (2nd order)
    GEAR2,          ///< 2nd order Gear
    GEAR3,          ///< 3rd order Gear
    TR_BDF2,        ///< TR-BDF2 (2nd order)
};

/// Variable-timestep BDF coefficients
/// Used for accurate integration with non-uniform timesteps in BDF2/BDF3 methods
struct BDFCoefficients {
    Real alpha0;  ///< Coefficient for V_{n+1}
    Real alpha1;  ///< Coefficient for V_n
    Real alpha2;  ///< Coefficient for V_{n-1}
    Real alpha3;  ///< Coefficient for V_{n-2} (BDF3 only)
    Real beta0;   ///< Effective timestep denominator
};

/// TR-BDF2 two-stage coefficients
/// Used for accurate TR-BDF2 integration with proper stage handling
struct TR_BDF2_Coefficients {
    int stage;           ///< Current stage: 1 (Trapezoidal) or 2 (Modified BDF2)
    Real gamma;          ///< Stage splitting parameter (2 - √2 ≈ 0.585786)
    Real stage_dt;       ///< Stage timestep: γΔt (stage 1) or (1-γ)Δt (stage 2)
    Real full_dt;        ///< Full timestep Δt

    // Stage 2 modified BDF2 coefficients (using V_n and V_{n+γ} as history)
    Real alpha0;  ///< Coefficient for V_{n+1}
    Real alpha1;  ///< Coefficient for V_{n+γ}
    Real alpha2;  ///< Coefficient for V_n
    Real beta0;   ///< Effective timestep denominator
};

/// Error estimation method
enum class ErrorEstimation {
    PHYSICAL,           ///< Physical quantity-based (charge/flux)
    PREDICTOR_CORRECTOR, ///< Predictor-corrector difference
    COMBINED,           ///< Maximum of both methods
};

/// Device model definition (.MODEL directive)
/// Stores parameters for BJT, MOSFET, Diode models
struct ModelDefinition {
    std::string name;                      ///< Model name (e.g., "2N3904", "1N4148")
    DeviceType device_type;                ///< Device type (BJT_NPN, BJT_PNP, DIODE, MOSFET_N, MOSFET_P)
    std::map<std::string, Real> params;    ///< Model parameters (e.g., "is", "bf", "vaf")
};

/// Measure type
/// Defines the type of measurement to perform on simulation results
enum class MeasureType {
    TRIG_TARG,  ///< Trigger-to-target delay measurement
    FIND,       ///< Find signal value at specific time/frequency
    WHEN,       ///< Find time/frequency when signal meets condition
    MAX,        ///< Maximum value
    MIN,        ///< Minimum value
    AVG,        ///< Average value
    RMS,        ///< Root-mean-square value
    PP,         ///< Peak-to-peak value (MAX - MIN)
    INTEG,      ///< Integral (area under curve)
    DERIV,      ///< Derivative at specific point
};

/// Edge detection type for TRIG/TARG/WHEN measurements
enum class MeasureEdge {
    CROSS,  ///< Any crossing of threshold (default)
    RISE,   ///< Rising edge only (signal crosses threshold upward)
    FALL,   ///< Falling edge only (signal crosses threshold downward)
};

/// Measure configuration
/// Defines a .measure statement for automatic waveform measurement
/// Examples:
///   .measure tran delay TRIG v(in) VAL=2.5 RISE=1 TARG v(out) VAL=2.5 RISE=1
///   .measure ac gain FIND vdb(out) AT=1kHz
///   .measure dc vmax MAX v(out) FROM=0 TO=5
struct MeasureConfig {
    std::string name;        ///< Measurement name (e.g., "delay", "gain")
    SimMode analysis_type;   ///< Analysis type (DC/TRAN/AC)
    MeasureType type;        ///< Measurement type

    // Signal specification
    std::string signal;           ///< Signal to measure (e.g., "v(out)", "i(V1)")
    bool is_db = false;           ///< Convert to dB (for AC analysis)
    bool is_phase = false;        ///< Convert to phase (for AC analysis)

    // Trigger specification (for TRIG_TARG, WHEN)
    std::string trig_signal;      ///< Trigger signal (for TRIG_TARG)
    Real trig_value = 0.0;        ///< Trigger threshold value
    MeasureEdge trig_edge = MeasureEdge::CROSS; ///< Trigger edge type
    int trig_occurrence = 1;      ///< Which occurrence (1=first, 2=second, etc.)

    // Target specification (for TRIG_TARG)
    std::string targ_signal;      ///< Target signal
    Real targ_value = 0.0;        ///< Target threshold value
    MeasureEdge targ_edge = MeasureEdge::CROSS; ///< Target edge type
    int targ_occurrence = 1;      ///< Which occurrence

    // Condition specification (for WHEN)
    Real when_value = 0.0;        ///< Threshold value for WHEN measurement
    MeasureEdge when_edge = MeasureEdge::CROSS; ///< Edge type for WHEN
    int when_occurrence = 1;      ///< Which occurrence

    // Range specification (optional)
    std::optional<Real> from; ///< Start of measurement range (time/frequency)
    std::optional<Real> to;   ///< End of measurement range (time/frequency)

    // AT specification (for FIND)
    std::optional<Real> at;   ///< Specific time/frequency for FIND measurement

    // Default values (for FIND when signal is not found)
    std::optional<Real> default_val; ///< Default value if measurement fails

    // Goal specification (for optimization)
    std::optional<Real> goal; ///< Target goal value
};

/// Parameter sweep type
enum class StepType {
    LIN,    ///< Linear sweep: .step param R 1k 10k 1k
    DEC,    ///< Decade logarithmic sweep: .step dec param C 1p 1u 10
    OCT,    ///< Octave logarithmic sweep: .step oct param L 1n 1m 5
    LIST    ///< Explicit value list: .step param R list 1k 2k 5k 10k
};

/// Parameter sweep configuration
/// Describes a parameter sweep where a global parameter is varied
/// Examples:
///   .step param R 1k 10k 1k          (LIN: R=1k, 2k, ..., 10k)
///   .step dec param C 1p 1u 10       (DEC: 10 points per decade)
///   .step oct param L 1n 1m 5        (OCT: 5 points per octave)
///   .step param R list 1k 2k 5k 10k  (LIST: explicit values)
struct StepConfig {
    std::string param_name;     ///< Parameter to sweep (e.g., "R_value", "tau")
    StepType sweep_type;        ///< Sweep type (LIN/DEC/OCT/LIST)

    // For LIN/DEC/OCT sweeps
    Real start_value;           ///< Starting value
    Real stop_value;            ///< Ending value
    Real step_value;            ///< Step size (LIN) or points per decade/octave (DEC/OCT)

    // For LIST sweep
    std::vector<Real> list_values;  ///< Explicit list of values

    /// Generate sweep points based on sweep type
    std::vector<Real> generate_points() const {
        std::vector<Real> points;

        if (sweep_type == StepType::LIST) {
            return list_values;
        }

        if (sweep_type == StepType::LIN) {
            // Linear sweep (improved: avoid floating-point accumulation error)
            if (step_value == 0.0) {
                points.push_back(start_value);
                return points;
            }

            // Calculate number of points first, then divide equally
            // This avoids floating-point accumulation errors
            int num = static_cast<int>(std::abs((stop_value - start_value) / step_value)) + 1;
            for (int i = 0; i < num; ++i) {
                Real value = start_value + step_value * i;
                // Stop if we've gone past the stop value (due to rounding)
                if ((step_value > 0 && value > stop_value + 1e-12) ||
                    (step_value < 0 && value < stop_value - 1e-12)) {
                    break;
                }
                points.push_back(value);
            }
            return points;
        }

        if (sweep_type == StepType::DEC) {
            // Decade logarithmic sweep (improved: support fractional decades)
            int num_points_per_decade = static_cast<int>(step_value);
            if (num_points_per_decade < 1) num_points_per_decade = 1;

            Real log_start = std::log10(start_value);
            Real log_stop = std::log10(stop_value);
            // Use floating-point decades to support fractional decades
            Real num_decades = std::abs(log_stop - log_start);
            int total_points = static_cast<int>(num_decades * num_points_per_decade) + 1;

            for (int i = 0; i < total_points; ++i) {
                Real log_val = log_start + (log_stop - log_start) * i / (total_points - 1);
                points.push_back(std::pow(10.0, log_val));
            }
            return points;
        }

        if (sweep_type == StepType::OCT) {
            // Octave logarithmic sweep (improved: support fractional octaves)
            int num_points_per_octave = static_cast<int>(step_value);
            if (num_points_per_octave < 1) num_points_per_octave = 1;

            Real log2_start = std::log2(start_value);
            Real log2_stop = std::log2(stop_value);
            // Use floating-point octaves to support fractional octaves
            Real num_octaves = std::abs(log2_stop - log2_start);
            int total_points = static_cast<int>(num_octaves * num_points_per_octave) + 1;

            for (int i = 0; i < total_points; ++i) {
                Real log2_val = log2_start + (log2_stop - log2_start) * i / (total_points - 1);
                points.push_back(std::pow(2.0, log2_val));
            }
            return points;
        }

        return points;
    }

    /// Number of points in this sweep
    std::size_t num_points() const {
        return generate_points().size();
    }
};

/// Temperature sweep configuration
/// Enables running analysis at multiple temperature points
/// Syntax: .TEMP temp1 temp2 temp3 ... (temperatures in Celsius)
/// Example: .TEMP -40 0 27 85 125
struct TemperatureSweep {
    std::vector<Real> temperatures;  ///< Temperature points [K] (converted from Celsius in parser)
    bool enabled = false;            ///< Whether temperature sweep is enabled
};

// ============================================================================
// Advanced analysis configurations (defined before SimulationConfig)
// ============================================================================

/// Sensitivity analysis configuration
/// Describes sensitivity analysis for DC operating point
/// Calculates ∂V_out/∂P for each device parameter P
struct SensitivityConfig {
    std::vector<std::string> output_nodes;  ///< Output nodes to analyze (e.g., {"out", "internal"})
    Real delta_relative = 1e-6;             ///< Relative perturbation (0.0001% default)
    Real delta_absolute = 1e-12;            ///< Absolute perturbation for near-zero values
    bool calculate_normalized = true;       ///< Calculate normalized sensitivity (dV/V)/(dP/P)
};

/// Monte Carlo distribution type
enum class DistributionType {
    UNIFORM,    ///< Uniform distribution [min, max]
    GAUSSIAN,   ///< Gaussian (normal) distribution N(μ, σ)
    LOGNORMAL,  ///< Log-normal distribution
};

/// Monte Carlo parameter variation
/// Describes statistical variation of a device parameter
struct MonteCarloVariation {
    std::string device_name;        ///< Device name (e.g., "R1", "*" for all resistors)
    std::string parameter_name;     ///< Parameter name (e.g., "resistance", "IS")
    DistributionType distribution;  ///< Distribution type
    Real nominal_value;             ///< Nominal value
    Real tolerance;                 ///< Tolerance (e.g., 0.05 for ±5%)
    Real sigma;                     ///< Standard deviation (for Gaussian)
    Real min_value;                 ///< Minimum value (for Uniform)
    Real max_value;                 ///< Maximum value (for Uniform)
};

/// Monte Carlo analysis configuration
struct MonteCarloConfig {
    std::vector<MonteCarloVariation> variations;  ///< Parameter variations
    std::uint32_t num_runs = 1000;                ///< Number of Monte Carlo runs
    std::uint32_t seed = 0;                       ///< Random seed (0 = use time)
    std::vector<std::string> output_nodes;        ///< Nodes to track (e.g., {"out"})
    bool calculate_yield = true;                  ///< Calculate yield (% passing spec)
    Real spec_min = -std::numeric_limits<Real>::infinity();  ///< Spec minimum
    Real spec_max = std::numeric_limits<Real>::infinity();   ///< Spec maximum
};

/// Control command types
/// Defines the types of commands that can appear in .CONTROL/.ENDC blocks
enum class ControlCommandType {
    // Interactive commands
    RUN,        ///< Run simulation (execute current analysis)
    PLOT,       ///< Plot waveform (graphical output)
    PRINT,      ///< Print data to file or console
    SET,        ///< Set variable or option
    LET,        ///< Compute expression and assign to variable
    ECHO,       ///< Print message to console
    QUIT,       ///< Quit/exit simulation
    IF,         ///< Conditional branch (if-else-end)
    ELSE,       ///< Else branch
    END,        ///< End of if or foreach block
    FOREACH,    ///< Loop over values (foreach-end)
    WHILE,      ///< While loop (while-end)
    REPEAT,     ///< Repeat loop (repeat-end)
    SOURCE,     ///< Source external script file
    // Analysis commands
    DC,         ///< dc <source> <start> <stop> <step>
    AC,         ///< ac <type> <npoints> <fstart> <fstop>
    TRAN,       ///< tran <tstep> <tstop> [<tstart>] [<tmax>]
    MEAS,       ///< meas <analysis> <name> <type> <signal> ...
    ALTER,      ///< alter <device> <param>=<value>
    UNKNOWN     ///< Unknown command (for future extension)
};

/// Single control command
/// Represents a single command within a .CONTROL/.ENDC block
/// Example: "run", "plot v(out)", "print v(out) > output.txt"
struct ControlCommand {
    ControlCommandType type;          ///< Command type
    std::string raw_command;          ///< Raw command string (for parsing/debugging)
    std::vector<std::string> args;    ///< Command arguments (tokenized)

    /// Optional: Output file for PRINT command
    std::optional<std::string> output_file;

    /// Optional: Condition for IF command (e.g., "v(out) > 2.5")
    std::optional<std::string> condition;

    /// Optional: Nested commands for IF/FOREACH blocks
    std::vector<ControlCommand> nested_commands;
};

/// Control block (.CONTROL/.ENDC) (extended)
/// Represents a complete .CONTROL/.ENDC block with all its commands
/// Example:
///   .CONTROL
///     run
///     plot v(out)
///     print v(out) > output.txt
///   .ENDC
struct ControlBlock {
    std::vector<ControlCommand> commands;  ///< All commands in the block
    bool enabled = true;                   ///< Enable/disable block execution

    /// Variables defined in control block (let statements)
    std::map<std::string, Real> variables;

    /// Check if block is empty
    bool empty() const { return commands.empty(); }

    /// Get number of commands
    std::size_t size() const { return commands.size(); }
};

//=============================================================================
// S-parameter Analysis
//=============================================================================

/// Behavioral source type
enum class BehavioralSourceType {
    VOLTAGE,  ///< V={expression}
    CURRENT,  ///< I={expression}
};

/// Behavioral source parameters
struct BehavioralSourceParams {
    BehavioralSourceType type;
    std::string expression;  ///< Expression to evaluate (e.g., "V(in)*2", "sin(2*pi*1000*TIME)")
};

/// Port definition for S-parameter analysis
/// Defines a port with characteristic impedance for RF/microwave circuits
struct PortDefinition {
    std::uint32_t port_number = 0;  ///< Port number (1-indexed)
    std::string node_positive;      ///< Positive terminal node name
    std::string node_negative = "0"; ///< Negative terminal node name (default: GND)
    Real impedance = 50.0;          ///< Characteristic impedance [Ω] (default: 50Ω)
};

/// S-parameter analysis configuration
struct SParameterConfig {
    std::vector<PortDefinition> ports;  ///< Port definitions
    Real start_freq = 1e6;              ///< Start frequency [Hz]
    Real stop_freq = 10e9;              ///< Stop frequency [Hz]
    std::uint32_t num_points = 100;     ///< Number of frequency points
    std::string sweep_type = "DEC";     ///< Sweep type: LIN/DEC/OCT
    bool enabled = false;               ///< Whether S-parameter analysis is enabled
};

/// S-parameter data at a single frequency point
struct SParameterPoint {
    Real frequency = 0.0;               ///< Frequency [Hz]
    std::vector<std::vector<Complex>> S_matrix;  ///< S-parameter matrix S[i][j]
};

/// S-parameter analysis result
/// Contains S-parameters across a frequency range
struct SParameterResult {
    std::vector<SParameterPoint> frequency_points;  ///< S-parameters at each frequency
    std::vector<PortDefinition> ports;  ///< Port definitions used
    bool success = false;               ///< Whether analysis completed successfully
};

// ============================================================================
// Worst-Case Analysis
// ============================================================================

/// Worst-case analysis configuration
/// Analyzes circuit performance across parameter variations to find worst-case scenarios
struct WorstCaseConfig {
    /// Analysis type for worst-case evaluation
    enum class AnalysisType {
        DC,     ///< DC operating point analysis
        TRAN,   ///< Transient analysis
        AC      ///< AC frequency response analysis
    };

    /// Objective function (maximize or minimize)
    enum class Objective {
        MAXIMIZE,  ///< Find maximum value of output variable
        MINIMIZE   ///< Find minimum value of output variable
    };

    /// Parameter variation type
    enum class VariationType {
        DEV,   ///< Device variation (process variation)
        LOT,   ///< Lot-to-lot variation
        BOTH   ///< Both device and lot variations
    };

    /// Corner case type
    enum class CornerCase {
        TYP,    ///< Typical case (nominal values)
        WORST,  ///< Worst-case corner
        BEST,   ///< Best-case corner
        ALL     ///< All corner combinations
    };

    /// Parameter variation specification
    struct ParameterVariation {
        std::string parameter_name;       ///< e.g., "R1.resistance", "Q1.BF"
        VariationType type = VariationType::DEV;
        Real deviation_percent = 10.0;    ///< ±X% variation from nominal
        Real min_value = 0.0;             ///< Minimum value (optional, overrides deviation)
        Real max_value = 0.0;             ///< Maximum value (optional, overrides deviation)
        bool use_absolute_limits = false; ///< Use min/max instead of deviation_percent
    };

    AnalysisType analysis_type = AnalysisType::DC;
    Objective objective = Objective::MAXIMIZE;
    std::string output_variable;          ///< e.g., "v(out)", "i(vload)"
    CornerCase corner_case = CornerCase::ALL;
    std::vector<ParameterVariation> variations;
    bool enabled = false;                 ///< Whether worst-case analysis is enabled
};

/// Worst-case analysis result for a single case
struct WorstCaseCaseResult {
    std::string case_name;                      ///< e.g., "R1_min,R2_max,Q1.BF_typ"
    std::map<std::string, Real> parameter_values;  ///< Actual parameter values used
    Real output_value = 0.0;                    ///< Value of output variable
    bool success = false;                       ///< Whether this case converged
};

/// Worst-case analysis result
/// Contains results for all corner cases and identifies worst/best cases
struct WorstCaseResult {
    std::vector<WorstCaseCaseResult> all_cases;  ///< Results for all corner combinations
    std::string worst_case_name;                 ///< Name of worst case
    Real worst_value = 0.0;                      ///< Worst-case output value
    std::map<std::string, Real> worst_parameter_values;  ///< Parameter values at worst case
    std::string best_case_name;                  ///< Name of best case
    Real best_value = 0.0;                       ///< Best-case output value
    std::map<std::string, Real> best_parameter_values;   ///< Parameter values at best case
    Real nominal_value = 0.0;                    ///< Nominal (typical) case value
    bool success = false;                        ///< Whether analysis completed successfully
};

// ============================================================================
// Circuit Optimization
// ============================================================================

/// Circuit optimization configuration
/// Uses numerical optimization to find optimal circuit parameter values
struct OptimizerConfig {
    /// Optimization algorithm
    enum class Algorithm {
        COBYLA,      ///< Constrained Optimization BY Linear Approximation (derivative-free)
        BOBYQA,      ///< Bound Optimization BY Quadratic Approximation (derivative-free)
        SLSQP,       ///< Sequential Least Squares Programming (gradient-based)
        NELDERMEAD   ///< Nelder-Mead simplex (derivative-free)
    };

    /// Optimization objective (maximize or minimize)
    enum class Objective {
        MINIMIZE,  ///< Minimize objective function
        MAXIMIZE   ///< Maximize objective function
    };

    /// Analysis type for optimization
    enum class AnalysisType {
        DC,    ///< DC operating point analysis
        TRAN,  ///< Transient analysis
        AC     ///< AC small-signal analysis
    };

    /// Parameter specification for optimization
    struct OptimizationParameter {
        std::string parameter_name;  ///< e.g., "R1.resistance", "C1.capacitance"
        Real initial_value = 1.0;    ///< Initial guess
        Real lower_bound = 0.0;      ///< Lower bound (box constraint)
        Real upper_bound = 1e6;      ///< Upper bound (box constraint)
    };

    /// Constraint specification
    struct Constraint {
        std::string measure_name;    ///< .MEASURE name to use as constraint
        Real lower_bound = -std::numeric_limits<Real>::infinity();  ///< Lower bound
        Real upper_bound = std::numeric_limits<Real>::infinity();   ///< Upper bound
    };

    /// NLopt-compatible tolerance specification
    struct Tolerances {
        Real xtol_rel = 1e-4;  ///< Relative tolerance on optimization parameters
        Real xtol_abs = 0.0;   ///< Absolute tolerance on optimization parameters (0 = not used)
        Real ftol_rel = 1e-4;  ///< Relative tolerance on objective function
        Real ftol_abs = 0.0;   ///< Absolute tolerance on objective function (0 = not used)
    };

    Algorithm algorithm = Algorithm::COBYLA;
    Objective objective = Objective::MINIMIZE;
    std::string objective_measure;               ///< .MEASURE name to optimize
    std::vector<OptimizationParameter> parameters;
    std::vector<Constraint> constraints;

    // Stopping criteria (NLopt-compatible)
    std::uint32_t max_evaluations = 1000;       ///< Maximum function evaluations
    Real max_time = 0.0;                        ///< Maximum optimization time [seconds] (0 = no limit)
    Tolerances tolerances;                       ///< NLopt-compatible tolerance settings

    // Legacy tolerance (deprecated, use tolerances.ftol_rel instead)
    Real tolerance = 1e-6;                      ///< Convergence tolerance (legacy, for backward compatibility)

    // Analysis type and parameters
    AnalysisType analysis_type = AnalysisType::DC;  ///< Type of analysis to run

    // Transient analysis parameters (used when analysis_type == TRAN)
    Real tran_start = 0.0;      ///< Start time [s]
    Real tran_stop = 1e-3;      ///< Stop time [s]
    Real tran_step = 1e-6;      ///< Time step [s]

    // AC analysis parameters (used when analysis_type == AC)
    Real ac_start_freq = 1.0;   ///< Start frequency [Hz]
    Real ac_stop_freq = 1e6;    ///< Stop frequency [Hz]
    std::uint32_t ac_points_per_decade = 10;  ///< Points per decade (for DEC sweep)

    bool enabled = false;                       ///< Whether optimization is enabled
};

/// Circuit optimization result
struct OptimizerResult {
    std::map<std::string, Real> optimal_parameters;  ///< Optimal parameter values
    Real optimal_value = 0.0;                        ///< Optimal objective function value
    std::uint32_t num_evaluations = 0;               ///< Number of function evaluations
    std::uint32_t num_iterations = 0;                ///< Number of iterations
    bool success = false;                            ///< Whether optimization converged
    std::string message;                             ///< Status message
};

/// DC sweep configuration
/// Describes a DC sweep analysis where a voltage or current source is varied
/// Note: DCSweepPoint and DCSweepResult are defined in matrix_types.hpp due to DenseVector dependency
struct DCSweepConfig {
    std::string source_name = "";  ///< Name of source to sweep (e.g., "V1", "I1")
    Real start_value = 0.0;         ///< Starting value [V or A]
    Real stop_value = 0.0;          ///< Ending value [V or A]
    Real step_value = 1.0;          ///< Step size [V or A]

    /// Optional nested sweep (for 2D sweeps)
    /// Example: .dc V1 0 5 0.5 V2 0 3 1
    std::shared_ptr<DCSweepConfig> nested_sweep = nullptr;

    /// Number of points in this sweep
    std::size_t num_points() const {
        if (step_value == 0.0) return 1;
        return static_cast<std::size_t>(std::abs((stop_value - start_value) / step_value)) + 1;
    }
};

/// Simulation configuration
struct SimulationConfig {
    // Common settings
    Real temperature = 300.15; ///< Temperature [K] (27°C)
    bool temperature_explicitly_set = false; ///< True if temperature was set via .TEMP or .OPTIONS TEMP
    Real gmin = 1e-12;         ///< Minimum conductance [S]

    // Initial conditions
    std::map<std::string, Real> nodeset;  ///< .NODESET V(node)=value (DC initial guess)
    std::map<std::string, Real> ic;       ///< .IC V(node)=value (Transient initial condition)

    // Global parameters and functions
    std::map<std::string, Real> global_params;  ///< .PARAM name=value (Global parameters)
    std::map<std::string, std::string> global_funcs;  ///< .FUNC name(args) {expr} (Global functions)

    // Global nodes
    std::set<std::string> global_nodes;  ///< .GLOBAL node1 node2 ... (Global nodes accessible across subcircuits)

    // Parameter sweeps
    std::vector<StepConfig> step_configs;  ///< .STEP parameter sweep configurations

    // Temperature sweep
    TemperatureSweep temp_sweep;  ///< .TEMP temperature sweep configuration

    // DC sweep
    DCSweepConfig dc_sweep;   ///< .DC sweep configuration (supports nested/2D sweeps)
    bool dc_sweep_enabled = false;  ///< Whether DC sweep is requested

    // Device models
    std::map<std::string, ModelDefinition> models;  ///< .MODEL definitions (model_name -> ModelDefinition)

    /// Find a .MODEL definition by name, ignoring case as SPICE does.
    /// Elements and subcircuit expansion look models up only through this.
    /// @return the definition, or nullptr if no model has that name
    const ModelDefinition* find_model(std::string_view name) const {
        if (auto it = models.find(std::string(name)); it != models.end()) {
            return &it->second;
        }
        for (const auto& [key, def] : models) {
            if (util::equals_icase(key, name)) {
                return &def;
            }
        }
        return nullptr;
    }

    // DC analysis settings
    struct DC {
        std::uint32_t max_iterations = 100;
        Real abstol = 1e-12; ///< Absolute tolerance [A or V]
        Real reltol = 1e-3;  ///< Relative tolerance
        Real vntol = 1e-6;   ///< Voltage tolerance [V]

        // Source Stepping
        bool enable_source_stepping = false;        ///< Enable source stepping for difficult circuits
        std::uint32_t source_stepping_steps = 10;   ///< Number of stepping iterations
        Real source_stepping_start = 0.01;          ///< Starting fraction (1% of final value)
        std::uint32_t source_stepping_max_iterations = 0; ///< Max iterations per step (ITL6, 0=use ITL1)
        std::uint32_t dc_sweep_max_iterations = 0;        ///< DC sweep max iterations per point (ITL2, 0=use ITL1)

        // Gmin Stepping
        bool enable_gmin_stepping = false;          ///< Enable Gmin stepping for difficult circuits
        std::uint32_t gmin_stepping_steps = 5;      ///< Number of Gmin stepping iterations
        Real gmin_start = 1e-6;                     ///< Starting Gmin conductance [S]
        Real gmin_end = 1e-12;                      ///< Final Gmin conductance [S] (same as global gmin)

        // Dynamic Gmin
        bool enable_dynamic_gmin = false;           ///< Enable adaptive Gmin adjustment (ngspice-style)
        Real dynamic_gmin_start = 1e-12;            ///< Initial dynamic Gmin [S]
        Real dynamic_gmin_min = 1e-12;              ///< Minimum dynamic Gmin [S]
        Real dynamic_gmin_max = 1e-3;               ///< Maximum dynamic Gmin [S]
        Real dynamic_gmin_increase_factor = 10.0;   ///< Multiplier when increasing Gmin after stalls
        Real dynamic_gmin_decrease_factor = 0.2;    ///< Multiplier when decreasing Gmin after improvement
        std::uint32_t dynamic_gmin_stall_iterations = 4; ///< Iterations without improvement before increase
        Real dynamic_gmin_improvement_threshold = 0.1;   ///< Required residual improvement ratio per iteration
        Real dynamic_gmin_residual_target = 1e-9;    ///< Residual level to aggressively relax Gmin

        // Continuation (Homotopy) method
        bool enable_continuation_method = false;     ///< Enable continuation after other methods
        std::uint32_t continuation_steps = 8;        ///< Number of continuation homotopy steps
        Real continuation_start = 0.05;              ///< Starting lambda (0..1)

        // Pseudo-transient fallback
        bool enable_pseudo_transient = false;        ///< Enable pseudo-transient after Newton failure
        Real pseudo_transient_dt = 1e-9;             ///< Artificial timestep [s]
        std::uint32_t pseudo_transient_steps = 30;   ///< Number of pseudo steps
        Real pseudo_transient_capacitance = 1e-12;   ///< Virtual capacitance to ground [F]

        // Voltage Limiting
        bool enable_voltage_limiting = true;        ///< Enable PN junction voltage limiting for better convergence
    } dc;

    // Transient analysis settings
    struct Transient {
        Real start_time = 0.0;         ///< Start time [s]
        Real end_time = 1e-3;          ///< End time [s]
        Real max_step = 1e-6;          ///< Maximum timestep [s]
        Real min_step = 1e-15;         ///< Minimum timestep [s] (ngspiceのdelmin相当を緩める)
        Real initial_step = 1e-9;      ///< Initial timestep [s]

        bool use_adaptive_timestep = false; ///< Enable adaptive timestep control
        bool use_initial_conditions = false; ///< UIC: Skip DC op-point, start from zero (for step response)

        IntegrationMethod method = IntegrationMethod::TRAPEZOIDAL;
        ErrorEstimation error_est = ErrorEstimation::PHYSICAL;

        // ngspice準拠のデフォルト値に緩和
        Real lte_abstol = 1e-6; ///< Local truncation error (absolute)
        Real lte_reltol = 1e-3; ///< Local truncation error (relative)
        Real trtol = 7.0;       ///< Truncation tolerance (ngspice TRTOL)
        Real chgtol = 1e-14;    ///< Charge tolerance [Coulombs] (ngspice CHGTOL)

        // Newton-Raphson convergence tolerances
        std::uint32_t max_iterations = 20; ///< Maximum Newton-Raphson iterations (ITL4)
        Real abstol = 1e-12;   ///< Absolute tolerance
        Real reltol = 1e-3;    ///< Relative tolerance
        Real vntol = 1e-6;     ///< Voltage tolerance

        std::uint32_t max_timesteps = 100000; ///< Maximum total timesteps (ITL5) - increased for complex adaptive circuits, prevents infinite loops

        std::uint32_t max_order = 3; ///< Maximum order (Gear methods) - increased from 2 to allow BDF3
        bool adaptive_order = true;  ///< Adaptive order control
        bool prefer_trapezoidal = false; ///< Use TRAPEZOIDAL instead of GEAR2 for order-2 (default: false = use GEAR2/BDF2)
    } transient;

    // AC analysis settings
    struct AC {
        /// Frequency sweep type
        enum class SweepType {
            LIN,  ///< Linear sweep (equal spacing)
            DEC,  ///< Decade sweep (logarithmic, 10x per decade)
            OCT   ///< Octave sweep (logarithmic, 2x per octave)
        };

        SweepType sweep_type = SweepType::DEC;  ///< Sweep type (default: decade)
        Real start_freq = 0.0;                  ///< Start frequency [Hz] (.ac 未指定時は 0)
        Real stop_freq = 0.0;                   ///< Stop frequency [Hz]
        std::uint32_t num_points = 100;         ///< Number of frequency points

        std::string input_source = "Vin";       ///< Input source name for AC stimulus
        Real input_magnitude = 1.0;             ///< AC input magnitude (V or A)
        Real input_phase = 0.0;                 ///< AC input phase [degrees]
    } ac;

    // Measure statements
    std::vector<MeasureConfig> measures;  ///< .measure statements to evaluate after simulation

    // Output control
    struct Output {
        std::vector<std::string> print_signals;  ///< .print specified signals (e.g., "v(out)", "i(v1)")
        std::vector<std::string> save_signals;   ///< .save specified signals (subset of print_signals for SSF)
        bool save_all = true;                    ///< Default: save all signals (no .save directive)
    } output;

    // Sensitivity analysis
    SensitivityConfig sensitivity;  ///< .sens sensitivity analysis configuration

    // Transfer function analysis
    struct TF {
        std::string output_signal;  ///< Output signal (e.g., "v(out)", "i(vload)")
        std::string input_source;   ///< Input source name (e.g., "Vin")
        bool enabled = false;       ///< Whether TF analysis is requested
    } tf;

    // Fourier analysis
    struct Fourier {
        Real fundamental_freq = 1000.0;  ///< Fundamental frequency [Hz]
        std::string signal;              ///< Signal to analyze (e.g., "v(out)")
        bool enabled = false;            ///< Whether Fourier analysis is requested
        std::uint32_t num_harmonics = 9; ///< Number of harmonics to calculate (default: 9)
    } fourier;

    // Pole-Zero analysis
    struct PZ {
        std::string node1;        ///< First node for transfer function (e.g., "out")
        std::string node2;        ///< Second node for transfer function (e.g., "0" for ground)
        std::string input_source; ///< Input source name (e.g., "Vin")
        enum class AnalysisType {
            POLES_ONLY,   ///< Calculate poles only
            ZEROS_ONLY,   ///< Calculate zeros only
            POLES_ZEROS   ///< Calculate both poles and zeros
        };
        AnalysisType type = AnalysisType::POLES_ZEROS;  ///< Analysis type
        bool enabled = false;  ///< Whether PZ analysis is requested
    } pz;

    // Distortion analysis
    struct Distortion {
        Real rload = 1000.0;           ///< Load resistance for distortion calculation [Ω]
        enum class AnalysisMode {
            HARMONIC,           ///< Harmonic distortion (HD2, HD3, THD)
            INTERMODULATION,    ///< Intermodulation distortion (IMD)
            BOTH                ///< Both harmonic and intermodulation
        };
        AnalysisMode mode = AnalysisMode::HARMONIC;  ///< Analysis mode
        Real f2_f1_ratio = 1.1;        ///< F2/F1 ratio for IMD (default: 1.1)
        Real skew = 1.0;               ///< Voltage ratio for IMD tones (default: 1.0)
        bool enabled = false;          ///< Whether distortion analysis is requested
    } distortion;

    // Monte Carlo analysis
    MonteCarloConfig monte_carlo;  ///< Monte Carlo analysis configuration

    // S-parameter analysis
    SParameterConfig sparam;  ///< S-parameter analysis configuration

    // Worst-case analysis
    WorstCaseConfig wcase;  ///< Worst-case analysis configuration

    // Circuit optimization
    OptimizerConfig optimizer;  ///< Circuit optimization configuration

    // Control block
    std::optional<ControlBlock> control_block;  ///< .CONTROL/.ENDC block (if present)

    // Logging settings
    struct Logging {
        std::string level = "info"; ///< trace/debug/info/warn/error
        bool console = true;
        std::string file_path = ""; ///< Empty = disabled
        bool noise_detail = false;  ///< Enable detailed noise contribution logs
    } logging;
};

/// Solver convergence status
enum class ConvergenceStatus {
    SUCCESS,            ///< Converged successfully
    MAX_ITERATIONS,     ///< Maximum iterations reached
    SINGULAR_MATRIX,    ///< Matrix is singular
    TIMESTEP_TOO_SMALL, ///< Timestep is too small
    NUMERICAL_ERROR,    ///< Numerical error
};

/// Solver statistics
struct SolverStats {
    std::uint64_t total_iterations = 0;
    std::uint64_t function_evals = 0;
    std::uint64_t jacobian_evals = 0;
    std::uint64_t linear_solves = 0;
    std::uint32_t timestep_rejects = 0;
    Real total_time_seconds = 0.0;
};

/// Noise source representation
/// Represents a single noise source in the circuit, either voltage or current.
/// Spectral density is in V²/Hz for voltage sources or A²/Hz for current sources.
struct NoiseSource {
    std::string name;        ///< Noise source identifier (e.g., "R1_thermal", "Q1_shot_ib")
    NodeId node_p;           ///< Positive terminal node
    NodeId node_n;           ///< Negative terminal node (0 for grounded)
    Real spectral_density;   ///< Noise spectral density (V²/Hz or A²/Hz)
    bool is_current;         ///< true: current source, false: voltage source
};

/// Fourier harmonic component
/// Represents a single harmonic component in the Fourier series
struct FourierHarmonic {
    std::uint32_t harmonic_number;  ///< Harmonic number (1=fundamental, 2=2nd, etc.)
    Real frequency;                 ///< Frequency [Hz]
    Real magnitude;                 ///< Magnitude (amplitude) [V or A]
    Real phase_degrees;             ///< Phase in degrees [°]
    Real normalized_mag;            ///< Magnitude normalized to fundamental (ratio)
    Real normalized_phase;          ///< Phase normalized to fundamental [°]
};

/// Fourier analysis result
/// Contains DC component, fundamental, and harmonics of a periodic signal
struct FourierResult {
    Real dc_component = 0.0;                    ///< DC component [V or A]
    Real fundamental_freq = 0.0;                ///< Fundamental frequency [Hz]
    std::vector<FourierHarmonic> harmonics;     ///< Harmonic components (1=fundamental, 2=2nd, ...)
    Real thd = 0.0;                             ///< Total Harmonic Distortion (%)
    std::string signal;                         ///< Signal analyzed (e.g., "v(out)")
    bool success = false;                       ///< Whether analysis succeeded
};

/// Complex pole or zero
/// Represents a pole or zero in the s-plane (Laplace domain)
struct ComplexPoleZero {
    std::complex<Real> value;  ///< Complex value (σ + jω)
    Real frequency_hz = 0.0;   ///< Natural frequency [Hz] (|s| / 2π)
    Real damping_ratio = 0.0;  ///< Damping ratio ζ (for conjugate pairs)
    bool is_real = true;       ///< Whether this is a real pole/zero (not complex conjugate pair)
};

/// Pole-Zero analysis result
/// Contains poles and zeros of the transfer function H(s)
struct PoleZeroResult {
    std::vector<ComplexPoleZero> poles;   ///< Poles of H(s) (denominator roots)
    std::vector<ComplexPoleZero> zeros;   ///< Zeros of H(s) (numerator roots)
    Real dc_gain = 0.0;                   ///< DC gain H(0)
    std::string transfer_function;        ///< Description (e.g., "V(out)/V(in)")
    bool success = false;                 ///< Whether analysis succeeded
};

/// Harmonic distortion component
/// Represents a single harmonic distortion measurement
struct HarmonicComponent {
    std::uint32_t harmonic_number;  ///< Harmonic number (2 = 2nd harmonic, 3 = 3rd, etc.)
    Real frequency_hz;              ///< Frequency of this harmonic [Hz]
    Real magnitude;                 ///< Magnitude of harmonic component [V or A]
    Real phase_deg;                 ///< Phase of harmonic component [degrees]
    Real hd_percent;                ///< Harmonic distortion percentage relative to fundamental
};

/// Distortion analysis result
/// Contains harmonic and intermodulation distortion measurements
struct DistortionResult {
    // Fundamental signal
    Real fundamental_freq = 0.0;    ///< Fundamental frequency [Hz]
    Real fundamental_mag = 0.0;     ///< Fundamental magnitude [V or A]

    // Harmonic distortion
    std::vector<HarmonicComponent> harmonics;  ///< Individual harmonic components
    Real hd2_percent = 0.0;         ///< 2nd harmonic distortion [%]
    Real hd3_percent = 0.0;         ///< 3rd harmonic distortion [%]
    Real thd_percent = 0.0;         ///< Total harmonic distortion [%]

    // Intermodulation distortion
    Real imd_percent = 0.0;         ///< Intermodulation distortion [%]
    Real f2_hz = 0.0;               ///< Second tone frequency [Hz]
    Real im2_freq_hz = 0.0;         ///< 2nd order IM frequency (f2 - f1) [Hz]
    Real im3_freq_hz = 0.0;         ///< 3rd order IM frequency (2*f1 - f2) [Hz]

    bool success = false;           ///< Whether analysis succeeded
};

/// Histogram data for Monte Carlo analysis
/// Represents binned statistical distribution of simulation results
struct HistogramData {
    std::vector<Real> bin_edges;    ///< Bin boundary values (size = num_bins + 1)
    std::vector<std::uint32_t> counts;  ///< Count in each bin (size = num_bins)
    Real bin_width = 0.0;           ///< Width of each bin (uniform binning)
    std::uint32_t num_bins = 20;    ///< Number of bins (default: 20)

    /// Calculate bin index for a given value
    int get_bin_index(Real value) const {
        if (bin_edges.empty() || value < bin_edges.front() || value > bin_edges.back()) {
            return -1;  // Out of range
        }
        // Binary search for bin
        for (size_t i = 0; i < bin_edges.size() - 1; ++i) {
            if (value >= bin_edges[i] && value < bin_edges[i + 1]) {
                return static_cast<int>(i);
            }
        }
        // Handle value equal to last edge (place in last bin)
        if (value == bin_edges.back()) {
            return static_cast<int>(bin_edges.size() - 2);
        }
        return -1;
    }
};

/// Statistical summary for Monte Carlo analysis
/// Contains key statistical measures for a distribution
struct MonteCarloStats {
    Real mean = 0.0;                ///< Mean value
    Real std_dev = 0.0;             ///< Standard deviation
    Real variance = 0.0;            ///< Variance (σ²)
    Real min = 0.0;                 ///< Minimum value
    Real max = 0.0;                 ///< Maximum value
    Real median = 0.0;              ///< Median (50th percentile)

    // Percentiles (for process capability analysis)
    Real percentile_1 = 0.0;        ///< 1st percentile
    Real percentile_5 = 0.0;        ///< 5th percentile
    Real percentile_25 = 0.0;       ///< 25th percentile (Q1)
    Real percentile_75 = 0.0;       ///< 75th percentile (Q3)
    Real percentile_95 = 0.0;       ///< 95th percentile
    Real percentile_99 = 0.0;       ///< 99th percentile

    // Six Sigma metrics
    Real sigma_level = 0.0;         ///< Process sigma level (Cpk equivalent)
};

/// Monte Carlo analysis result
/// Contains results from multiple simulation runs with parameter variations
struct MonteCarloResult {
    std::string output_node;        ///< Output node that was analyzed (e.g., "v(out)")
    std::uint32_t num_runs = 0;     ///< Number of simulation runs completed
    std::uint32_t num_converged = 0; ///< Number of runs that converged

    // Statistical summary
    MonteCarloStats stats;          ///< Statistical measures
    HistogramData histogram;        ///< Histogram distribution

    // Yield analysis
    Real spec_min = -std::numeric_limits<Real>::infinity();  ///< Specification minimum
    Real spec_max = std::numeric_limits<Real>::infinity();   ///< Specification maximum
    std::uint32_t num_passed = 0;   ///< Number of runs meeting spec
    std::uint32_t num_failed = 0;   ///< Number of runs failing spec
    Real yield_percent = 0.0;       ///< Yield percentage (passed / converged * 100)

    // Raw data (for post-processing)
    std::vector<Real> all_values;   ///< All output values from converged runs
    std::vector<std::map<std::string, Real>> parameter_samples;  ///< Parameter values for each run

    bool success = false;           ///< Whether analysis completed successfully
};

// ============================================================================
// BSIM4 Parameter Extraction
// ============================================================================

/// Single measurement point for BSIM4 parameter extraction
/// Contains terminal voltages and measured current
struct BSIM4MeasurementPoint {
    Real vgs = 0.0;         ///< Gate-source voltage [V]
    Real vds = 0.0;         ///< Drain-source voltage [V]
    Real vbs = 0.0;         ///< Bulk-source voltage [V]
    Real ids_measured = 0.0; ///< Measured drain current [A]
};

/// BSIM4 measurement dataset for parameter extraction
/// Contains I-V characteristic data points
struct BSIM4MeasurementData {
    std::vector<BSIM4MeasurementPoint> iv_data;  ///< I-V measurement points
    std::string device_name;                      ///< Device identifier
    bool is_nmos = true;                          ///< true: NMOS, false: PMOS
};

/// BSIM4 parameter extraction result
/// Contains extracted parameters and fitting quality metrics
struct BSIM4ParameterExtractionResult {
    std::map<std::string, Real> extracted_params;  ///< Extracted parameter values
    Real rms_error = 0.0;                          ///< RMS error between measured and simulated
    Real max_error = 0.0;                          ///< Maximum absolute error
    std::uint32_t num_evaluations = 0;             ///< Number of objective function evaluations
    std::uint32_t num_iterations = 0;              ///< Number of optimization iterations
    std::string message;                           ///< Status message
    bool success = false;                          ///< Whether extraction completed successfully
};

} // namespace kirimaspice
