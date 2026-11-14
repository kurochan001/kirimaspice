// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/solvers/transient_solver.hpp"

namespace kirimaspice::output {

/**
 * @brief SPICE3 RAW Binary format writer
 *
 * Format specification:
 * - ASCII header section (Title, Date, Plotname, Flags, Variables)
 * - Binary data section (8-byte double precision floats)
 * - Byte order of the host (little-endian on x86-64 and arm64)
 * - ngspice-47 loads a transient plot written by it (checked 2026-11-14)
 */
class RawBinaryWriter {
public:
    /**
     * @brief Construct a new Raw Binary Writer
     * @param filepath Output file path (.raw extension)
     * @param append_mode If true, append to existing file
     */
    RawBinaryWriter(std::string filepath, bool append_mode);

    /**
     * @brief Write DC operating point analysis results
     * @param node_names Node names (e.g., "v(out)")
     * @param node_voltages Node voltage vector
     * @param branch_names Branch current names (e.g., "i(V1)")
     * @param branch_currents Branch current vector
     */
    void write_dc(const std::vector<std::string>& node_names,
                  const DenseVector& node_voltages,
                  const std::vector<std::string>& branch_names,
                  const DenseVector& branch_currents);

    /**
     * @brief Write DC sweep analysis results
     * @param sweep_name Sweep variable name
     * @param nested_name Optional nested sweep variable name
     * @param node_names Node names
     * @param branch_names Branch current names
     * @param result DC sweep result data
     */
    void write_dc_sweep(const std::string& sweep_name,
                        const std::optional<std::string>& nested_name,
                        const std::vector<std::string>& node_names,
                        const std::vector<std::string>& branch_names,
                        const DCSweepResult& result);

    /**
     * @brief Write transient analysis results
     * @param node_names Node names
     * @param branch_names Branch current names
     * @param result Transient result data
     */
    void write_transient(const std::vector<std::string>& node_names,
                         const std::vector<std::string>& branch_names,
                         const solvers::TransientResult& result);

private:
    static constexpr int OUTPUT_PRECISION = 12;

    std::ofstream open_stream();
    std::string current_timestamp() const;
    std::vector<std::string> align_node_names(const std::vector<std::string>& node_names,
                                              std::size_t expected) const;

    void write_variables(std::ofstream& out,
                         const std::vector<std::pair<std::string, std::string>>& vars) const;

    void write_header(std::ofstream& out,
                      const std::string& plotname,
                      const std::string& flags,
                      std::size_t num_vars,
                      std::size_t num_points,
                      const std::string& title_suffix = "") const;

    void write_binary_marker(std::ofstream& out) const;

    void write_binary_values(std::ofstream& out,
                             const std::vector<Real>& values) const;

    std::vector<Real> build_dc_values(const DenseVector& node_voltages,
                                      const DenseVector& branch_currents) const;

    std::string filepath_;
    bool append_mode_;
};

} // namespace kirimaspice::output
