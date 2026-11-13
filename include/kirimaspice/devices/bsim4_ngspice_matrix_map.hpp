#pragma once

// Helper utilities to bridge ngspice BSIM4 matrix pointers to KirimaSpice MNA
// Only compiled when the vendored ngspice BSIM4 reference is enabled.

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF

#include "kirimaspice/core/matrix_types.hpp"
extern "C" {
#include "bsim4def.h"
}
#include <vector>

namespace kirimaspice::devices {

/// Row/col indices (0-based, ground=-1) for BSIM4 terminal/internal nodes.
struct Bsim4NodeIndices {
    int d = -1;   ///< External drain
    int g = -1;   ///< External gate
    int s = -1;   ///< External source
    int b = -1;   ///< External bulk
    int dp = -1;  ///< Drain prime
    int gp = -1;  ///< Gate prime
    int sp = -1;  ///< Source prime
    int bp = -1;  ///< Bulk prime
    int q = -1;   ///< NQS charge node (may be -1 when disabled)

    // Optional internal nodes for rgate/rbody models
    int ge = -1;  ///< Gate external
    int gm = -1;  ///< Gate mid (internal gate resistor)
    int db = -1;  ///< Drain body node (rbody)
    int sb = -1;  ///< Source body node (rbody)
};

/// Stores matrix entries that ngspice's BSIM4load writes through pointer fields.
/// After BSIM4load is called, accumulated values can be merged into the MNA G matrix.
class Bsim4MatrixMap {
public:
    struct Entry {
        int row;
        int col;
        double value;
    };

    /// Bind a BSIM4 matrix pointer field to internal storage (or dummy if invalid).
    void bind(double*& field, int row, int col);

    /// Reset all stored values to zero (reused across Newton iterations).
    void clear_values();

    /// Clear bindings (entries and dummy storage). All pointers must be rebound after this.
    void reset();

    /// Add accumulated values into the given MNA system.
    void accumulate_to_mna(MNASystem& mna) const;

private:
    std::vector<Entry> entries_;
    std::vector<double> dummy_;  // storage for invalid (grounded/unused) pointers
};

/// Wire the minimal BSIM4 pointer set (no rgate/rbody/rds modules) to the map.
void bind_basic_matrix_pointers(BSIM4instance& inst,
                                const Bsim4NodeIndices& idx,
                                Bsim4MatrixMap& map);

/// Wire additional pointers when rgateMod != 0 (gate resistance network).
void bind_rgate_pointers(BSIM4instance& inst,
                        const Bsim4NodeIndices& idx,
                        Bsim4MatrixMap& map);

/// Wire additional pointers when rbodyMod == 1 or 2 (body resistance network).
void bind_rbody_pointers(BSIM4instance& inst,
                        const Bsim4NodeIndices& idx,
                        Bsim4MatrixMap& map);

/// Wire additional pointers when rdsMod != 0 (drain/source resistance to gate/bulk).
void bind_rds_pointers(BSIM4instance& inst,
                      const Bsim4NodeIndices& idx,
                      Bsim4MatrixMap& map);

} // namespace kirimaspice::devices

#endif // KIRIMASPICE_USE_NGSPICE_BSIM4_REF
