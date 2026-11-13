// tests/unit/test_controlled_sources.cpp
// Unit tests for controlled sources (VCVS, CCCS, VCCS, CCVS)

#include <gtest/gtest.h>
#include "kirimaspice/parser/netlist_parser.hpp"
#include "kirimaspice/circuit/circuit_graph.hpp"
#include "kirimaspice/devices/vcvs.hpp"
#include "kirimaspice/devices/cccs.hpp"
#include "kirimaspice/devices/vccs.hpp"
#include "kirimaspice/devices/ccvs.hpp"
#include "kirimaspice/devices/voltage_source.hpp"
#include "kirimaspice/core/matrix_types.hpp"
#include <memory>

using namespace kirimaspice;
using namespace kirimaspice::parser;
using namespace kirimaspice::circuit;
using namespace kirimaspice::devices;

//==============================================================================
// VCVS Tests
//==============================================================================

TEST(ControlledSourcesTest, VCVS_Construction) {
    // E1 out 0 in 0 10
    CircuitGraph circuit;
    circuit.add_vcvs("E1", "out", "0", "in", "0", 10.0);

    ASSERT_EQ(circuit.num_devices(), 1);

    auto device = circuit.get_device("E1");
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(device->type(), DeviceType::VCVS);

    auto vcvs = std::dynamic_pointer_cast<VCVS>(device);
    ASSERT_NE(vcvs, nullptr);
    EXPECT_EQ(vcvs->gain(), 10.0);
    EXPECT_EQ(vcvs->np(), 1);  // "out" node
    EXPECT_EQ(vcvs->nn(), 0);  // GND
    EXPECT_EQ(vcvs->ncp(), 2); // "in" node
    EXPECT_EQ(vcvs->ncn(), 0); // GND
}

TEST(ControlledSourcesTest, VCVS_Parser) {
    std::string netlist = R"(VCVS Test Circuit
E1 out 0 in 0 10
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    ASSERT_EQ(circuit->num_devices(), 1);

    auto device = circuit->get_device("E1");
    ASSERT_NE(device, nullptr);

    auto vcvs = std::dynamic_pointer_cast<VCVS>(device);
    ASSERT_NE(vcvs, nullptr);
    EXPECT_EQ(vcvs->gain(), 10.0);
}

TEST(ControlledSourcesTest, VCVS_MNA_Stamp) {
    // E1 out 0 in 0 5
    CircuitGraph circuit;
    circuit.add_vcvs("E1", "out", "0", "in", "0", 5.0);

    auto device = circuit.get_device("E1");
    auto vcvs = std::dynamic_pointer_cast<VCVS>(device);
    ASSERT_NE(vcvs, nullptr);

    // Verify device properties
    EXPECT_TRUE(vcvs->is_linear());
    EXPECT_FALSE(vcvs->is_dynamic());

    // Setup MNA system (2 nodes + 1 branch variable)
    MNASystem mna;
    mna.resize(2, 1);  // 2 nodes, 1 branch

    DenseVector node_voltages(2, 0.0);

    // Test that stamping doesn't throw
    EXPECT_NO_THROW(vcvs->stamp_dc_linear(mna, node_voltages));

    // Verify that some entries were stamped (COO format should have non-zero entries)
    EXPECT_GT(mna.G.nnz(), 0u);
}

//==============================================================================
// CCCS Tests
//==============================================================================

TEST(ControlledSourcesTest, CCCS_Construction) {
    // F1 out 0 Vsense 100
    CircuitGraph circuit;
    circuit.add_voltage_source("Vsense", "n1", "0", 1.0);  // Add controlling voltage source first
    circuit.add_cccs("F1", "out", "0", "Vsense", 100.0);

    ASSERT_EQ(circuit.num_devices(), 2);

    auto device = circuit.get_device("F1");
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(device->type(), DeviceType::CCCS);

    auto cccs = std::dynamic_pointer_cast<CCCS>(device);
    ASSERT_NE(cccs, nullptr);
    EXPECT_EQ(cccs->gain(), 100.0);
    EXPECT_EQ(cccs->control_vsource_name(), "Vsense");
}

TEST(ControlledSourcesTest, CCCS_Parser_And_Reference_Resolution) {
    std::string netlist = R"(CCCS Test Circuit
Vsense n1 0 1
F1 out 0 Vsense 50
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    ASSERT_EQ(circuit->num_devices(), 2);

    auto cccs_device = circuit->get_device("F1");
    auto cccs = std::dynamic_pointer_cast<CCCS>(cccs_device);
    ASSERT_NE(cccs, nullptr);
    EXPECT_EQ(cccs->gain(), 50.0);

    // Check that control branch ID was resolved (parser calls resolve_controlled_source_references())
    EXPECT_EQ(cccs->control_branch_id(), 0);  // Vsense is the first voltage source, branch_id=0
}

TEST(ControlledSourcesTest, CCCS_MNA_Stamp) {
    // Vsense n1 0 1
    // F1 out 0 Vsense 3
    CircuitGraph circuit;
    circuit.add_voltage_source("Vsense", "n1", "0", 1.0);
    circuit.add_cccs("F1", "out", "0", "Vsense", 3.0);
    circuit.resolve_controlled_source_references();

    auto cccs_device = circuit.get_device("F1");
    auto cccs = std::dynamic_pointer_cast<CCCS>(cccs_device);
    ASSERT_NE(cccs, nullptr);
    EXPECT_EQ(cccs->control_branch_id(), 0);

    // Verify device properties
    EXPECT_TRUE(cccs->is_linear());
    EXPECT_FALSE(cccs->is_dynamic());

    // Setup MNA system (2 nodes + 1 branch for Vsense)
    MNASystem mna;
    mna.resize(2, 1);  // 2 nodes, 1 branch

    DenseVector node_voltages(2, 0.0);

    // Test that stamping doesn't throw
    EXPECT_NO_THROW(cccs->stamp_dc_linear(mna, node_voltages));

    // Verify that some entries were stamped
    EXPECT_GT(mna.G.nnz(), 0u);
}

//==============================================================================
// VCCS Tests
//==============================================================================

TEST(ControlledSourcesTest, VCCS_Construction) {
    // G1 out 0 in 0 0.01
    CircuitGraph circuit;
    circuit.add_vccs("G1", "out", "0", "in", "0", 0.01);

    ASSERT_EQ(circuit.num_devices(), 1);

    auto device = circuit.get_device("G1");
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(device->type(), DeviceType::VCCS);

    auto vccs = std::dynamic_pointer_cast<VCCS>(device);
    ASSERT_NE(vccs, nullptr);
    EXPECT_EQ(vccs->transconductance(), 0.01);
}

TEST(ControlledSourcesTest, VCCS_Parser) {
    std::string netlist = R"(VCCS Test Circuit
G1 out 0 in 0 0.02
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    ASSERT_EQ(circuit->num_devices(), 1);

    auto device = circuit->get_device("G1");
    auto vccs = std::dynamic_pointer_cast<VCCS>(device);
    ASSERT_NE(vccs, nullptr);
    EXPECT_EQ(vccs->transconductance(), 0.02);
}

TEST(ControlledSourcesTest, VCCS_MNA_Stamp) {
    // G1 out 0 in 0 0.005
    CircuitGraph circuit;
    circuit.add_vccs("G1", "out", "0", "in", "0", 0.005);

    auto device = circuit.get_device("G1");
    auto vccs = std::dynamic_pointer_cast<VCCS>(device);
    ASSERT_NE(vccs, nullptr);

    // Verify device properties
    EXPECT_TRUE(vccs->is_linear());
    EXPECT_FALSE(vccs->is_dynamic());

    // Setup MNA system (2 nodes, no branch variables)
    MNASystem mna;
    mna.resize(2, 0);  // 2 nodes, 0 branches

    DenseVector node_voltages(2, 0.0);

    // Test that stamping doesn't throw
    EXPECT_NO_THROW(vccs->stamp_dc_linear(mna, node_voltages));

    // Verify that some entries were stamped
    EXPECT_GT(mna.G.nnz(), 0u);
}

//==============================================================================
// CCVS Tests
//==============================================================================

TEST(ControlledSourcesTest, CCVS_Construction) {
    // H1 out 0 Vsense 1000
    CircuitGraph circuit;
    circuit.add_voltage_source("Vsense", "n1", "0", 0.1);
    circuit.add_ccvs("H1", "out", "0", "Vsense", 1000.0);

    ASSERT_EQ(circuit.num_devices(), 2);

    auto device = circuit.get_device("H1");
    ASSERT_NE(device, nullptr);
    ASSERT_EQ(device->type(), DeviceType::CCVS);

    auto ccvs = std::dynamic_pointer_cast<CCVS>(device);
    ASSERT_NE(ccvs, nullptr);
    EXPECT_EQ(ccvs->transresistance(), 1000.0);
    EXPECT_EQ(ccvs->control_vsource_name(), "Vsense");
}

TEST(ControlledSourcesTest, CCVS_Parser_And_Reference_Resolution) {
    std::string netlist = R"(CCVS Test Circuit
Vsense n1 0 0.1
H1 out 0 Vsense 500
.end
)";

    NetlistParser parser;
    auto result = parser.parse_string(netlist);
    auto& circuit = result.circuit;

    ASSERT_EQ(circuit->num_devices(), 2);

    auto ccvs_device = circuit->get_device("H1");
    auto ccvs = std::dynamic_pointer_cast<CCVS>(ccvs_device);
    ASSERT_NE(ccvs, nullptr);
    EXPECT_EQ(ccvs->transresistance(), 500.0);

    // Check that control branch ID was resolved
    EXPECT_EQ(ccvs->control_branch_id(), 0);  // Vsense is the first voltage source

    // Check that CCVS itself has a branch ID (it needs one for MNA)
    EXPECT_EQ(ccvs->branch_id(), 1);  // Second voltage source (CCVS acts like voltage source)
}

TEST(ControlledSourcesTest, CCVS_MNA_Stamp) {
    // Vsense n1 0 0.1
    // H1 out 0 Vsense 100
    CircuitGraph circuit;
    circuit.add_voltage_source("Vsense", "n1", "0", 0.1);
    circuit.add_ccvs("H1", "out", "0", "Vsense", 100.0);
    circuit.resolve_controlled_source_references();

    auto ccvs_device = circuit.get_device("H1");
    auto ccvs = std::dynamic_pointer_cast<CCVS>(ccvs_device);
    ASSERT_NE(ccvs, nullptr);
    EXPECT_EQ(ccvs->control_branch_id(), 0);
    EXPECT_EQ(ccvs->branch_id(), 1);

    // Verify device properties
    EXPECT_TRUE(ccvs->is_linear());
    EXPECT_FALSE(ccvs->is_dynamic());

    // Setup MNA system (2 nodes + 2 branches: Vsense + H1)
    MNASystem mna;
    mna.resize(2, 2);  // 2 nodes, 2 branches

    DenseVector node_voltages(2, 0.0);

    // Test that stamping doesn't throw
    EXPECT_NO_THROW(ccvs->stamp_dc_linear(mna, node_voltages));

    // Verify that some entries were stamped
    EXPECT_GT(mna.G.nnz(), 0u);
}

//==============================================================================
// Parser Error Tests
//==============================================================================

TEST(ControlledSourcesTest, CCCS_InvalidControl) {
    std::string netlist = R"(
CCCS Invalid Control
F1 out 0 R1 50
.end
)";

    NetlistParser parser;
    // Should throw because control must be a voltage source (V<name>)
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

TEST(ControlledSourcesTest, CCVS_InvalidControl) {
    std::string netlist = R"(
CCVS Invalid Control
H1 out 0 I1 1000
.end
)";

    NetlistParser parser;
    // Should throw because control must be a voltage source (V<name>)
    EXPECT_THROW(parser.parse_string(netlist), ParseError);
}

TEST(ControlledSourcesTest, CCCS_MissingControl) {
    std::string netlist = R"(
CCCS Missing Control
F1 out 0 Vmissing 50
.end
)";

    NetlistParser parser;
    // Should throw because Vmissing doesn't exist
    EXPECT_THROW(parser.parse_string(netlist), std::runtime_error);
}
