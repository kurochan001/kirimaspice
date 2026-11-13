// src/output/ssf_writer.cpp
#include "kirimaspice/output/ssf_writer.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <zstd.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace kirimaspice::output {

//==============================================================================
// SSFHeader implementation
//==============================================================================

void SSFHeader::write_to_stream(std::ofstream& out) const {
    // Magic (4 bytes)
    out.write(magic, 4);

    // Version (4 bytes)
    SSFWriter::write_uint16(out, version_major);
    SSFWriter::write_uint16(out, version_minor);

    // Offsets and sizes (96 bytes)
    SSFWriter::write_uint64(out, metadata_offset);
    SSFWriter::write_uint64(out, metadata_size);
    SSFWriter::write_uint64(out, scope_dir_offset);
    SSFWriter::write_uint64(out, scope_dir_size);
    SSFWriter::write_uint64(out, signal_dir_offset);
    SSFWriter::write_uint64(out, signal_dir_size);
    SSFWriter::write_uint64(out, l1_index_offset);
    SSFWriter::write_uint64(out, l1_index_size);
    SSFWriter::write_uint64(out, data_blocks_offset);
    SSFWriter::write_uint64(out, data_blocks_size);

    // Compression and flags (2 bytes)
    SSFWriter::write_uint8(out, compression_type);
    SSFWriter::write_uint8(out, flags);

    // Axis0 timing (38 bytes)
    SSFWriter::write_uint64(out, start_time);
    SSFWriter::write_uint64(out, end_time);
    SSFWriter::write_uint32(out, timescale_num);
    SSFWriter::write_uint32(out, timescale_den);
    SSFWriter::write_uint64(out, block_time_window);

    // Padding (256 - 122 = 134 bytes)
    out.write(reinterpret_cast<const char*>(padding), sizeof(padding));
}

//==============================================================================
// SSFWriter implementation
//==============================================================================

SSFWriter::SSFWriter(const std::string& filename, std::uint8_t compression_type)
    : filename_(filename) {
    file_.open(filename_, std::ios::binary);
    if (!file_) {
        throw std::runtime_error(fmt::format("Failed to open file: {}", filename_));
    }

    header_.compression_type = compression_type;

    // Reserve space for header (will be overwritten in finalize())
    std::vector<char> placeholder(256, 0);
    file_.write(placeholder.data(), 256);
    // Check I/O error after initial write
    if (!file_.good()) {
        throw std::runtime_error(fmt::format("Failed to write header placeholder: {}", filename_));
    }

    spdlog::info("SSF Writer initialized: {}", filename_);
}

SSFWriter::~SSFWriter() {
    if (!finalized_) {
        try {
            finalize();
        } catch (const std::exception& e) {
            spdlog::error("Exception during SSFWriter finalization: {}", e.what());
        }
    }
}

void SSFWriter::set_output_config(const SimulationConfig::Output& config) {
    output_config_ = &config;
    spdlog::debug("SSF Writer output config set: save_all={}, {} save signals, {} print signals",
                 config.save_all, config.save_signals.size(), config.print_signals.size());
}

bool SSFWriter::should_save_signal(const std::string& signal_name) const {
    // If no output config is set, or save_all is true, save everything
    if (!output_config_ || output_config_->save_all) {
        return true;
    }

    // Check if signal is in save_signals list
    for (const auto& sig : output_config_->save_signals) {
        if (sig == signal_name) {
            return true;
        }
    }

    return false;
}

void SSFWriter::write_dc_analysis(const DenseVector& node_voltages,
                                   const std::vector<std::string>& node_names,
                                   const DenseVector& branch_currents,
                                   const std::vector<std::string>& branch_names) {
    spdlog::info("Writing DC analysis results to SSF");

    init_dc_metadata(node_names, branch_names);

    // Build single data block for DC operating point
    auto block_data = build_dc_data_block(node_voltages, branch_currents);

    // Compress block
    auto compressed_frame = compress_zstd_frame(block_data);

    // Add to data blocks
    data_blocks_.push_back(compressed_frame);

    // Add L1 index entry
    L1IndexEntry entry;
    entry.timestamp = 0;  // DC is at t=0
    entry.block_id = 0;
    entry.data_offset = 0;  // Will be updated in write_data_blocks()
    entry.size = compressed_frame.size();
    l1_index_.push_back(entry);

    finalize();
}

void SSFWriter::write_transient_analysis(const solvers::TransientResult& result,
                                         const std::vector<std::string>& node_names,
                                         const std::vector<std::string>& branch_names) {
    spdlog::info("Writing transient analysis results to SSF ({} timepoints)",
                 result.timepoints.size());

    init_transient_metadata(result, node_names, branch_names);

    // Build data blocks for all timepoints
    auto blocks = build_transient_data_blocks(result);

    // Compress and add to data blocks
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto compressed_frame = compress_zstd_frame(blocks[i]);
        data_blocks_.push_back(compressed_frame);

        // Add L1 index entry
        L1IndexEntry entry;
        entry.timestamp = static_cast<std::uint64_t>(
            result.timepoints[i].time * 1e9);  // Convert to ns
        entry.block_id = static_cast<std::uint32_t>(i);
        entry.data_offset = 0;  // Will be updated in write_data_blocks()
        entry.size = compressed_frame.size();
        l1_index_.push_back(entry);
    }

    finalize();
}

//==============================================================================
// Metadata initialization
//==============================================================================

void SSFWriter::init_dc_metadata(const std::vector<std::string>& node_names,
                                  const std::vector<std::string>& branch_names) {
    metadata_["file.format"] = "SSF";
    metadata_["file.version"] = "4.3";
    metadata_["file.analysis"] = "OP";  // Operating point (DC)
    metadata_["file.generator"] = "kirimaspice v0.1.0";

    // Timestamp (ISO 8601)
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now_t), "%Y-%m-%dT%H:%M:%SZ");
    metadata_["file.timestamp"] = ss.str();

    // Axis0 (DC sweep - single point)
    metadata_["axis.0.type"] = "dc_sweep";
    metadata_["axis.0.unit"] = "V";
    metadata_["axis.0.start"] = "0.0";
    metadata_["axis.0.stop"] = "0.0";

    // Build scope and signals
    scopes_.clear();
    signals_.clear();

    // Root scope
    ScopeEntry root;
    root.scope_id = 1;
    root.parent_id = 0;
    root.scope_type = 0;  // MODULE
    root.name = "top";
    root.filename = "";   // No source file for simulation top level
    root.line_number = 0; // Unknown
    scopes_.push_back(root);

    // Add node voltage signals (with filtering)
    std::uint32_t signal_id = 1;
    signal_index_map_.clear();
    signal_index_map_.reserve(node_names.size() + branch_names.size());

    for (size_t i = 0; i < node_names.size(); ++i) {
        const auto& node_name = node_names[i];
        std::string signal_name = "v(" + node_name + ")";

        // Check if signal should be saved
        if (!should_save_signal(signal_name)) {
            signal_index_map_.push_back(-1);  // Filtered out
            spdlog::debug("Filtered out signal: {}", signal_name);
            continue;
        }

        signal_index_map_.push_back(static_cast<int>(signal_id - 1));  // Map to filtered index

        SignalEntry sig;
        sig.signal_id = signal_id;
        sig.scope_id = 1;
        sig.name = signal_name;
        sig.type = 2;  // REAL
        sig.width = 0;
        sig.flags = 0x01;  // is_real
        sig.alias_id = 0;
        signals_.push_back(sig);

        metadata_[fmt::format("signal.{}.name", signal_id)] = sig.name;
        metadata_[fmt::format("signal.{}.quantity", signal_id)] = "voltage";
        metadata_[fmt::format("signal.{}.unit", signal_id)] = "V";
        metadata_[fmt::format("signal.{}.node", signal_id)] = node_name;

        ++signal_id;
    }

    // Add branch current signals (with filtering)
    for (size_t i = 0; i < branch_names.size(); ++i) {
        const auto& branch_name = branch_names[i];
        std::string signal_name = "i(" + branch_name + ")";

        // Check if signal should be saved
        if (!should_save_signal(signal_name)) {
            signal_index_map_.push_back(-1);  // Filtered out
            spdlog::debug("Filtered out signal: {}", signal_name);
            continue;
        }

        signal_index_map_.push_back(static_cast<int>(signal_id - 1));  // Map to filtered index

        SignalEntry sig;
        sig.signal_id = signal_id;
        sig.scope_id = 1;
        sig.name = signal_name;
        sig.type = 2;  // REAL
        sig.width = 0;
        sig.flags = 0x01;  // is_real
        sig.alias_id = 0;
        signals_.push_back(sig);

        metadata_[fmt::format("signal.{}.name", signal_id)] = sig.name;
        metadata_[fmt::format("signal.{}.quantity", signal_id)] = "current";
        metadata_[fmt::format("signal.{}.unit", signal_id)] = "A";
        metadata_[fmt::format("signal.{}.device", signal_id)] = branch_name;

        ++signal_id;
    }

    spdlog::info("DC metadata: {} total signals, {} saved after filtering",
                 node_names.size() + branch_names.size(), signals_.size());
}

void SSFWriter::init_transient_metadata(const solvers::TransientResult& result,
                                        const std::vector<std::string>& node_names,
                                        const std::vector<std::string>& branch_names) {
    metadata_["file.format"] = "SSF";
    metadata_["file.version"] = "4.3";
    metadata_["file.analysis"] = "TRAN";
    metadata_["file.generator"] = "kirimaspice v0.1.0";

    // Timestamp (ISO 8601)
    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now_t), "%Y-%m-%dT%H:%M:%SZ");
    metadata_["file.timestamp"] = ss.str();

    // Axis0 (time)
    metadata_["axis.0.type"] = "time";
    metadata_["axis.0.unit"] = "s";
    metadata_["axis.0.start"] = fmt::format("{:.12e}", result.timepoints.front().time);
    metadata_["axis.0.stop"] = fmt::format("{:.12e}", result.timepoints.back().time);

    // Update header timing
    header_.start_time = 0;
    header_.end_time = static_cast<std::uint64_t>(
        result.timepoints.back().time * 1e9);  // Convert to ns
    header_.timescale_num = 1;
    header_.timescale_den = 1000000000;  // 1ns resolution

    // Build scope and signals
    scopes_.clear();
    signals_.clear();

    // Root scope
    ScopeEntry root;
    root.scope_id = 1;
    root.parent_id = 0;
    root.scope_type = 0;  // MODULE
    root.name = "top";
    root.filename = "";   // No source file for simulation top level
    root.line_number = 0; // Unknown
    scopes_.push_back(root);

    // Add node voltage signals (with filtering)
    std::uint32_t signal_id = 1;
    signal_index_map_.clear();
    signal_index_map_.reserve(node_names.size() + branch_names.size());

    for (size_t i = 0; i < node_names.size(); ++i) {
        const auto& node_name = node_names[i];
        std::string signal_name = "v(" + node_name + ")";

        // Check if signal should be saved
        if (!should_save_signal(signal_name)) {
            signal_index_map_.push_back(-1);  // Filtered out
            spdlog::debug("Filtered out signal: {}", signal_name);
            continue;
        }

        signal_index_map_.push_back(static_cast<int>(signal_id - 1));  // Map to filtered index

        SignalEntry sig;
        sig.signal_id = signal_id;
        sig.scope_id = 1;
        sig.name = signal_name;
        sig.type = 2;  // REAL
        sig.width = 0;
        sig.flags = 0x01;  // is_real
        sig.alias_id = 0;
        signals_.push_back(sig);

        metadata_[fmt::format("signal.{}.name", signal_id)] = sig.name;
        metadata_[fmt::format("signal.{}.quantity", signal_id)] = "voltage";
        metadata_[fmt::format("signal.{}.unit", signal_id)] = "V";
        metadata_[fmt::format("signal.{}.node", signal_id)] = node_name;

        ++signal_id;
    }

    // Add branch current signals (with filtering)
    for (size_t i = 0; i < branch_names.size(); ++i) {
        const auto& branch_name = branch_names[i];
        std::string signal_name = "i(" + branch_name + ")";

        // Check if signal should be saved
        if (!should_save_signal(signal_name)) {
            signal_index_map_.push_back(-1);  // Filtered out
            spdlog::debug("Filtered out signal: {}", signal_name);
            continue;
        }

        signal_index_map_.push_back(static_cast<int>(signal_id - 1));  // Map to filtered index

        SignalEntry sig;
        sig.signal_id = signal_id;
        sig.scope_id = 1;
        sig.name = signal_name;
        sig.type = 2;  // REAL
        sig.width = 0;
        sig.flags = 0x01;  // is_real
        sig.alias_id = 0;
        signals_.push_back(sig);

        metadata_[fmt::format("signal.{}.name", signal_id)] = sig.name;
        metadata_[fmt::format("signal.{}.quantity", signal_id)] = "current";
        metadata_[fmt::format("signal.{}.unit", signal_id)] = "A";
        metadata_[fmt::format("signal.{}.device", signal_id)] = branch_name;

        ++signal_id;
    }

    spdlog::info("Transient metadata: {} total signals, {} saved after filtering",
                 node_names.size() + branch_names.size(), signals_.size());
}

//==============================================================================
// Write sections
//==============================================================================

void SSFWriter::write_metadata() {
    std::uint64_t offset = file_.tellp();

    // Count entries
    std::vector<std::byte> data;
    auto count_bytes = encode_varint(metadata_.size());
    data.insert(data.end(), count_bytes.begin(), count_bytes.end());

    // Write key-value pairs
    for (const auto& [key, value] : metadata_) {
        auto key_bytes = encode_string(key);
        auto value_bytes = encode_string(value);
        data.insert(data.end(), key_bytes.begin(), key_bytes.end());
        data.insert(data.end(), value_bytes.begin(), value_bytes.end());
    }

    write_bytes(file_, data);

    header_.metadata_offset = offset;
    header_.metadata_size = data.size();
}

void SSFWriter::write_scope_directory() {
    std::uint64_t offset = file_.tellp();

    std::vector<std::byte> data;

    // Number of scopes
    auto count_bytes = encode_varint(scopes_.size());
    data.insert(data.end(), count_bytes.begin(), count_bytes.end());

    // Write each scope
    // Format: [scope_id: varint][parent_id: varint][scope_type: uint8][name: string][filename: string][line_number: uint32 LE]
    for (const auto& scope : scopes_) {
        auto id_bytes = encode_varint(scope.scope_id);
        auto parent_bytes = encode_varint(scope.parent_id);
        auto name_bytes = encode_string(scope.name);
        auto filename_bytes = encode_string(scope.filename);

        data.insert(data.end(), id_bytes.begin(), id_bytes.end());
        data.insert(data.end(), parent_bytes.begin(), parent_bytes.end());
        data.push_back(static_cast<std::byte>(scope.scope_type));
        data.insert(data.end(), name_bytes.begin(), name_bytes.end());
        data.insert(data.end(), filename_bytes.begin(), filename_bytes.end());

        // Line number (uint32 LE)
        data.push_back(static_cast<std::byte>(scope.line_number & 0xFF));
        data.push_back(static_cast<std::byte>((scope.line_number >> 8) & 0xFF));
        data.push_back(static_cast<std::byte>((scope.line_number >> 16) & 0xFF));
        data.push_back(static_cast<std::byte>((scope.line_number >> 24) & 0xFF));
    }

    write_bytes(file_, data);

    header_.scope_dir_offset = offset;
    header_.scope_dir_size = data.size();
}

void SSFWriter::write_signal_directory() {
    std::uint64_t offset = file_.tellp();

    std::vector<std::byte> data;

    // Number of signals
    auto count_bytes = encode_varint(signals_.size());
    data.insert(data.end(), count_bytes.begin(), count_bytes.end());

    // Write each signal
    // Format: [signal_id: varint][scope_id: varint][signal_type: uint8][width: varint][name: string][alias_id: varint][flags: uint8]
    for (const auto& signal : signals_) {
        auto id_bytes = encode_varint(signal.signal_id);
        auto scope_bytes = encode_varint(signal.scope_id);
        auto width_bytes = encode_varint(signal.width);
        auto name_bytes = encode_string(signal.name);
        auto alias_bytes = encode_varint(signal.alias_id);

        data.insert(data.end(), id_bytes.begin(), id_bytes.end());
        data.insert(data.end(), scope_bytes.begin(), scope_bytes.end());
        data.push_back(static_cast<std::byte>(signal.type));
        data.insert(data.end(), width_bytes.begin(), width_bytes.end());
        data.insert(data.end(), name_bytes.begin(), name_bytes.end());
        data.insert(data.end(), alias_bytes.begin(), alias_bytes.end());
        data.push_back(static_cast<std::byte>(signal.flags));
    }

    write_bytes(file_, data);

    header_.signal_dir_offset = offset;
    header_.signal_dir_size = data.size();
}

void SSFWriter::write_data_blocks() {
    std::uint64_t offset = file_.tellp();

    // Write each data block and update L1 index offsets
    for (size_t i = 0; i < data_blocks_.size(); ++i) {
        l1_index_[i].data_offset = file_.tellp();
        write_bytes(file_, data_blocks_[i]);
    }

    header_.data_blocks_offset = offset;
    header_.data_blocks_size = static_cast<std::uint64_t>(file_.tellp()) - offset;
}

void SSFWriter::write_l1_index() {
    std::uint64_t offset = file_.tellp();

    // Write each L1 index entry (28 bytes each)
    for (const auto& entry : l1_index_) {
        write_uint64(file_, entry.timestamp);
        write_uint64(file_, entry.data_offset);
        write_uint64(file_, entry.size);
        write_uint32(file_, entry.block_id);
    }

    header_.l1_index_offset = offset;
    header_.l1_index_size = l1_index_.size() * 28;  // 28 bytes per entry
}

void SSFWriter::finalize() {
    if (finalized_) {
        return;
    }

    // Write all sections
    write_metadata();
    write_scope_directory();
    write_signal_directory();
    write_data_blocks();
    write_l1_index();

    // Update header at file start
    file_.seekp(0);
    header_.write_to_stream(file_);

    // Check I/O errors on flush and close
    file_.flush();
    if (!file_.good()) {
        throw std::runtime_error(fmt::format("SSFWriter: Failed to flush file: {}", filename_));
    }
    file_.close();
    if (file_.fail()) {
        throw std::runtime_error(fmt::format("SSFWriter: Failed to close file: {}", filename_));
    }

    finalized_ = true;
    spdlog::info("SSF file finalized: {}", filename_);
}

//==============================================================================
// Data block builders
//==============================================================================

std::vector<std::byte> SSFWriter::build_dc_data_block(
    const DenseVector& node_voltages,
    const DenseVector& branch_currents) const {
    std::vector<std::byte> data;

    // R0 quantizer (1μV resolution)
    R0Quantizer quantizer{1e-6, 0};

    // Write all values (with filtering)
    std::vector<Real> all_values;
    all_values.insert(all_values.end(), node_voltages.begin(), node_voltages.end());
    all_values.insert(all_values.end(), branch_currents.begin(), branch_currents.end());

    // Apply signal filtering using signal_index_map_
    for (size_t i = 0; i < all_values.size(); ++i) {
        // Check if this signal is filtered out
        if (i < signal_index_map_.size() && signal_index_map_[i] == -1) {
            continue;  // Skip filtered signals
        }

        Real value = all_values[i];
        std::int64_t quant = quantizer.quantize(value);
        // Store as signed varint (use zigzag encoding for negative values)
        std::uint64_t zigzag = (quant << 1) ^ (quant >> 63);
        auto varint = encode_varint(zigzag);
        data.insert(data.end(), varint.begin(), varint.end());
    }

    return data;
}

std::vector<std::vector<std::byte>> SSFWriter::build_transient_data_blocks(
    const solvers::TransientResult& result) const {
    std::vector<std::vector<std::byte>> blocks;

    // R0 quantizer (1μV resolution)
    R0Quantizer quantizer{1e-6, 0};

    for (const auto& tp : result.timepoints) {
        std::vector<std::byte> data;

        // Write all values (node voltages + branch currents) with filtering
        std::vector<Real> all_values;
        all_values.insert(all_values.end(),
                         tp.node_voltages.begin(), tp.node_voltages.end());
        all_values.insert(all_values.end(),
                         tp.branch_currents.begin(), tp.branch_currents.end());

        // Apply signal filtering using signal_index_map_
        for (size_t i = 0; i < all_values.size(); ++i) {
            // Check if this signal is filtered out
            if (i < signal_index_map_.size() && signal_index_map_[i] == -1) {
                continue;  // Skip filtered signals
            }

            Real value = all_values[i];
            std::int64_t quant = quantizer.quantize(value);
            // Zigzag encoding for signed values
            std::uint64_t zigzag = (quant << 1) ^ (quant >> 63);
            auto varint = encode_varint(zigzag);
            data.insert(data.end(), varint.begin(), varint.end());
        }

        blocks.push_back(data);
    }

    return blocks;
}

//==============================================================================
// Compression
//==============================================================================

std::vector<std::byte> SSFWriter::compress_zstd_frame(
    const std::vector<std::byte>& data) const {
    if (header_.compression_type == 0) {
        // No compression - still need streaming frame format
        std::vector<std::byte> frame;
        std::uint32_t uncompressed_size = static_cast<std::uint32_t>(data.size());

        // Little-endian 4 bytes
        frame.push_back(static_cast<std::byte>(uncompressed_size & 0xFF));
        frame.push_back(static_cast<std::byte>((uncompressed_size >> 8) & 0xFF));
        frame.push_back(static_cast<std::byte>((uncompressed_size >> 16) & 0xFF));
        frame.push_back(static_cast<std::byte>((uncompressed_size >> 24) & 0xFF));

        frame.insert(frame.end(), data.begin(), data.end());
        return frame;
    }

    if (header_.compression_type != 3) {
        throw std::runtime_error(
            fmt::format("Unsupported compression type: {}", header_.compression_type));
    }

    // Zstd compression
    size_t compressed_size_bound = ZSTD_compressBound(data.size());
    std::vector<std::byte> compressed(compressed_size_bound);

    size_t compressed_size = ZSTD_compress(
        compressed.data(), compressed.size(),
        data.data(), data.size(),
        3  // Compression level
    );

    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error(
            fmt::format("Zstd compression failed: {}", ZSTD_getErrorName(compressed_size)));
    }

    compressed.resize(compressed_size);

    // Build streaming frame: [uncompressed_size: uint32 LE] + compressed_payload
    std::vector<std::byte> frame;
    std::uint32_t uncompressed_size = static_cast<std::uint32_t>(data.size());

    // Little-endian 4 bytes
    frame.push_back(static_cast<std::byte>(uncompressed_size & 0xFF));
    frame.push_back(static_cast<std::byte>((uncompressed_size >> 8) & 0xFF));
    frame.push_back(static_cast<std::byte>((uncompressed_size >> 16) & 0xFF));
    frame.push_back(static_cast<std::byte>((uncompressed_size >> 24) & 0xFF));

    // Compressed payload
    frame.insert(frame.end(), compressed.begin(), compressed.end());

    return frame;
}

//==============================================================================
// Encoding helpers
//==============================================================================

std::vector<std::byte> SSFWriter::encode_varint(std::uint64_t value) {
    std::vector<std::byte> result;
    do {
        std::uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;  // Continuation bit
        }
        result.push_back(static_cast<std::byte>(byte));
    } while (value != 0);
    return result;
}

std::vector<std::byte> SSFWriter::encode_string(const std::string& str) {
    // SSF V4.3 requires UTF-8 encoded strings
    // Note: std::string in C++ can hold any byte sequence, including UTF-8.
    // This function assumes the input string is valid UTF-8 (or ASCII, which is a subset of UTF-8).
    // For production use, consider adding UTF-8 validation if accepting user input.

    auto length_bytes = encode_varint(str.size());
    std::vector<std::byte> result(length_bytes.begin(), length_bytes.end());

    // Copy UTF-8 bytes directly
    for (char c : str) {
        result.push_back(static_cast<std::byte>(c));
    }

    return result;
}

//==============================================================================
// Binary write helpers
//==============================================================================

void SSFWriter::write_uint8(std::ofstream& out, std::uint8_t value) {
    out.write(reinterpret_cast<const char*>(&value), 1);
    // Check I/O errors after write
    if (!out.good()) {
        throw std::runtime_error("SSFWriter: Failed to write uint8");
    }
}

void SSFWriter::write_uint16(std::ofstream& out, std::uint16_t value) {
    // Little-endian
    std::uint8_t bytes[2];
    bytes[0] = value & 0xFF;
    bytes[1] = (value >> 8) & 0xFF;
    out.write(reinterpret_cast<const char*>(bytes), 2);
    // Check I/O errors after write
    if (!out.good()) {
        throw std::runtime_error("SSFWriter: Failed to write uint16");
    }
}

void SSFWriter::write_uint32(std::ofstream& out, std::uint32_t value) {
    // Little-endian
    std::uint8_t bytes[4];
    bytes[0] = value & 0xFF;
    bytes[1] = (value >> 8) & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 24) & 0xFF;
    out.write(reinterpret_cast<const char*>(bytes), 4);
    // Check I/O errors after write
    if (!out.good()) {
        throw std::runtime_error("SSFWriter: Failed to write uint32");
    }
}

void SSFWriter::write_uint64(std::ofstream& out, std::uint64_t value) {
    // Little-endian
    std::uint8_t bytes[8];
    bytes[0] = value & 0xFF;
    bytes[1] = (value >> 8) & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 24) & 0xFF;
    bytes[4] = (value >> 32) & 0xFF;
    bytes[5] = (value >> 40) & 0xFF;
    bytes[6] = (value >> 48) & 0xFF;
    bytes[7] = (value >> 56) & 0xFF;
    out.write(reinterpret_cast<const char*>(bytes), 8);
    // Check I/O errors after write
    if (!out.good()) {
        throw std::runtime_error("SSFWriter: Failed to write uint64");
    }
}

void SSFWriter::write_bytes(std::ofstream& out, const std::vector<std::byte>& data) {
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    // Check I/O errors after write
    if (!out.good()) {
        throw std::runtime_error("SSFWriter: Failed to write bytes");
    }
}

} // namespace kirimaspice::output
