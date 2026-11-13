// include/kirimaspice/devices/triac.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"

namespace kirimaspice::devices {

/// TRIAC (Triode for Alternating Current)
/// Bidirectional thyristor for AC control and phase-control applications
///
/// Device structure:
/// - 3 terminals: MT1 (Main Terminal 1), MT2 (Main Terminal 2), Gate (G)
/// - Bidirectional pnpn structure (two antiparallel SCRs)
/// - Latching behavior in both directions: once triggered, remains ON until current drops below IH
///
/// Operating regions:
/// 1. Blocking (|Vmt| < VBO and |Ig| < IGT):
///    Imt = 0 (OFF state, blocking voltage in both directions)
///
/// 2. Triggering in 4 quadrants:
///    Q1+ (MT2+, G+): Positive Vmt, positive Ig → latching in positive direction
///    Q1- (MT2+, G-): Positive Vmt, negative Ig → latching in positive direction
///    Q3+ (MT2-, G+): Negative Vmt, positive Ig → latching in negative direction
///    Q3- (MT2-, G-): Negative Vmt, negative Ig → latching in negative direction
///
/// 3. Latched ON (|Imt| >= IH):
///    Conducting state, gate signal no longer needed
///    Imt = (Vmt - sign(Vmt)*VTM) / RON  (diode-like characteristic in both directions)
///
/// 4. Turn-off (|Imt| < IH):
///    Latching released, returns to blocking state
///
/// Key features:
///   - Bidirectional voltage blocking: 100V-800V typical
///   - Bidirectional current capability: 1A-40A+
///   - Latching behavior in both directions (AC control)
///   - AC phase control (dimming, heating, motor speed control)
///   - Low ON-state voltage drop: 1-2V
///
/// Key parameters:
///   VBO = breakover voltage [V] (100-800V typical, both directions)
///   IGT = gate trigger current [A] (5-50mA typical, both directions)
///   IH  = holding current [A] (5-50mA typical, both directions)
///   VGT = gate trigger voltage [V] (0.7-1.5V typical)
///   RON = on-state resistance [Ω] (0.01-1Ω typical)
///   VTM = on-state voltage drop [V] (1-2V typical)
///   DV_DT = dv/dt rating [V/μs] (optional, for snubber circuit design)
///
/// Simplified model equations:
///   - Blocking: Imt = 0 if (|Vmt| < VBO && !latched_pos_ && !latched_neg_)
///   - Trigger: latched_pos_/neg_ = true if (|Ig| >= IGT || |Vmt| >= VBO)
///   - Conducting: Imt = (Vmt - sign(Vmt)*VTM) / RON if latched
///   - Turn-off: latched_pos_/neg_ = false if (|Imt| < IH)
///
/// Note: This is a simplified static model for educational/research purposes.
/// For production use, consider dynamic models with di/dt, dv/dt, commutation effects.
class TRIAC : public Device {
public:
    TRIAC(DeviceId id, const std::string& name,
          NodeId mt2, NodeId mt1, NodeId gate,
          // Basic TRIAC parameters
          Real vbo = 400.0,      // Breakover voltage [V] (100-800V typical)
          Real igt = 20e-3,      // Gate trigger current [A] (5-50mA typical)
          Real ih = 10e-3,       // Holding current [A] (5-50mA typical)
          Real vgt = 1.0,        // Gate trigger voltage [V] (0.7-1.5V typical)
          Real ron = 0.1,        // On-state resistance [Ω] (0.01-1Ω typical)
          Real vtm = 1.5,        // On-state voltage drop [V] (1-2V typical)
          Real dv_dt = 10.0,     // dv/dt rating [V/μs] (optional, future use)
          // Temperature parameters
          Real tnom = 300.15);   // Nominal temperature [K] (27°C)

    ~TRIAC() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: Nonlinear device requires Newton-Raphson
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override {
        // TRIAC is nonlinear - no linear contribution
        (void)mna; (void)node_voltages;
    }

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override;

    // Transient analysis: Simplified static model (no dynamic capacitance)
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return false; }  // No dynamic capacitance in this model

    // AC analysis: Small-signal linearization
    void stamp_ac(ComplexMNASystem& sys, Real omega,
                  const DenseVector& dc_solution) const override;

    // Noise analysis
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                               Real frequency) const override;

    // LTE for adaptive timestepping
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override { return 0.0; }

    // State management
    void reset_state() override;
    void accept_solution(const DenseVector& node_voltages) override;

    // Parameter access
    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

private:
    std::vector<NodeId> nodes_;  // [0]=MT2, [1]=MT1, [2]=Gate

    // TRIAC parameters
    Real vbo_;     // Breakover voltage [V] (both directions)
    Real igt_;     // Gate trigger current [A] (both directions)
    Real ih_;      // Holding current [A] (both directions)
    Real vgt_;     // Gate trigger voltage [V]
    Real ron_;     // On-state resistance [Ω]
    Real vtm_;     // On-state voltage drop [V]
    Real dv_dt_;   // dv/dt rating [V/μs] (future use)
    Real tnom_;    // Nominal temperature [K]

    // State variables (bidirectional latching)
    mutable bool latched_pos_;   // Latching state for positive direction (MT2 > MT1)
    mutable bool latched_neg_;   // Latching state for negative direction (MT2 < MT1)
    mutable Real imt_prev_;      // Previous MT2-MT1 current [A] (for turn-off detection)
    mutable bool first_accept_;  // First accept_solution() call flag

    // Helper methods
    Real compute_gate_current(Real vg_mt1) const;
    Real compute_mt_current(Real vmt, const DenseVector& node_voltages) const;
    bool check_trigger_condition_pos(Real vmt, Real ig) const;  // Q1+/Q1-
    bool check_trigger_condition_neg(Real vmt, Real ig) const;  // Q3+/Q3-
    bool check_turnoff_condition(Real imt) const;
};

} // namespace kirimaspice::devices
