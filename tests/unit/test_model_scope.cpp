// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// A .MODEL in a .SUBCKT is local to its body, where it hides a global model
// of its name, as in ngspice-47 (run 2026-11-21): with dm IS = 1e-14 at the
// top level and IS = 1e-10 in the subcircuit, a diode at 0.6 V draws
// 1.18720e-4 A outside and 1.18720 A inside; a model defined only in a
// subcircuit is not found outside it ("could not find a valid modelname").

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/util/string_utils.hpp"

using namespace kirimaspice;

namespace {

double saturation_current(const parser::NetlistParseResult& parsed, const std::string& name) {
    for (const auto& device : parsed.circuit->get_devices()) {
        if (util::equals_icase(device->name(), name)) {
            const auto diode = std::dynamic_pointer_cast<devices::Diode>(device);
            if (diode) {
                return diode->saturation_current();
            }
        }
    }
    ADD_FAILURE() << "no diode " << name;
    return 0.0;
}

}  // namespace

TEST(ModelScope, ModelOfASubcircuitHidesTheGlobalOne) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.model dm D IS=1e-14\n.subckt sub a\n.model dm D IS=1e-10\nD1 a 0 dm\n.ends\n"
        "V1 a 0 0.6\nD2 a 0 dm\nX1 b sub\nV2 b 0 0.6\n.end\n");
    parsed.circuit->expand_all_subcircuits();
    EXPECT_DOUBLE_EQ(saturation_current(parsed, "D2"), 1e-14);
    EXPECT_DOUBLE_EQ(saturation_current(parsed, "X1.D1"), 1e-10);
    EXPECT_EQ(parsed.config.models.size(), 1u);
}

TEST(ModelScope, EachSubcircuitKeepsItsOwnModel) {
    parser::NetlistParser parser;
    auto parsed = parser.parse_string(
        "t\n.subckt s1 a\n.model dm D IS=1e-14\nD1 a 0 dm\n.ends\n"
        ".subckt s2 a\n.model DM D IS=1e-10\nD1 a 0 dm\n.ends\n"
        "V1 a 0 0.6\nX1 a s1\nV2 b 0 0.6\nX2 b s2\n.end\n");
    parsed.circuit->expand_all_subcircuits();
    EXPECT_DOUBLE_EQ(saturation_current(parsed, "X1.D1"), 1e-14);
    EXPECT_DOUBLE_EQ(saturation_current(parsed, "X2.D1"), 1e-10);
}

TEST(ModelScope, ModelOfASubcircuitIsNotFoundOutsideIt) {
    parser::NetlistParser parser;
    EXPECT_ANY_THROW(parser.parse_string(
        "t\n.subckt sub a\n.model dl D IS=1e-10\nD1 a 0 dl\n.ends\nV1 a 0 0.6\nD2 a 0 dl\n.end\n"));
}
