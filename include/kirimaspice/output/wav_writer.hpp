// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

namespace kirimaspice::output {

/**
 * @brief WAV (RIFF WAVE) format writer for audio circuit simulation
 *
 * Writes transient analysis results as WAV audio files compatible with
 * LTspice .wave command format.
 *
 * Format specification:
 * - Standard RIFF WAVE PCM format
 * - Each node becomes a separate channel
 * - Configurable sample rate (default: 44100 Hz)
 * - Configurable bit depth (default: 16-bit, range: 8/16/24/32)
 * - Signal scaling: ±1V corresponds to full-scale audio range
 * - Uncompressed PCM data
 *
 * Voltage scaling:
 * - Input voltage range: -1V to +1V (2Vp-p)
 * - Values outside this range are clipped
 * - Scaling formula: sample = voltage × max_value
 *   - 16-bit: max_value = 32767
 *   - 8-bit: max_value = 127
 *   - 24-bit: max_value = 8388607
 *   - 32-bit: max_value = 2147483647
 *
 * Usage example:
 * @code
 *   WavWriter writer("output.wav", 44100, 16);
 *   writer.write_transient(node_names, result);
 * @endcode
 */
class WavWriter {
public:
    /**
     * @brief Construct WAV writer
     * @param filepath Output WAV file path
     * @param sample_rate Sample rate in Hz (default: 44100)
     * @param bits_per_sample Bit depth: 8, 16, 24, or 32 (default: 16)
     */
    WavWriter(std::string filepath,
              std::uint32_t sample_rate = 44100,
              std::uint16_t bits_per_sample = 16);

    /**
     * @brief Write transient analysis result as WAV file
     *
     * Each node in node_names becomes a separate channel in the WAV file.
     * Time points are resampled to the specified sample rate using linear interpolation.
     *
     * @param node_names List of node names to export
     * @param result Transient analysis result
     * @throws std::runtime_error If file cannot be opened or written
     * @throws std::invalid_argument If node_names is empty or result is empty
     */
    void write_transient(const std::vector<std::string>& node_names,
                         const solvers::TransientResult& result);

private:
    /**
     * @brief RIFF WAVE file header structure
     */
    struct WavHeader {
        // RIFF chunk
        char riff_header[4];      // "RIFF"
        std::uint32_t wav_size;   // File size - 8 bytes
        char wave_header[4];      // "WAVE"

        // fmt sub-chunk
        char fmt_header[4];       // "fmt "
        std::uint32_t fmt_chunk_size;  // 16 for PCM
        std::uint16_t audio_format;    // 1 for PCM
        std::uint16_t num_channels;    // Number of channels
        std::uint32_t sample_rate;     // Sample rate in Hz
        std::uint32_t byte_rate;       // Bytes per second
        std::uint16_t block_align;     // Bytes per sample frame
        std::uint16_t bits_per_sample; // Bits per sample

        // data sub-chunk
        char data_header[4];      // "data"
        std::uint32_t data_bytes; // Size of data section
    };

    /**
     * @brief Resample transient data to target sample rate
     *
     * Uses linear interpolation to convert variable-timestep simulation data
     * to fixed sample rate audio data.
     *
     * @param timepoints Original time points from simulation
     * @param target_sample_rate Target sample rate in Hz
     * @return Vector of resampled time points
     */
    std::vector<Real> resample_time_points(const std::vector<Real>& timepoints,
                                            std::uint32_t target_sample_rate) const;

    /**
     * @brief Interpolate voltage at specific time point
     *
     * @param time Target time point
     * @param timepoints Original time array
     * @param voltages Original voltage array
     * @return Interpolated voltage value
     */
    Real interpolate_voltage(Real time,
                             const std::vector<Real>& timepoints,
                             const std::vector<Real>& voltages) const;

    /**
     * @brief Clip and scale voltage to sample value
     *
     * Clips voltage to ±1V range and scales to integer sample value.
     *
     * @param voltage Input voltage (±1V nominal range)
     * @return Scaled integer sample value
     */
    std::int32_t voltage_to_sample(Real voltage) const;

    /**
     * @brief Write sample to output stream (handles multi-byte samples)
     *
     * @param out Output file stream
     * @param sample Sample value
     */
    void write_sample(std::ofstream& out, std::int32_t sample) const;

    /**
     * @brief Create and write WAV header
     *
     * @param out Output file stream
     * @param num_channels Number of audio channels
     * @param num_samples Number of samples per channel
     */
    void write_header(std::ofstream& out,
                      std::uint16_t num_channels,
                      std::uint32_t num_samples) const;

    /**
     * @brief Align node names to match voltage array size
     *
     * Removes GND node ("0" or "GND") if present, and ensures the
     * returned list size matches the expected voltage array size.
     * Auto-generates missing node names if needed.
     *
     * @param node_names Original node name list (may include GND)
     * @param expected Expected size (voltage array size, excludes GND)
     * @return Aligned node names matching voltage array
     */
    std::vector<std::string> align_node_names(const std::vector<std::string>& node_names,
                                               std::size_t expected) const;

    std::string filepath_;
    std::uint32_t sample_rate_;
    std::uint16_t bits_per_sample_;
    std::int32_t max_sample_value_;  // Maximum absolute sample value for scaling
};

} // namespace kirimaspice::output
