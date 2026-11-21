// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Input that would change the result is not dropped: an element of a letter
// the program does not simulate is a parse error (parse_element), at the
// top level and in a subcircuit body, in either case of the letter. A
// directive the parser does not read is named in one warning, and every
// directive read is recorded (SimulationConfig::directives_read).

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "kirimaspice/parser/netlist_parser.hpp"

using namespace kirimaspice;

namespace {

std::string parse_error_of(const std::string& netlist) {
    parser::NetlistParser parser;
    try {
        auto parsed = parser.parse_string(netlist);
        parsed.circuit->expand_all_subcircuits();
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST(ParserUnknownInput, UnknownElementLetterIsAnError) {
    for (const char* line : {"J1 in 0 0 JM", "j1 in 0 0 JM", "N1 in 0", "P1 in 0", "U1 in 0 x"}) {
        SCOPED_TRACE(line);
        const std::string error =
            parse_error_of(std::string("t\nV1 in 0 1\nR1 in 0 1k\n") + line + "\n.end\n");
        EXPECT_NE(error.find("Unknown element letter"), std::string::npos) << error;
    }
}

TEST(ParserUnknownInput, UnknownElementLetterInASubcircuitIsAnError) {
    const std::string error = parse_error_of(
        "t\n.subckt cell a\nJ1 a 0 0 JM\n.ends\nV1 in 0 1\nX1 in cell\n.end\n");
    EXPECT_NE(error.find("Unknown element letter"), std::string::npos) << error;
}

TEST(ParserUnknownInput, KnownElementLettersStillParse) {
    EXPECT_EQ(parse_error_of("t\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1n\nL1 out 0 1m\n.end\n"), "");
}

namespace {

// The warnings of parsing netlist, and its configuration
std::string parse_warnings(const std::string& netlist, SimulationConfig* config = nullptr) {
    std::ostringstream captured;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(captured);
    sink->set_pattern("%v");
    auto logger = std::make_shared<spdlog::logger>("unknown_input", sink);
    logger->set_level(spdlog::level::warn);
    const auto previous = spdlog::default_logger();
    spdlog::set_default_logger(logger);
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(netlist);
    spdlog::set_default_logger(previous);
    if (config) {
        *config = parsed.config;
    }
    return captured.str();
}

std::size_t count(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

}  // namespace

TEST(ParserUnknownInput, UnknownDirectiveIsNamedOnce) {
    const std::string warnings =
        parse_warnings("t\nV1 in 0 1\nR1 in 0 1k\n.fooo a\n.fooo b\n.op\n.end\n");
    EXPECT_EQ(count(warnings, "unknown directive .fooo is ignored"), 1u) << warnings;
}

TEST(ParserUnknownInput, UnsupportedDirectivesAreNamed) {
    const std::string warnings = parse_warnings(
        "t\nV1 in 0 1\nR1 in 0 1k\n.noise v(in) V1 dec 10 1 1k\n.measure tran x max v(in)\n.end\n");
    EXPECT_EQ(count(warnings, ".noise is not supported and is ignored"), 1u) << warnings;
    EXPECT_EQ(count(warnings, ".measure is not supported and is ignored"), 1u) << warnings;
}

TEST(ParserUnknownInput, DirectivesOfThePreprocessorAndEndGiveNoWarning) {
    const std::string warnings = parse_warnings(
        "t\nV1 in 0 1\nR1 in 0 1k\n.if (1)\n.op\n.else\n.dc V1 0 1 1\n.endif\n.end\n");
    EXPECT_EQ(warnings, "");
}

TEST(ParserUnknownInput, DirectivesReadAreRecorded) {
    SimulationConfig config;
    parse_warnings("t\nV1 in 0 1\nR1 in 0 1k\n.tf v(in) V1\n.op\n.end\n", &config);
    EXPECT_TRUE(config.directives_read.count(".TF"));
    EXPECT_TRUE(config.directives_read.count(".OP"));
    EXPECT_FALSE(config.directives_read.count(".TRAN"));
}

TEST(ParserUnknownInput, UnusedCardNamesAreNamedOncePerCard) {
    // Two transistors on one card: each name the card gives and the device
    // does not take is named once
    const std::string bjt = parse_warnings(
        "t\nV1 c 0 1\nQ1 c c 0 QM\nQ2 c c 0 QM AREA=2\n.model QM NPN BF=100 FOO=1\n.end\n");
    EXPECT_EQ(count(bjt, "BJT model QM: foo is not used"), 1u) << bjt;
    EXPECT_EQ(count(bjt, "Q2: 'AREA=2' is not used"), 1u) << bjt;
    const std::string bsim4 = parse_warnings(
        "t\nV1 d 0 1\nM1 d d 0 0 NB\nM2 d d 0 0 NB\n.model NB NMOS LEVEL=14 BAR=3\n.end\n");
    EXPECT_EQ(count(bsim4, "BSIM4 model NB: bar is not used"), 1u) << bsim4;
    const std::string mesfet =
        parse_warnings("t\nV1 d 0 1\nZ1 d d 0 ZM\n.model ZM NMF BETA=0.01 QUUX=2\n.end\n");
    EXPECT_EQ(count(mesfet, "MESFET model ZM: quux is not used"), 1u) << mesfet;
}

TEST(ParserUnknownInput, DiodeAndLevel1CardsNameUnusedNamesOncePerCard) {
    const std::string diode = parse_warnings(
        "t\nV1 a 0 1\nR1 a b 1k\nD1 b 0 DM\nD2 b 0 DM\n.model DM D IS=1e-14 KF=1e-16\n.end\n");
    EXPECT_EQ(count(diode, "Diode model DM: kf is not used"), 1u) << diode;
    const std::string level1 = parse_warnings(
        "t\nV1 d 0 1\nM1 d d 0 0 NX\nM2 d d 0 0 NX\n.model NX NMOS LEVEL=1 VTO=0.7 TOX=1e-8\n.end\n");
    EXPECT_EQ(count(level1, "MOSFET model NX (level 1): tox is not used"), 1u) << level1;
}

TEST(ParserUnknownInput, UnusedElementLineNamesOfLevel1AndMesfetAreNamed) {
    // The level-1 element line takes KP, W, L, VTH and the noise parameters;
    // the MESFET element line takes the names of its card
    const std::string level1 = parse_warnings(
        "t\nV1 d 0 1\nM1 d d 0 0 NX W=10u LAMBDA=0.5\n.model NX NMOS LEVEL=1 VTO=0.7\n.end\n");
    EXPECT_EQ(count(level1, "M1: 'LAMBDA=0.5' is not used"), 1u) << level1;
    EXPECT_EQ(count(level1, "W=10u"), 0u) << level1;
    const std::string mesfet = parse_warnings(
        "t\nV1 d 0 1\nZ1 d d 0 ZM BETA=0.02 FOO=1\n.model ZM NMF BETA=0.01\n.end\n");
    EXPECT_EQ(count(mesfet, "Z1: 'FOO=1' is not used"), 1u) << mesfet;
    EXPECT_EQ(count(mesfet, "BETA"), 0u) << mesfet;
}

TEST(ParserUnknownInput, Bsim4JunctionNamesAreNotNamedAsUnused) {
    // CJS, MJS, PBS and their sidewall and drain names are read from the
    // card (bsim4_params_from_model's junction table); JSS is not
    const std::string warnings = parse_warnings(
        "t\nVD d 0 1\nM1 d d 0 0 NB\n.model NB NMOS LEVEL=14 CJS=1e-3 MJS=0.5 PBS=1 CJD=2e-3 "
        "CJSWS=1e-10 MJSWGS=0.3 JSS=1e-4\n.end\n");
    for (const char* name : {"cjs", "mjs", "pbs", "cjd", "cjsws", "mjswgs"}) {
        EXPECT_EQ(count(warnings, std::string("BSIM4 model NB: ") + name + " is not used"), 0u)
            << name << "\n" << warnings;
    }
    EXPECT_EQ(count(warnings, "BSIM4 model NB: jss is not used"), 1u) << warnings;
}

// A .MODEL that gives a name a model already has in its scope (the top level,
// or one .SUBCKT) is named in a warning, and the first is used
// (parse_model_directive). A .MODEL in a .SUBCKT is local to it, so one there
// of a top-level model's name is no second definition; ngspice-47 (run
// 2026-11-21) keeps it to the subcircuit: with dm IS=1e-14 at the top and
// IS=1e-10 in the subcircuit, a diode at 0.6 V draws 0.119 mA outside it and
// 1.19 A in it; and of two definitions at the top level it uses the first.
TEST(ParserUnknownInput, ModelDefinedAgainIsNamed) {
    const std::string twice = parse_warnings(
        "t\n.model dm D IS=1e-14\n.model DM D IS=1e-10\nV1 a 0 0.6\nD1 a 0 dm\n.end\n");
    EXPECT_EQ(count(twice, "model dm is defined again; the first definition is used"), 1u) << twice;
    const std::string in_subcircuit = parse_warnings(
        "t\n.model dm D IS=1e-14\n.subckt sub a\n.model dm D IS=1e-10\nD1 a 0 dm\n.ends\n"
        "V1 a 0 0.6\nD2 a 0 dm\nX1 b sub\nV2 b 0 0.6\n.end\n");
    EXPECT_EQ(count(in_subcircuit, "defined again"), 0u) << in_subcircuit;
    const std::string twice_in_subcircuit = parse_warnings(
        "t\n.subckt sub a\n.model dm D IS=1e-14\n.model dm D IS=1e-10\nD1 a 0 dm\n.ends\n"
        "X1 b sub\nV2 b 0 0.6\n.end\n");
    EXPECT_EQ(count(twice_in_subcircuit, "model dm is defined again in .SUBCKT sub"), 1u)
        << twice_in_subcircuit;
    const std::string once =
        parse_warnings("t\n.model dm D IS=1e-14\nV1 a 0 0.6\nD1 a 0 dm\n.end\n");
    EXPECT_EQ(count(once, "defined again"), 0u) << once;
}

// VERSION on a card is text, not a number (ModelDefinition::version): 4.8.3
// is the version the BSIM4 code follows (bsim4_params_from_model), and is not
// warned of as 4.8; another is named as written
TEST(ParserUnknownInput, Bsim4VersionIsReadAsText) {
    const std::string netlist =
        "t\nVD d 0 1\nM1 d d 0 0 NB W=1u L=0.1u\nM2 d d 0 0 NC W=1u L=0.1u\n"
        "M3 d d 0 0 ND W=1u L=0.1u\n.model NB NMOS LEVEL=14 version=4.8.3\n"
        ".model NC NMOS LEVEL=14 VERSION=4.8.1\n.model ND NMOS LEVEL=14 version=4.83\n.end\n";
    const std::string warnings = parse_warnings(netlist);
    EXPECT_EQ(count(warnings, "targets version"), 1u) << warnings;
    EXPECT_EQ(count(warnings, "BSIM4 model NC targets version 4.8.1;"), 1u) << warnings;
    EXPECT_EQ(count(warnings, "version is not used"), 0u) << warnings;
    SimulationConfig config;
    parse_warnings(netlist, &config);
    EXPECT_EQ(config.find_model("NB")->version, "4.8.3");
    EXPECT_EQ(config.find_model("NB")->params.count("version"), 0u);
}

// The BSIM4 element line takes W, L, NF, M, AS, AD, PS, PD, NRD, NRS and the
// MULT_ multipliers (set_bsim4_instance_parameter); another token is named as
// on a level-1 line. ngspice-47 (run 2026-11-21) stops on FOO=3 with
// "unknown parameter (foo)".
TEST(ParserUnknownInput, UnusedElementLineNamesOfBsim4AreNamed) {
    const std::string bsim4 = parse_warnings(
        "t\nVD d 0 1\nM1 d d 0 0 NB W=1u L=0.1u NF=2 M=2 AS=1p FOO=3 bar\n"
        ".model NB NMOS LEVEL=14\n.end\n");
    EXPECT_EQ(count(bsim4, "M1: 'FOO=3' is not used"), 1u) << bsim4;
    EXPECT_EQ(count(bsim4, "M1: 'bar' is not used"), 1u) << bsim4;
    EXPECT_EQ(count(bsim4, "' is not used"), 2u) << bsim4;
}

TEST(ParserUnknownInput, UnusedElementLineNamesOfEveryMosfetLevelAreNamed) {
    // Levels 3, 8/49 and 9 take W and L from the element line, level 12
    // (FinFET) L only: the other tokens, M= and AD= among them, are not used
    // and are named, as for level 1 and BSIM4
    for (const char* level : {"3", "8", "9"}) {
        const std::string warnings = parse_warnings(std::string("t\nVD d 0 1\nM1 d d 0 0 NX W=2u L=1u AD=1p M=2\n") +
                                                    ".model NX NMOS LEVEL=" + level + "\n.end\n");
        EXPECT_EQ(count(warnings, "M1: 'AD=1p' is not used"), 1u) << level << "\n" << warnings;
        EXPECT_EQ(count(warnings, "M1: 'M=2' is not used"), 1u) << level << "\n" << warnings;
        EXPECT_EQ(count(warnings, "W=2u"), 0u) << level << "\n" << warnings;
    }
    const std::string finfet =
        parse_warnings("t\nVD d 0 1\nM1 d d 0 0 NX W=2u L=30n\n.model NX NMOS LEVEL=12\n.end\n");
    EXPECT_EQ(count(finfet, "M1: 'W=2u' is not used"), 1u) << finfet;
    EXPECT_EQ(count(finfet, "L=30n"), 0u) << finfet;
}

TEST(ParserUnknownInput, Bsim4MobilityModelNotComputedIsNamed) {
    // MOBMOD 0, 1 and 2 are computed; 3 to 6 are computed as MOBMOD 0, and
    // the card is told so
    for (const char* mobmod : {"3", "4", "6"}) {
        const std::string warnings = parse_warnings(std::string("t\nVD d 0 1\nM1 d d 0 0 nb W=1u L=1u\n") +
                                                    ".model nb nmos level=14 mobmod=" + mobmod + "\n.end\n");
        EXPECT_EQ(count(warnings, std::string("MOBMOD ") + mobmod + " is not computed; MOBMOD 0 is used"), 1u)
            << warnings;
    }
    for (const char* mobmod : {"0", "1", "2"}) {
        const std::string warnings = parse_warnings(std::string("t\nVD d 0 1\nM1 d d 0 0 nb W=1u L=1u\n") +
                                                    ".model nb nmos level=14 mobmod=" + mobmod + "\n.end\n");
        EXPECT_EQ(count(warnings, "is not computed"), 0u) << warnings;
    }
}
