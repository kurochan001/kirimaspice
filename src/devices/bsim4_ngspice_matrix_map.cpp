#include "kirimaspice/devices/bsim4_ngspice_matrix_map.hpp"

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF

namespace kirimaspice::devices {

void Bsim4MatrixMap::bind(double*& field, int row, int col) {
    if (row < 0 || col < 0) {
        dummy_.push_back(0.0);
        field = &dummy_.back();
        return;
    }
    entries_.push_back({row, col, 0.0});
    field = &entries_.back().value;
}

void Bsim4MatrixMap::clear_values() {
    for (auto& e : entries_) {
        e.value = 0.0;
    }
    for (auto& d : dummy_) {
        d = 0.0;
    }
}

void Bsim4MatrixMap::reset() {
    entries_.clear();
    dummy_.clear();
}

void Bsim4MatrixMap::accumulate_to_mna(MNASystem& mna) const {
    const std::uint32_t size = mna.size();
    for (const auto& e : entries_) {
        if (e.row >= 0 && e.col >= 0 && static_cast<std::uint32_t>(e.row) < size
            && static_cast<std::uint32_t>(e.col) < size) {
            mna.G.add_entry(static_cast<std::uint32_t>(e.row),
                            static_cast<std::uint32_t>(e.col),
                            static_cast<Real>(e.value));
        }
    }
}

// Bind the core pointer set that BSIM4setup allocates when rgateMod=0, rbodyMod=0, rdsMod=0.
void bind_basic_matrix_pointers(BSIM4instance& inst,
                                const Bsim4NodeIndices& idx,
                                Bsim4MatrixMap& map) {
    map.bind(inst.BSIM4DPbpPtr, idx.dp, idx.bp);
    map.bind(inst.BSIM4GPbpPtr, idx.gp, idx.bp);
    map.bind(inst.BSIM4SPbpPtr, idx.sp, idx.bp);

    map.bind(inst.BSIM4BPdpPtr, idx.bp, idx.dp);
    map.bind(inst.BSIM4BPgpPtr, idx.bp, idx.gp);
    map.bind(inst.BSIM4BPspPtr, idx.bp, idx.sp);
    map.bind(inst.BSIM4BPbpPtr, idx.bp, idx.bp);

    map.bind(inst.BSIM4DdPtr,  idx.d,  idx.d);
    map.bind(inst.BSIM4GPgpPtr, idx.gp, idx.gp);
    map.bind(inst.BSIM4SsPtr,  idx.s,  idx.s);
    map.bind(inst.BSIM4DPdpPtr, idx.dp, idx.dp);
    map.bind(inst.BSIM4SPspPtr, idx.sp, idx.sp);
    map.bind(inst.BSIM4DdpPtr,  idx.d,  idx.dp);
    map.bind(inst.BSIM4GPdpPtr, idx.gp, idx.dp);
    map.bind(inst.BSIM4GPspPtr, idx.gp, idx.sp);
    map.bind(inst.BSIM4SspPtr,  idx.s,  idx.sp);
    map.bind(inst.BSIM4DPspPtr, idx.dp, idx.sp);
    map.bind(inst.BSIM4DPdPtr,  idx.dp, idx.d);
    map.bind(inst.BSIM4DPgpPtr, idx.dp, idx.gp);
    map.bind(inst.BSIM4SPgpPtr, idx.sp, idx.gp);
    map.bind(inst.BSIM4SPsPtr,  idx.sp, idx.s);
    map.bind(inst.BSIM4SPdpPtr, idx.sp, idx.dp);

    // NQS/charge node; idx.q may be -1 when trnqsMod is disabled.
    map.bind(inst.BSIM4QqPtr,  idx.q, idx.q);
    map.bind(inst.BSIM4QbpPtr, idx.q, idx.bp);
    map.bind(inst.BSIM4QdpPtr, idx.q, idx.dp);
    map.bind(inst.BSIM4QspPtr, idx.q, idx.sp);
    map.bind(inst.BSIM4QgpPtr, idx.q, idx.gp);
    map.bind(inst.BSIM4DPqPtr, idx.dp, idx.q);
    map.bind(inst.BSIM4SPqPtr, idx.sp, idx.q);
    map.bind(inst.BSIM4GPqPtr, idx.gp, idx.q);
}

void bind_rgate_pointers(BSIM4instance& inst,
                        const Bsim4NodeIndices& idx,
                        Bsim4MatrixMap& map) {
    // Requires idx.ge / idx.gm
    map.bind(inst.BSIM4GEgePtr, idx.ge, idx.ge);
    map.bind(inst.BSIM4GEgpPtr, idx.ge, idx.gp);
    map.bind(inst.BSIM4GPgePtr, idx.gp, idx.ge);
    map.bind(inst.BSIM4GEdpPtr, idx.ge, idx.dp);
    map.bind(inst.BSIM4GEspPtr, idx.ge, idx.sp);
    map.bind(inst.BSIM4GEbpPtr, idx.ge, idx.bp);
    map.bind(inst.BSIM4GMdpPtr, idx.gm, idx.dp);
    map.bind(inst.BSIM4GMgpPtr, idx.gm, idx.gp);
    map.bind(inst.BSIM4GMgmPtr, idx.gm, idx.gm);
    map.bind(inst.BSIM4GMgePtr, idx.gm, idx.ge);
    map.bind(inst.BSIM4GMspPtr, idx.gm, idx.sp);
    map.bind(inst.BSIM4GMbpPtr, idx.gm, idx.bp);
    map.bind(inst.BSIM4DPgmPtr, idx.dp, idx.gm);
    map.bind(inst.BSIM4GPgmPtr, idx.gp, idx.gm);
    map.bind(inst.BSIM4GEgmPtr, idx.ge, idx.gm);
    map.bind(inst.BSIM4SPgmPtr, idx.sp, idx.gm);
    map.bind(inst.BSIM4BPgmPtr, idx.bp, idx.gm);
}

void bind_rbody_pointers(BSIM4instance& inst,
                        const Bsim4NodeIndices& idx,
                        Bsim4MatrixMap& map) {
    // idx.db / idx.sb expected when rbodyMod==1/2
    map.bind(inst.BSIM4DPdbPtr, idx.dp, idx.db);
    map.bind(inst.BSIM4SPsbPtr, idx.sp, idx.sb);

    map.bind(inst.BSIM4DBdpPtr, idx.db, idx.dp);
    map.bind(inst.BSIM4DBdbPtr, idx.db, idx.db);
    map.bind(inst.BSIM4DBbpPtr, idx.db, idx.bp);
    map.bind(inst.BSIM4DBbPtr,  idx.db, idx.b);

    map.bind(inst.BSIM4BPdbPtr, idx.bp, idx.db);
    map.bind(inst.BSIM4BPbPtr,  idx.bp, idx.b);
    map.bind(inst.BSIM4BPsbPtr, idx.bp, idx.sb);

    map.bind(inst.BSIM4SBspPtr, idx.sb, idx.sp);
    map.bind(inst.BSIM4SBbpPtr, idx.sb, idx.bp);
    map.bind(inst.BSIM4SBbPtr,  idx.sb, idx.b);
    map.bind(inst.BSIM4SBsbPtr, idx.sb, idx.sb);

    map.bind(inst.BSIM4BdbPtr, idx.b, idx.db);
    map.bind(inst.BSIM4BbpPtr, idx.b, idx.bp);
    map.bind(inst.BSIM4BsbPtr, idx.b, idx.sb);
    map.bind(inst.BSIM4BbPtr,  idx.b, idx.b);
}

void bind_rds_pointers(BSIM4instance& inst,
                      const Bsim4NodeIndices& idx,
                      Bsim4MatrixMap& map) {
    map.bind(inst.BSIM4DgpPtr, idx.d, idx.gp);
    map.bind(inst.BSIM4DspPtr, idx.d, idx.sp);
    map.bind(inst.BSIM4DbpPtr, idx.d, idx.bp);
    map.bind(inst.BSIM4SdpPtr, idx.s, idx.dp);
    map.bind(inst.BSIM4SgpPtr, idx.s, idx.gp);
    map.bind(inst.BSIM4SbpPtr, idx.s, idx.bp);
}

} // namespace kirimaspice::devices

#endif // KIRIMASPICE_USE_NGSPICE_BSIM4_REF
