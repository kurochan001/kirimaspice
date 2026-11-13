// include/kirimaspice/output/ac_csv_writer.hpp
#pragma once

#include "kirimaspice/core/types.hpp"
#include "kirimaspice/solvers/ac_solver.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"

#include <string>
#include <fstream>
#include <vector>

namespace kirimaspice::output {

/// CSV Writer for AC Analysis Results
///
/// Output format:
/// - Header lines (starting with #): metadata
/// - Column headers: frequency, v(node1)_real, v(node1)_imag, v(node1)_mag_dB, v(node1)_phase_deg, ...
/// - Data rows: one row per frequency point
class ACCSVWriter {
public:
    /// Constructor
    /// @param filepath Output CSV file path
    /// @param circuit Circuit graph (for node names)
    ACCSVWriter(const std::string& filepath,
                const circuit::CircuitGraph& circuit);

    /// Write AC analysis result to CSV
    /// @param result AC analysis result
    /// @param title Circuit title (optional)
    void write(const solvers::ACResult& result,
               const std::string& title = "AC Analysis");

    /// Write metadata header
    void write_metadata(const std::string& title,
                       const solvers::ACResult& result);

    /// Write column headers
    void write_column_headers(const std::vector<std::string>& node_names);

    /// Write data rows
    void write_data_rows(const solvers::ACResult& result);

private:
    std::string filepath_;
    const circuit::CircuitGraph& circuit_;
    std::ofstream out_;

    /// Get node name from node ID
    std::string get_node_name(NodeId node_id) const;

    /// Compute magnitude in dB
    static Real magnitude_db(Complex z) {
        return 20.0 * std::log10(std::abs(z));
    }

    /// Compute phase in degrees
    static Real phase_degrees(Complex z) {
        return std::arg(z) * 180.0 / M_PI;
    }
};

} // namespace kirimaspice::output
