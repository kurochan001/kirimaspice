#pragma once

// Simple global flag to enable/disable ngspice BSIM4 bridge stamping.
// When KIRIMASPICE_USE_NGSPICE_BSIM4_REF is OFF, setters are no-ops and getter returns false.

namespace kirimaspice::devices {

bool ngspice_bsim4_bridge_enabled();
void set_ngspice_bsim4_bridge_enabled(bool enable);

} // namespace kirimaspice::devices

