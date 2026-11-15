// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/bjt_pnp.hpp
#pragma once

#include "kirimaspice/devices/bjt.hpp"

namespace kirimaspice::devices {

/// The PNP bipolar transistor: the mirror of the NPN device (bjt.hpp)
using BJT_PNP = BJT<BJTPolarity::PNP>;

}  // namespace kirimaspice::devices
