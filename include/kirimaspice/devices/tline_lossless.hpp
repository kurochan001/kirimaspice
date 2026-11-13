// include/kirimaspice/devices/tline_lossless.hpp
#pragma once
#include "kirimaspice/devices/device.hpp"
#include <deque>

namespace kirimaspice::devices {

/// Lossless Transmission Line: 4-terminal distributed element
/// Implements Branin's method (method of characteristics) for lossless propagation
///
/// SPICE syntax: TXXXXXXX A+ A- B+ B- Z0=value TD=value
///
/// Physical model:
/// - Characteristic impedance: Z0 [Ω]
/// - Propagation delay: TD [s]
/// - Port A: nodes (A+, A-)
/// - Port B: nodes (B+, B-)
///
/// Governing equations (method of characteristics):
///   I_A(t) = G0 * V_A(t) - G0 * V_B(t-TD) + I_B(t-TD)
///   I_B(t) = G0 * V_B(t) - G0 * V_A(t-TD) + I_A(t-TD)
/// where G0 = 1/Z0
///
/// MNA stamp (companion model):
///   Port A: Conductance G0 to ground + history current source
///   Port B: Conductance G0 to ground + history current source
///
/// History buffer: Stores (time, V_A, V_B, I_A, I_B) for delayed signal retrieval
class TLineLossless : public Device {
public:
    TLineLossless(DeviceId id, const std::string& name,
                  NodeId ap, NodeId an, NodeId bp, NodeId bn,
                  Real z0, Real td);

    ~TLineLossless() override = default;

    const std::vector<NodeId>& nodes() const override { return nodes_; }

    // DC analysis: open circuit (infinite delay → no DC coupling)
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override {
        // Transmission line is linear, no nonlinear contribution
    }

    // Transient analysis: Branin method with history buffer
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: Complex admittance (frequency-dependent phase shift)
    void stamp_ac(ComplexMNASystem& sys,
                 Real omega,
                 const DenseVector& dc_solution) const override;

    // State management
    void save_state() override;
    void restore_state() override;
    void reset_state() override;
    void accept_solution(const DenseVector& node_voltages) override;

    bool is_linear() const override { return true; }
    bool is_dynamic() const override { return true; }

    // Accessors
    Real z0() const { return z0_; }
    Real td() const { return td_; }
    Real g0() const { return g0_; }
    NodeId ap() const { return nodes_[0]; }
    NodeId an() const { return nodes_[1]; }
    NodeId bp() const { return nodes_[2]; }
    NodeId bn() const { return nodes_[3]; }
    BranchId branch_id_port_a() const { return branch_id_port_a_; }
    BranchId branch_id_port_b() const { return branch_id_port_b_; }

    void set_branch_id_port_a(BranchId id) { branch_id_port_a_ = id; }
    void set_branch_id_port_b(BranchId id) { branch_id_port_b_ = id; }

private:
    /// History point: (time, V_A, V_B, I_A, I_B)
    struct HistoryPoint {
        Real time;
        Real v_a;  // V_A = V(ap) - V(an)
        Real v_b;  // V_B = V(bp) - V(bn)
        Real i_a;  // Current flowing into port A
        Real i_b;  // Current flowing into port B
    };

    /// Get delayed signals at time (current_time - td) using linear interpolation
    /// @param current_time Current simulation time
    /// @param[out] v_a_delayed Delayed voltage V_A(t-TD)
    /// @param[out] v_b_delayed Delayed voltage V_B(t-TD)
    /// @param[out] i_a_delayed Delayed current I_A(t-TD)
    /// @param[out] i_b_delayed Delayed current I_B(t-TD)
    void get_delayed_signals(Real current_time,
                            Real& v_a_delayed, Real& v_b_delayed,
                            Real& i_a_delayed, Real& i_b_delayed) const;

    std::vector<NodeId> nodes_;  // [ap, an, bp, bn]
    Real z0_;   // Characteristic impedance [Ω]
    Real td_;   // Propagation delay [s]
    Real g0_;   // Conductance = 1/Z0 [S]

    // Branch current variables
    BranchId branch_id_port_a_;  // Branch ID for port A current
    BranchId branch_id_port_b_;  // Branch ID for port B current

    // History buffer: Stores recent signal values for delay interpolation
    // Circular buffer: Old entries removed when time > current_time - 2*td
    mutable std::deque<HistoryPoint> history_;

    // Current time (updated in accept_solution)
    mutable Real current_time_ = 0.0;
    mutable Real last_timestep_ = 0.0;  // Cached from stamp_transient

    // Saved state (for timestep rejection)
    std::deque<HistoryPoint> history_saved_;
    Real current_time_saved_ = 0.0;
};

} // namespace kirimaspice::devices
