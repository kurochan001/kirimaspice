#include "kirimaspice/output/wav_writer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <fmt/format.h>

namespace kirimaspice::output {

std::vector<std::string> WavWriter::align_node_names(const std::vector<std::string>& node_names,
                                                      std::size_t expected) const {
    std::vector<std::string> aligned;
    aligned.reserve(expected);

    // Prefer to drop ground node if solution excludes it
    for (const auto& name : node_names) {
        if ((name == "0" || name == "GND") && node_names.size() > expected) {
            continue;
        }
        aligned.push_back(name);
        if (aligned.size() == expected) {
            break;
        }
    }

    while (aligned.size() < expected) {
        aligned.push_back(fmt::format("node{}", aligned.size() + 1));
    }
    return aligned;
}

WavWriter::WavWriter(std::string filepath,
                     std::uint32_t sample_rate,
                     std::uint16_t bits_per_sample)
    : filepath_(std::move(filepath))
    , sample_rate_(sample_rate)
    , bits_per_sample_(bits_per_sample) {
    // Validate bit depth
    if (bits_per_sample != 8 && bits_per_sample != 16 &&
        bits_per_sample != 24 && bits_per_sample != 32) {
        throw std::invalid_argument(
            "Invalid bits_per_sample: must be 8, 16, 24, or 32");
    }

    // Calculate max sample value for scaling
    if (bits_per_sample == 8) {
        max_sample_value_ = 127;  // 2^7 - 1
    } else if (bits_per_sample == 16) {
        max_sample_value_ = 32767;  // 2^15 - 1
    } else if (bits_per_sample == 24) {
        max_sample_value_ = 8388607;  // 2^23 - 1
    } else {  // 32-bit
        max_sample_value_ = 2147483647;  // 2^31 - 1
    }

    spdlog::debug("WavWriter created: {} ({} Hz, {} bits)", filepath_,
                  sample_rate_, bits_per_sample_);
}

void WavWriter::write_transient(const std::vector<std::string>& node_names,
                                const solvers::TransientResult& result) {
    if (node_names.empty()) {
        throw std::invalid_argument("node_names cannot be empty");
    }

    if (result.timepoints.empty()) {
        throw std::invalid_argument("Transient result is empty");
    }

    // Align node names to match voltage array size
    // The voltage array excludes GND, so we need to remove it from node_names as well
    const std::size_t num_sim_points = result.timepoints.size();
    const std::size_t voltage_array_size = result.timepoints.front().node_voltages.size();
    const std::vector<std::string> aligned_names = align_node_names(node_names, voltage_array_size);
    const std::size_t num_channels = aligned_names.size();

    spdlog::info("Writing WAV file: {} ({} channels, {} Hz, {} bits)",
                 filepath_, num_channels, sample_rate_, bits_per_sample_);

    // Extract time points
    std::vector<Real> time_array;
    time_array.reserve(num_sim_points);
    for (const auto& tp : result.timepoints) {
        time_array.push_back(tp.time);
    }

    // Calculate duration and number of audio samples
    const Real duration = time_array.back() - time_array.front();
    const std::uint32_t num_audio_samples =
        static_cast<std::uint32_t>(std::ceil(duration * sample_rate_));

    if (num_audio_samples == 0) {
        spdlog::warn("Transient duration too short for audio sampling, skipping WAV output");
        return;
    }

    spdlog::debug("Transient duration: {} s, audio samples: {}",
                  duration, num_audio_samples);

    // Open output file
    std::ofstream out(filepath_, std::ios::binary);
    if (!out.is_open() || !out.good()) {
        throw std::runtime_error("Failed to open WAV output file: " + filepath_);
    }

    // Write header (placeholder, will update later)
    write_header(out, static_cast<std::uint16_t>(num_channels), num_audio_samples);

    // Resample and write data
    const Real dt = 1.0 / sample_rate_;
    const Real t_start = time_array.front();

    for (std::uint32_t i = 0; i < num_audio_samples; ++i) {
        const Real t = t_start + i * dt;

        // Write one sample for each channel
        for (std::size_t ch = 0; ch < num_channels; ++ch) {
            // Extract voltage array for this node
            std::vector<Real> voltage_array;
            voltage_array.reserve(num_sim_points);
            for (const auto& tp : result.timepoints) {
                if (ch < tp.node_voltages.size()) {
                    voltage_array.push_back(tp.node_voltages[ch]);
                } else {
                    voltage_array.push_back(0.0);
                }
            }

            // Interpolate voltage at time t
            const Real voltage = interpolate_voltage(t, time_array, voltage_array);

            // Convert to sample and write
            const std::int32_t sample = voltage_to_sample(voltage);
            write_sample(out, sample);
        }
    }

    out.close();

    spdlog::info("Wrote WAV file: {} ({} samples/channel, {} channels)",
                 filepath_, num_audio_samples, num_channels);
}

Real WavWriter::interpolate_voltage(Real time,
                                    const std::vector<Real>& timepoints,
                                    const std::vector<Real>& voltages) const {
    if (timepoints.empty() || voltages.empty()) {
        return 0.0;
    }

    // Handle edge cases
    if (time <= timepoints.front()) {
        return voltages.front();
    }
    if (time >= timepoints.back()) {
        return voltages.back();
    }

    // Binary search for surrounding time points
    auto it_upper = std::upper_bound(timepoints.begin(), timepoints.end(), time);
    if (it_upper == timepoints.begin()) {
        return voltages.front();
    }

    const std::size_t idx_upper = it_upper - timepoints.begin();
    const std::size_t idx_lower = idx_upper - 1;

    const Real t0 = timepoints[idx_lower];
    const Real t1 = timepoints[idx_upper];
    const Real v0 = voltages[idx_lower];
    const Real v1 = voltages[idx_upper];

    // Linear interpolation
    const Real alpha = (time - t0) / (t1 - t0);
    return v0 + alpha * (v1 - v0);
}

std::int32_t WavWriter::voltage_to_sample(Real voltage) const {
    // Clip to ±1V range
    voltage = std::clamp(voltage, -1.0, 1.0);

    // Scale to sample value
    const Real scaled = voltage * max_sample_value_;

    return static_cast<std::int32_t>(std::round(scaled));
}

void WavWriter::write_sample(std::ofstream& out, std::int32_t sample) const {
    if (bits_per_sample_ == 8) {
        // 8-bit: unsigned, offset by 128
        const std::uint8_t u8_sample = static_cast<std::uint8_t>(sample + 128);
        out.write(reinterpret_cast<const char*>(&u8_sample), 1);
    } else if (bits_per_sample_ == 16) {
        // 16-bit: signed, little-endian
        const std::int16_t i16_sample = static_cast<std::int16_t>(sample);
        out.write(reinterpret_cast<const char*>(&i16_sample), 2);
    } else if (bits_per_sample_ == 24) {
        // 24-bit: signed, little-endian, 3 bytes
        const std::uint8_t b0 = sample & 0xFF;
        const std::uint8_t b1 = (sample >> 8) & 0xFF;
        const std::uint8_t b2 = (sample >> 16) & 0xFF;
        out.write(reinterpret_cast<const char*>(&b0), 1);
        out.write(reinterpret_cast<const char*>(&b1), 1);
        out.write(reinterpret_cast<const char*>(&b2), 1);
    } else {  // 32-bit
        // 32-bit: signed, little-endian
        out.write(reinterpret_cast<const char*>(&sample), 4);
    }
}

void WavWriter::write_header(std::ofstream& out,
                              std::uint16_t num_channels,
                              std::uint32_t num_samples) const {
    WavHeader header{};

    // RIFF chunk descriptor
    std::memcpy(header.riff_header, "RIFF", 4);
    std::memcpy(header.wave_header, "WAVE", 4);

    // fmt sub-chunk
    std::memcpy(header.fmt_header, "fmt ", 4);
    header.fmt_chunk_size = 16;  // PCM format
    header.audio_format = 1;     // PCM
    header.num_channels = num_channels;
    header.sample_rate = sample_rate_;
    header.bits_per_sample = bits_per_sample_;
    header.block_align = num_channels * (bits_per_sample_ / 8);
    header.byte_rate = sample_rate_ * header.block_align;

    // data sub-chunk
    std::memcpy(header.data_header, "data", 4);
    header.data_bytes = num_samples * header.block_align;

    // wav_size = entire file size - 8 (RIFF header)
    header.wav_size = 36 + header.data_bytes;

    // Write header to file
    out.write(reinterpret_cast<const char*>(&header), sizeof(WavHeader));

    spdlog::debug("WAV header: {} channels, {} Hz, {} bits, {} samples",
                  num_channels, sample_rate_, bits_per_sample_, num_samples);
}

} // namespace kirimaspice::output
