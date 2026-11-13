// include/kirimaspice/devices/polarity.hpp
// Polarity enum for N/P-channel device templates
#pragma once

namespace kirimaspice::devices {

/// MOSFET/JFET channel polarity
/// Used to unify N-channel and P-channel implementations via templates
enum class Polarity {
    N,  ///< N-channel (electrons as carriers)
    P   ///< P-channel (holes as carriers)
};

/// Polarity-dependent constants
/// For N-channel: positive current flows from drain to source
/// For P-channel: positive current flows from source to drain (in physical terms)
///                but MNA convention requires current from drain to source

template<Polarity P>
struct PolarityTraits;

template<>
struct PolarityTraits<Polarity::N> {
    /// Voltage sign: N-channel uses Vgs = Vg - Vs (direct)
    static constexpr double voltage_sign = 1.0;

    /// Current sign: N-channel current is positive (drain to source)
    static constexpr double current_sign = 1.0;

    /// Threshold is used directly for N-channel
    static constexpr bool use_abs_threshold = false;

    /// Device type string for logging
    static constexpr const char* type_string = "N-channel";
};

template<>
struct PolarityTraits<Polarity::P> {
    /// Voltage sign: P-channel uses Vsg = Vs - Vg = -(Vg - Vs)
    static constexpr double voltage_sign = -1.0;

    /// Current sign: P-channel current is negated for MNA convention
    static constexpr double current_sign = -1.0;

    /// Threshold uses absolute value for P-channel (Vth is negative)
    static constexpr bool use_abs_threshold = true;

    /// Device type string for logging
    static constexpr const char* type_string = "P-channel";
};

} // namespace kirimaspice::devices
