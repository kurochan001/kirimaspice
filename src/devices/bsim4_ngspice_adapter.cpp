#include "kirimaspice/devices/bsim4_ngspice_adapter.hpp"

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF

extern "C" {
#include "bsim4def.h"
#include "ngspice/cktdefs.h"
#include "ngspice/ngspice.h"
int BSIM4temp(GENmodel*, CKTcircuit*);
int BSIM4load(GENmodel*, CKTcircuit*);
}
#ifdef TRAPEZOIDAL
#undef TRAPEZOIDAL
#endif

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <spdlog/spdlog.h>

namespace kirimaspice {

// Helper: allocate and zero ngspice model/instance.
static BSIM4model* alloc_model() {
    auto* m = (BSIM4model*)std::calloc(1, sizeof(BSIM4model));
    return m;
}

static BSIM4instance* alloc_instance() {
    auto* inst = (BSIM4instance*)std::calloc(1, sizeof(BSIM4instance));
    return inst;
}

NgspiceBsim4Adapter::NgspiceBsim4Adapter(BSIM4model* m, BSIM4instance* inst)
    : model_(m), inst_(inst) {}

NgspiceBsim4Adapter::~NgspiceBsim4Adapter() {
    // ngspice normally frees via BSIM4destroy; here simple free is enough for our short-lived adapter.
    if (inst_) std::free(inst_);
    if (model_) std::free(model_);
}

std::unique_ptr<NgspiceBsim4Adapter> NgspiceBsim4Adapter::create_from_defaults() {
    BSIM4model* m = alloc_model();
    BSIM4instance* inst = alloc_instance();
    if (!m || !inst) {
        if (m) std::free(m);
        if (inst) std::free(inst);
        throw std::runtime_error("ngspice bsim4 alloc failed");
    }

    m->BSIM4type = NMOS;
    m->BSIM4typeGiven = 1;
    // Minimal model defaults to get a conductive device for bridge smoke tests.
    m->BSIM4toxe = 1.0e-9;     m->BSIM4toxeGiven = 1;
    m->BSIM4toxp = 1.2e-9;     m->BSIM4toxpGiven = 1;
    m->BSIM4toxm = 1.0e-9;     m->BSIM4toxmGiven = 1;
    m->BSIM4vth0 = 0.45;       m->BSIM4vth0Given = 1;
    m->BSIM4k1   = 0.6;        m->BSIM4k1Given = 1;
    m->BSIM4k2   = 0.05;       m->BSIM4k2Given = 1;
    m->BSIM4u0   = 0.05;       m->BSIM4u0Given = 1;     // [m^2/Vs]
    m->BSIM4vsat = 8.0e4;      m->BSIM4vsatGiven = 1;   // [m/s]
    m->BSIM4pclm = 1.3;        m->BSIM4pclmGiven = 1;
    m->BSIM4eta0 = 0.08;       m->BSIM4eta0Given = 1;
    m->BSIM4etab = 0.1;        m->BSIM4etabGiven = 1;
    m->BSIM4dvt0 = 2.2;        m->BSIM4dvt0Given = 1;
    m->BSIM4dvt1 = 0.53;       m->BSIM4dvt1Given = 1;
    m->BSIM4dvt2 = -0.032;     m->BSIM4dvt2Given = 1;
    m->BSIM4nsub = 1.0e17;     m->BSIM4nsubGiven = 1;
    m->BSIM4ndep = 1.5e17;     m->BSIM4ndepGiven = 1;
    m->BSIM4rdsw = 150.0;      m->BSIM4rdswGiven = 1;
    m->BSIM4rdswmin = 80.0;    m->BSIM4rdswminGiven = 1;
    m->BSIM4rdsMod = 0;        m->BSIM4rdsModGiven = 1;
    m->BSIM4rgateMod = 0;      m->BSIM4rgateModGiven = 1;
    m->BSIM4rbodyMod = 0;      m->BSIM4rbodyModGiven = 1;
    m->BSIM4tnom = 300.15;     m->BSIM4tnomGiven = 1;

    inst->BSIM4l = 45e-9;
    inst->BSIM4w = 10e-6;
    inst->BSIM4m = 1.0;
    inst->BSIM4drainSquares = 1.0;
    inst->BSIM4sourceSquares = 1.0;
    inst->BSIM4nf = 1.0;

    return std::unique_ptr<NgspiceBsim4Adapter>(new NgspiceBsim4Adapter(m, inst));
}

std::optional<NgspiceBsim4Result> NgspiceBsim4Adapter::evaluate_dc(Real vgs, Real vds, Real vbs, Real temp_K) {
    (void)vgs; (void)vds; (void)vbs; (void)temp_K;
    return std::nullopt;
}

void NgspiceBsim4Adapter::bind_basic_matrix(const devices::Bsim4NodeIndices& idx,
                                             devices::Bsim4MatrixMap& map) {
    if (!inst_) return;

    // Rebind pointers every call to follow current node mapping.
    map.reset();

    // Set node numbers (ngspice uses 1-based node ids; 0 is ground). Translate from 0-based (-1 for GND).
    auto to_node = [](int i) -> int { return (i < 0) ? 0 : (i + 1); };

    auto set_const_node = [](const int& field, int v) {
        const_cast<int&>(field) = v;
    };

    set_const_node(inst_->BSIM4dNode, to_node(idx.d));
    set_const_node(inst_->BSIM4gNodeExt, to_node(idx.g));
    set_const_node(inst_->BSIM4sNode, to_node(idx.s));
    set_const_node(inst_->BSIM4bNode, to_node(idx.b));
    inst_->BSIM4dNodePrime  = to_node(idx.dp);
    inst_->BSIM4gNodePrime  = to_node(idx.gp);
    inst_->BSIM4sNodePrime  = to_node(idx.sp);
    inst_->BSIM4bNodePrime  = to_node(idx.bp);
    inst_->BSIM4qNode       = to_node(idx.q);

    // Optional rgate/rbody nodes
    inst_->BSIM4gNodeMid    = to_node(idx.gm);
    inst_->BSIM4dbNode      = to_node(idx.db);
    inst_->BSIM4sbNode      = to_node(idx.sb);

    map.clear_values();
    devices::bind_basic_matrix_pointers(*inst_, idx, map);

    if (inst_->BSIM4rgateMod != 0) {
        devices::bind_rgate_pointers(*inst_, idx, map);
    }
    if (inst_->BSIM4rbodyMod == 1 || inst_->BSIM4rbodyMod == 2) {
        devices::bind_rbody_pointers(*inst_, idx, map);
    }
}

// Minimal ngspice CKT/state scaffolding for calling BSIM4temp/load.
struct NgspiceBsim4Adapter::NgCktContext {
    CKTcircuit ckt{};
    std::vector<double> state0;
    std::vector<double> state1;
    std::vector<double> state2;
    std::vector<double> rhs;
    std::vector<double> rhsOld;
    bool initialized = false;
};

bool NgspiceBsim4Adapter::load_dc_into_mna(const devices::Bsim4NodeIndices& idx,
                                            const DenseVector& node_voltages,
                                            devices::Bsim4MatrixMap& map,
                                            MNASystem& mna,
                                            Real temp_K) {
    if (!model_ || !inst_) return false;

    // Lazily create context
    if (!ctx_) ctx_ = std::make_unique<NgCktContext>();
    auto& ctx = *ctx_;

    // Determine max node index (ngspice uses 1-based, 0 = GND)
    int max_idx = 0;
    auto track = [&](int v) { if (v > max_idx) max_idx = v; };
    track(idx.d); track(idx.g); track(idx.s); track(idx.b);
    track(idx.dp); track(idx.gp); track(idx.sp); track(idx.bp);
    track(idx.q); track(idx.ge); track(idx.gm); track(idx.db); track(idx.sb);

    const int node_count = max_idx + 1; // include ground

    if (!ctx.initialized) {
        // Allocate state vectors (single device -> BSIM4numStates)
        ctx.state0.assign(BSIM4numStates, 0.0);
        ctx.state1.assign(BSIM4numStates, 0.0);
        ctx.state2.assign(BSIM4numStates, 0.0);
        inst_->BSIM4states = 0; // base offset

        // Allocate RHS vectors
        ctx.rhs.assign(node_count + 4, 0.0);
        ctx.rhsOld.assign(node_count + 4, 0.0);

        ctx.ckt.CKTstate0 = ctx.state0.data();
        ctx.ckt.CKTstate1 = ctx.state1.data();
        ctx.ckt.CKTstate2 = ctx.state2.data();
        ctx.ckt.CKTrhs    = ctx.rhs.data();
        ctx.ckt.CKTrhsOld = ctx.rhsOld.data();
        ctx.ckt.CKTomega  = 0.0;
        ctx.ckt.CKTtime   = 0.0;
        ctx.ckt.CKTorder  = 1;
        ctx.ckt.CKTdelta  = 1e-12;
        ctx.ckt.CKTdeltaOld[0] = 1e-12;
        ctx.ckt.CKTdeltaOld[1] = 1e-12;
        ctx.ckt.CKTag[0] = 1.0;
        ctx.ckt.CKTag[1] = 1.0;
        ctx.initialized = true;
    } else if (static_cast<int>(ctx.rhs.size()) < node_count + 1) {
        ctx.rhs.assign(node_count + 4, 0.0);
        ctx.rhsOld.assign(node_count + 4, 0.0);
        ctx.ckt.CKTrhs    = ctx.rhs.data();
        ctx.ckt.CKTrhsOld = ctx.rhsOld.data();
    }

    auto node_voltage = [&](int idx1) -> double {
        if (idx1 <= 0) return 0.0; // ground or invalid
        std::size_t offset = static_cast<std::size_t>(idx1 - 1);
        if (offset < node_voltages.size()) return node_voltages[offset];
        return 0.0;
    };

    // rhs: start from zero (ngspice stamps currents into CKTrhs)
    std::fill(ctx.rhs.begin(), ctx.rhs.end(), 0.0);
    std::fill(ctx.rhsOld.begin(), ctx.rhsOld.end(), 0.0);
    for (int n = 1; n <= max_idx; ++n) {
        double v = node_voltage(n);
        ctx.rhsOld[n] = v; // previous solution (used by device limiting)
    }

    // Translate to device terminal voltages (already 1-based inside instance)
    const double vd = node_voltage(idx.dp <= 0 ? idx.d : idx.dp);
    const double vg = node_voltage(idx.gp <= 0 ? idx.g : idx.gp);
    const double vs = node_voltage(idx.sp <= 0 ? idx.s : idx.sp);
    const double vb = node_voltage(idx.bp <= 0 ? idx.b : idx.bp);

    const double vds = vd - vs;
    const double vgs = vg - vs;
    const double vbs = vb - vs;

    auto set_state = [&](int offset, double val) {
        if (offset >= 0 && offset < static_cast<int>(ctx.state0.size())) {
            ctx.state0[offset] = val;
            ctx.state1[offset] = val;
            ctx.state2[offset] = val;
        }
    };

    set_state(inst_->BSIM4vds, vds);
    set_state(inst_->BSIM4vgs, vgs);
    set_state(inst_->BSIM4vbs, vbs);
    set_state(inst_->BSIM4vbd, vbs - vds);

    // Update temperature
    ctx.ckt.CKTtemp   = temp_K;
    ctx.ckt.CKTnomTemp = temp_K;

    // Use DC operating point mode
    ctx.ckt.CKTmode = MODEDCOP;

    // Ensure matrix pointers bound for current node mapping
    bind_basic_matrix(idx, map);

    // Refresh temperature-dependent params
    int err = BSIM4temp(reinterpret_cast<GENmodel*>(model_), &ctx.ckt);
    if (err) {
        spdlog::error("BSIM4temp failed (err={})", err);
        return false;
    }

    // Run load; writes into bound matrix pointers
    err = BSIM4load(reinterpret_cast<GENmodel*>(model_), &ctx.ckt);
    if (err) {
        spdlog::error("BSIM4load failed (err={})", err);
        return false;
    }

    // Accumulate matrix contributions into MNA
    map.accumulate_to_mna(mna);

    // Accumulate RHS contributions (CKTrhs) into MNA b-vector
    const std::size_t mna_size = mna.size();
    for (int n = 1; n <= max_idx; ++n) {
        const std::size_t dst = static_cast<std::size_t>(n - 1);
        if (dst < mna_size) {
            const double curr = ctx.rhs[n];
            if (curr != 0.0) {
                mna.b[dst] += static_cast<Real>(curr);
            }
        }
    }
    return true;
}

} // namespace kirimaspice

#else  // KIRIMASPICE_USE_NGSPICE_BSIM4_REF
// When disabled, provide empty stubs to keep the build lean.
namespace kirimaspice {
std::unique_ptr<NgspiceBsim4Adapter> NgspiceBsim4Adapter::create_from_defaults() { return nullptr; }
std::optional<NgspiceBsim4Result> NgspiceBsim4Adapter::evaluate_dc(Real, Real, Real, Real) { return std::nullopt; }
NgspiceBsim4Adapter::~NgspiceBsim4Adapter() = default;
NgspiceBsim4Adapter::NgspiceBsim4Adapter(BSIM4model*, BSIM4instance*) = default;
} // namespace kirimaspice
#endif
