// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <gtest/gtest.h>
#include "kirimaspice/util/units.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <sstream>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

using kirimaspice::Real;
using kirimaspice::util::parse_value_with_unit;

TEST(UnitsParse, MilliAmpAndMicroFarad) {
    // 10mA -> 0.01 A
    EXPECT_NEAR(parse_value_with_unit("10mA"), 0.01, 1e-9);
    // 4.7uF -> 4.7e-6 F
    EXPECT_NEAR(parse_value_with_unit("4.7uF"), 4.7e-6, 1e-12);

    // MEG (mega) prefix in SPICE style
    EXPECT_NEAR(parse_value_with_unit("2meg"), 2e6, 1e-3);
    // milli-ohm should stay milli even with following unit letters
    EXPECT_NEAR(parse_value_with_unit("5mOhm"), 5e-3, 1e-12);
    // kHz suffix keeps kilo scaling and drops unit
    EXPECT_NEAR(parse_value_with_unit("1kHz"), 1e3, 1e-9);
    // pico-farad upper bound
    EXPECT_NEAR(parse_value_with_unit("0.5pF"), 0.5e-12, 1e-15);
    // tera prefix
    EXPECT_NEAR(parse_value_with_unit("7T"), 7e12, 1e-3);
}

TEST(UnitsParse, MixedCaseAndFollowedUnits) {
    // Lower-case m should mean milli even when followed by unit letters
    EXPECT_NEAR(parse_value_with_unit("1mV"), 1e-3, 1e-9);
    EXPECT_NEAR(parse_value_with_unit("3mOhm"), 3e-3, 1e-12);

    // Upper-case MEG and mixed-case should still map to mega
    EXPECT_NEAR(parse_value_with_unit("1MEG"), 1e6, 1e-3);
    EXPECT_NEAR(parse_value_with_unit("2Meg"), 2e6, 1e-3);

    // Letters after a scale factor are dropped; M is milli whatever follows
    // (NETLIST_CONVENTIONS rule 1, as in ngspice), so 10MHz is 10 mHz
    EXPECT_NEAR(parse_value_with_unit("10MHz"), 10e-3, 1e-15);
    EXPECT_NEAR(parse_value_with_unit("0.5GHz"), 0.5e9, 1e-3);

    // Very small prefix to exercise precision warning path (no throw expected)
    EXPECT_NEAR(parse_value_with_unit("1fA"), 1e-15, 1e-21);
}

// NETLIST_CONVENTIONS rule 1: the scale factors of ngspice, in any case.
TEST(UnitsParse, ScaleFactorsFollowNgspice) {
    EXPECT_NEAR(parse_value_with_unit("1T"), 1e12, 1e-3);
    EXPECT_NEAR(parse_value_with_unit("1g"), 1e9, 1e-6);
    EXPECT_NEAR(parse_value_with_unit("1meg"), 1e6, 1e-9);
    EXPECT_NEAR(parse_value_with_unit("1K"), 1e3, 1e-12);
    EXPECT_NEAR(parse_value_with_unit("1M"), 1e-3, 1e-18);
    EXPECT_NEAR(parse_value_with_unit("1m"), 1e-3, 1e-18);
    EXPECT_NEAR(parse_value_with_unit("2MIL"), 2 * 25.4e-6, 1e-18);
    EXPECT_NEAR(parse_value_with_unit("2mil"), 2 * 25.4e-6, 1e-18);
    EXPECT_NEAR(parse_value_with_unit("1u"), 1e-6, 1e-21);
    EXPECT_NEAR(parse_value_with_unit("1N"), 1e-9, 1e-24);
    EXPECT_NEAR(parse_value_with_unit("1p"), 1e-12, 1e-27);
    EXPECT_NEAR(parse_value_with_unit("1F"), 1e-15, 1e-30);
    EXPECT_NEAR(parse_value_with_unit("3a"), 3e-18, 1e-33);
    // Letters that are not a scale factor are ignored
    EXPECT_NEAR(parse_value_with_unit("10Ohm"), 10.0, 1e-12);
    EXPECT_NEAR(parse_value_with_unit("5V"), 5.0, 1e-12);
}

// A scale factor multiplies the number in double precision, and IEEE 754
// rounds the product correctly: the result is within one rounding of the
// factor and one of the product (together at most the machine epsilon,
// relative) of the decimal value, however far apart the exponents are. So
// no scale factor loses precision, and parsing one warns of nothing.
TEST(UnitsParse, ScaleFactorsKeepDoublePrecisionWithoutWarning) {
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    auto logger = std::make_shared<spdlog::logger>("units_parse", sink);
    logger->set_level(spdlog::level::trace);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);

    const struct { const char* scaled; const char* decimal; } cases[] = {
        {"1.5T", "1.5e12"}, {"1.5G", "1.5e9"}, {"1.5meg", "1.5e6"}, {"1.5k", "1.5e3"},
        {"1.5m", "1.5e-3"}, {"1.5u", "1.5e-6"}, {"1.5n", "1.5e-9"}, {"1.5p", "1.5e-12"},
        {"10f", "10e-15"}, {"1.5f", "1.5e-15"}, {"5a", "5e-18"}, {"1.5a", "1.5e-18"},
        {"123456789012345f", "123456789012345e-15"}};
    for (const auto& c : cases) {
        const Real decimal = std::strtod(c.decimal, nullptr);
        const Real scaled = parse_value_with_unit(c.scaled);
        EXPECT_LE(std::fabs(scaled - decimal), std::numeric_limits<Real>::epsilon() * std::fabs(decimal))
            << c.scaled;
    }
    spdlog::set_default_logger(previous);
    EXPECT_EQ(captured.str(), "");
}

// Only letters, a scale factor and a unit, may follow the number; anything
// else is an error, not dropped (parse_value_with_unit). ngspice-47 (run
// 2026-11-21) stops on "R1 a 0 2*1k" with "unknown parameter (*)", and reads
// 1.5.3k as 1.5, 1k5 as 1k and 2k_x as 2k; here each is an error.
TEST(UnitsParse, TextAfterTheNumberIsAnError) {
    for (const char* text : {"2*1k", "1.5.3k", "1k5", "2k_x", "2.5e+", "3)", "0x10", "1k,"}) {
        EXPECT_THROW(parse_value_with_unit(text), std::runtime_error) << text;
    }
    EXPECT_DOUBLE_EQ(parse_value_with_unit(" 1.5k "), 1.5e3);
    EXPECT_DOUBLE_EQ(parse_value_with_unit("-2.5e-3"), -2.5e-3);
    EXPECT_DOUBLE_EQ(parse_value_with_unit("1e3meg"), 1e9);
}

// A .PARAM value is an expression in braces, a number, or else an expression
// without braces (read_parameter_value): ngspice-47 (run 2026-11-21) gives
// r = 2000 for ".param r = 2*1000", in a subcircuit too
TEST(UnitsParse, ParameterValueWithoutBraces) {
    auto twice = [](const std::string& expression) -> Real {
        if (expression == "2*1000") {
            return 2000.0;
        }
        throw std::runtime_error("not an expression: " + expression);
    };
    EXPECT_DOUBLE_EQ(kirimaspice::util::read_parameter_value("2*1000", twice), 2000.0);
    EXPECT_DOUBLE_EQ(kirimaspice::util::read_parameter_value("{2*1000}", twice), 2000.0);
    EXPECT_DOUBLE_EQ(kirimaspice::util::read_parameter_value("1.5k", twice), 1500.0);
    EXPECT_THROW(kirimaspice::util::read_parameter_value("2*1k", twice), std::runtime_error);
}
