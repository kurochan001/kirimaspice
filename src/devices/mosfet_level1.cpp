// src/devices/mosfet_level1.cpp
// Unified MOSFET Level 1 (Shichman-Hodges) template implementation
#include "kirimaspice/devices/mosfet_level1.hpp"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stdexcept>

namespace kirimaspice::devices {

template<Polarity P>
MOSFET_Level1<P>::MOSFET_Level1(DeviceId id, const std::string& name,
                                  NodeId drain, NodeId gate, NodeId source,
                                  Real kp, Real w, Real l, Real vth,
                                  Real cgso, Real cgdo, Real cgbo,
                                  Real kf, Real af, Real ef, Real gamma,
                                  Real kt1, Real kt2, Real ute, Real tnom)
    : MOSFET_Base(id, name, (P == Polarity::N) ? DeviceType::MOSFET_N : DeviceType::MOSFET_P)
    , nodes_{drain, gate, source}
    , kp_(kp)
    , w_(w)
    , l_(l)
    , vth_(vth)
    , cgso_(cgso)
    , cgdo_(cgdo)
    , cgbo_(cgbo)
    , kf_(kf)
    , af_(af)
    , ef_(ef)
    , gamma_(gamma)
    , vth_tnom_(vth)
    , kp_tnom_(kp)
    , beta_(kp * w / l) {
    // Set temperature parameters on base class
    kt1_ = kt1;
    kt2_ = kt2;
    ute_ = ute;
    tnom_ = tnom;

    if (kp <= 0.0) {
        throw std::runtime_error("MOSFET process transconductance must be positive: " + name);
    }
    if (w <= 0.0) {
        throw std::runtime_error("MOSFET channel width must be positive: " + name);
    }
    if (l <= 0.0) {
        throw std::runtime_error("MOSFET channel length must be positive: " + name);
    }
    if (cgso < 0.0 || cgdo < 0.0 || cgbo < 0.0) {
        throw std::runtime_error("MOSFET overlap capacitance must be non-negative: " + name);
    }

    // P-channel specific validation: Vth must be negative for PMOS
    if constexpr (P == Polarity::P) {
        if (vth >= 0.0) {
            throw std::runtime_error("PMOS threshold voltage must be negative: " + name);
        }
    }

    spdlog::debug("Created MOSFET_{} {} (D={}, G={}, S={}, Kp={:.6e}A/V², W={:.6e}m, L={:.6e}m, Vth={:.3f}V, β={:.6e}A/V²)",
                  Traits::type_string, name, drain, gate, source, kp_, w_, l_, vth_, beta_);
}

template<Polarity P>
void MOSFET_Level1<P>::set_threshold_voltage(Real vth) {
    vth_ = vth;
    spdlog::debug("{} set threshold voltage to {:.3f}V", name_, vth_);
}

template<Polarity P>
void MOSFET_Level1<P>::set_temperature(Real temp_kelvin) {
    if (temp_kelvin <= 0.0) {
        throw std::runtime_error("Temperature must be positive: " + name_);
    }

    // Temperature difference
    Real temp_diff = temp_kelvin - tnom_;

    // Update threshold voltage VTO(T)
    // SPICE equation: VTO(T) = VTO(Tnom) + KT1 * (T - Tnom) + KT2 * (T - Tnom)²
    vth_ = vth_tnom_ + kt1_ * temp_diff + kt2_ * temp_diff * temp_diff;

    // Update mobility (process transconductance) KP(T)
    // SPICE equation: KP(T) = KP(Tnom) * (T/Tnom)^UTE
    Real temp_ratio = temp_kelvin / tnom_;
    kp_ = kp_tnom_ * std::pow(temp_ratio, ute_);

    // Recompute beta
    beta_ = kp_ * w_ / l_;

    spdlog::debug("{} set temperature to {:.2f}K (VTO={:.4f}V, KP={:.6e}A/V²)",
                  name_, temp_kelvin, vth_, kp_);
}

template<Polarity P>
typename MOSFET_Level1<P>::Region MOSFET_Level1<P>::get_region(Real v_gs_eff, Real v_ds_eff) const {
    Real vth_eff = get_effective_threshold();

    if (v_gs_eff < vth_eff) {
        return Region::CUTOFF;
    }

    Real vds_sat = v_gs_eff - vth_eff;  // Saturation voltage

    if (v_ds_eff < vds_sat) {
        return Region::LINEAR;
    } else {
        return Region::SATURATION;
    }
}

template<Polarity P>
Real MOSFET_Level1<P>::get_drain_current(Real v_gs_eff, Real v_ds_eff) const {
    // Shichman-Hodges Level 1 model
    Real vth_eff = get_effective_threshold();

    // Region 1: Cutoff
    if (v_gs_eff < vth_eff) {
        return 0.0;
    }

    Real vov = v_gs_eff - vth_eff;  // Overdrive voltage
    Real vds_sat = vov;              // Saturation voltage

    // Ensure Vds_eff >= 0 (forward current only)
    if (v_ds_eff < 0.0) {
        v_ds_eff = 0.0;
    }

    // Region 2: Linear (Triode)
    if (v_ds_eff < vds_sat) {
        // Id = β × ((Vgs - Vth) × Vds - Vds²/2)
        return beta_ * (vov * v_ds_eff - 0.5 * v_ds_eff * v_ds_eff);
    }

    // Region 3: Saturation
    // Id = (β/2) × (Vgs - Vth)²
    return 0.5 * beta_ * vov * vov;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_transconductance(Real v_gs_eff, Real v_ds_eff) const {
    // gm = dId/dVgs
    Real vth_eff = get_effective_threshold();

    // Cutoff region
    if (v_gs_eff < vth_eff) {
        return gmin_default;
    }

    Real vov = v_gs_eff - vth_eff;
    Real vds_sat = vov;

    // Ensure Vds_eff >= 0
    if (v_ds_eff < 0.0) {
        v_ds_eff = 0.0;
    }

    // Linear region: gm = β × Vds
    if (v_ds_eff < vds_sat) {
        Real gm = beta_ * v_ds_eff;
        return std::max(gm, gmin_default);
    }

    // Saturation region: gm = β × (Vgs - Vth)
    Real gm = beta_ * vov;
    return std::max(gm, gmin_default);
}

template<Polarity P>
Real MOSFET_Level1<P>::get_output_conductance(Real v_gs_eff, Real v_ds_eff) const {
    // gds = dId/dVds
    Real vth_eff = get_effective_threshold();

    // Cutoff region
    if (v_gs_eff < vth_eff) {
        return gmin_default;
    }

    Real vov = v_gs_eff - vth_eff;
    Real vds_sat = vov;

    // Ensure Vds_eff >= 0
    if (v_ds_eff < 0.0) {
        v_ds_eff = 0.0;
    }

    // Linear region: gds = β × (Vgs - Vth - Vds)
    if (v_ds_eff < vds_sat) {
        Real gds = beta_ * (vov - v_ds_eff);
        return std::max(gds, gmin_default);
    }

    // Saturation region: gds = 0 (ideal), add gmin for stability
    return gmin_default;
}

template<Polarity P>
void MOSFET_Level1<P>::stamp_gmin(MNASystem& mna) const {
    // Add small conductance gmin between each terminal pair for numerical stability
    NodeId nd = nodes_[0];  // Drain
    NodeId ng = nodes_[1];  // Gate
    NodeId ns = nodes_[2];  // Source
    Real g = gmin_default;

    // D-S conductance
    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

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
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);

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
void MOSFET_Level1<P>::stamp_dc_nonlinear(MNASystem& mna,
                                           const DenseVector& node_voltages) const {
    NodeId nd = nodes_[0];  // Drain
    NodeId ng = nodes_[1];  // Gate
    NodeId ns = nodes_[2];  // Source

    // Get terminal voltages
    Real vd = (nd == NODE_GND) ? 0.0 : node_voltages[nd - 1];
    Real vg = (ng == NODE_GND) ? 0.0 : node_voltages[ng - 1];
    Real vs = (ns == NODE_GND) ? 0.0 : node_voltages[ns - 1];

    // Compute effective voltages based on polarity
    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(vd, vg, vs, v_gs_eff, v_ds_eff);

    // Calculate drain current and small-signal parameters (physical values)
    Real id_physical = get_drain_current(v_gs_eff, v_ds_eff);
    Real gm = get_transconductance(v_gs_eff, v_ds_eff);
    Real gds = get_output_conductance(v_gs_eff, v_ds_eff);

    // Convert to MNA convention
    // For N-channel: id_mna = id_physical (drain to source is positive)
    // For P-channel: id_mna = -id_physical (current direction reversal for MNA)
    Real id = Traits::current_sign * id_physical;

    // Equivalent current source (for Newton-Raphson)
    // The linearization uses effective voltages, but MNA stamps use terminal voltages
    // For N-channel: Id = gm × (Vg - Vs) + gds × (Vd - Vs) + id_eq
    // For P-channel: -Id = gm × (Vs - Vg) + gds × (Vs - Vd) + id_eq
    //                Id = -gm × (Vs - Vg) - gds × (Vs - Vd) - id_eq
    //                   = gm × (Vg - Vs) + gds × (Vd - Vs) - id_eq
    // So we can use the same stamping pattern with adjusted id_eq

    Real id_eq;
    if constexpr (P == Polarity::N) {
        id_eq = id - gm * v_gs_eff - gds * v_ds_eff;
    } else {
        // For P-channel: id_mna = -id_physical
        // The equivalent current is computed from physical current and effective voltages
        // id_eq = id_mna - gm × (Vg - Vs) - gds × (Vd - Vs)
        //       = -id_physical - gm × (-(Vs - Vg)) - gds × (-(Vs - Vd))
        //       = -id_physical + gm × v_gs_eff + gds × v_ds_eff
        id_eq = id + gm * v_gs_eff + gds * v_ds_eff;
    }

    Region region = get_region(v_gs_eff, v_ds_eff);
    const char* region_str = (region == Region::CUTOFF) ? "CUTOFF" :
                             (region == Region::LINEAR) ? "LINEAR" : "SATURATION";

    spdlog::trace("{} stamp_nonlinear: V_gs_eff={:.6f}V, V_ds_eff={:.6f}V, Id={:.6e}A, gm={:.6e}S, gds={:.6e}S, region={}",
                 name_, v_gs_eff, v_ds_eff, id, gm, gds, region_str);

    // Convert to MNA indices
    auto nd_idx = (nd == NODE_GND) ? -1 : static_cast<int>(nd - 1);
    auto ng_idx = (ng == NODE_GND) ? -1 : static_cast<int>(ng - 1);
    auto ns_idx = (ns == NODE_GND) ? -1 : static_cast<int>(ns - 1);

    // Stamp transconductance gm (gate-source voltage controls drain current)
    // MNA form: Id = gm × (Vg - Vs) + gds × (Vd - Vs) + id_eq
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

    // Stamp RHS (equivalent current source)
    if (nd_idx >= 0) {
        mna.b[nd_idx] -= id_eq;
    }
    if (ns_idx >= 0) {
        mna.b[ns_idx] += id_eq;
    }
}

template<Polarity P>
void MOSFET_Level1<P>::stamp_transient(MNASystem& mna,
                                        const DenseVector& node_voltages,
                                        Real timestep,
                                        IntegrationMethod method,
                                        const BDFCoefficients* bdf_coef,
                                        const TR_BDF2_Coefficients* tr_bdf2_coef) const {
    // Step 1: Stamp DC nonlinear model
    stamp_dc_nonlinear(mna, node_voltages);

    // Save timestep and method for accept_solution()
    timestep_ = timestep;
    last_method_ = method;

    // Get node indices
    const int nd_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0]);
    const int ng_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1]);
    const int ns_idx = (nodes_[2] == NODE_GND) ? -1 : static_cast<int>(nodes_[2]);

    // Helper lambda for stamping capacitor companion model
    auto stamp_capacitor = [&](Real cap, int node_p, int node_n,
                               Real v_prev, Real v_prev2, Real v_prev3,
                               Real* g_eq_out, Real* i_eq_out) {
        if (cap <= 0.0) return;

        // Get current voltage
        Real v_now = 0.0;
        if (node_p >= 0) v_now += node_voltages[node_p];
        if (node_n >= 0) v_now -= node_voltages[node_n];

        Real g_eq = 0.0;
        Real i_eq = 0.0;

        switch (method) {
            case IntegrationMethod::BACKWARD_EULER:
                g_eq = cap / timestep;
                i_eq = -g_eq * v_prev;
                break;
            case IntegrationMethod::TRAPEZOIDAL:
                g_eq = 2.0 * cap / timestep;
                i_eq = -g_eq * v_prev;
                break;
            case IntegrationMethod::GEAR2:
                g_eq = 1.5 * cap / timestep;
                i_eq = -(2.0 * cap / timestep) * v_prev + (0.5 * cap / timestep) * v_prev2;
                break;
            case IntegrationMethod::GEAR3:
                g_eq = (11.0 / 6.0) * cap / timestep;
                i_eq = -(3.0 * cap / timestep) * v_prev
                     + (1.5 * cap / timestep) * v_prev2
                     - (cap / (3.0 * timestep)) * v_prev3;
                break;
            case IntegrationMethod::TR_BDF2:
                g_eq = 1.5 * cap / timestep;
                i_eq = -(2.0 * cap / timestep) * v_prev + (0.5 * cap / timestep) * v_prev2;
                break;
            default:
                throw std::runtime_error("Unknown integration method");
        }

        // Handle variable-timestep BDF
        if (bdf_coef != nullptr && (method == IntegrationMethod::GEAR2 || method == IntegrationMethod::GEAR3)) {
            use_bdf_var_ = true;
            bdf_alpha0_ = bdf_coef->alpha0;
            bdf_alpha1_ = bdf_coef->alpha1;
            bdf_alpha2_ = bdf_coef->alpha2;
            bdf_alpha3_ = bdf_coef->alpha3;
            bdf_beta0_ = bdf_coef->beta0;

            g_eq = cap * bdf_alpha0_ / bdf_beta0_;
            i_eq = cap * (bdf_alpha1_ * v_prev + bdf_alpha2_ * v_prev2 + bdf_alpha3_ * v_prev3) / bdf_beta0_;
        } else {
            use_bdf_var_ = false;
        }

        // Stamp companion model
        if (node_p >= 0) {
            mna.G.add_entry(node_p, node_p, g_eq);
            if (node_n >= 0) {
                mna.G.add_entry(node_p, node_n, -g_eq);
            }
            mna.b[node_p] += i_eq;
        }
        if (node_n >= 0) {
            if (node_p >= 0) {
                mna.G.add_entry(node_n, node_p, -g_eq);
            }
            mna.G.add_entry(node_n, node_n, g_eq);
            mna.b[node_n] -= i_eq;
        }

        if (g_eq_out) *g_eq_out = g_eq;
        if (i_eq_out) *i_eq_out = i_eq;
    };

    // Junction 1: Gate-Source (G-S) capacitance
    Real cgs = cgso_ * w_;
    stamp_capacitor(cgs, ng_idx, ns_idx, vgs_prev_, vgs_prev2_, vgs_prev3_, nullptr, nullptr);

    // Junction 2: Gate-Drain (G-D) capacitance
    Real cgd = cgdo_ * w_;
    stamp_capacitor(cgd, ng_idx, nd_idx, vgd_prev_, vgd_prev2_, vgd_prev3_, nullptr, nullptr);

    // Junction 3: Gate-Bulk/Body (G-B) capacitance (body connected to source)
    Real cgb = cgbo_ * l_;
    stamp_capacitor(cgb, ng_idx, ns_idx, vgb_prev_, vgb_prev2_, vgb_prev3_, nullptr, nullptr);
}

template<Polarity P>
Real MOSFET_Level1<P>::get_gate_source_capacitance(Real v_gs_eff, Real v_ds_eff) const {
    return cgso_ * w_;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_gate_drain_capacitance(Real v_gs_eff, Real v_ds_eff) const {
    return cgdo_ * w_;
}

template<Polarity P>
void MOSFET_Level1<P>::stamp_ac(ComplexMNASystem& sys, Real omega,
                                 const DenseVector& dc_solution) const {
    NodeId nd_node = nodes_[0];
    NodeId ng_node = nodes_[1];
    NodeId ns_node = nodes_[2];

    // Get DC operating point voltages
    Real v_d = (nd_node == NODE_GND) ? 0.0 : dc_solution[nd_node - 1];
    Real v_g = (ng_node == NODE_GND) ? 0.0 : dc_solution[ng_node - 1];
    Real v_s = (ns_node == NODE_GND) ? 0.0 : dc_solution[ns_node - 1];

    // Compute effective voltages
    Real v_gs_eff, v_ds_eff;
    compute_effective_voltages(v_d, v_g, v_s, v_gs_eff, v_ds_eff);

    // Calculate small-signal parameters
    Real gm = get_transconductance(v_gs_eff, v_ds_eff);
    Real gds = get_output_conductance(v_gs_eff, v_ds_eff);
    Real cgs = get_gate_source_capacitance(v_gs_eff, v_ds_eff);
    Real cgd = get_gate_drain_capacitance(v_gs_eff, v_ds_eff);

    spdlog::trace("{} stamp_ac: V_gs_eff={:.6f}V, V_ds_eff={:.6f}V, Gm={:.6e}S, gds={:.6e}S, Cgs={:.6e}F, Cgd={:.6e}F",
                 name_, v_gs_eff, v_ds_eff, gm, gds, cgs, cgd);

    // Convert to MNA indices
    auto nd_idx = (nd_node == NODE_GND) ? -1 : static_cast<int>(nd_node - 1);
    auto ng_idx = (ng_node == NODE_GND) ? -1 : static_cast<int>(ng_node - 1);
    auto ns_idx = (ns_node == NODE_GND) ? -1 : static_cast<int>(ns_node - 1);

    // Admittances
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

    // Stamp controlled current source: Id = Gm × (Vg - Vs)
    // This uses MNA convention for current direction
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
std::vector<NoiseSource> MOSFET_Level1<P>::get_noise_sources(const DenseVector& dc_solution,
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

    Real I_d = get_drain_current(v_gs_eff, v_ds_eff);
    Real g_m = get_transconductance(v_gs_eff, v_ds_eff);

    constexpr Real gm_threshold = 1e-15;

    // Channel thermal noise
    if (g_m > gm_threshold) {
        Real I_n_th_squared = 4.0 * k_boltzmann * T_nominal * gamma_ * g_m;

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
void MOSFET_Level1<P>::accept_solution(const DenseVector& node_voltages) {
    const int nd_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0]);
    const int ng_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1]);
    const int ns_idx = (nodes_[2] == NODE_GND) ? -1 : static_cast<int>(nodes_[2]);

    // G-S junction
    Real vgs_new = 0.0;
    if (ng_idx >= 0) vgs_new += node_voltages[ng_idx];
    if (ns_idx >= 0) vgs_new -= node_voltages[ns_idx];

    if (cgso_ > 0.0) {
        Real cgs = cgso_ * w_;
        charge_gs_prev_ = charge_gs_;
        charge_gs_ = cgs * vgs_new;
    }

    vgs_prev3_ = vgs_prev2_;
    vgs_prev2_ = vgs_prev_;
    vgs_prev_ = vgs_new;

    // G-D junction
    Real vgd_new = 0.0;
    if (ng_idx >= 0) vgd_new += node_voltages[ng_idx];
    if (nd_idx >= 0) vgd_new -= node_voltages[nd_idx];

    if (cgdo_ > 0.0) {
        Real cgd = cgdo_ * w_;
        charge_gd_prev_ = charge_gd_;
        charge_gd_ = cgd * vgd_new;
    }

    vgd_prev3_ = vgd_prev2_;
    vgd_prev2_ = vgd_prev_;
    vgd_prev_ = vgd_new;

    // G-B junction (body = source)
    Real vgb_new = 0.0;
    if (ng_idx >= 0) vgb_new += node_voltages[ng_idx];
    if (ns_idx >= 0) vgb_new -= node_voltages[ns_idx];

    if (cgbo_ > 0.0) {
        Real cgb = cgbo_ * l_;
        charge_gb_prev_ = charge_gb_;
        charge_gb_ = cgb * vgb_new;
    }

    vgb_prev3_ = vgb_prev2_;
    vgb_prev2_ = vgb_prev_;
    vgb_prev_ = vgb_new;

    first_accept_ = false;
}

template<Polarity P>
Real MOSFET_Level1<P>::get_physical_lte(Real chgtol, const DenseVector& node_voltages) const {
    if (!is_dynamic()) {
        return 0.0;
    }

    const int nd_idx = (nodes_[0] == NODE_GND) ? -1 : static_cast<int>(nodes_[0]);
    const int ng_idx = (nodes_[1] == NODE_GND) ? -1 : static_cast<int>(nodes_[1]);
    const int ns_idx = (nodes_[2] == NODE_GND) ? -1 : static_cast<int>(nodes_[2]);

    Real total_lte = 0.0;

    // G-S junction LTE
    if (cgso_ > 0.0) {
        Real vgs_now = 0.0;
        if (ng_idx >= 0) vgs_now += node_voltages[ng_idx];
        if (ns_idx >= 0) vgs_now -= node_voltages[ns_idx];

        Real cgs = cgso_ * w_;
        Real charge_gs_now = cgs * vgs_now;
        Real delta_q_gs = std::abs(charge_gs_now - charge_gs_prev_);
        Real q_scale_gs = std::max(std::abs(charge_gs_prev_), std::abs(charge_gs_now));
        Real lte_gs = delta_q_gs / (chgtol + q_scale_gs);
        total_lte += lte_gs * lte_gs;
    }

    // G-D junction LTE
    if (cgdo_ > 0.0) {
        Real vgd_now = 0.0;
        if (ng_idx >= 0) vgd_now += node_voltages[ng_idx];
        if (nd_idx >= 0) vgd_now -= node_voltages[nd_idx];

        Real cgd = cgdo_ * w_;
        Real charge_gd_now = cgd * vgd_now;
        Real delta_q_gd = std::abs(charge_gd_now - charge_gd_prev_);
        Real q_scale_gd = std::max(std::abs(charge_gd_prev_), std::abs(charge_gd_now));
        Real lte_gd = delta_q_gd / (chgtol + q_scale_gd);
        total_lte += lte_gd * lte_gd;
    }

    // G-B junction LTE
    if (cgbo_ > 0.0) {
        Real vgb_now = 0.0;
        if (ng_idx >= 0) vgb_now += node_voltages[ng_idx];
        if (ns_idx >= 0) vgb_now -= node_voltages[ns_idx];

        Real cgb = cgbo_ * l_;
        Real charge_gb_now = cgb * vgb_now;
        Real delta_q_gb = std::abs(charge_gb_now - charge_gb_prev_);
        Real q_scale_gb = std::max(std::abs(charge_gb_prev_), std::abs(charge_gb_now));
        Real lte_gb = delta_q_gb / (chgtol + q_scale_gb);
        total_lte += lte_gb * lte_gb;
    }

    return std::sqrt(total_lte);
}

template<Polarity P>
std::optional<Real> MOSFET_Level1<P>::get_parameter(const std::string& param_name) const {
    if (param_name == "KP" || param_name == "kp") {
        return kp_;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        return vth_;
    } else if (param_name == "W" || param_name == "w") {
        return w_;
    } else if (param_name == "L" || param_name == "l") {
        return l_;
    }

    return std::nullopt;
}

template<Polarity P>
bool MOSFET_Level1<P>::set_parameter(const std::string& param_name, Real value) {
    if (param_name == "KP" || param_name == "kp") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_{} {}: KP must be positive (got {}), not updated",
                        Traits::type_string, name_, value);
            return false;
        }
        kp_ = value;
        beta_ = kp_ * w_ / l_;
        spdlog::trace("MOSFET_{} {}: set KP = {} A/V², beta = {} A/V²",
                     Traits::type_string, name_, kp_, beta_);
        return true;
    } else if (param_name == "VTO" || param_name == "vto" ||
               param_name == "VTH" || param_name == "vth") {
        vth_ = value;
        spdlog::trace("MOSFET_{} {}: set VTO = {} V", Traits::type_string, name_, vth_);
        return true;
    } else if (param_name == "W" || param_name == "w") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_{} {}: W must be positive (got {}), not updated",
                        Traits::type_string, name_, value);
            return false;
        }
        w_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    } else if (param_name == "L" || param_name == "l") {
        if (value <= 0.0) {
            spdlog::warn("MOSFET_{} {}: L must be positive (got {}), not updated",
                        Traits::type_string, name_, value);
            return false;
        }
        l_ = value;
        beta_ = kp_ * w_ / l_;
        return true;
    }

    return false;
}

template<Polarity P>
std::vector<std::string> MOSFET_Level1<P>::get_perturbable_parameters() const {
    return {"KP", "VTO", "W", "L"};
}

// Explicit template instantiations
template class MOSFET_Level1<Polarity::N>;
template class MOSFET_Level1<Polarity::P>;

} // namespace kirimaspice::devices
