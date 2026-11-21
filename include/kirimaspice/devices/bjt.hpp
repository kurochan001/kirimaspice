// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "kirimaspice/devices/device.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace kirimaspice::devices {

enum class BJTPolarity { NPN, PNP };

/// The bipolar transistor of SPICE's Gummel-Poon model, one class for NPN and
/// PNP. A PNP device is the mirror of an NPN device: its junction voltages are
/// Veb and Vcb, and its terminal currents are the negatives of the NPN
/// device's at those voltages. Every analysis takes the device from the same
/// evaluate() for the currents and their analytic derivatives, and the
/// capacitances from junction_charges().
///
/// The junction voltages are those of the NPN-like device: vbe and vbc are
/// Vbe and Vbc for NPN, Veb and Vcb for PNP. The currents are then those that
/// flow into the collector and the base of the NPN-like device.
///
/// Not modelled (KNOWN_ISSUES): the series resistances RB, RE and RC (RB, RBM
/// and IRB are read and give get_base_resistance), the substrate junction,
/// XTF, VTF, ITF, PTF, XCJC and an FC other than 0.5.
template <BJTPolarity Pol>
class BJT : public Device {
public:
    static constexpr Real polarity = (Pol == BJTPolarity::NPN) ? 1.0 : -1.0;

    BJT(DeviceId id, const std::string& name, NodeId collector, NodeId base, NodeId emitter,
        Real is = 1e-14, Real beta_f = 100.0, Real nf = 1.0, Real isc = -1.0, Real beta_r = 1.0,
        Real nr = 1.0, Real vaf = 0.0, Real var = 0.0, Real ikf = 0.0, Real ikr = 0.0,
        Real rb = 0.0, Real rbm = 0.0, Real irb = 0.0, Real ise = 0.0, Real ne = 1.5,
        Real isc_leak = 0.0, Real nc = 2.0, Real cje = 0.0, Real vje = 0.75, Real mje = 0.33,
        Real cjc = 0.0, Real vjc = 0.75, Real mjc = 0.33, Real tf = 0.0, Real tr = 0.0,
        Real kf = 0.0, Real af = 1.0, Real ff = 1.0, Real xti = 3.0, Real xtb = 0.0,
        Real eg = 1.11, Real tnom = 300.15);

    const std::vector<NodeId>& nodes() const override { return nodes_; }
    NodeId collector() const { return nodes_[0]; }
    NodeId base() const { return nodes_[1]; }
    NodeId emitter() const { return nodes_[2]; }

    void stamp_dc_linear(MNASystem& mna, const DenseVector& node_voltages) const override {
        (void)mna;
        (void)node_voltages;
    }
    void stamp_dc_nonlinear(MNASystem& mna, const DenseVector& node_voltages) const override;
    void stamp_transient(MNASystem& mna, const DenseVector& node_voltages, Real timestep,
                         IntegrationMethod method, const BDFCoefficients* bdf_coef = nullptr,
                         const TR_BDF2_Coefficients* tr_bdf2_coef = nullptr) const override;
    void stamp_ac(ComplexMNASystem& sys, Real omega, const DenseVector& dc_solution) const override;
    void stamp_gmin(MNASystem& mna, const DenseVector& node_voltages) const override;

    bool is_linear() const override { return false; }
    bool is_dynamic() const override { return cje_ > 0.0 || cjc_ > 0.0 || tf_ > 0.0 || tr_ > 0.0; }

    void accept_solution(const DenseVector& node_voltages) override;
    void save_state() override;
    void restore_state() override;
    void reset_state() override;
    Real get_charge() const override { return history_.qbe + history_.qbc; }
    Real get_physical_lte(Real chgtol, const DenseVector& node_voltages) const override;

    std::vector<NoiseSource> get_noise_sources(const DenseVector& dc_solution,
                                               Real frequency) const override;

    std::optional<Real> get_parameter(const std::string& param_name) const override;
    bool set_parameter(const std::string& param_name, Real value) override;
    std::vector<std::string> get_perturbable_parameters() const override;

    void set_voltage_limiting(bool enable) override { enable_voltage_limiting_ = enable; }
    bool voltage_limiting_enabled() const { return enable_voltage_limiting_; }
    bool limited_in_last_stamp() const override { return limited_in_last_stamp_; }

    /// Scale IS, ISE, ISC (XTI, EG) and BF, BR (XTB) to temp_kelvin
    void set_temperature(Real temp_kelvin);

    // Parameters
    Real saturation_current() const { return is_; }
    Real forward_beta() const { return beta_f_; }
    Real forward_emission_coefficient() const { return nf_; }
    Real collector_saturation_current() const { return isc_; }
    Real reverse_beta() const { return beta_r_; }
    Real reverse_emission_coefficient() const { return nr_; }
    Real forward_early_voltage() const { return vaf_; }
    Real reverse_early_voltage() const { return var_; }
    Real get_xti() const { return xti_; }
    Real get_xtb() const { return xtb_; }
    Real get_eg() const { return eg_; }
    Real get_tnom() const { return tnom_; }

    /// The collector and base currents of the NPN-like device at the junction
    /// voltages vbe and vbc (SPICE's Gummel-Poon equations)
    struct Currents {
        Real ic = 0.0, ib = 0.0;
        Real qb = 1.0;  ///< the normalized base charge
    };
    Currents currents(Real vbe, Real vbc) const;

    /// Their derivatives by vbe and vbc
    struct Derivatives {
        Real dic_dvbe = 0.0, dic_dvbc = 0.0, dib_dvbe = 0.0, dib_dvbc = 0.0;
    };
    Derivatives derivatives(Real vbe, Real vbc) const;

    Real get_collector_current(Real vbe, Real vbc) const { return currents(vbe, vbc).ic; }
    Real get_base_current(Real vbe, Real vbc) const { return currents(vbe, vbc).ib; }
    Real get_collector_conductance_vbe(Real vbe, Real vbc) const { return derivatives(vbe, vbc).dic_dvbe; }
    Real get_collector_conductance_vbc(Real vbe, Real vbc) const { return derivatives(vbe, vbc).dic_dvbc; }
    Real get_base_conductance_vbe(Real vbe, Real vbc) const { return derivatives(vbe, vbc).dib_dvbe; }
    Real get_base_conductance_vbc(Real vbe, Real vbc) const { return derivatives(vbe, vbc).dib_dvbc; }
    // The PNP names of the same, at Veb and Vcb
    Real get_collector_conductance_veb(Real veb, Real vcb) const { return get_collector_conductance_vbe(veb, vcb); }
    Real get_collector_conductance_vcb(Real veb, Real vcb) const { return get_collector_conductance_vbc(veb, vcb); }
    Real get_base_conductance_veb(Real veb, Real vcb) const { return get_base_conductance_vbe(veb, vcb); }
    Real get_base_conductance_vcb(Real veb, Real vcb) const { return get_base_conductance_vbc(veb, vcb); }
    Real get_transconductance(Real vbe, Real vbc) const { return get_collector_conductance_vbe(vbe, vbc); }
    Real get_input_conductance(Real vbe, Real vbc) const { return get_base_conductance_vbe(vbe, vbc); }
    Real get_feedback_conductance(Real vbe, Real vbc) const { return get_collector_conductance_vbc(vbe, vbc); }

    /// SPICE's base resistance: RBM + (RB - RBM) / qb, or with IRB
    /// RBM + 3 (RB - RBM) (tan z - z) / (z tan^2 z). Not in the stamps.
    Real get_base_resistance(Real vbe, Real vbc) const;

    /// The capacitances of the B-E and B-C junctions: depletion (CJE, VJE,
    /// MJE; CJC, VJC, MJC; FC = 0.5) and diffusion (TF If / qb, TR Ir)
    struct Capacitances {
        Real cbe = 0.0, cbc = 0.0;
    };
    Capacitances capacitances(Real vbe, Real vbc) const;
    Real get_input_capacitance(Real vbe, Real vbc) const { return capacitances(vbe, vbc).cbe; }
    Real get_feedback_capacitance(Real vbe, Real vbc) const { return capacitances(vbe, vbc).cbc; }

private:
    struct Evaluation {
        Currents currents;
        Derivatives derivatives;
    };
    Evaluation evaluate(Real vbe, Real vbc) const;

    /// The charges of the two junctions, whose derivatives are capacitances()
    struct Charges {
        Real qbe = 0.0, qbc = 0.0;
    };
    Charges junction_charges(Real vbe, Real vbc) const;
    /// The junction voltages of the NPN-like device from node voltages
    /// (node k at element k - 1, or at element k when ground_at_zero)
    std::array<Real, 2> junction_voltages(const DenseVector& v, bool ground_at_zero) const;

    std::vector<NodeId> nodes_;  // collector, base, emitter

    // Parameters (at the present temperature; _tnom at TNOM)
    Real is_, beta_f_, nf_, isc_, beta_r_, nr_, vaf_, var_, ikf_, ikr_;
    Real rb_, rbm_, irb_, ise_, ne_, isc_leak_, nc_;
    Real cje_, vje_, mje_, cjc_, vjc_, mjc_, tf_, tr_;
    Real kf_, af_, ff_, xti_, xtb_, eg_, tnom_;
    Real is_tnom_, isc_tnom_, ise_tnom_, isc_leak_tnom_, beta_f_tnom_, beta_r_tnom_;
    Real temp_;  // the present temperature [K]: TNOM until set_temperature
    Real vt_;
    bool isc_tracks_is_;

    bool enable_voltage_limiting_ = true;
    mutable bool limited_in_last_stamp_ = false;
    mutable Real vbe_last_eval_ = 0.0;  // iteration state of the step limiting
    mutable Real vbc_last_eval_ = 0.0;

    // History of the capacitances at accepted points: the junction voltages
    // of the last three and the capacitor currents of the last one; the
    // companion of the step the stamp built, for accept_solution
    struct History {
        std::array<Real, 3> vbe{}, vbc{};
        Real ibe = 0.0, ibc = 0.0;
        Real qbe = 0.0, qbc = 0.0;
        bool valid = false;
    };
    History history_;
    History saved_history_;
    mutable Real cap_dt_ = 0.0;
    mutable IntegrationMethod cap_method_ = IntegrationMethod::BACKWARD_EULER;
    mutable std::optional<BDFCoefficients> cap_bdf_;
    mutable std::optional<TR_BDF2_Coefficients> cap_tr_bdf2_;
    mutable bool cap_stamped_ = false;
};

extern template class BJT<BJTPolarity::NPN>;
extern template class BJT<BJTPolarity::PNP>;

}  // namespace kirimaspice::devices
