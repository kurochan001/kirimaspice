// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Debug test to check BJT current calculation
#include "kirimaspice/devices/bjt_npn.hpp"
#include <iostream>
#include <iomanip>

using namespace kirimaspice;
using namespace kirimaspice::devices;

int main() {
    std::cout << "=== BJT Current Calculation Debug ===\n\n";

    // Create BJT with same parameters as ngspice
    BJT_NPN bjt(1, "Q1", 2, 3, 0,  // id, name, C, B, E (node IDs)
                1e-14,              // IS
                100.0,              // BF
                1.0);               // NF

    // Test with ngspice's Vbe = 0.701V
    double vbe_ngspice = 0.700606;
    double vbc = 0.0;  // Ignored in simplified model

    double ib = bjt.get_base_current(vbe_ngspice, vbc);
    double ic = bjt.get_collector_current(vbe_ngspice, vbc);
    double g_be = bjt.get_base_conductance_vbe(vbe_ngspice, vbc);

    std::cout << std::fixed << std::setprecision(9);
    std::cout << "Input:\n";
    std::cout << "  Vbe = " << vbe_ngspice << " V\n\n";

    std::cout << "BJT Model Output:\n";
    std::cout << "  Ib  = " << ib << " A = " << (ib * 1e3) << " mA\n";
    std::cout << "  Ic  = " << ic << " A = " << (ic * 1e3) << " mA\n";
    std::cout << "  g_be = " << g_be << " S\n\n";

    std::cout << "Expected (from ngspice):\n";
    std::cout << "  Ib  ≈ 0.0493 mA\n";
    std::cout << "  Ic  ≈ 4.933 mA\n\n";

    // Also test with my solver's Vbe = 0.698V (Rb=1k case)
    double vbe_mine = 0.698;
    ib = bjt.get_base_current(vbe_mine, vbc);
    ic = bjt.get_collector_current(vbe_mine, vbc);

    std::cout << "With Vbe = 0.698V (my solver, Rb=1k):\n";
    std::cout << "  Ib  = " << ib << " A = " << (ib * 1e3) << " mA\n";
    std::cout << "  Ic  = " << ic << " A = " << (ic * 1e3) << " mA\n";
    std::cout << "  Expected: Ib ≈ 0.048 mA, Ic ≈ 4.826 mA (from ngspice)\n";

    return 0;
}
