// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/output/ssf_writer.hpp
#pragma once

#include <cmath>

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace kirimaspice::output {

/// SSF V4.3 File Header (256 bytes fixed)
struct SSFHeader {
    char magic[4] = {'S', 'S', 'F', 0x04};  // "SSF\x04"
    std::uint16_t version_major = 3;        // Version 3.4
    std::uint16_t version_minor = 4;
    std::uint64_t metadata_offset = 0;
    std::uint64_t metadata_size = 0;
    std::uint64_t scope_dir_offset = 0;
    std::uint64_t scope_dir_size = 0;
    std::uint64_t signal_dir_offset = 0;
    std::uint64_t signal_dir_size = 0;
    std::uint64_t l1_index_offset = 0;
    std::uint64_t l1_index_size = 0;
    std::uint64_t data_blocks_offset = 0;
    std::uint64_t data_blocks_size = 0;

    std::uint8_t compression_type = 3;      // 0=NONE, 1=LZ4, 2=ZLIB, 3=ZSTD
    std::uint8_t flags = 0xC0;              // bit6: STREAMING_COMPRESSION, bit7: ANALOG_EXTENSIONS

    std::uint64_t start_time = 0;           // Axis0 start (integer coordinates)
    std::uint64_t end_time = 0;             // Axis0 end
    std::uint32_t timescale_num = 1;        // Timescale numerator
    std::uint32_t timescale_den = 1000000000; // Timescale denominator (1ns default)
    std::uint64_t block_time_window = 0;    // Block time window

    std::uint8_t padding[256 - 122] = {};   // Padding to 256 bytes (122 bytes of data fields)

    /// Write header to binary stream
    void write_to_stream(std::ofstream& out) const;
};

/// Scope entry
struct ScopeEntry {
    std::uint32_t scope_id;      // >= 1
    std::uint32_t parent_id;     // 0 = root
    std::uint8_t scope_type;     // 0=MODULE, 1=TASK, 2=FUNCTION, 3=BEGIN, 4=FORK
    std::string name;            // UTF-8
    std::string filename;        // Source filename (UTF-8, empty if unknown)
    std::uint32_t line_number;   // Source line number (0 if unknown)
};

/// Signal entry
struct SignalEntry {
    std::uint32_t signal_id;     // >= 1
    std::uint32_t scope_id;      // Parent scope
    std::string name;            // UTF-8
    std::uint8_t type;           // 0=WIRE, 1=REG, 2=REAL
    std::uint32_t width;         // 0 for real (analog)
    std::uint8_t flags;          // bit0: is_real
    std::uint32_t alias_id;      // 0 = not alias
};

/// L1 index entry
struct L1IndexEntry {
    std::uint64_t timestamp;     // Timestamp
    std::uint64_t data_offset;   // Data block file offset
    std::uint64_t size;          // Frame total length (STREAMING_COMPRESSION=1)
    std::uint32_t block_id;      // Block ID
};

/// R0 quantizer (recommended for real values)
struct R0Quantizer {
    double scale;                // Quantization scale
    std::int64_t offset;         // Quantization offset

    /// Quantize real value to integer
    std::int64_t quantize(double value) const {
        // To the nearest step: truncation would bias every value toward 0
        return static_cast<std::int64_t>(std::llround((value - offset) / scale));
    }

    /// Dequantize integer to real value
    double dequantize(std::int64_t quant) const {
        return quant * scale + offset;
    }
};

/// SSF V4.3 Writer
/// Writes DC and transient analysis results to SSF format
class SSFWriter {
    // Allow SSFHeader to access private write_* methods
    friend struct SSFHeader;

public:
    /// Constructor
    /// @param filename Output filename
    /// @param compression_type 0=NONE, 1=LZ4, 2=ZLIB, 3=ZSTD
    explicit SSFWriter(const std::string& filename, std::uint8_t compression_type = 3);

    /// Destructor (finalizes file)
    ~SSFWriter();

    // Disable copy/move
    SSFWriter(const SSFWriter&) = delete;
    SSFWriter& operator=(const SSFWriter&) = delete;

    /// Set output configuration for selective signal filtering
    /// @param config Output configuration from SimulationConfig
    void set_output_config(const SimulationConfig::Output& config);

    /// Write DC analysis results
    /// @param node_voltages Node voltage vector
    /// @param node_names Node names (in order)
    /// @param branch_currents Branch current vector
    /// @param branch_names Branch names (in order)
    void write_dc_analysis(const DenseVector& node_voltages,
                          const std::vector<std::string>& node_names,
                          const DenseVector& branch_currents,
                          const std::vector<std::string>& branch_names);

    /// Write transient analysis results
    /// @param result Transient analysis result
    /// @param node_names Node names (in order)
    /// @param branch_names Branch names (in order)
    void write_transient_analysis(const solvers::TransientResult& result,
                                  const std::vector<std::string>& node_names,
                                  const std::vector<std::string>& branch_names);

private:
    std::string filename_;
    std::ofstream file_;
    SSFHeader header_;
    bool finalized_ = false;

    // Metadata
    std::map<std::string, std::string> metadata_;

    // Scopes and signals
    std::vector<ScopeEntry> scopes_;
    std::vector<SignalEntry> signals_;

    // Data blocks and L1 index
    std::vector<L1IndexEntry> l1_index_;
    std::vector<std::vector<std::byte>> data_blocks_;

    // Output configuration
    const SimulationConfig::Output* output_config_ = nullptr;

    // Signal filtering indices
    // Maps original signal index to filtered signal index (-1 if filtered out)
    std::vector<int> signal_index_map_;

    /// Initialize metadata for DC analysis
    void init_dc_metadata(const std::vector<std::string>& node_names,
                         const std::vector<std::string>& branch_names);

    /// Initialize metadata for transient analysis
    void init_transient_metadata(const solvers::TransientResult& result,
                                const std::vector<std::string>& node_names,
                                const std::vector<std::string>& branch_names);

    /// Write header to file
    void write_header();

    /// Write metadata section
    void write_metadata();

    /// Write scope directory
    void write_scope_directory();

    /// Write signal directory
    void write_signal_directory();

    /// Write L1 index
    void write_l1_index();

    /// Write all data blocks
    void write_data_blocks();

    /// Finalize file (update header with offsets)
    void finalize();

    /// Encode varint (variable-length integer)
    static std::vector<std::byte> encode_varint(std::uint64_t value);

    /// Encode string with varint length prefix (UTF-8)
    /// @param str UTF-8 encoded string (ASCII is a valid subset of UTF-8)
    /// @note SSF V4.3 requires UTF-8 encoding for all strings
    static std::vector<std::byte> encode_string(const std::string& str);

    /// Compress data using zstd with streaming frame format
    /// @param data Uncompressed data
    /// @return Compressed frame: [uncompressed_size: uint32 LE] + compressed_payload
    std::vector<std::byte> compress_zstd_frame(const std::vector<std::byte>& data) const;

    /// Build data block for DC analysis
    /// @return Serialized block bytes (before compression)
    std::vector<std::byte> build_dc_data_block(const DenseVector& node_voltages,
                                               const DenseVector& branch_currents) const;

    /// Build data blocks for transient analysis
    /// @return Vector of serialized block bytes (before compression)
    std::vector<std::vector<std::byte>> build_transient_data_blocks(
        const solvers::TransientResult& result) const;

    /// Check if a signal should be included in output
    /// @param signal_name Signal name (e.g., "v(out)", "i(V1)")
    /// @return true if signal should be saved, false otherwise
    bool should_save_signal(const std::string& signal_name) const;

    /// Write uint8 to stream
    static void write_uint8(std::ofstream& out, std::uint8_t value);

    /// Write uint16 little-endian to stream
    static void write_uint16(std::ofstream& out, std::uint16_t value);

    /// Write uint32 little-endian to stream
    static void write_uint32(std::ofstream& out, std::uint32_t value);

    /// Write uint64 little-endian to stream
    static void write_uint64(std::ofstream& out, std::uint64_t value);

    /// Write bytes to stream
    static void write_bytes(std::ofstream& out, const std::vector<std::byte>& data);
};

} // namespace kirimaspice::output
