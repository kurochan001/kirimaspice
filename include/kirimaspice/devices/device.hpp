// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/device.hpp
#pragma once
#include <limits>
#include "kirimaspice/core/types.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace kirimaspice::devices {

/// Device base class (abstract interface)
class Device {
public:
    Device(DeviceId id, const std::string& name, DeviceType type)
        : id_(id), name_(name), type_(type) {}

    virtual ~Device() = default;

    // Identifiers
    DeviceId id() const { return id_; }
    const std::string& name() const { return name_; }
    DeviceType type() const { return type_; }

    // Connection nodes (max 4 terminals: c/b/e/s or d/g/s/b)
    virtual const std::vector<NodeId>& nodes() const = 0;

    /// DC analysis: MNA stamp (linear devices)
    /// @param mna MNA system
    /// @param node_voltages Current node voltages
    virtual void stamp_dc_linear(MNASystem& mna,
                                 const DenseVector& node_voltages) const = 0;

    /// DC analysis: Nonlinear device Newton-Raphson update
    /// @param mna Jacobian matrix and residual vector
    /// @param node_voltages Current node voltages
    virtual void stamp_dc_nonlinear(MNASystem& mna,
                                    const DenseVector& node_voltages) const = 0;

    /// Transient analysis: Companion model stamp
    /// @param mna MNA system
    /// @param node_voltages Current node voltages (t+Δt predicted)
    /// @param timestep Current timestep Δt
    /// @param method Integration method
    /// @param bdf_coef Variable-timestep BDF coefficients (nullptr = use fixed coefficients)
    /// @param tr_bdf2_coef TR-BDF2 two-stage coefficients (nullptr = not TR-BDF2 or simple mode)
    virtual void stamp_transient(MNASystem& mna,
                                 const DenseVector& node_voltages,
                                 Real timestep,
                                 IntegrationMethod method,
                                 const BDFCoefficients* bdf_coef = nullptr,
                                 const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const = 0;

    /// AC analysis: Small-signal admittance stamp
    /// @param sys Complex MNA system (admittance matrix Y)
    /// @param omega Angular frequency [rad/s] (ω = 2πf)
    /// @param dc_solution DC operating point voltages
    /// @note Must call linearize_at(dc_solution) before this method
    virtual void stamp_ac(ComplexMNASystem& sys,
                         Real omega,
                         const DenseVector& dc_solution) const {
        // Default: no AC contribution (voltage/current sources without AC specification)
        (void)sys; (void)omega; (void)dc_solution;  // Suppress unused parameter warnings
    }

    /// Linearize device at DC operating point
    /// Computes small-signal parameters (gm, rπ, Cπ, etc.) for nonlinear devices
    /// @param dc_solution DC operating point voltages
    /// @note Linear devices (R/C/L) don't need linearization
    virtual void linearize_at(const DenseVector& dc_solution) {
        (void)dc_solution;  // Suppress unused parameter warning
    }

    /// DC analysis: Stamp gmin for numerical stability
    /// Adds small conductances (gmin) to prevent singular matrix in cut-off
    /// @param mna MNA system
    /// @param node_voltages Current node voltages
    /// @note Only needed for nonlinear devices (BJT, diode, MOSFET)
    virtual void stamp_gmin(MNASystem& mna, const DenseVector& node_voltages) const {
        (void)mna; (void)node_voltages;  // Suppress unused parameter warnings
        // Default: no gmin (linear devices don't need it)
    }

    /// True when the last nonlinear stamp limited a junction voltage, so that
    /// the stamp was not taken at the present iterate. The solvers do not
    /// accept convergence after such an iteration (as SPICE does with its
    /// non-convergence flag); see core::limit_junction_step.
    virtual bool limited_in_last_stamp() const { return false; }

    /// Turn the junction voltage limiting of a device on or off; devices
    /// without junctions ignore it (.OPTIONS VOLTAGE_LIMITING,
    /// apply_voltage_limiting_option).
    virtual void set_voltage_limiting(bool enable) { (void)enable; }

    /// The time of the transient at which the next stamps are evaluated:
    /// the end of the step being solved, 0 for the operating point that
    /// starts a transient, and none outside a transient
    /// (TransientSolver::set_device_time). Devices whose equations depend on
    /// time (the waveforms of sources, the TIME of B sources, the IGBT's tail
    /// current) override it; the others ignore it.
    virtual void set_time(std::optional<Real> time) { (void)time; }

    /// State save/restore (for dynamic elements)
    virtual void save_state() {}
    virtual void restore_state() {}
    virtual void reset_state() {}  // Reset to initial state (for reused circuits)
    /// Under UIC, after the initial node voltages have been accepted: take
    /// the state from the element's own IC= (the voltage of a capacitor,
    /// the current of an inductor) when it has one
    virtual void apply_initial_condition() {}
    /// Record an accepted transient time point. node_voltages has ground at
    /// element 0 and node k at element k (core::accepted_node_voltage), as
    /// TransientSolver::accept_devices builds it; the stamps' vectors instead
    /// hold node k at element k - 1.
    virtual void accept_solution(const DenseVector& node_voltages) {}

    /// Record the operating point that starts a transient (not with UIC),
    /// in the same layout as accept_solution. A device whose transient
    /// state follows from its DC model takes it from there: an inductor's
    /// current is the current of its DC short. By default it is an accepted
    /// point like any other.
    virtual void accept_operating_point(const DenseVector& node_voltages) {
        accept_solution(node_voltages);
    }

    /// The longest time step the device's transient model allows: a
    /// transmission line reads its delayed waves from the accepted points,
    /// so a step longer than its delay TD would delay by the step instead,
    /// and SPICE limits the step to TD. The transient takes no longer step.
    virtual Real max_time_step() const { return std::numeric_limits<Real>::infinity(); }

    /// Get charge/flux (for error estimation, dynamic elements only)
    virtual Real get_charge() const { return 0.0; }
    virtual Real get_flux() const { return 0.0; }

    /// Physical quantity-based LTE estimation
    /// Returns normalized error: |ΔQ|/chgtol or |ΔΦ|/(chgtol/sec)
    /// node_voltages: Current solution voltages (for computing new charge/flux)
    virtual Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const { return 0.0; }

    /// Noise analysis: Get noise sources
    /// Returns vector of noise sources contributed by this device
    /// @param dc_solution DC operating point voltages
    /// @param frequency Frequency for noise calculation [Hz]
    /// @return Vector of noise sources (thermal, shot, flicker, etc.)
    /// @note Default implementation returns empty vector (no noise)
    virtual std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                                        Real frequency) const {
        (void)dc_solution; (void)frequency;  // Suppress unused parameter warnings
        return {};  // No noise by default
    }

    /// Is device linear? (R, C, L, independent sources are true)
    virtual bool is_linear() const = 0;

    /// Is device dynamic? (C, L, semiconductor junction capacitances are true)
    virtual bool is_dynamic() const = 0;

    /// Sensitivity analysis: Get parameter value
    /// @param param_name Parameter name (e.g., "resistance", "IS", "VTO")
    /// @return Parameter value, or nullopt if parameter doesn't exist
    /// @note Default implementation returns nullopt
    virtual std::optional<Real> get_parameter(const std::string& param_name) const {
        (void)param_name;  // Suppress unused parameter warning
        return std::nullopt;  // Default: no parameters exposed
    }

    /// Sensitivity analysis: Set parameter value
    /// @param param_name Parameter name
    /// @param value New parameter value
    /// @return true if parameter was set successfully, false otherwise
    /// @note Default implementation returns false
    virtual bool set_parameter(const std::string& param_name, Real value) {
        (void)param_name; (void)value;  // Suppress unused parameter warnings
        return false;  // Default: no parameters can be modified
    }

    /// Sensitivity analysis: Get list of perturbable parameters
    /// @return Vector of parameter names that can be perturbed
    /// @note Default implementation returns empty vector
    virtual std::vector<std::string> get_perturbable_parameters() const {
        return {};  // Default: no parameters
    }

    // ===== Internal node support for vendor bridges (optional) =====
    /// Number of additional internal nodes the device needs in MNA (rows/cols).
    /// Default: 0 (no extra nodes). Override when bridge mode uses prime/internal nodes.
    virtual std::uint32_t extra_node_count() const { return 0; }

    /// Assign base index for the device's extra nodes (0-based, after external nodes).
    /// Solver sets this before stamping; devices read it during stamp_*().
    void set_extra_node_base(std::uint32_t base) { extra_node_base_ = base; }
    std::uint32_t extra_node_base() const { return extra_node_base_; }

protected:
    DeviceId id_;
    std::string name_;
    DeviceType type_;
    std::uint32_t extra_node_base_ = 0;
};

} // namespace kirimaspice::devices
