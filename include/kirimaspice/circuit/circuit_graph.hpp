// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/circuit/circuit_graph.hpp
#pragma once
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/devices/mosfet_level1.hpp"         // Unified MOSFET Level 1 (MOSFET_N/P)
#include "kirimaspice/devices/mosfet_level3_params.hpp"
#include "kirimaspice/devices/mosfet_bsim1_params.hpp"
#include "kirimaspice/devices/mosfet_bsim3_params.hpp"
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include "kirimaspice/devices/waveform.hpp"
#include "kirimaspice/devices/igbt.hpp"
#include <boost/graph/adjacency_list.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace kirimaspice {

// Forward declarations
namespace devices {
class Device;
class Resistor;
class Capacitor;
class Inductor;
class VoltageSource;
class CurrentSource;
class Diode;
class BJT_NPN;
class BJT_PNP;
// MOSFET_N and MOSFET_P are type aliases defined in mosfet_level1.hpp
class TLineLossless;
class TLineLossy;
}

// Forward declaration for SimulationConfig
struct SimulationConfig;

namespace circuit {

// Forward declaration
class SubcircuitRegistry;
class SubcircuitLineReader;

/// Node properties for Boost.Graph
struct NodeProperties {
    NodeId id;
    std::string name;
};

/// Edge properties for Boost.Graph
struct EdgeProperties {
    DeviceId device_id;
};

/// Circuit graph type (undirected graph)
using Graph = boost::adjacency_list<
    boost::vecS,        // OutEdgeList
    boost::vecS,        // VertexList
    boost::undirectedS, // Directed
    NodeProperties,     // VertexProperties
    EdgeProperties      // EdgeProperties
>;

/// Circuit graph representation
class CircuitGraph {
public:
    CircuitGraph();
    ~CircuitGraph();

    // Disable copy (due to unique_ptr members)
    CircuitGraph(const CircuitGraph&) = delete;
    CircuitGraph& operator=(const CircuitGraph&) = delete;

    // Enable move (defined in .cpp to allow incomplete SubcircuitRegistry type)
    CircuitGraph(CircuitGraph&&) noexcept;
    CircuitGraph& operator=(CircuitGraph&&) noexcept;

    // Node management
    /// Add or get node by name (returns NodeId)
    /// @param name Node name (e.g., "in", "out", "0" for GND)
    /// @return Node ID
    NodeId add_node(const std::string& name);

    /// Get node ID by name
    /// @throws std::runtime_error if node not found
    NodeId get_node_id(const std::string& name) const;

    /// Check if node exists
    bool has_node(const std::string& name) const;

    /// Get total number of nodes (including GND)
    std::uint32_t num_nodes() const;

    /// Get all node names
    std::vector<std::string> get_node_names() const;

    // Device management
    /// Add resistor
    /// @param name Device name (e.g., "R1")
    /// @param np Positive node name
    /// @param nn Negative node name
    /// @param resistance Resistance value [Ω]
    void add_resistor(const std::string& name,
                     const std::string& np,
                     const std::string& nn,
                     Real resistance);

    /// Add capacitor
    void add_capacitor(const std::string& name,
                      const std::string& np,
                      const std::string& nn,
                      Real capacitance);

    /// Add inductor
    void add_inductor(const std::string& name,
                     const std::string& np,
                     const std::string& nn,
                     Real inductance);

    /// Add voltage source
    /// @param dc_value DC voltage [V]
    /// @param ac_magnitude AC magnitude [V] (default: 1.0V for AC analysis)
    /// @param ac_phase AC phase [degrees] (default: 0.0)
    void add_voltage_source(const std::string& name,
                           const std::string& np,
                           const std::string& nn,
                           Real dc_value,
                           Real ac_magnitude = 1.0,  // Default AC amplitude 1.0V
                           Real ac_phase = 0.0);

    /// Add current source
    /// @param dc_value DC current [A]
    void add_current_source(const std::string& name,
                           const std::string& np,
                           const std::string& nn,
                           Real dc_value,
                           Real ac_magnitude = 0.0,
                           Real ac_phase = 0.0);

    /// Add behavioral source
    /// @param name Source name (e.g., "B1")
    /// @param np Positive terminal node name
    /// @param nn Negative terminal node name
    /// @param type VOLTAGE or CURRENT
    /// @param expression Expression to evaluate (e.g., "V(in)*2", "sin(2*pi*1000*TIME)")
    void add_behavioral_source(const std::string& name,
                              const std::string& np,
                              const std::string& nn,
                              BehavioralSourceType type,
                              const std::string& expression);

    /// Add sinusoidal voltage source: V(t) = offset + amplitude * sin(2*pi*freq*t + phase)
    /// @param name Source name (e.g., "V1")
    /// @param np Positive terminal node name
    /// @param nn Negative terminal node name
    /// @param offset DC offset voltage [V]
    /// @param amplitude AC amplitude [V]
    /// @param frequency Frequency [Hz]
    /// @param delay Time delay [s] (default: 0)
    /// @param damping Damping factor [1/s] (default: 0)
    /// @param phase Phase angle [degrees] (default: 0)
    void add_sin_voltage_source(const std::string& name,
                                const std::string& np,
                                const std::string& nn,
                                Real offset,
                                Real amplitude,
                                Real frequency,
                                Real delay = 0.0,
                                Real damping = 0.0,
                                Real phase = 0.0);

    /// Add pulse voltage source: rectangular pulse train
    /// @param name Source name (e.g., "V1")
    /// @param np Positive terminal node name
    /// @param nn Negative terminal node name
    /// @param v1 Initial value [V]
    /// @param v2 Pulsed value [V]
    /// @param delay Time delay [s]
    /// @param rise_time Rise time [s]
    /// @param fall_time Fall time [s]
    /// @param pulse_width Pulse width [s]
    /// @param period Period [s]
    void add_pulse_voltage_source(const std::string& name,
                                  const std::string& np,
                                  const std::string& nn,
                                  Real v1,
                                  Real v2,
                                  Real delay,
                                  Real rise_time,
                                  Real fall_time,
                                  Real pulse_width,
                                  Real period);

    /// Add piecewise linear voltage source
    /// @param name Source name (e.g., "V1")
    /// @param np Positive terminal node name
    /// @param nn Negative terminal node name
    /// @param points Time-voltage pairs (time in seconds, voltage in volts)
    void add_pwl_voltage_source(const std::string& name,
                                const std::string& np,
                                const std::string& nn,
                                const std::vector<std::pair<Real, Real>>& points);

    /// Add an independent source with a transient waveform: the DC value
    /// (dc_given false when the netlist gives none, so the waveform's value
    /// at time 0 stands for it), the AC input and the waveform
    /// (VoltageSource::set_waveform)
    void add_voltage_source_with_waveform(const std::string& name, const std::string& np,
                                          const std::string& nn, Real dc_value, bool dc_given,
                                          Real ac_magnitude, Real ac_phase,
                                          std::shared_ptr<const devices::Waveform> waveform);
    void add_current_source_with_waveform(const std::string& name, const std::string& np,
                                          const std::string& nn, Real dc_value, bool dc_given,
                                          Real ac_magnitude, Real ac_phase,
                                          std::shared_ptr<const devices::Waveform> waveform);

    /// Add diode
    /// @param name Device name (e.g., "D1")
    /// @param anode Anode node name
    /// @param cathode Cathode node name
    /// @param is Saturation current [A] (default: 1e-14)
    /// @param n Emission coefficient (default: 1.0)
    void add_diode(const std::string& name,
                  const std::string& anode,
                  const std::string& cathode,
                  Real is = 1e-14,
                  Real n = 1.0);

    /// Add BJT NPN transistor
    /// @param name Device name (e.g., "Q1")
    /// @param collector Collector node name
    /// @param base Base node name
    /// @param emitter Emitter node name
    /// @param is Saturation current [A] (default: 1e-14)
    /// @param beta_f Forward current gain (default: 100.0)
    /// @param n Emission coefficient (default: 1.0)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ff Flicker noise frequency exponent (default: 1.0)
    /// @param ise B-E leakage saturation current [A] (default: 0.0)
    /// @param ne B-E leakage emission coefficient (default: 1.5)
    /// @param isc_leak B-C leakage saturation current [A] (default: 0.0)
    /// @param nc B-C leakage emission coefficient (default: 2.0)
    void add_bjt_npn(const std::string& name,
                     const std::string& collector,
                     const std::string& base,
                     const std::string& emitter,
                     Real is = 1e-14,
                     Real beta_f = 100.0,
                     Real n = 1.0,
                     Real kf = 0.0,
                     Real af = 1.0,
                     Real ff = 1.0,
                     Real ise = 0.0,
                     Real ne = 1.5,
                     Real isc_leak = 0.0,
                     Real nc = 2.0);

    /// Add BJT PNP transistor
    /// @param name Device name (e.g., "Q2")
    /// @param collector Collector node name
    /// @param base Base node name
    /// @param emitter Emitter node name
    /// @param is Saturation current [A] (default: 1e-14)
    /// @param beta_f Forward current gain (default: 100.0)
    /// @param n Emission coefficient (default: 1.0)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ff Flicker noise frequency exponent (default: 1.0)
    /// @param ise E-B leakage saturation current [A] (default: 0.0)
    /// @param ne E-B leakage emission coefficient (default: 1.5)
    /// @param isc_leak C-B leakage saturation current [A] (default: 0.0)
    /// @param nc C-B leakage emission coefficient (default: 2.0)
    void add_bjt_pnp(const std::string& name,
                     const std::string& collector,
                     const std::string& base,
                     const std::string& emitter,
                     Real is = 1e-14,
                     Real beta_f = 100.0,
                     Real n = 1.0,
                     Real kf = 0.0,
                     Real af = 1.0,
                     Real ff = 1.0,
                     Real ise = 0.0,
                     Real ne = 1.5,
                     Real isc_leak = 0.0,
                     Real nc = 2.0);

    /// Add MOSFET N-channel transistor
    /// @param name Device name (e.g., "M1")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param kp Process transconductance [A/V²] (default: 2e-5)
    /// @param w Channel width [m] (default: 10e-6)
    /// @param l Channel length [m] (default: 1e-6)
    /// @param vth Threshold voltage [V] (default: 0.7)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param gamma Channel thermal noise coefficient (default: 0.667)
    void add_mosfet_n(const std::string& name,
                      const std::string& drain,
                      const std::string& gate,
                      const std::string& source,
                      Real kp = 2e-5,
                      Real w = 10e-6,
                      Real l = 1e-6,
                      Real vth = 0.7,
                      Real cgso = 0.0,
                      Real cgdo = 0.0,
                      Real cgbo = 0.0,
                      Real kf = 0.0,
                      Real af = 1.0,
                      Real ef = 1.0,
                      Real gamma = 0.667,
                      Real kt1 = 0.0,
                      Real kt2 = 0.0,
                      Real ute = -1.5,
                      Real tnom = 300.15);

    /// Add MOSFET P-channel transistor
    /// @param name Device name (e.g., "M2")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param kp Process transconductance [A/V²] (default: 2e-5)
    /// @param w Channel width [m] (default: 10e-6)
    /// @param l Channel length [m] (default: 1e-6)
    /// @param vth Threshold voltage [V] (default: -0.7, NEGATIVE for P-channel)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param gamma Channel thermal noise coefficient (default: 0.667)
    void add_mosfet_p(const std::string& name,
                      const std::string& drain,
                      const std::string& gate,
                      const std::string& source,
                      Real kp = 2e-5,
                      Real w = 10e-6,
                      Real l = 1e-6,
                      Real vth = -0.7,
                      Real cgso = 0.0,
                      Real cgdo = 0.0,
                      Real cgbo = 0.0,
                      Real kf = 0.0,
                      Real af = 1.0,
                      Real ef = 1.0,
                      Real gamma = 0.667,
                      Real kt1 = 0.0,
                      Real kt2 = 0.0,
                      Real ute = -1.5,
                      Real tnom = 300.15);

    /// Add MOSFET Level 3 N-channel transistor
    /// @param name Device name (e.g., "M1")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params Level 3 model parameters
    void add_mosfet_level3_n(const std::string& name,
                             const std::string& drain,
                             const std::string& gate,
                             const std::string& source,
                             const std::string& bulk,
                             const devices::MOSFETLevel3Params& params);

    /// Add MOSFET Level 3 P-channel transistor
    /// @param name Device name (e.g., "M2")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params Level 3 model parameters
    void add_mosfet_level3_p(const std::string& name,
                             const std::string& drain,
                             const std::string& gate,
                             const std::string& source,
                             const std::string& bulk,
                             const devices::MOSFETLevel3Params& params);

    /// Add MOSFET BSIM3v3 N-channel transistor
    /// @param name Device name (e.g., "M1")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params BSIM3 model parameters
    void add_mosfet_bsim3_n(const std::string& name,
                            const std::string& drain,
                            const std::string& gate,
                            const std::string& source,
                            const std::string& bulk,
                            const devices::BSIM3Params& params);

    /// Add MOSFET BSIM3v3 P-channel transistor
    /// @param name Device name (e.g., "M2")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params BSIM3 model parameters
    void add_mosfet_bsim3_p(const std::string& name,
                            const std::string& drain,
                            const std::string& gate,
                            const std::string& source,
                            const std::string& bulk,
                            const devices::BSIM3Params& params);

    /// Add MOSFET BSIM1 (Level 9) N-channel transistor
    /// @param name Device name (e.g., "M1")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params BSIM1 model parameters
    void add_mosfet_bsim1_n(const std::string& name,
                            const std::string& drain,
                            const std::string& gate,
                            const std::string& source,
                            const std::string& bulk,
                            const devices::MOSFETBsim1Params& params);

    /// Add MOSFET BSIM1 (Level 9) P-channel transistor
    /// @param name Device name (e.g., "M2")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params BSIM1 model parameters
    void add_mosfet_bsim1_p(const std::string& name,
                            const std::string& drain,
                            const std::string& gate,
                            const std::string& source,
                            const std::string& bulk,
                            const devices::MOSFETBsim1Params& params);

    /// Add MOSFET BSIM4.8 N-channel transistor
    /// @param name Device name (e.g., "M1")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params BSIM4 model parameters
    void add_mosfet_bsim4_n(const std::string& name,
                            const std::string& drain,
                            const std::string& gate,
                            const std::string& source,
                            const std::string& bulk,
                            const devices::BSIM4Params& params);

    /// Add MOSFET BSIM4.8 P-channel transistor
    /// @param name Device name (e.g., "M2")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param bulk Bulk/substrate node name
    /// @param params BSIM4 model parameters
    void add_mosfet_bsim4_p(const std::string& name,
                            const std::string& drain,
                            const std::string& gate,
                            const std::string& source,
                            const std::string& bulk,
                            const devices::BSIM4Params& params);

    // ========== SiC MOSFET ==========

    /// Add SiC MOSFET N-channel transistor (Silicon Carbide power device)
    /// @param name Device name (e.g., "MSiC1")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param kp Process transconductance [A/V²] (default: 2e-5)
    /// @param w Channel width [m] (default: 10e-6)
    /// @param l Channel length [m] (default: 1e-6)
    /// @param vth Threshold voltage [V] (default: 2.5, SiC: 2-4V typical)
    /// @param theta Mobility modulation [V⁻¹] (default: 0.1, SiC: 0.05-0.2)
    /// @param rdrift Drift region resistance [Ω] (default: 10e-3, SiC: 1mΩ-100mΩ)
    /// @param nfs Subthreshold fitting constant [cm⁻²V⁻¹] (default: 1e11)
    /// @param vbd Breakdown voltage [V] (default: 1200.0, SiC: 600-1700V)
    /// @param cgso G-S overlap capacitance per width [F/m] (default: 1e-9)
    /// @param cgdo G-D overlap capacitance per width [F/m] (default: 1e-9)
    /// @param cgbo G-B overlap capacitance per length [F/m] (default: 0.0)
    /// @param cjo Zero-bias junction capacitance [F] (default: 1e-9)
    /// @param pb Built-in potential [V] (default: 0.8, SiC: ~3V)
    /// @param mj Grading coefficient (default: 0.5)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param gamma Channel thermal noise coefficient (default: 0.667)
    /// @param kt1 VTO temperature coefficient [V/K] (default: -5e-3, SiC: -3 to -7 mV/K)
    /// @param kt2 VTO second-order temperature coefficient [V/K²] (default: 0.0)
    /// @param ute Mobility temperature exponent (default: -1.5, SiC: -1.5 to -2.0)
    /// @param rdtemp Drift resistance temperature exponent (default: 2.5, SiC: 2-3)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_mosfet_sic_n(const std::string& name,
                          const std::string& drain,
                          const std::string& gate,
                          const std::string& source,
                          Real kp = 2e-5,
                          Real w = 10e-6,
                          Real l = 1e-6,
                          Real vth = 2.5,
                          Real theta = 0.1,
                          Real rdrift = 10e-3,
                          Real nfs = 1e11,
                          Real vbd = 1200.0,
                          Real cgso = 1e-9,
                          Real cgdo = 1e-9,
                          Real cgbo = 0.0,
                          Real cjo = 1e-9,
                          Real pb = 0.8,
                          Real mj = 0.5,
                          Real kf = 0.0,
                          Real af = 1.0,
                          Real ef = 1.0,
                          Real gamma = 0.667,
                          Real kt1 = -5e-3,
                          Real kt2 = 0.0,
                          Real ute = -1.5,
                          Real rdtemp = 2.5,
                          Real tnom = 300.15);

    /// Add SiC MOSFET P-channel transistor (Silicon Carbide power device)
    /// @param name Device name (e.g., "MSiC2")
    /// @param drain Drain node name
    /// @param gate Gate node name
    /// @param source Source node name
    /// @param kp Process transconductance [A/V²] (default: 2e-5)
    /// @param w Channel width [m] (default: 10e-6)
    /// @param l Channel length [m] (default: 1e-6)
    /// @param vth Threshold voltage [V] (default: -2.5, NEGATIVE for P-channel)
    /// @param theta Mobility modulation [V⁻¹] (default: 0.1)
    /// @param rdrift Drift region resistance [Ω] (default: 10e-3)
    /// @param nfs Subthreshold fitting constant [cm⁻²V⁻¹] (default: 1e11)
    /// @param vbd Breakdown voltage [V] (default: 1200.0)
    /// @param cgso G-S overlap capacitance per width [F/m] (default: 1e-9)
    /// @param cgdo G-D overlap capacitance per width [F/m] (default: 1e-9)
    /// @param cgbo G-B overlap capacitance per length [F/m] (default: 0.0)
    /// @param cjo Zero-bias junction capacitance [F] (default: 1e-9)
    /// @param pb Built-in potential [V] (default: 0.8)
    /// @param mj Grading coefficient (default: 0.5)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param gamma Channel thermal noise coefficient (default: 0.667)
    /// @param kt1 VTO temperature coefficient [V/K] (default: -5e-3)
    /// @param kt2 VTO second-order temperature coefficient [V/K²] (default: 0.0)
    /// @param ute Mobility temperature exponent (default: -1.5)
    /// @param rdtemp Drift resistance temperature exponent (default: 2.5)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_mosfet_sic_p(const std::string& name,
                          const std::string& drain,
                          const std::string& gate,
                          const std::string& source,
                          Real kp = 2e-5,
                          Real w = 10e-6,
                          Real l = 1e-6,
                          Real vth = -2.5,
                          Real theta = 0.1,
                          Real rdrift = 10e-3,
                          Real nfs = 1e11,
                          Real vbd = 1200.0,
                          Real cgso = 1e-9,
                          Real cgdo = 1e-9,
                          Real cgbo = 0.0,
                          Real cjo = 1e-9,
                          Real pb = 0.8,
                          Real mj = 0.5,
                          Real kf = 0.0,
                          Real af = 1.0,
                          Real ef = 1.0,
                          Real gamma = 0.667,
                          Real kt1 = -5e-3,
                          Real kt2 = 0.0,
                          Real ute = -1.5,
                          Real rdtemp = 2.5,
                          Real tnom = 300.15);

    // ========== GaN HEMT ==========
    void add_gan_hemt_n(const std::string& name,
                        const std::string& drain,
                        const std::string& gate,
                        const std::string& source,
                        Real beta = 0.1,
                        Real vth = -2.0,
                        Real lambda = 0.01,
                        Real rs = 1e-3,
                        Real rd = 1e-3,
                        Real vbd = 1200.0,
                        Real trap = 0.0,
                        Real cgs = 1e-9,
                        Real cgd = 1e-10,
                        Real cds = 1e-11,
                        Real kf = 0.0,
                        Real af = 1.0,
                        Real ef = 1.0,
                        Real kt1 = -3e-3,
                        Real kt2 = 0.0,
                        Real ute = -1.0,
                        Real tnom = 300.15);

    void add_gan_hemt_p(const std::string& name,
                        const std::string& drain,
                        const std::string& gate,
                        const std::string& source,
                        Real beta = 0.1,
                        Real vth = 2.0,
                        Real lambda = 0.01,
                        Real rs = 1e-3,
                        Real rd = 1e-3,
                        Real vbd = 1200.0,
                        Real trap = 0.0,
                        Real cgs = 1e-9,
                        Real cgd = 1e-10,
                        Real cds = 1e-11,
                        Real kf = 0.0,
                        Real af = 1.0,
                        Real ef = 1.0,
                        Real kt1 = -3e-3,
                        Real kt2 = 0.0,
                        Real ute = -1.0,
                        Real tnom = 300.15);

    void add_finfet_n(const std::string& name,
                      const std::string& drain,
                      const std::string& gate,
                      const std::string& source,
                      int nfin = 1,
                      Real hfin = 30e-9,
                      Real tfin = 7e-9,
                      Real vth = 0.4,
                      Real u0 = 300e-4,
                      Real vsat = 1e5,
                      Real ss = 70e-3,
                      Real dibl = 0.05,
                      Real tox = 1e-9,
                      Real cgs = 1e-15,
                      Real cgd = 1e-16,
                      Real kf = 0.0,
                      Real af = 1.0,
                      Real kt1 = -1e-3,
                      Real tnom = 300.15,
                      Real l = 1e-6);

    void add_finfet_p(const std::string& name,
                      const std::string& drain,
                      const std::string& gate,
                      const std::string& source,
                      int nfin = 1,
                      Real hfin = 30e-9,
                      Real tfin = 7e-9,
                      Real vth = -0.4,
                      Real u0 = 250e-4,
                      Real vsat = 8e4,
                      Real ss = 75e-3,
                      Real dibl = 0.06,
                      Real tox = 1e-9,
                      Real cgs = 1e-15,
                      Real cgd = 1e-16,
                      Real kf = 0.0,
                      Real af = 1.0,
                      Real kt1 = 1e-3,
                      Real tnom = 300.15,
                      Real l = 1e-6);

    /// Add VDMOS N-channel power MOSFET (LEVEL=20)
    /// Netlist: M<name> <drain> <gate> <source> <model> [parameters]
    /// @param name Device name (e.g., "M1")
    /// @param drain Drain node
    /// @param gate Gate node
    /// @param source Source node
    /// @param vto Threshold voltage [V] (default: 4.0)
    /// @param kp Transconductance parameter [A/V²] (default: 20.0)
    /// @param rds On-resistance [Ω] (default: 0.1)
    /// @param vbd Breakdown voltage [V] (default: 600.0)
    /// @param lambda Channel length modulation [1/V] (default: 0.01)
    /// @param cgs Gate-source capacitance [F] (default: 3e-9)
    /// @param cgdmax Gate-drain max capacitance [F] (default: 1e-9)
    /// @param cgdmin Gate-drain min capacitance [F] (default: 1e-11)
    /// @param cds Drain-source capacitance [F] (default: 1e-10)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param gamma Channel thermal noise coefficient (default: 0.667)
    /// @param kt1 VTO temperature coefficient [V/K] (default: -3e-3)
    /// @param kt2 VTO second-order temperature coefficient [V/K²] (default: 0.0)
    /// @param ute Mobility temperature exponent (default: -1.5)
    /// @param rtc RDS temperature coefficient (default: 2.0)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_vdmos_n(const std::string& name,
                     const std::string& drain,
                     const std::string& gate,
                     const std::string& source,
                     Real vto = 4.0,
                     Real kp = 20.0,
                     Real rds = 0.1,
                     Real vbd = 600.0,
                     Real lambda = 0.01,
                     Real cgs = 3e-9,
                     Real cgdmax = 1e-9,
                     Real cgdmin = 1e-11,
                     Real cds = 1e-10,
                     Real kf = 0.0,
                     Real af = 1.0,
                     Real ef = 1.0,
                     Real gamma = 0.667,
                     Real kt1 = -3e-3,
                     Real kt2 = 0.0,
                     Real ute = -1.5,
                     Real rtc = 2.0,
                     Real tnom = 300.15);

    /// Add VDMOS P-channel power MOSFET (LEVEL=20)
    /// Netlist: M<name> <drain> <gate> <source> <model> [parameters]
    /// @param name Device name (e.g., "M2")
    /// @param drain Drain node
    /// @param gate Gate node
    /// @param source Source node
    /// @param vto Threshold voltage [V] (default: -4.0)
    /// @param kp Transconductance parameter [A/V²] (default: 10.0)
    /// @param rds On-resistance [Ω] (default: 0.2)
    /// @param vbd Breakdown voltage [V] (default: 600.0)
    /// @param lambda Channel length modulation [1/V] (default: 0.01)
    /// @param cgs Gate-source capacitance [F] (default: 2e-9)
    /// @param cgdmax Gate-drain max capacitance [F] (default: 8e-10)
    /// @param cgdmin Gate-drain min capacitance [F] (default: 8e-12)
    /// @param cds Drain-source capacitance [F] (default: 8e-11)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param gamma Channel thermal noise coefficient (default: 0.667)
    /// @param kt1 VTO temperature coefficient [V/K] (default: 3e-3)
    /// @param kt2 VTO second-order temperature coefficient [V/K²] (default: 0.0)
    /// @param ute Mobility temperature exponent (default: -1.5)
    /// @param rtc RDS temperature coefficient (default: 2.0)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_vdmos_p(const std::string& name,
                     const std::string& drain,
                     const std::string& gate,
                     const std::string& source,
                     Real vto = -4.0,
                     Real kp = 10.0,
                     Real rds = 0.2,
                     Real vbd = 600.0,
                     Real lambda = 0.01,
                     Real cgs = 2e-9,
                     Real cgdmax = 8e-10,
                     Real cgdmin = 8e-12,
                     Real cds = 8e-11,
                     Real kf = 0.0,
                     Real af = 1.0,
                     Real ef = 1.0,
                     Real gamma = 0.667,
                     Real kt1 = 3e-3,
                     Real kt2 = 0.0,
                     Real ute = -1.5,
                     Real rtc = 2.0,
                     Real tnom = 300.15);

    /// Add IGBT N-channel power transistor (LEVEL=21)
    /// Netlist: Q<name> <collector> <gate> <emitter> <model> [parameters]
    /// @param name Device name (e.g., "Q1")
    /// @param collector Collector node
    /// @param gate Gate node
    /// @param emitter Emitter node
    /// @param vge_th Gate-emitter threshold voltage [V] (default: 5.0)
    /// @param kp Transconductance parameter [A/V²] (default: 50.0)
    /// @param vce_sat Collector-emitter saturation voltage [V] (default: 2.0)
    /// @param tau Carrier lifetime / tail current time constant [s] (default: 1e-6)
    /// @param area Device area scaling factor (default: 1.0)
    /// @param cge Gate-emitter capacitance [F] (default: 5e-9)
    /// @param cce Collector-emitter capacitance [F] (default: 5e-10)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param kt1 VGE_TH temperature coefficient [V/K] (default: -2e-3)
    /// @param kt2 VGE_TH second-order temperature coefficient [V/K²] (default: 0.0)
    /// @param ute Mobility temperature exponent (default: -1.5)
    /// @param tcv VCE_SAT temperature coefficient [1/K] (default: 1e-3)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_igbt_n(const std::string& name,
                    const std::string& collector,
                    const std::string& gate,
                    const std::string& emitter,
                    Real vge_th = devices::IGBTDefaults::vge_th,
                    Real kp = devices::IGBTDefaults::kp,
                    Real vce_sat = devices::IGBTDefaults::vce_sat,
                    Real tau = devices::IGBTDefaults::tau,
                    Real area = devices::IGBTDefaults::area,
                    Real cge = devices::IGBTDefaults::cge,
                    Real cce = devices::IGBTDefaults::cce,
                    Real kf = devices::IGBTDefaults::kf,
                    Real af = devices::IGBTDefaults::af,
                    Real kt1 = devices::IGBTDefaults::kt1,
                    Real kt2 = devices::IGBTDefaults::kt2,
                    Real ute = devices::IGBTDefaults::ute,
                    Real tcv = devices::IGBTDefaults::tcv,
                    Real tnom = devices::IGBTDefaults::tnom);

    /// Add IGBT P-channel power transistor (LEVEL=21)
    /// Netlist: Q<name> <collector> <gate> <emitter> <model> [parameters]
    /// @param name Device name (e.g., "Q2")
    /// @param collector Collector node
    /// @param gate Gate node
    /// @param emitter Emitter node
    /// @param veg_th Emitter-gate threshold voltage [V] (default: 5.0, positive value)
    /// @param kp Transconductance parameter [A/V²] (default: 50.0)
    /// @param vce_sat Collector-emitter saturation voltage [V] (default: 2.0, positive value)
    /// @param tau Carrier lifetime / tail current time constant [s] (default: 1e-6)
    /// @param area Device area scaling factor (default: 1.0)
    /// @param ceg Emitter-gate capacitance [F] (default: 5e-9)
    /// @param cec Emitter-collector capacitance [F] (default: 5e-10)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param kt1 VEG_TH temperature coefficient [V/K] (default: -2e-3)
    /// @param kt2 VEG_TH second-order temperature coefficient [V/K²] (default: 0.0)
    /// @param ute Mobility temperature exponent (default: -1.5)
    /// @param tcv VCE_SAT temperature coefficient [1/K] (default: 1e-3)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_igbt_p(const std::string& name,
                    const std::string& collector,
                    const std::string& gate,
                    const std::string& emitter,
                    Real veg_th = devices::IGBTDefaults::vge_th,
                    Real kp = devices::IGBTDefaults::kp,
                    Real vce_sat = devices::IGBTDefaults::vce_sat,
                    Real tau = devices::IGBTDefaults::tau,
                    Real area = devices::IGBTDefaults::area,
                    Real ceg = devices::IGBTDefaults::cge,
                    Real cec = devices::IGBTDefaults::cce,
                    Real kf = devices::IGBTDefaults::kf,
                    Real af = devices::IGBTDefaults::af,
                    Real kt1 = devices::IGBTDefaults::kt1,
                    Real kt2 = devices::IGBTDefaults::kt2,
                    Real ute = devices::IGBTDefaults::ute,
                    Real tcv = devices::IGBTDefaults::tcv,
                    Real tnom = devices::IGBTDefaults::tnom);

    // ========== SCR (Silicon Controlled Rectifier / Thyristor) ==========

    /// Add SCR_N (N-type Silicon Controlled Rectifier / Thyristor)
    /// 2-transistor equivalent circuit model for AC control and phase-control applications
    /// Netlist: S<name> <anode> <cathode> <gate> <model> [parameters]
    /// @param name Device name (e.g., "S1")
    /// @param anode Anode node (positive terminal)
    /// @param cathode Cathode node (negative terminal)
    /// @param gate Gate node (control terminal)
    /// @param vbf Forward breakover voltage [V] (default: 400.0, 100-1200V typical)
    /// @param igt Gate trigger current [A] (default: 20e-3, 5-50mA typical)
    /// @param ih Holding current [A] (default: 10e-3, 5-50mA typical)
    /// @param il Latching current [A] (default: 30e-3, typically 2-3× IH)
    /// @param vgt Gate trigger voltage [V] (default: 1.0, 0.7-1.5V typical)
    /// @param ron On-state resistance [Ω] (default: 0.1, 0.01-1Ω typical)
    /// @param vtm On-state voltage drop [V] (default: 1.5, 1-2V typical)
    /// @param dv_dt dv/dt rating [V/μs] (default: 10.0, for snubber circuit design)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_scr_n(const std::string& name,
                   const std::string& anode,
                   const std::string& cathode,
                   const std::string& gate,
                   Real vbf = 400.0,
                   Real igt = 20e-3,
                   Real ih = 10e-3,
                   Real il = 30e-3,
                   Real vgt = 1.0,
                   Real ron = 0.1,
                   Real vtm = 1.5,
                   Real dv_dt = 10.0,
                   Real tnom = 300.15);

    /// Add SCR_P (P-type Silicon Controlled Rectifier / Thyristor)
    /// 2-transistor equivalent circuit model for AC control (reverse polarity)
    /// Netlist: S<name> <cathode> <anode> <gate> <model> [parameters]
    /// @param name Device name (e.g., "S2")
    /// @param cathode Cathode node (positive terminal for P-type)
    /// @param anode Anode node (negative terminal for P-type)
    /// @param gate Gate node (control terminal)
    /// @param vbf Forward breakover voltage [V] (default: 400.0, 100-1200V typical)
    /// @param igt Gate trigger current [A] (default: 20e-3, 5-50mA typical)
    /// @param ih Holding current [A] (default: 10e-3, 5-50mA typical)
    /// @param il Latching current [A] (default: 30e-3, typically 2-3× IH)
    /// @param vgt Gate trigger voltage [V] (default: 1.0, 0.7-1.5V typical)
    /// @param ron On-state resistance [Ω] (default: 0.1, 0.01-1Ω typical)
    /// @param vtm On-state voltage drop [V] (default: 1.5, 1-2V typical)
    /// @param dv_dt dv/dt rating [V/μs] (default: 10.0, for snubber circuit design)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_scr_p(const std::string& name,
                   const std::string& cathode,
                   const std::string& anode,
                   const std::string& gate,
                   Real vbf = 400.0,
                   Real igt = 20e-3,
                   Real ih = 10e-3,
                   Real il = 30e-3,
                   Real vgt = 1.0,
                   Real ron = 0.1,
                   Real vtm = 1.5,
                   Real dv_dt = 10.0,
                   Real tnom = 300.15);

    // ========== TRIAC ==========

    /// Add TRIAC (Triode for Alternating Current) - bidirectional thyristor
    /// Netlist: Y<name> <mt2> <mt1> <gate> [params]
    /// @param name Device name (e.g., "Y1")
    /// @param mt2 Main Terminal 2 node
    /// @param mt1 Main Terminal 1 node
    /// @param gate Gate node
    /// @param vbo Breakover voltage [V] (default: 400, 100-800V typical, both directions)
    /// @param igt Gate trigger current [A] (default: 20mA, 5-50mA typical, both directions)
    /// @param ih Holding current [A] (default: 10mA, 5-50mA typical, both directions)
    /// @param vgt Gate trigger voltage [V] (default: 1.0, 0.7-1.5V typical)
    /// @param ron On-state resistance [Ω] (default: 0.1, 0.01-1Ω typical)
    /// @param vtm On-state voltage drop [V] (default: 1.5, 1-2V typical)
    /// @param dv_dt dv/dt rating [V/μs] (default: 10.0, for snubber circuit design)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_triac(const std::string& name,
                   const std::string& mt2,
                   const std::string& mt1,
                   const std::string& gate,
                   Real vbo = 400.0,
                   Real igt = 20e-3,
                   Real ih = 10e-3,
                   Real vgt = 1.0,
                   Real ron = 0.1,
                   Real vtm = 1.5,
                   Real dv_dt = 10.0,
                   Real tnom = 300.15);

    // ========== MESFET ==========

    /// Add N-channel MESFET (Metal-Semiconductor FET)
    /// Netlist: Z<name> <drain> <gate> <source> <model_name> [params]
    /// @param name Device name (e.g., "Z1")
    /// @param drain Drain node
    /// @param gate Gate node
    /// @param source Source node
    /// @param model 1=Curtice, 2=Statz (default: 1)
    /// @param beta Transconductance parameter [A/V²] (default: 0.1, typical 0.01-0.5)
    /// @param vto Threshold voltage [V] (default: -2.0, negative for n-channel depletion)
    /// @param lambda Channel length modulation [1/V] (default: 0.05)
    /// @param alpha Saturation voltage parameter [1/V] (Curtice, default: 2.0)
    /// @param b Doping tail parameter (Statz, default: 0.3)
    /// @param vp Pinch-off voltage [V] (Statz, default: 3.0)
    /// @param cgs Gate-source capacitance [F] (default: 1e-12)
    /// @param cgd Gate-drain capacitance [F] (default: 0.5e-12)
    /// @param cds Drain-source capacitance [F] (default: 0.1e-12)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_mesfet_n(const std::string& name,
                      const std::string& drain,
                      const std::string& gate,
                      const std::string& source,
                      int model = 1,
                      Real beta = 0.1,
                      Real vto = -2.0,
                      Real lambda = 0.05,
                      Real alpha = 2.0,
                      Real b = 0.3,
                      Real vp = 3.0,
                      Real cgs = 1e-12,
                      Real cgd = 0.5e-12,
                      Real cds = 0.1e-12,
                      Real kf = 0.0,
                      Real af = 1.0,
                      Real ef = 1.0,
                      Real tnom = 300.15);

    /// Add P-channel MESFET (Metal-Semiconductor FET)
    /// Netlist: Z<name> <drain> <gate> <source> <model_name> [params]
    /// @param name Device name (e.g., "Z2")
    /// @param drain Drain node
    /// @param gate Gate node
    /// @param source Source node
    /// @param model 1=Curtice, 2=Statz (default: 1)
    /// @param beta Transconductance parameter [A/V²] (default: 0.1)
    /// @param vto Threshold voltage [V] (default: 2.0, positive for p-channel depletion)
    /// @param lambda Channel length modulation [1/V] (default: 0.05)
    /// @param alpha Saturation voltage parameter [1/V] (Curtice, default: 2.0)
    /// @param b Doping tail parameter (Statz, default: 0.3)
    /// @param vp Pinch-off voltage [V] (Statz, default: 3.0)
    /// @param cgs Gate-source capacitance [F] (default: 1e-12)
    /// @param cgd Gate-drain capacitance [F] (default: 0.5e-12)
    /// @param cds Drain-source capacitance [F] (default: 0.1e-12)
    /// @param kf Flicker noise coefficient (default: 0.0)
    /// @param af Flicker noise current exponent (default: 1.0)
    /// @param ef Flicker noise frequency exponent (default: 1.0)
    /// @param tnom Nominal temperature [K] (default: 300.15)
    void add_mesfet_p(const std::string& name,
                      const std::string& drain,
                      const std::string& gate,
                      const std::string& source,
                      int model = 1,
                      Real beta = 0.1,
                      Real vto = 2.0,
                      Real lambda = 0.05,
                      Real alpha = 2.0,
                      Real b = 0.3,
                      Real vp = 3.0,
                      Real cgs = 1e-12,
                      Real cgd = 0.5e-12,
                      Real cds = 0.1e-12,
                      Real kf = 0.0,
                      Real af = 1.0,
                      Real ef = 1.0,
                      Real tnom = 300.15);

    // ========== Controlled Sources ==========

    /// Add VCVS (Voltage-Controlled Voltage Source)
    /// Netlist: E<name> <n+> <n-> <nc+> <nc-> <gain>
    /// @param name Device name (e.g., "E1")
    /// @param np Positive output node
    /// @param nn Negative output node
    /// @param ncp Positive control node
    /// @param ncn Negative control node
    /// @param gain Voltage gain (dimensionless)
    void add_vcvs(const std::string& name,
                  const std::string& np,
                  const std::string& nn,
                  const std::string& ncp,
                  const std::string& ncn,
                  Real gain);

    /// Add CCCS (Current-Controlled Current Source)
    /// Netlist: F<name> <n+> <n-> V<control> <gain>
    /// @param name Device name (e.g., "F1")
    /// @param np Positive output node
    /// @param nn Negative output node
    /// @param control_vsource_name Name of controlling voltage source
    /// @param gain Current gain (dimensionless)
    void add_cccs(const std::string& name,
                  const std::string& np,
                  const std::string& nn,
                  const std::string& control_vsource_name,
                  Real gain);

    /// Add VCCS (Voltage-Controlled Current Source)
    /// Netlist: G<name> <n+> <n-> <nc+> <nc-> <transconductance>
    /// @param name Device name (e.g., "G1")
    /// @param np Positive output node
    /// @param nn Negative output node
    /// @param ncp Positive control node
    /// @param ncn Negative control node
    /// @param transconductance Transconductance [S] (Siemens)
    void add_vccs(const std::string& name,
                  const std::string& np,
                  const std::string& nn,
                  const std::string& ncp,
                  const std::string& ncn,
                  Real transconductance);

    /// Add CCVS (Current-Controlled Voltage Source)
    /// Netlist: H<name> <n+> <n-> V<control> <transresistance>
    /// @param name Device name (e.g., "H1")
    /// @param np Positive output node
    /// @param nn Negative output node
    /// @param control_vsource_name Name of controlling voltage source
    /// @param transresistance Transresistance [Ω] (Ohms)
    void add_ccvs(const std::string& name,
                  const std::string& np,
                  const std::string& nn,
                  const std::string& control_vsource_name,
                  Real transresistance);

    // ========== Mutual Inductance ==========

    /// Add Mutual Inductance (couples two inductors)
    /// Netlist: K<name> L<inductor1> L<inductor2> <coupling_coefficient>
    /// @param name Device name (e.g., "K1")
    /// @param inductor1_name Name of first inductor (e.g., "L1")
    /// @param inductor2_name Name of second inductor (e.g., "L2")
    /// @param coupling_coefficient Coupling coefficient k (0 <= k <= 1)
    void add_mutual_inductance(const std::string& name,
                               const std::string& inductor1_name,
                               const std::string& inductor2_name,
                               Real coupling_coefficient);

    /// Add lossless transmission line (T-line)
    /// @param name Device name (e.g., "T1")
    /// @param ap Port A positive node
    /// @param an Port A negative node
    /// @param bp Port B positive node
    /// @param bn Port B negative node
    /// @param z0 Characteristic impedance [Ω]
    /// @param td Propagation delay [s]
    void add_tline_lossless(const std::string& name,
                            const std::string& ap, const std::string& an,
                            const std::string& bp, const std::string& bn,
                            Real z0, Real td);

    void add_tline_lossy(const std::string& name,
                         const std::string& ap, const std::string& an,
                         const std::string& bp, const std::string& bn,
                         Real r_per_m, Real l_per_m, Real c_per_m, Real g_per_m,
                         Real td);

    /// Add lumped LC ladder transmission line model
    /// Expands T-line into N LC sections (π-type ladder)
    /// @param name Device name prefix (e.g., "T1")
    /// @param ap Port A positive node
    /// @param an Port A negative node
    /// @param bp Port B positive node
    /// @param bn Port B negative node
    /// @param z0 Characteristic impedance [Ω]
    /// @param td Propagation delay [s]
    /// @param num_sections Number of LC sections (default: 10)
    void add_tline_lumped(const std::string& name,
                         const std::string& ap, const std::string& an,
                         const std::string& bp, const std::string& bn,
                         Real z0, Real td,
                         int num_sections = 10);

    /// Get total number of devices
    std::uint32_t num_devices() const;

    /// Get all devices (const access)
    const std::vector<std::shared_ptr<devices::Device>>& get_devices() const;

    /// Get device by name
    /// @throws std::runtime_error if device not found
    std::shared_ptr<devices::Device> get_device(const std::string& name) const;

    /// Get all voltage sources (for branch current tracking)
    const std::vector<std::shared_ptr<devices::Device>>& get_voltage_sources() const {
        return voltage_sources_;
    }

    /// Get circuit title (first line of netlist)
    const std::string& title() const { return title_; }

    /// Set circuit title
    void set_title(const std::string& title) { title_ = title; }

    /// Resolve control voltage source references for CCCS and CCVS devices
    /// This must be called after all devices are added, before running simulation
    void resolve_controlled_source_references();

    /// Resolve inductor references for MutualInductance devices
    /// This must be called after all devices are added, before running simulation
    void resolve_mutual_inductance_references();

    /// Get Boost.Graph instance (for advanced operations)
    const Graph& graph() const { return graph_; }

    /// Set simulation configuration. Subcircuit expansion no longer reads it:
    /// the parser's SubcircuitLineReader holds the models and parameters
    /// (set_subcircuit_line_reader); the callers that still set it are kept.
    /// @param config Pointer to SimulationConfig
    void set_simulation_config(const SimulationConfig* config);

    /// The reader of subcircuit body lines (SubcircuitLineReader), which the
    /// netlist parser sets; expand_subcircuit_instance needs one
    void set_subcircuit_line_reader(std::shared_ptr<SubcircuitLineReader> reader) {
        subcircuit_line_reader_ = std::move(reader);
    }

    // Subcircuit management
    /// Get subcircuit registry
    SubcircuitRegistry& subcircuits();

    /// Get subcircuit registry (const)
    const SubcircuitRegistry& subcircuits() const;

    // Subcircuit expansion
    /// Expand a single subcircuit instance by name
    /// @param instance_name Instance name (e.g., "X1")
    /// @throws std::runtime_error if instance not found or expansion fails
    void expand_subcircuit_instance(const std::string& instance_name);

    /// Expand all subcircuit instances in the circuit
    /// Expands in dependency order (leaf subcircuits first)
    /// @throws std::runtime_error if circular reference detected or expansion fails
    void expand_all_subcircuits();

private:
    /// Every device enters the circuit here. Its name must not be in use, as
    /// in ngspice ("device already exists").
    /// @throws std::runtime_error if another device has the name
    void register_device(const std::string& name, const std::shared_ptr<devices::Device>& device);

    Graph graph_;                                           ///< Boost.Graph instance
    std::unordered_map<std::string, NodeId> node_map_;     ///< Node name → ID mapping
    std::vector<std::string> node_names_;                  ///< NodeId → name mapping
    std::vector<std::shared_ptr<devices::Device>> devices_; ///< Device list
    std::unordered_map<std::string, std::shared_ptr<devices::Device>> device_map_; ///< Device name → pointer
    std::vector<std::shared_ptr<devices::Device>> voltage_sources_; ///< Voltage sources, for branch current management
    std::string title_;                                    ///< Circuit title
    NodeId next_node_id_;                                  ///< Next node ID counter
    DeviceId next_device_id_;                              ///< Next device ID counter
    std::unique_ptr<SubcircuitRegistry> subcircuits_;      ///< Subcircuit registry
    const SimulationConfig* config_;                       ///< Simulation config (for model parameter access)
    std::shared_ptr<SubcircuitLineReader> subcircuit_line_reader_;  ///< Reads subcircuit body lines
};

} // namespace circuit
} // namespace kirimaspice
