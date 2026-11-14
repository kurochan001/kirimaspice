# NETLIST_CONVENTIONS: where kirimaspice follows ngspice

[日本語](NETLIST_CONVENTIONS.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

Status: **approved (2026-11-14)**.

kirimaspice reads netlists written for SPICE simulators, which differ in a
few conventions. For the points below kirimaspice follows ngspice, so that
a netlist written for ngspice means the same in kirimaspice. Each point
gives the rule, the section of the ngspice User's Manual where ngspice
describes it (the HTML manual of version 47+, development version, read on
2026-11-14), and the result of running ngspice-47 (Homebrew, macOS arm64)
on a small netlist on the same day. The manual is summarized, not quoted.

## 1. Scale factors

- **Rule:** a number may be followed by one of these scale factors, in any
  case: `T` (10¹²), `G` (10⁹), `MEG` (10⁶), `K` (10³), `MIL` (25.4×10⁻⁶),
  `M` (10⁻³), `U` (10⁻⁶), `N` (10⁻⁹), `P` (10⁻¹²), `F` (10⁻¹⁵), `A`
  (10⁻¹⁸). Letters after a number that are not a scale factor, and letters
  after a scale factor, are ignored. So `M` and `m` are both milli,
  `10MHz` is 10×10⁻³, and `1A` is 10⁻¹⁸.
- **ngspice:** sections 2.1.3.2 (table 2.1) and 2.1.3.3. A resistor of `1M`
  was 1 mΩ; `1MEG` was 1 MΩ.
- **Before:** upper-case `M`, and `M` followed by `H`, `O`, `V`, `W`, `S`
  or `F`, was 10⁶; `MIL` and `A` were not scale factors.

## 2. Inputs of AC analysis

- **Rule:** an independent source takes part in AC analysis only if its
  line has the keyword `AC`. `AC` without a magnitude means magnitude 1;
  the phase defaults to 0. A source without `AC` has magnitude 0 in AC
  analysis.
- **ngspice:** section 4.1. In a run, a voltage source written `DC 5`
  without `AC` gave 0 V in AC analysis, and one written `DC 0 AC` without a
  magnitude gave 1 V.
- **Before:** a voltage source without `AC` had magnitude 1. Current
  sources already had magnitude 0.

## 3. Current of a branch defined by a voltage

- **Rule:** the current of a voltage source, and of `E`, `H` and
  voltage-form `B` sources, is positive when it flows into the `+`
  terminal, through the source, and out of the `−` terminal. This is the
  current printed and written as `I(<source>)`, and the current that `F`
  and `H` use as their control. A source that delivers current to the
  circuit therefore has a negative current. Current sources are not
  changed: a positive value drives current from `n+` through the source
  into `n−`, as in ngspice.
- **ngspice:** section 4.1. In a run with `V1 in 0 DC 1` across 1 kΩ:
  `i(V1)` = −1 mA, `F1 0 f V1 3` into 1 kΩ gave `v(f)` = −3 V, and
  `H1 h 0 V1 100` gave `v(h)` = −0.1 V. `i(E1)` of `E1 e 0 in 0 2` into
  1 kΩ was −2 mA, `i(H1)` of an `H` source driving 1 V into 1 kΩ was
  −1 mA, and `i(B1)` of `B1 b 0 V=2*V(in)` into 1 kΩ was −2 mA.
- **Before:** the opposite sign, so `F` and `H` also gave the opposite
  result.

## 4. Nested DC sweep

- **Rule:** in `.DC src1 start1 stop1 step1 src2 start2 stop2 step2`,
  `src1` is swept over its range for each value of `src2`: `src1` is the
  inner loop. Points are computed and written in that order.
- **ngspice:** section 11.3.2. A run of `.dc V1 0 1 0.5 V2 0 1 1` gave
  `V1` = 0, 0.5, 1 with `V2` = 0, then the same with `V2` = 1.
- **Before:** `src2` was the inner loop.

## 5. Ground

- **Rule:** the node `0` is ground. A node named `gnd`, in any case, is
  ground too, at the top level as inside subcircuits.
- **ngspice:** section 2.1.3.5 (ngspice treats `gnd` as a global node and
  converts it to `0`; its switch `no_auto_gnd` is not provided here). In a
  run, `V1 a gnd DC 1` with `R1 a GND 1k` gave `v(a)` = 1 V.
- **Before:** `GND` was ground only inside subcircuits.

## Not decided here

- ngspice treats node names without regard to case in batch mode
  (section 2.1.3.4); kirimaspice treats them as case-sensitive.
- ngspice lets the DC value be omitted when it is zero, so
  `V1 in 0 PULSE(...)` is accepted (section 4.1); kirimaspice needs it.
- ngspice can sweep a resistor or the temperature with `.DC`
  (section 11.3.2); kirimaspice sweeps only independent sources.

## Tests whose expected values change

Following these rules changes the expected values of existing tests. Those
changes go in commits of their own, separate from the implementation, and
carry `Approved-by`:

- rule 1: `UnitsParse.MixedCaseAndFollowedUnits` (`10MHz`);
- rule 3: `DCSolver.MultipleVoltageSources`, `DCSolver.NODESETInitialGuess`,
  `DCSolver.SingleResistor`, `DCSolver.VoltageDivider`,
  `DCSweepTest.BranchCurrentExtraction`,
  `LargeScale100.DC_ResistorMesh_100Devices`,
  `LargeScale300.DC_ResistorMesh_300Devices`,
  `LargeScale1000.DC_ResistorMesh_1000Devices`,
  `LargeScale3000.DC_ResistorMesh_3000Devices`,
  `TransferFunctionSolver.InputResistanceCheck`,
  `TransientSolver.DCCircuitSteadyState` and `cli_op_gnd_is_a_node`
  (measured by changing the sign of voltage sources only; `E`, `H` and `B`
  may add more);
- rule 4: `DCSweepTest.TwoDimensionalSweep_BasicStructure`;
- rule 5: `cli_op_gnd_is_a_node`;
- rule 2: none.

`cli_op_gnd_is_a_node` was renamed `cli_op_gnd_is_ground` when its
expected value was changed (commit `e147d5c`).
