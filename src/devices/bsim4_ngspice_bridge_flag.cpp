#include "kirimaspice/devices/bsim4_ngspice_bridge_flag.hpp"

namespace kirimaspice::devices {

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF
static bool g_bridge_enabled = false;

bool ngspice_bsim4_bridge_enabled() {
    return g_bridge_enabled;
}

void set_ngspice_bsim4_bridge_enabled(bool enable) {
    g_bridge_enabled = enable;
}
#else
bool ngspice_bsim4_bridge_enabled() { return false; }
void set_ngspice_bsim4_bridge_enabled(bool) {}
#endif

} // namespace kirimaspice::devices

