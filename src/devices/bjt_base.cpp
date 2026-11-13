// src/devices/bjt_base.cpp
#include "kirimaspice/devices/bjt_base.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

BJT_Base::BJT_Base(DeviceId id, const std::string& name, DeviceType type,
                   NodeId collector, NodeId base, NodeId emitter,
                   Real is, Real beta_f, Real nf, Real isc, Real beta_r, Real nr,
                   Real cje, Real vje, Real mje,
                   Real cjc, Real vjc, Real mjc,
                   Real tf, Real tr,
                   Real kf, Real af, Real ff)
    : Device(id, name, type)
    , nodes_{collector, base, emitter}
    , is_(is)
    , beta_f_(beta_f)
    , nf_(nf)
    , isc_(isc < 0.0 ? is : isc)  // Default: same as Is
    , beta_r_(beta_r)
    , nr_(nr)
    , vt_(k_boltzmann * 300.15 / q_electron)  // Default: 300.15K (27°C)
    , cje_(cje)
    , vje_(vje)
    , mje_(mje)
    , cjc_(cjc)
    , vjc_(vjc)
    , mjc_(mjc)
    , tf_(tf)
    , tr_(tr)
    , kf_(kf)
    , af_(af)
    , ff_(ff)
    , v_fwd_prev_(0.0)
    , v_fwd_prev2_(0.0)
    , v_fwd_prev3_(0.0)
    , v_fwd_saved_(0.0)
    , v_fwd_saved2_(0.0)
    , v_fwd_saved3_(0.0)
    , charge_fwd_(0.0)
    , charge_fwd_prev_(0.0)
    , v_rev_prev_(0.0)
    , v_rev_prev2_(0.0)
    , v_rev_prev3_(0.0)
    , v_rev_saved_(0.0)
    , v_rev_saved2_(0.0)
    , v_rev_saved3_(0.0)
    , charge_rev_(0.0)
    , charge_rev_prev_(0.0)
    , timestep_(0.0)
    , last_method_(IntegrationMethod::BACKWARD_EULER)
    , timestep_saved_(0.0)
    , method_saved_(IntegrationMethod::BACKWARD_EULER)
    , first_accept_(true)
    , bdf_alpha0_(0.0)
    , bdf_alpha1_(0.0)
    , bdf_alpha2_(0.0)
    , bdf_alpha3_(0.0)
    , bdf_beta0_(0.0)
    , use_bdf_var_(false)
{
    // Validation
    if (is_ <= 0.0) {
        throw std::runtime_error("BJT saturation current IS must be positive: " + name);
    }
    if (beta_f_ <= 0.0) {
        throw std::runtime_error("BJT forward beta BF must be positive: " + name);
    }
}

void BJT_Base::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }
    vt_ = k_boltzmann * temp_kelvin / q_electron;
    spdlog::debug("{} set temperature to {:.2f}K (Vt={:.6f}V)", name_, temp_kelvin, vt_);
}

void BJT_Base::save_state() {
    v_fwd_saved_ = v_fwd_prev_;
    v_fwd_saved2_ = v_fwd_prev2_;
    v_fwd_saved3_ = v_fwd_prev3_;
    v_rev_saved_ = v_rev_prev_;
    v_rev_saved2_ = v_rev_prev2_;
    v_rev_saved3_ = v_rev_prev3_;
    timestep_saved_ = timestep_;
    method_saved_ = last_method_;

    spdlog::trace("{} save_state: v_fwd={:.6f}V, v_rev={:.6f}V, timestep={:.3e}s",
                 name_, v_fwd_prev_, v_rev_prev_, timestep_);
}

void BJT_Base::restore_state() {
    v_fwd_prev_ = v_fwd_saved_;
    v_fwd_prev2_ = v_fwd_saved2_;
    v_fwd_prev3_ = v_fwd_saved3_;
    v_rev_prev_ = v_rev_saved_;
    v_rev_prev2_ = v_rev_saved2_;
    v_rev_prev3_ = v_rev_saved3_;
    timestep_ = timestep_saved_;
    last_method_ = method_saved_;

    spdlog::trace("{} restore_state: v_fwd={:.6f}V, v_rev={:.6f}V, timestep={:.3e}s",
                 name_, v_fwd_prev_, v_rev_prev_, timestep_);
}

Real BJT_Base::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    // Physical local truncation error (charge-based):
    // LTE = |Q_fwd(t+Δt) - Q_fwd(t)| / chgtol + |Q_rev(t+Δt) - Q_rev(t)| / chgtol
    //
    // This provides adaptive timestepping based on charge conservation.

    (void)node_voltages;  // Suppress unused parameter warning (charge already calculated in stamp_transient)

    Real lte_fwd = std::abs(charge_fwd_ - charge_fwd_prev_) / chgtol;
    Real lte_rev = std::abs(charge_rev_ - charge_rev_prev_) / chgtol;
    Real total_lte = lte_fwd + lte_rev;

    spdlog::trace("{} physical_lte: lte_fwd={:.3e}, lte_rev={:.3e}, total={:.3e} (chgtol={:.3e}C)",
                 name_, lte_fwd, lte_rev, total_lte, chgtol);

    return total_lte;
}

std::optional<Real> BJT_Base::get_parameter(const std::string& param_name) const {
    if (param_name == "IS" || param_name == "is") {
        return is_;
    } else if (param_name == "BF" || param_name == "bf" || param_name == "BETA_F" || param_name == "beta_f") {
        return beta_f_;
    } else if (param_name == "BR" || param_name == "br" || param_name == "BETA_R" || param_name == "beta_r") {
        return beta_r_;
    } else if (param_name == "NF" || param_name == "nf") {
        return nf_;
    } else if (param_name == "NR" || param_name == "nr") {
        return nr_;
    } else if (param_name == "CJE" || param_name == "cje") {
        return cje_;
    } else if (param_name == "CJC" || param_name == "cjc") {
        return cjc_;
    }
    return std::nullopt;  // Parameter not found
}

bool BJT_Base::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "IS" || param_name == "is") {
        if (value <= 0.0) {
            spdlog::warn("{}: IS must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        is_ = value;
        // Update isc_ if it was set to default (same as Is)
        if (isc_ == is_) {
            isc_ = value;
        }
        spdlog::trace("{}: set IS = {} A", name_, is_);
        return true;
    } else if (param_name == "BF" || param_name == "bf" || param_name == "BETA_F" || param_name == "beta_f") {
        if (value <= 0.0) {
            spdlog::warn("{}: BF must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        beta_f_ = value;
        spdlog::trace("{}: set BF = {}", name_, beta_f_);
        return true;
    } else if (param_name == "BR" || param_name == "br" || param_name == "BETA_R" || param_name == "beta_r") {
        if (value <= 0.0) {
            spdlog::warn("{}: BR must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        beta_r_ = value;
        spdlog::trace("{}: set BR = {}", name_, beta_r_);
        return true;
    } else if (param_name == "NF" || param_name == "nf") {
        if (value <= 0.0) {
            spdlog::warn("{}: NF must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        nf_ = value;
        spdlog::trace("{}: set NF = {}", name_, nf_);
        return true;
    } else if (param_name == "NR" || param_name == "nr") {
        if (value <= 0.0) {
            spdlog::warn("{}: NR must be positive (got {}), not updated",
                        name_, value);
            return false;
        }
        nr_ = value;
        spdlog::trace("{}: set NR = {}", name_, nr_);
        return true;
    } else if (param_name == "CJE" || param_name == "cje") {
        if (value < 0.0) {
            spdlog::warn("{}: CJE must be non-negative (got {}), not updated",
                        name_, value);
            return false;
        }
        cje_ = value;
        spdlog::trace("{}: set CJE = {} F", name_, cje_);
        return true;
    } else if (param_name == "CJC" || param_name == "cjc") {
        if (value < 0.0) {
            spdlog::warn("{}: CJC must be non-negative (got {}), not updated",
                        name_, value);
            return false;
        }
        cjc_ = value;
        spdlog::trace("{}: set CJC = {} F", name_, cjc_);
        return true;
    }
    return false;  // Parameter not found
}

std::vector<std::string> BJT_Base::get_perturbable_parameters() const {
    return {"IS", "BF", "BR", "NF"};  // Main parameters for sensitivity analysis
}

Real BJT_Base::get_junction_capacitance(Real v_junction, Real cj0, Real vj, Real m) const {
    // Junction capacitance (same formula as diode):
    // Cj(V) = Cj0 / (1 - V/Vj)^m  for V < FC*Vj
    // Cj(V) = Cj_FC + (V - FC*Vj) * dCj/dV  for V >= FC*Vj (linear extrapolation)
    //
    // where FC = 0.5 (forward-bias coefficient)

    if (cj0 == 0.0) {
        return 0.0;
    }

    constexpr Real FC = 0.5;
    Real fc_vj = FC * vj;

    if (v_junction < fc_vj) {
        // Normal region: Cj(V) = Cj0 / (1 - V/Vj)^m
        Real factor = 1.0 - v_junction / vj;
        if (factor <= 0.0) {
            return cj0 * 100.0;  // Avoid singularity (high capacitance)
        }
        return cj0 / std::pow(factor, m);
    } else {
        // Forward bias: linear extrapolation
        // Cj_FC = Cj0 / (1 - FC)^m
        Real cj_fc = cj0 / std::pow(1.0 - FC, m);
        // dCj/dV = m * Cj_FC / Vj
        Real slope = m * cj_fc / vj;
        return cj_fc + slope * (v_junction - fc_vj);
    }
}

void BJT_Base::update_junction_states(Real v_fwd_new, Real v_rev_new) {
    if (first_accept_) {
        // First call: just mark as initialized
        first_accept_ = false;
        spdlog::trace("{} accept_solution: first call (timestep={:.3e}s)", name_, timestep_);
        return;
    }

    // Shift forward junction voltage history
    v_fwd_prev3_ = v_fwd_prev2_;
    v_fwd_prev2_ = v_fwd_prev_;
    v_fwd_prev_ = v_fwd_new;

    // Shift reverse junction voltage history
    v_rev_prev3_ = v_rev_prev2_;
    v_rev_prev2_ = v_rev_prev_;
    v_rev_prev_ = v_rev_new;

    // Update charge for physical LTE calculation
    charge_fwd_prev_ = charge_fwd_;
    charge_rev_prev_ = charge_rev_;

    spdlog::trace("{} accept_solution: v_fwd_prev={:.6f}V, v_fwd_prev2={:.6f}V, v_fwd_prev3={:.6f}V, "
                 "v_rev_prev={:.6f}V, v_rev_prev2={:.6f}V, v_rev_prev3={:.6f}V",
                 name_, v_fwd_prev_, v_fwd_prev2_, v_fwd_prev3_, v_rev_prev_, v_rev_prev2_, v_rev_prev3_);
}

} // namespace kirimaspice::devices
