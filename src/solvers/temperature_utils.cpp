// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// src/solvers/temperature_utils.cpp
#include "kirimaspice/solvers/temperature_utils.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/diode.hpp"
#include "kirimaspice/devices/bjt_npn.hpp"
#include "kirimaspice/devices/bjt_pnp.hpp"
#include "kirimaspice/devices/jfet.hpp"  // Unified JFET
#include "kirimaspice/devices/mosfet_level1.hpp"  // Unified MOSFET Level 1
#include "kirimaspice/devices/mosfet_bsim1.hpp"    // Unified BSIM1 (Level 9)
#include "kirimaspice/devices/mosfet_bsim3.hpp"  // Unified BSIM3 template
#include "kirimaspice/devices/mosfet_bsim4.hpp"  // Unified BSIM4 template
#include "kirimaspice/devices/mosfet_sic.hpp"    // Unified SiC MOSFET
#include "kirimaspice/devices/gan_hemt.hpp"  // Unified GaN HEMT
#include "kirimaspice/devices/vdmos.hpp"   // Unified VDMOS
#include "kirimaspice/devices/igbt.hpp"  // Unified IGBT
#include "kirimaspice/devices/finfet.hpp"

namespace kirimaspice::solvers {

void apply_temperature_to_devices(const circuit::CircuitGraph& circuit, Real temp_K) {
    for (const auto& device : circuit.get_devices()) {
        switch (device->type()) {
            case DeviceType::DIODE:
                if (auto* d = dynamic_cast<devices::Diode*>(device.get())) d->set_temperature(temp_K);
                break;
            case DeviceType::BJT_NPN:
                if (auto* b = dynamic_cast<devices::BJT_NPN*>(device.get())) b->set_temperature(temp_K);
                break;
            case DeviceType::BJT_PNP:
                if (auto* b = dynamic_cast<devices::BJT_PNP*>(device.get())) b->set_temperature(temp_K);
                break;
            case DeviceType::JFET_N:
                if (auto* j = dynamic_cast<devices::JFET_N*>(device.get())) j->set_temperature(temp_K);
                break;
            case DeviceType::JFET_P:
                if (auto* j = dynamic_cast<devices::JFET_P*>(device.get())) j->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_N:
                if (auto* m = dynamic_cast<devices::MOSFET_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_P:
                if (auto* m = dynamic_cast<devices::MOSFET_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_BSIM1_N:
                if (auto* m = dynamic_cast<devices::MOSFET_BSIM1_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_BSIM1_P:
                if (auto* m = dynamic_cast<devices::MOSFET_BSIM1_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_BSIM3_N:
                if (auto* m = dynamic_cast<devices::MOSFET_BSIM3_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_BSIM3_P:
                if (auto* m = dynamic_cast<devices::MOSFET_BSIM3_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_BSIM4_N:
                if (auto* m = dynamic_cast<devices::MOSFET_BSIM4_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_BSIM4_P:
                if (auto* m = dynamic_cast<devices::MOSFET_BSIM4_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_SIC_N:
                if (auto* m = dynamic_cast<devices::MOSFET_SiC_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::MOSFET_SIC_P:
                if (auto* m = dynamic_cast<devices::MOSFET_SiC_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::GAN_HEMT_N:
                if (auto* m = dynamic_cast<devices::GaN_HEMT_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::GAN_HEMT_P:
                if (auto* m = dynamic_cast<devices::GaN_HEMT_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::VDMOS_N:
                if (auto* m = dynamic_cast<devices::VDMOS_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::VDMOS_P:
                if (auto* m = dynamic_cast<devices::VDMOS_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::IGBT_N:
                if (auto* m = dynamic_cast<devices::IGBT_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::IGBT_P:
                if (auto* m = dynamic_cast<devices::IGBT_P*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::FINFET_N:
                if (auto* m = dynamic_cast<devices::FinFET_N*>(device.get())) m->set_temperature(temp_K);
                break;
            case DeviceType::FINFET_P:
                if (auto* m = dynamic_cast<devices::FinFET_P*>(device.get())) m->set_temperature(temp_K);
                break;
            default:
                break; // Devices without temperature support remain unchanged
        }
    }
}

}  // namespace kirimaspice::solvers
