// tests/unit/test_circuit.cpp
#include <gtest/gtest.h>
#include "kirimaspice/circuit/circuit_graph.hpp"

using namespace kirimaspice;
using namespace kirimaspice::circuit;

//==============================================================================
// CircuitGraph - Node Management Tests
//==============================================================================
TEST(CircuitGraph, ConstructorInitializesGround) {
    CircuitGraph circuit;

    // Ground node (0) should be automatically created
    EXPECT_TRUE(circuit.has_node("0"));
    EXPECT_EQ(circuit.get_node_id("0"), NODE_GND);
    EXPECT_EQ(circuit.num_nodes(), 1);  // Only GND
}

TEST(CircuitGraph, AddNode) {
    CircuitGraph circuit;

    NodeId id1 = circuit.add_node("in");
    NodeId id2 = circuit.add_node("out");

    EXPECT_TRUE(circuit.has_node("in"));
    EXPECT_TRUE(circuit.has_node("out"));
    EXPECT_NE(id1, id2);
    EXPECT_EQ(circuit.num_nodes(), 3);  // GND, in, out
}

TEST(CircuitGraph, AddDuplicateNode) {
    CircuitGraph circuit;

    NodeId id1 = circuit.add_node("test");
    NodeId id2 = circuit.add_node("test");  // Same name

    // Should return the same ID
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(circuit.num_nodes(), 2);  // GND, test
}

TEST(CircuitGraph, GetNodeID) {
    CircuitGraph circuit;

    circuit.add_node("vdd");
    circuit.add_node("vss");

    NodeId vdd_id = circuit.get_node_id("vdd");
    NodeId vss_id = circuit.get_node_id("vss");

    EXPECT_NE(vdd_id, vss_id);
}

TEST(CircuitGraph, GetNodeIDThrowsOnNotFound) {
    CircuitGraph circuit;

    EXPECT_THROW(circuit.get_node_id("nonexistent"), std::runtime_error);
}

TEST(CircuitGraph, HasNode) {
    CircuitGraph circuit;

    circuit.add_node("a");

    EXPECT_TRUE(circuit.has_node("a"));
    EXPECT_TRUE(circuit.has_node("0"));  // GND
    EXPECT_FALSE(circuit.has_node("b"));
}

TEST(CircuitGraph, GetNodeNames) {
    CircuitGraph circuit;

    circuit.add_node("in");
    circuit.add_node("out");
    circuit.add_node("mid");

    auto names = circuit.get_node_names();

    EXPECT_EQ(names.size(), 4);  // GND, in, out, mid

    // Check all names are present
    EXPECT_NE(std::find(names.begin(), names.end(), "0"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "in"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "out"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "mid"), names.end());
}

//==============================================================================
// CircuitGraph - Title Tests
//==============================================================================
TEST(CircuitGraph, TitleDefaultEmpty) {
    CircuitGraph circuit;

    EXPECT_EQ(circuit.title(), "");
}

TEST(CircuitGraph, SetTitle) {
    CircuitGraph circuit;

    circuit.set_title("Test Circuit");

    EXPECT_EQ(circuit.title(), "Test Circuit");
}

//==============================================================================
// CircuitGraph - Device Management Tests (Stubs for Week 1-2)
//==============================================================================
TEST(CircuitGraph, AddResistor) {
    CircuitGraph circuit;

    // This is a stub implementation for Week 1-2
    // Just check it doesn't crash and creates nodes
    EXPECT_NO_THROW(circuit.add_resistor("R1", "in", "out", 1000.0));

    EXPECT_TRUE(circuit.has_node("in"));
    EXPECT_TRUE(circuit.has_node("out"));
    EXPECT_EQ(circuit.num_nodes(), 3);  // GND, in, out
}

TEST(CircuitGraph, AddCapacitor) {
    CircuitGraph circuit;

    EXPECT_NO_THROW(circuit.add_capacitor("C1", "a", "0", 1e-6));

    EXPECT_TRUE(circuit.has_node("a"));
    EXPECT_EQ(circuit.num_nodes(), 2);  // GND, a
}

TEST(CircuitGraph, AddInductor) {
    CircuitGraph circuit;

    EXPECT_NO_THROW(circuit.add_inductor("L1", "x", "y", 1e-3));

    EXPECT_TRUE(circuit.has_node("x"));
    EXPECT_TRUE(circuit.has_node("y"));
    EXPECT_EQ(circuit.num_nodes(), 3);  // GND, x, y
}

TEST(CircuitGraph, AddVoltageSource) {
    CircuitGraph circuit;

    EXPECT_NO_THROW(circuit.add_voltage_source("Vin", "vdd", "0", 5.0));

    EXPECT_TRUE(circuit.has_node("vdd"));
    EXPECT_EQ(circuit.num_nodes(), 2);  // GND, vdd
}

TEST(CircuitGraph, AddCurrentSource) {
    CircuitGraph circuit;

    EXPECT_NO_THROW(circuit.add_current_source("Idc", "0", "out", 1e-3));

    EXPECT_TRUE(circuit.has_node("out"));
    EXPECT_EQ(circuit.num_nodes(), 2);  // GND, out
}

TEST(CircuitGraph, MultipleDevices) {
    CircuitGraph circuit;

    circuit.add_resistor("R1", "in", "mid", 1000.0);
    circuit.add_capacitor("C1", "mid", "0", 1e-6);
    circuit.add_voltage_source("Vin", "in", "0", 5.0);

    EXPECT_EQ(circuit.num_nodes(), 3);  // GND, in, mid
    EXPECT_TRUE(circuit.has_node("in"));
    EXPECT_TRUE(circuit.has_node("mid"));
    EXPECT_TRUE(circuit.has_node("0"));
}

//==============================================================================
// CircuitGraph - Complex Circuit Tests
//==============================================================================
TEST(CircuitGraph, ComplexRCCircuit) {
    CircuitGraph circuit;
    circuit.set_title("RC Filter");

    // RC low-pass filter
    circuit.add_voltage_source("Vin", "in", "0", 1.0);
    circuit.add_resistor("R1", "in", "out", 1000.0);
    circuit.add_capacitor("C1", "out", "0", 1e-6);

    EXPECT_EQ(circuit.title(), "RC Filter");
    EXPECT_EQ(circuit.num_nodes(), 3);  // GND, in, out
}

TEST(CircuitGraph, VoltageDivider) {
    CircuitGraph circuit;
    circuit.set_title("Voltage Divider");

    circuit.add_voltage_source("Vdd", "vdd", "0", 5.0);
    circuit.add_resistor("R1", "vdd", "mid", 1000.0);
    circuit.add_resistor("R2", "mid", "0", 1000.0);

    EXPECT_EQ(circuit.num_nodes(), 3);  // GND, vdd, mid
}

TEST(CircuitGraph, RLCResonator) {
    CircuitGraph circuit;
    circuit.set_title("RLC Resonator");

    circuit.add_resistor("R1", "in", "node1", 100.0);
    circuit.add_inductor("L1", "node1", "node2", 1e-3);
    circuit.add_capacitor("C1", "node2", "0", 10e-9);
    circuit.add_voltage_source("Vin", "in", "0", 1.0);

    EXPECT_EQ(circuit.num_nodes(), 4);  // GND, in, node1, node2
}

//==============================================================================
// CircuitGraph - Boost.Graph Integration Tests
//==============================================================================
TEST(CircuitGraph, GraphAccessible) {
    CircuitGraph circuit;

    circuit.add_node("a");
    circuit.add_node("b");

    // Check we can access the Boost.Graph instance
    const auto& graph = circuit.graph();

    // Basic sanity check: graph should have vertices
    EXPECT_GT(boost::num_vertices(graph), 0);
}

TEST(CircuitGraph, NodePropertiesInGraph) {
    CircuitGraph circuit;

    NodeId id_a = circuit.add_node("test_node");

    const auto& graph = circuit.graph();

    // Verify node properties are stored in graph
    // (This is a basic check, more detailed tests will be in Week 3)
    EXPECT_GT(boost::num_vertices(graph), 0);
}
