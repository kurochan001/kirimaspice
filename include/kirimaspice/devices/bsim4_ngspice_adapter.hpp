#pragma once

#include <vector>
#include <string>
#include <optional>
#include <memory>
#include "kirimaspice/core/types.hpp"

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF
#include "kirimaspice/devices/bsim4_ngspice_matrix_map.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <vector>
#endif

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF
extern "C" {
#include "bsim4def.h"
}
#else
// Forward decls when ngspice is disabled
extern "C" {
struct BSIM4model;
struct BSIM4instance;
}
#endif

namespace kirimaspice {

struct NgspiceBsim4Result {
    Real ids {0.0};
    Real vgs {0.0};
    Real vds {0.0};
    Real vbs {0.0};
    Real vth {0.0};
    Real vdsat {0.0};
};

// Minimal adapter interface: create ngspice model/instance from our params and evaluate DC.
class NgspiceBsim4Adapter {
public:
    // Build model/instance once and cache inside adapter.
    static std::unique_ptr<NgspiceBsim4Adapter> create_from_defaults();

    // Evaluate at a single bias point (DC operating point only).
    std::optional<NgspiceBsim4Result> evaluate_dc(Real vgs, Real vds, Real vbs, Real temp_K = 300.15);

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF
    // Expose raw instance/model for pointer wiring (advanced use).
    BSIM4instance* raw_instance() { return inst_; }
    BSIM4model* raw_model() { return model_; }

    // Bind basic matrix pointers to our map using provided node indices (ground=-1, others 0-based).
    void bind_basic_matrix(const devices::Bsim4NodeIndices& idx,
                           devices::Bsim4MatrixMap& map);

    // Experimental: run BSIM4temp/load for one DC point and accumulate matrix into MNA.
    bool load_dc_into_mna(const devices::Bsim4NodeIndices& idx,
                          const DenseVector& node_voltages,
                          devices::Bsim4MatrixMap& map,
                          MNASystem& mna,
                          Real temp_K = 300.15);
#endif

    ~NgspiceBsim4Adapter();

private:
    NgspiceBsim4Adapter(BSIM4model* m, BSIM4instance* inst);
    BSIM4model* model_;
    BSIM4instance* inst_;

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF
    struct NgCktContext;
    std::unique_ptr<NgCktContext> ctx_;
#endif
};

} // namespace kirimaspice
