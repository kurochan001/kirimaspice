// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception AND ECL-2.0

// Parts of this file are based on the BSIM4 4.8.3 model code (b4ld.c, b4temp.c),
// as distributed by UC Berkeley and in ngspice. That code carries this notice:
//
//   Copyright (c) 2025 University of California
//   Project Directors: Prof. Sayeef Salahuddin and Prof. Chenming Hu
//   Developers list: https://www.bsim.berkeley.edu/models/bsim4/auth_bsim4/
//
//   Licensed under Educational Community License, Version 2.0 (the "License"); you may
//   not use this file except in compliance with the License. You may obtain a copy of the
//   license at http://opensource.org/licenses/ECL-2.0
//   Unless required by applicable law or agreed to in writing, software distributed under
//   the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the License for the specific language governing
//   permissions and limitations under the License.
//
// Modified for this project: rewritten in C++ with a different structure.

// include/kirimaspice/devices/mosfet_bsim4.hpp
// Unified MOSFET BSIM4.8.3 template class
// Reference: BSIM4.8.3 User's Manual (UC Berkeley, 2025)
#pragma once

#include <optional>

#include "kirimaspice/devices/device.hpp"
#include "kirimaspice/devices/polarity.hpp"
#include "kirimaspice/devices/mosfet_bsim4_params.hpp"
#include "kirimaspice/devices/bsim4_core.hpp"

namespace kirimaspice::devices {

/**
 * @brief MOSFET BSIM4.8.3 device template class
 *
 * BSIM4 (Berkeley Short-channel IGFET Model version 4) is the latest
 * industry-standard compact MOSFET model for IC design. It extends BSIM3
 * with improved models for:
 *   - Gate tunneling current (IGIDL/IGISL)
 *   - STI (Shallow Trench Isolation) stress effects
 *   - Well proximity effects (WPE)
 *   - Asymmetric source/drain junctions
 *   - Layout-dependent parasitics
 *   - High-k metal gate (HKMG) support
 *
 * **Implementation status (+)**:
 *   - DC current model (Ids, gm, gds, gmb)
 *   - Capacitance model (Meyer gate + PN junction)
 *   - AC small-signal model
 *   - Temperature dependence (KT1/KT2/UTE)
 *   - Noise analysis (thermal + flicker)
 *
 * Terminals: drain (D), gate (G), source (S), bulk/substrate (B)
 *
 * For N-channel: standard Vgs, Vds, Vbs convention
 * For P-channel: BSIM4Core handles polarity transformation internally
 *
 * Unified N/P template
 */
template<Polarity P>
class MOSFET_BSIM4 : public Device {
public:
    /// Polarity traits for compile-time constants
    using Traits = PolarityTraits<P>;

    /// Device type for N-channel or P-channel BSIM4 MOSFET
    static constexpr DeviceType device_type =
        (P == Polarity::N) ? DeviceType::MOSFET_BSIM4_N : DeviceType::MOSFET_BSIM4_P;

    /**
     * @brief Construct BSIM4 MOSFET device with specified polarity
     * @param id Device ID
     * @param name Device name
     * @param drain Drain node ID
     * @param gate Gate node ID
     * @param source Source node ID
     * @param bulk Bulk/substrate node ID
     * @param params BSIM4 model parameters
     */
    MOSFET_BSIM4(DeviceId id, const std::string& name,
                 NodeId drain, NodeId gate, NodeId source, NodeId bulk,
                 const BSIM4Params& params);

    ~MOSFET_BSIM4() override;

    const std::vector<NodeId>& nodes() const override { return nodes_; }
    std::uint32_t extra_node_count() const override { return kExtraInternalNodes; }

    // DC analysis: Nonlinear device requires Newton-Raphson
    void stamp_dc_linear(MNASystem& mna,
                        const DenseVector& node_voltages) const override;

    void stamp_dc_nonlinear(MNASystem& mna,
                           const DenseVector& node_voltages) const override;

    // Transient analysis: Dynamic device with capacitances
    void stamp_transient(MNASystem& mna,
                        const DenseVector& node_voltages,
                        Real timestep,
                        IntegrationMethod method,
                        const BDFCoefficients* bdf_coef = nullptr,
                        const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;

    // AC analysis: Small-signal model
    void stamp_ac(ComplexMNASystem& mna,
                 Real frequency,
                 const DenseVector& dc_solution) const override;

    // Device characteristics
    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return true; }  // Has capacitances

    // Convergence helpers
    void accept_solution(const DenseVector& node_voltages) override;
    /// Back to the state before any transient: no voltage or current history
    void reset_state() override;

    // Physical LTE estimation
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    // Noise analysis
    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                                Real frequency) const override;

    // Public interface for parameter extraction
    /// Compute threshold voltage with all BSIM4 effects
    /// @param vds Drain-source voltage [V] (for P-channel: use |Vsd|)
    /// @param vbs Bulk-source voltage [V]
    /// @return Threshold voltage Vth [V]
    Real compute_threshold_voltage(Real vds, Real vbs) const;

    /// Compute drain current with all BSIM4 effects
    /// @param vgs Gate-source voltage [V] (for P-channel: use |Vsg|)
    /// @param vds Drain-source voltage [V] (for P-channel: use |Vsd|)
    /// @param vbs Bulk-source voltage [V]
    /// @param vth Threshold voltage [V]
    /// @return Drain current Ids [A]
    Real compute_drain_current(Real vgs, Real vds, Real vbs, Real vth) const;

    /// Get the instance-specific theta0vb0 coefficient (for testing)
    Real get_theta0vb0_for_testing() const { return theta0vb0_; }

    /// Set operating temperature (for temperature sweep support)
    /// @param temp_kelvin Operating temperature [K]
    void set_temperature(Real temp_kelvin);

    // Accessors
    const BSIM4Params& params() const { return params_; }
    NodeId drain() const { return drain_; }
    NodeId gate() const { return gate_; }
    NodeId source() const { return source_; }
    NodeId bulk() const { return bulk_; }

private:
    std::vector<NodeId> nodes_;  ///< [drain, gate, source, bulk]
    NodeId drain_;               ///< Drain node
    NodeId gate_;                ///< Gate node
    NodeId source_;              ///< Source node
    NodeId bulk_;                ///< Bulk/substrate node
    BSIM4Params params_;         ///< Model parameters
    BSIM4Core core_;             ///< Compact-model core evaluator (DC gm/gds/gmb)

    // State variables for transient analysis
    // Junction voltage history for BDF integration (prev = t-Δt, prev2 = t-2Δt, prev3 = t-3Δt)
    mutable Real vgs_prev_ = 0.0;   ///< V_GS(t-Δt)
    mutable Real vgs_prev2_ = 0.0;  ///< V_GS(t-2Δt) - for BDF2
    mutable Real vgs_prev3_ = 0.0;  ///< V_GS(t-3Δt) - for BDF3
    mutable Real vgd_prev_ = 0.0;   ///< V_GD(t-Δt)
    mutable Real vgd_prev2_ = 0.0;  ///< V_GD(t-2Δt) - for BDF2
    mutable Real vgd_prev3_ = 0.0;  ///< V_GD(t-3Δt) - for BDF3
    mutable Real vgb_prev_ = 0.0;   ///< V_GB(t-Δt)
    mutable Real vgb_prev2_ = 0.0;  ///< V_GB(t-2Δt) - for BDF2
    mutable Real vgb_prev3_ = 0.0;  ///< V_GB(t-3Δt) - for BDF3
    mutable Real vbd_prev_ = 0.0;   ///< V_BD(t-Δt)
    mutable Real vbd_prev2_ = 0.0;  ///< V_BD(t-2Δt) - for BDF2
    mutable Real vbd_prev3_ = 0.0;  ///< V_BD(t-3Δt) - for BDF3
    mutable Real vbs_prev_ = 0.0;   ///< V_BS(t-Δt)
    mutable Real vbs_prev2_ = 0.0;  ///< V_BS(t-2Δt) - for BDF2
    mutable Real vbs_prev3_ = 0.0;  ///< V_BS(t-3Δt) - for BDF3

    // The capacitances' currents at the last accepted point (cgs, cgd, cgb,
    // cbd, cbs), for the trapezoidal rule, and the step they were
    // integrated with (device_utils::capacitor_companion)
    mutable Real icap_prev_[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
    mutable Real cap_dt_ = 0.0;
    mutable IntegrationMethod cap_method_ = IntegrationMethod::BACKWARD_EULER;
    mutable std::optional<BDFCoefficients> cap_bdf_;

    // Integration method state (for accept_solution)
    mutable Real timestep_ = 0.0;
    mutable IntegrationMethod last_method_ = IntegrationMethod::BACKWARD_EULER;

    // Charge history for Physical LTE estimation
    mutable Real charge_gs_prev_ = 0.0;  ///< Q_GS(t-Δt) - gate-source charge
    mutable Real charge_gd_prev_ = 0.0;  ///< Q_GD(t-Δt) - gate-drain charge
    mutable Real charge_gb_prev_ = 0.0;  ///< Q_GB(t-Δt) - gate-bulk charge
    mutable Real charge_bd_prev_ = 0.0;  ///< Q_BD(t-Δt) - bulk-drain charge
    mutable Real charge_bs_prev_ = 0.0;  ///< Q_BS(t-Δt) - bulk-source charge

    // Legacy DC analysis state (for compatibility)
    mutable Real vds_prev_ = 0.0;   ///< Previous Vds (DC analysis)

    // Temperature dependence
    mutable Real temperature_ = 300.15;  ///< Operating temperature [K] (default 27°C)

    // Instance-specific DIBL parameter (theta0vb0)
    Real theta0vb0_ = 0.0;  ///< DIBL coefficient at Vbs=0 (instance-specific)

    // Private helper methods
    /// Compute effective channel width and length
    void compute_effective_dimensions(Real& weff, Real& leff) const;

    /// Compute effective mobility with vertical field degradation
    Real compute_mobility(Real vgs, Real vth, Real weff, Real leff) const;

    /// Compute gate capacitances (Meyer model + overlap capacitances)
    void compute_gate_capacitances(Real vgs, Real vds, Real vbs, Real vth,
                                    Real& cgs, Real& cgd, Real& cgb) const;

    /// Compute junction capacitances (bottom + sidewall + gate-edge)
    void compute_junction_capacitances(Real vbs, Real vbd,
                                        Real& cbd, Real& cbs) const;

    /// Helper to get polarity name for logging
    static constexpr const char* polarity_name() {
        return (P == Polarity::N) ? "BSIM4_N" : "BSIM4_P";
    }

    static constexpr std::uint32_t kExtraInternalNodes = 9; // dp/gp/sp/bp/q/ge/gm/db/sb

};

// Explicit template instantiation declarations
extern template class MOSFET_BSIM4<Polarity::N>;
extern template class MOSFET_BSIM4<Polarity::P>;

// Type aliases for backward compatibility
using MOSFET_BSIM4_N = MOSFET_BSIM4<Polarity::N>;
using MOSFET_BSIM4_P = MOSFET_BSIM4<Polarity::P>;

} // namespace kirimaspice::devices
