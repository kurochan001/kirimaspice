// Copyright (c) 2026 The kirimaspice Authors
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// include/kirimaspice/devices/bjt_npn.hpp
#pragma once

#include "kirimaspice/devices/bjt.hpp"

namespace kirimaspice::devices {

/// The NPN bipolar transistor: the Gummel-Poon device of bjt.hpp
using BJT_NPN = BJT<BJTPolarity::NPN>;

}  // namespace kirimaspice::devices
