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

class RawWriter {
public:
    RawWriter(std::string filepath, bool append_mode);

    void write_dc(const std::vector<std::string>& node_names,
                  const DenseVector& node_voltages,
                  const std::vector<std::string>& branch_names,
                  const DenseVector& branch_currents);

    void write_dc_sweep(const std::string& sweep_name,
                        const std::optional<std::string>& nested_name,
                        const std::vector<std::string>& node_names,
                        const std::vector<std::string>& branch_names,
                        const DCSweepResult& result);

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
    void write_values_block(std::ofstream& out,
                            std::size_t point_index,
                            Real abscissa,
                            const std::vector<Real>& values) const;
    std::vector<Real> build_dc_values(const DenseVector& node_voltages,
                                      const DenseVector& branch_currents) const;

    std::string filepath_;
    bool append_mode_;
};

} // namespace kirimaspice::output
