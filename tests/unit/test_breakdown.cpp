// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Breakdown above VBD. The GaN HEMT adds the current 1 mS (VDS - VBD) with
// its conductance, with the channel on or off (GaN_HEMT::drain_current);
// before, it added the conductance alone, and nothing with the channel off.
// The VDMOS names its breakdown once, not at every Newton iteration: 700 V
// through 1 Mohm with VBD = 600 gave 97 warnings.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/solvers/dc_solver.hpp"

using namespace kirimaspice;

namespace {

struct Solved {
    DenseVector x;
    std::string warnings;
    parser::NetlistParseResult parsed;
};

Solved solve(const std::string& netlist) {
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("breakdown", sink);
    logger->set_level(spdlog::level::warn);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    parser::NetlistParser parser;
    Solved s{{}, {}, parser.parse_string(netlist)};
    solvers::DCSolver solver(*s.parsed.circuit, s.parsed.config);
    SolverStats stats;
    const auto status = solver.solve(s.x, stats);
    spdlog::set_default_logger(previous);
    EXPECT_EQ(status, ConvergenceStatus::SUCCESS) << netlist;
    s.warnings = captured.str();
    return s;
}

double unknown(const Solved& s, const std::string& name) {
    for (std::uint32_t i = 0; i < s.x.size(); ++i) {
        if (s.parsed.circuit->unknown_name(i) == name) {
            return s.x[i];
        }
    }
    ADD_FAILURE() << "no unknown " << name;
    return 0.0;
}

std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

}  // namespace

TEST(Breakdown, GanHemtOffCarriesTheBreakdownCurrent) {
    // Gate off, 1300 V through 1 Mohm, VBD 1200 V: (1300 - V)/1e6 =
    // 1e-3 (V - 1200), V = 1201.3 / 1.001 (analytic for the model's rule)
    const Solved s = solve("t\nVG g 0 -5\nVD dd 0 1300\nR1 dd d 1meg\nM1 d g 0 m\n.model m nmos level=11\n.end\n");
    EXPECT_NEAR(unknown(s, "V(d)"), 1201.3 / 1.001, 1e-6);
    EXPECT_EQ(count(s.warnings, "exceeds breakdown"), 1u) << s.warnings;
}

TEST(Breakdown, GanHemtOnAddsTheBreakdownCurrentToTheChannel) {
    // The same channel at VD = 1300 V: VBD = 1200 adds 1 mS x 100 V
    const auto drain = [](const char* vbd) {
        return -unknown(solve(std::string("t\nVG g 0 0\nVD d 0 1300\nM1 d g 0 m\n.model m nmos level=11 vbd=") +
                              vbd + "\n.end\n"),
                        "I(VD)");
    };
    EXPECT_NEAR(drain("1200") - drain("5000"), 0.1, 1e-9);
}

TEST(Breakdown, VdmosNamesItsBreakdownOnce) {
    const Solved s = solve("t\nVG g 0 0\nVD dd 0 700\nR1 dd d 1meg\nM1 d g 0 m\n.model m nmos level=20 vbd=600\n.end\n");
    EXPECT_EQ(count(s.warnings, "entered breakdown region"), 1u) << s.warnings;
}
