// src/devices/mosfet_sic.cpp
// Unified SiC MOSFET template implementation
#include "kirimaspice/devices/mosfet_sic.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

template<Polarity P>
MOSFET_SiC<P>::MOSFET_SiC(DeviceId id, const std::string& name,
                          NodeId drain, NodeId gate, NodeId source,
                          Real kp, Real w, Real l, Real vth,
                          Real theta, Real rdrift, Real nfs, Real vbd,
                          Real cgso, Real cgdo, Real cgbo,
                          Real cjo, Real pb, Real mj,
                          Real kf, Real af, Real ef, Real gamma,
                          Real kt1, Real kt2, Real ute, Real rdtemp, Real tnom)
    : Device(id, name, device_type)
    , nodes_{drain, gate, source}
    , kp_(kp)
    , w_(w)
    , l_(l)
    , vth_(vth)
    , beta_(kp * w / l)
    // SiC-specific DC parameters
    , theta_(theta)
    , rdrift_(rdrift)
    , nfs_(nfs)
    , vbd_(vbd)
    // Capacitance parameters
    , cgso_(cgso)
    , cgdo_(cgdo)
    , cgbo_(cgbo)
    , cjo_(cjo)
    , pb_(pb)
    , mj_(mj)
    // Noise parameters
    , kf_(kf)
    , af_(af)
    , ef_(ef)
    , gamma_(gamma)
    // Temperature parameters
    , kt1_(kt1)
    , kt2_(kt2)
    , ute_(ute)
    , rdtemp_(rdtemp)
    , tnom_(tnom)
    , temperature_(tnom) {

    // Parameter validation
    if (kp <= 0.0) {
        throw std::runtime_error("SiC MOSFET process transconductance must be positive: " + name);
    }
    if (w <= 0.0) {
        throw std::runtime_error("SiC MOSFET channel width must be positive: " + name);
    }
    if (l <= 0.0) {
        throw std::runtime_error("SiC MOSFET channel length must be positive: " + name);
    }
    if (theta < 0.0) {
        throw std::runtime_error("SiC MOSFET mobility modulation must be non-negative: " + name);
    }
    if (rdrift < 0.0) {
        throw std::runtime_error("SiC MOSFET drift resistance must be non-negative: " + name);
    }
    if (vbd <= 0.0) {
        throw std::runtime_error("SiC MOSFET breakdown voltage must be positive: " + name);
    }
    if (cgso < 0.0 || cgdo < 0.0 || cgbo < 0.0) {
        throw std::runtime_error("SiC MOSFET overlap capacitance must be non-negative: " + name);
    }

    const char* polarity_str = (P == Polarity::N) ? "N" : "P";
    spdlog::debug("Created MOSFET_SiC_{} {} (D={}, G={}, S={}, Kp={:.6e}A/V², W={:.6e}m, L={:.6e}m, "
                  "Vth={:.3f}V, β={:.6e}A/V², Rdrift={:.6e}Ω, THETA={:.6e}V⁻¹, Vbd={:.1f}V)",
                  polarity_str, name, drain, gate, source, kp_, w_, l_, vth_, beta_, rdrift_, theta_, vbd_);
}

template<Polarity P>
void MOSFET_SiC<P>::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }
    temperature_ = temp_kelvin;
    spdlog::debug("{} set temperature to {:.2f}K", name_, temperature_);
}

template<Polarity P>
Real MOSFET_SiC<P>::compute_temperature_scaled_vth(Real vbs) const {
    // Temperature scaling for threshold voltage (SiC-specific)
    // VTO(T) = VTO(Tnom) + KT1×(T-Tnom) + KT2×(T-Tnom)²
    Real temp_diff = temperature_ - tnom_;
    return vth_ + kt1_ * temp_diff + kt2_ * temp_diff * temp_diff;
}

template<Polarity P>
Real MOSFET_SiC<P>::compute_temperature_scaled_kp() const {
    // Temperature scaling for mobility (process transconductance)
    // KP(T) = KP(Tnom) × (T/Tnom)^UTE
    Real temp_ratio = temperature_ / tnom_;
    return kp_ * std::pow(temp_ratio, ute_);
}

template<Polarity P>
Real MOSFET_SiC<P>::compute_temperature_scaled_rdrift() const {
    // SiC-specific: Drift resistance temperature scaling
    // Rd(T) = Rd(Tnom) × (T/Tnom)^RDTEMP
    Real temp_ratio = temperature_ / tnom_;
    return rdrift_ * std::pow(temp_ratio, rdtemp_);
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_gmin(MNASystem& mna) const {
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];
    Real g = gmin_default;

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // D-S conductance
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, g);
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -g);
        }
    }
    if (ns_idx >= 0) {
        mna.G.add_entry(ns_idx, ns_idx, g);
        if (nd_idx >= 0) {
            mna.G.add_entry(ns_idx, nd_idx, -g);
        }
    }

    // G-S conductance
    if (ng_idx >= 0) {
        mna.G.add_entry(ng_idx, ng_idx, g);
        if (ns_idx >= 0) {
            mna.G.add_entry(ng_idx, ns_idx, -g);
        }
    }
    if (ns_idx >= 0) {
        mna.G.add_entry(ns_idx, ns_idx, g);
        if (ng_idx >= 0) {
            mna.G.add_entry(ns_idx, ng_idx, -g);
        }
    }
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_dc_nonlinear(MNASystem& mna,
                                       const DenseVector& node_voltages) const {
    // SiC MOSFET DC model: Shichman-Hodges Level 1 with SiC-specific enhancements
    //
    // Enhancements:
    // 1. Mobility modulation (THETA): β_eff = β / (1 + THETA × Vds_eff)
    // 2. Drift region resistance (Rdrift): Voltage drop ΔV = Id × Rdrift
    // 3. Temperature scaling: Vth(T), KP(T), Rdrift(T)
    //
    // For N-channel: Vgs = Vg - Vs, Vds = Vd - Vs, Vth > 0
    // For P-channel: Vgs_eff = Vs - Vg (Vsg), Vds_eff = Vs - Vd (Vsd), |Vth| for comparison

    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    // Get terminal voltages
    Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

    // Compute effective voltages based on polarity
    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);

    // Temperature-scaled parameters
    Real vth_temp = compute_temperature_scaled_vth();
    Real kp_temp = compute_temperature_scaled_kp();
    Real rdrift_temp = compute_temperature_scaled_rdrift();
    Real beta_temp = kp_temp * w_ / l_;

    // Get effective threshold for cutoff comparison
    Real vth_eff = get_effective_threshold();

    // Cutoff region: v_gs_eff < vth_eff
    if (v_gs_eff < vth_eff) {
        return;
    }

    // Overdrive voltage
    Real v_ov = v_gs_eff - vth_eff;
    Real v_ds_sat = v_ov;

    // Ensure Vds >= 0
    if (v_ds_eff < 0.0) {
        v_ds_eff = 0.0;
    }

    // Breakdown voltage check
    if (v_ds_eff > vbd_) {
        spdlog::warn("{} Vds={:.3f}V exceeds breakdown voltage Vbd={:.1f}V",
                     name_, v_ds_eff, vbd_);
    }

    // SiC-specific: Mobility modulation
    Real beta_eff = beta_temp / (1.0 + theta_ * v_ds_eff);

    // Calculate intrinsic drain current
    Real id_intrinsic = 0.0;
    if (v_ds_eff < v_ds_sat) {
        // Linear region
        id_intrinsic = beta_eff * (v_ov * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
    } else {
        // Saturation region
        id_intrinsic = 0.5 * beta_eff * v_ov * v_ov;
    }

    // SiC-specific: Drift resistance effect
    Real id = id_intrinsic;
    if (rdrift_temp > 0.0 && v_ds_eff > 1e-6) {
        Real rdrift_factor = id_intrinsic * rdrift_temp / v_ds_eff;
        id = id_intrinsic / (1.0 + rdrift_factor);
    }

    // Numerical differentiation for small-signal parameters
    const Real delta = 1e-6;

    // Transconductance (gm)
    Real v_gs_plus = v_gs_eff + delta;
    Real v_ov_plus = v_gs_plus - vth_eff;
    Real beta_eff_gm = beta_temp / (1.0 + theta_ * v_ds_eff);
    Real id_plus = 0.0;

    if (v_ov_plus > 0.0) {
        if (v_ds_eff < v_ov_plus) {
            id_plus = beta_eff_gm * (v_ov_plus * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
        } else {
            id_plus = 0.5 * beta_eff_gm * v_ov_plus * v_ov_plus;
        }
        if (rdrift_temp > 0.0 && v_ds_eff > 1e-6) {
            Real rdrift_factor = id_plus * rdrift_temp / v_ds_eff;
            id_plus = id_plus / (1.0 + rdrift_factor);
        }
    }

    Real gm = (id_plus - id) / delta;
    gm = std::max(gm, gmin_default);

    // Output conductance (gds)
    Real v_ds_plus = v_ds_eff + delta;
    Real beta_eff_gds = beta_temp / (1.0 + theta_ * v_ds_plus);
    Real id_plus_vds = 0.0;

    if (v_ds_plus < v_ds_sat) {
        id_plus_vds = beta_eff_gds * (v_ov * v_ds_plus - 0.5 * v_ds_plus * v_ds_plus);
    } else {
        id_plus_vds = 0.5 * beta_eff_gds * v_ov * v_ov;
    }

    if (rdrift_temp > 0.0 && v_ds_plus > 1e-6) {
        Real rdrift_factor = id_plus_vds * rdrift_temp / v_ds_plus;
        id_plus_vds = id_plus_vds / (1.0 + rdrift_factor);
    }

    Real gds = (id_plus_vds - id) / delta;
    gds = std::max(gds, gmin_default);

    // Equivalent current source (Newton-Raphson linearization)
    Real id_eq = id - gm * v_gs_eff - gds * v_ds_eff;

    const char* region_str = (v_ds_eff < v_ds_sat) ? "LINEAR" : "SATURATION";
    const char* polarity_str = (P == Polarity::N) ? "N" : "P";
    spdlog::trace("{} stamp_nonlinear ({}-ch): Vgs_eff={:.6f}V, Vds_eff={:.6f}V, Id={:.6e}A, "
                  "gm={:.6e}S, gds={:.6e}S, β_eff={:.6e}A/V², Rdrift={:.6e}Ω, region={}",
                  name_, polarity_str, v_gs_eff, v_ds_eff, id, gm, gds, beta_eff, rdrift_temp, region_str);

    // Convert to MNA indices
    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // Stamp transconductance gm
    if (nd_idx >= 0) {
        if (ng_idx >= 0) {
            mna.G.add_entry(nd_idx, ng_idx, gm);
        }
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -gm);
        }
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) {
            mna.G.add_entry(ns_idx, ng_idx, -gm);
        }
        mna.G.add_entry(ns_idx, ns_idx, gm);
    }

    // Stamp output conductance gds
    if (nd_idx >= 0) {
        mna.G.add_entry(nd_idx, nd_idx, gds);
        if (ns_idx >= 0) {
            mna.G.add_entry(nd_idx, ns_idx, -gds);
        }
    }
    if (ns_idx >= 0) {
        if (nd_idx >= 0) {
            mna.G.add_entry(ns_idx, nd_idx, -gds);
        }
        mna.G.add_entry(ns_idx, ns_idx, gds);
    }

    // Stamp RHS
    if (nd_idx >= 0) {
        mna.b[nd_idx] -= id_eq;
    }
    if (ns_idx >= 0) {
        mna.b[ns_idx] += id_eq;
    }
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_transient(MNASystem& mna,
                                    const DenseVector& node_voltages,
                                    Real timestep,
                                    IntegrationMethod method,
                                    const BDFCoefficients* bdf_coef,
                                    const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Step 1: Stamp DC nonlinear model
    stamp_dc_nonlinear(mna, node_voltages);

    // Step 2: Stamp overlap capacitances (simplified model)
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // G-S capacitance
    if (cgso_ > 0.0) {
        Real cgs = cgso_ * w_;
        Real g_eq = cgs / timestep;
        Real i_eq = -g_eq * vgs_prev_;

        if (ng_idx >= 0) {
            mna.G.add_entry(ng_idx, ng_idx, g_eq);
            if (ns_idx >= 0) {
                mna.G.add_entry(ng_idx, ns_idx, -g_eq);
            }
            mna.b[ng_idx] += i_eq;
        }
        if (ns_idx >= 0) {
            if (ng_idx >= 0) {
                mna.G.add_entry(ns_idx, ng_idx, -g_eq);
            }
            mna.G.add_entry(ns_idx, ns_idx, g_eq);
            mna.b[ns_idx] -= i_eq;
        }
    }

    // G-D capacitance
    if (cgdo_ > 0.0) {
        Real cgd = cgdo_ * w_;
        Real g_eq = cgd / timestep;
        Real i_eq = -g_eq * vgd_prev_;

        if (ng_idx >= 0) {
            mna.G.add_entry(ng_idx, ng_idx, g_eq);
            if (nd_idx >= 0) {
                mna.G.add_entry(ng_idx, nd_idx, -g_eq);
            }
            mna.b[ng_idx] += i_eq;
        }
        if (nd_idx >= 0) {
            if (ng_idx >= 0) {
                mna.G.add_entry(nd_idx, ng_idx, -g_eq);
            }
            mna.G.add_entry(nd_idx, nd_idx, g_eq);
            mna.b[nd_idx] -= i_eq;
        }
    }
}

template<Polarity P>
void MOSFET_SiC<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                             const DenseVector& dc_solution) const {
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    Real vd = (nd == NODE_GND) ? 0.0 : dc_solution[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : dc_solution[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : dc_solution[ns - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);

    // Temperature-scaled parameters
    Real vth_temp = compute_temperature_scaled_vth();
    Real kp_temp = compute_temperature_scaled_kp();
    Real rdrift_temp = compute_temperature_scaled_rdrift();
    Real beta_temp = kp_temp * w_ / l_;

    Real vth_eff = get_effective_threshold();

    // Calculate small-signal parameters
    const Real delta = 1e-6;
    Real beta_eff = beta_temp / (1.0 + theta_ * v_ds_eff);
    Real v_ov = v_gs_eff - vth_eff;
    Real v_ds_sat = v_ov;

    Real id = 0.0;
    Real id_plus = 0.0;

    if (v_ov > 0.0) {
        if (v_ds_eff < v_ds_sat) {
            id = beta_eff * (v_ov * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
        } else {
            id = 0.5 * beta_eff * v_ov * v_ov;
        }

        Real v_ov_plus = (v_gs_eff + delta) - vth_eff;
        if (v_ds_eff < v_ov_plus) {
            id_plus = beta_eff * (v_ov_plus * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
        } else {
            id_plus = 0.5 * beta_eff * v_ov_plus * v_ov_plus;
        }
    }

    Real gm = (id_plus - id) / delta;
    gm = std::max(gm, gmin_default);

    Real gds = gmin_default;
    if (rdrift_temp > 0.0 && v_ds_eff > 1e-6) {
        gds = id / (rdrift_temp * id + v_ds_eff);
        gds = std::max(gds, gmin_default);
    }

    Real cgs = cgso_ * w_;
    Real cgd = cgdo_ * w_;

    const char* polarity_str = (P == Polarity::N) ? "N" : "P";
    spdlog::trace("{} stamp_ac ({}-ch): Vgs_eff={:.6f}V, Vds_eff={:.6f}V, Gm={:.6e}S, gds={:.6e}S, "
                  "Cgs={:.6e}F, Cgd={:.6e}F, ω={:.3e}rad/s",
                  name_, polarity_str, v_gs_eff, v_ds_eff, gm, gds, cgs, cgd, omega);

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    Complex ygs(0.0, omega * cgs);
    Complex ygd(0.0, omega * cgd);
    Complex yds(gds, 0.0);
    Complex gmvgs(gm, 0.0);

    // Stamp G-S capacitance
    if (ng_idx >= 0) {
        sys.Y.add_entry(ng_idx, ng_idx, ygs);
        if (ns_idx >= 0) {
            sys.Y.add_entry(ng_idx, ns_idx, -ygs);
        }
    }
    if (ns_idx >= 0) {
        sys.Y.add_entry(ns_idx, ns_idx, ygs);
        if (ng_idx >= 0) {
            sys.Y.add_entry(ns_idx, ng_idx, -ygs);
        }
    }

    // Stamp G-D capacitance
    if (ng_idx >= 0) {
        sys.Y.add_entry(ng_idx, ng_idx, ygd);
        if (nd_idx >= 0) {
            sys.Y.add_entry(ng_idx, nd_idx, -ygd);
        }
    }
    if (nd_idx >= 0) {
        sys.Y.add_entry(nd_idx, nd_idx, ygd);
        if (ng_idx >= 0) {
            sys.Y.add_entry(nd_idx, ng_idx, -ygd);
        }
    }

    // Stamp D-S output conductance
    if (nd_idx >= 0) {
        sys.Y.add_entry(nd_idx, nd_idx, yds);
        if (ns_idx >= 0) {
            sys.Y.add_entry(nd_idx, ns_idx, -yds);
        }
    }
    if (ns_idx >= 0) {
        sys.Y.add_entry(ns_idx, ns_idx, yds);
        if (nd_idx >= 0) {
            sys.Y.add_entry(ns_idx, nd_idx, -yds);
        }
    }

    // Stamp controlled current source
    if (nd_idx >= 0) {
        if (ng_idx >= 0) {
            sys.Y.add_entry(nd_idx, ng_idx, +gmvgs);
        }
        if (ns_idx >= 0) {
            sys.Y.add_entry(nd_idx, ns_idx, -gmvgs);
        }
    }
    if (ns_idx >= 0) {
        if (ng_idx >= 0) {
            sys.Y.add_entry(ns_idx, ng_idx, -gmvgs);
        }
        sys.Y.add_entry(ns_idx, ns_idx, +gmvgs);
    }
}

template<Polarity P>
std::vector<NoiseSource> MOSFET_SiC<P>::get_noise_sources(const DenseVector& dc_solution,
                                                          Real frequency) const {
    std::vector<NoiseSource> sources;

    NodeId drain_node = nodes_[0];
    NodeId gate_node = nodes_[1];
    NodeId source_node = nodes_[2];

    Real V_d = (drain_node == NODE_GND) ? 0.0 : dc_solution[drain_node - 1];
    Real V_g = (gate_node == NODE_GND) ? 0.0 : dc_solution[gate_node - 1];
    Real V_s = (source_node == NODE_GND) ? 0.0 : dc_solution[source_node - 1];

    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(V_d, V_g, V_s, v_gs_eff, v_ds_eff);

    // Temperature-scaled parameters
    Real kp_temp = compute_temperature_scaled_kp();
    Real beta_temp = kp_temp * w_ / l_;
    Real vth_eff = get_effective_threshold();

    // Calculate drain current and transconductance
    Real beta_eff = beta_temp / (1.0 + theta_ * v_ds_eff);
    Real v_ov = v_gs_eff - vth_eff;
    Real v_ds_sat = v_ov;

    Real I_d = 0.0;
    if (v_ov > 0.0) {
        if (v_ds_eff < v_ds_sat) {
            I_d = beta_eff * (v_ov * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
        } else {
            I_d = 0.5 * beta_eff * v_ov * v_ov;
        }
    }

    // gm (numerical differentiation)
    const Real delta = 1e-6;
    Real I_d_plus = 0.0;
    Real v_ov_plus = (v_gs_eff + delta) - vth_eff;
    if (v_ov_plus > 0.0) {
        if (v_ds_eff < v_ov_plus) {
            I_d_plus = beta_eff * (v_ov_plus * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
        } else {
            I_d_plus = 0.5 * beta_eff * v_ov_plus * v_ov_plus;
        }
    }
    Real g_m = (I_d_plus - I_d) / delta;

    constexpr Real gm_threshold = 1e-15;

    // Thermal noise
    if (g_m > gm_threshold) {
        Real I_n_th_squared = 4.0 * k_boltzmann * temperature_ * gamma_ * g_m;

        NoiseSource thermal;
        thermal.name = name_ + "_thermal";
        thermal.node_p = drain_node;
        thermal.node_n = source_node;
        thermal.spectral_density = I_n_th_squared;
        thermal.is_current = true;

        sources.push_back(thermal);
    }

    // Flicker noise
    constexpr Real I_threshold = 1e-15;
    if (kf_ > 0.0 && std::abs(I_d) > I_threshold && frequency > 0.0) {
        Real W_L = w_ * l_;
        Real I_n_f_squared = kf_ * std::pow(std::abs(I_d), af_) /
                             (std::pow(frequency, ef_) * std::pow(W_L, ef_));

        NoiseSource flicker;
        flicker.name = name_ + "_flicker";
        flicker.node_p = drain_node;
        flicker.node_n = source_node;
        flicker.spectral_density = I_n_f_squared;
        flicker.is_current = true;

        sources.push_back(flicker);
    }

    return sources;
}

template<Polarity P>
void MOSFET_SiC<P>::reset_state() {
    vgs_prev_ = 0.0;
    vgd_prev_ = 0.0;
    vds_prev_ = 0.0;
}

template<Polarity P>
void MOSFET_SiC<P>::accept_solution(const DenseVector& node_voltages) {
    NodeId nd = nodes_[0];
    NodeId ng = nodes_[1];
    NodeId ns = nodes_[2];

    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns);

    Real vgs_new = 0.0;
    if (ng_idx >= 0) vgs_new += node_voltages[ng_idx];
    if (ns_idx >= 0) vgs_new -= node_voltages[ns_idx];

    Real vgd_new = 0.0;
    if (ng_idx >= 0) vgd_new += node_voltages[ng_idx];
    if (nd_idx >= 0) vgd_new -= node_voltages[nd_idx];

    Real vds_new = 0.0;
    if (nd_idx >= 0) vds_new += node_voltages[nd_idx];
    if (ns_idx >= 0) vds_new -= node_voltages[ns_idx];

    vgs_prev_ = vgs_new;
    vgd_prev_ = vgd_new;
    vds_prev_ = vds_new;
}

template<Polarity P>
std::optional<Real> MOSFET_SiC<P>::get_parameter(const std::string& param_name) const {
    if (param_name == "KP" || param_name == "kp") {
        return kp_;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        return vth_;
    } else if (param_name == "W" || param_name == "w") {
        return w_;
    } else if (param_name == "L" || param_name == "l") {
        return l_;
    } else if (param_name == "THETA" || param_name == "theta") {
        return theta_;
    } else if (param_name == "RDRIFT" || param_name == "rdrift" || param_name == "RD") {
        return rdrift_;
    } else if (param_name == "VBD" || param_name == "vbd") {
        return vbd_;
    }
    return std::nullopt;
}

template<Polarity P>
bool MOSFET_SiC<P>::set_parameter(const std::string& param_name, Real value) {
    const char* polarity_str = (P == Polarity::N) ? "N" : "P";

    if (param_name == "KP" || param_name == "kp") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: KP must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        kp_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        vth_ = value;
        return true;
    } else if (param_name == "W" || param_name == "w") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: W must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        w_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "L" || param_name == "l") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: L must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        l_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "THETA" || param_name == "theta") {
        if (value < 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: THETA must be non-negative (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        theta_ = value;
        return true;
    } else if (param_name == "RDRIFT" || param_name == "rdrift" || param_name == "RD") {
        if (value < 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: RDRIFT must be non-negative (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        rdrift_ = value;
        return true;
    } else if (param_name == "VBD" || param_name == "vbd") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_SiC_{} {}: VBD must be positive (got {}), not updated",
                         polarity_str, name_, value);
            return false;
        }
        vbd_ = value;
        return true;
    }
    return false;
}

template<Polarity P>
std::vector<std::string> MOSFET_SiC<P>::get_perturbable_parameters() const {
    return {"KP", "VTO", "W", "L", "THETA", "RDRIFT"};
}

// Explicit template instantiations
template class MOSFET_SiC<Polarity::N>;
template class MOSFET_SiC<Polarity::P>;

} // namespace kirimaspice::devices
