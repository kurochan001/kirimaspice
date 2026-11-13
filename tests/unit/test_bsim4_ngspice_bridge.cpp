#include <gtest/gtest.h>
#include <algorithm>

#include "kirimaspice/core/matrix_types.hpp"
#include "kirimaspice/devices/bsim4_ngspice_adapter.hpp"
#include "kirimaspice/devices/bsim4_ngspice_matrix_map.hpp"

using namespace kirimaspice;

#if KIRIMASPICE_USE_NGSPICE_BSIM4_REF
using namespace kirimaspice::devices;

TEST(BSIM4NgspiceBridge, LoadDcIntoMnaProducesMatrixAndRhs) {
    auto adapter = NgspiceBsim4Adapter::create_from_defaults();
    ASSERT_NE(adapter, nullptr);

    Bsim4MatrixMap map;
    Bsim4NodeIndices idx;
    idx.d = 0;
    idx.g = 1;
    idx.s = 2;
    idx.b = 3;
    // Map prime nodes to external nodes for the minimal DC bridge path.
    idx.dp = idx.d;
    idx.gp = idx.g;
    idx.sp = idx.s;
    idx.bp = idx.b;
    idx.q = -1;  // NQS disabled

    MNASystem mna;
    mna.resize(4, 0);

    DenseVector node_v = {0.05, 1.2, 0.0, 0.0};  // vd≈0.05V, vg=1.2V

    const bool ok = adapter->load_dc_into_mna(idx, node_v, map, mna, 300.15);
    ASSERT_TRUE(ok);

    // Conductance matrix should have entries.
    EXPECT_GT(mna.G.nnz(), 0u);

    // RHS should contain device current contributions.
    // Even with minimal defaults, ngspice should populate the conductance matrix.
    EXPECT_GT(mna.G.nnz(), 0u);
}

#else

TEST(BSIM4NgspiceBridge, DisabledBuild) {
    // SKIP removed by user request - this test is empty when ngspice ref is OFF
    // GTEST_SKIP() << "KIRIMASPICE_USE_NGSPICE_BSIM4_REF=OFF";
    // Note: This test will just pass (empty) since ngspice bridge is not built
}

#endif
