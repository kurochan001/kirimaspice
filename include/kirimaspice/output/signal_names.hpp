// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// The names of signals in the files the program writes: v(<node>) and
// i(<source>), in lower case, as ngspice-47 writes them in both raw forms
// ("v(out)", "i(v1)"). The ASCII and binary raw writers and the SSF writer
// all take them from here.

#pragma once

#include <cctype>
#include <string>

namespace kirimaspice::output {

inline std::string lower_case(std::string name) {
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return name;
}

inline std::string voltage_signal(const std::string& node) { return "v(" + lower_case(node) + ")"; }

inline std::string current_signal(const std::string& source) {
    return "i(" + lower_case(source) + ")";
}

}  // namespace kirimaspice::output
