# To do

[日本語](TODO.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

**Written 2026-11-21** at commit `6004202`. The work still to do, in the
order recommended. [KNOWN_ISSUES](KNOWN_ISSUES.md) describes each item
with what was measured; this list says what to do about it. Size: S is a
day or less, M a few days, L a week or more (estimates, not measured).
Tick an item when its fix is pushed, and remove its entry from
KNOWN_ISSUES in the same commit.

## 1. Netlist compatibility with ngspice

Small items with a direct effect on netlists written for ngspice.

- [x] **Global parameters in `.IF` and `.ELSEIF`** (S). Since `0faf3b9`,
  both conditions use the final global `.PARAM` values, including forward definitions.
- [x] **`.FUNC` in element values** (S). Functions can be called in
  every expression in braces and in `B` expressions, also in subcircuit
  bodies (`util::ExpressionFunctions`).
- [x] **Parameters and other functions in a `.FUNC` body** (S). A body
  sees the parameters where it is called, in a subcircuit the
  instance's, and the other functions; one that calls itself is an error.
- [x] **`I(<source>)` in `B` expressions** (M). The current of a voltage
  source, `E`, `H` or voltage-form `B`, with its derivative in Newton and
  AC (`BehavioralSource::linearize`).
- [ ] **`I(<name>)` of other elements in `B` expressions** (M), as
  ngspice-45.2 reads `I(R1)` ([netlist input](KNOWN_ISSUES.md#netlist-input)).
- [x] **`POLY` form of `E`, `F`, `G` and `H`** (M), read as a `B` source
  of the polynomial (`parse_poly_controlled_source`).
- [x] **`J` elements** (M), with the JFET as ngspice's level 1 and its
  gate capacitances through `device_utils::CapacitorHistory`.
- [ ] **The JFET's `RD`, `RS` and temperature of `VTO`, `BETA`, `PB` and the
  capacitances** (M) ([device models](KNOWN_ISSUES.md#device-models)).
- [x] **Scale factors in `.OPTIONS` values** (S). A numeric option is read
  as an element value is (`util::parse_value_with_unit`): `itl1=1k` is 1000.
- [ ] **An empty first line is the title** (S), as in ngspice-47. Many
  tests write their netlists starting with an empty line, so the tests
  change with it (a person's approval)
  ([netlist input](KNOWN_ISSUES.md#netlist-input)).
- [x] **`S` and `Y` with a model not defined before them** (S): an error,
  as for the other elements; `.MODEL` has no SCR or TRIAC type.
- [x] **`.PRINT DC` and `.PRINT AC`**, as tables on standard output.
- [x] **`.SAVE`**, for the raw and SSF files (`output::SavedSignals`).
- [ ] **Directives read but not run** (M each): `.STEP` first (ngspice has
  no `.STEP`, so its output is to be decided with a person); then `.SENS`, `.TF`, `.MC`, `.PZ`,
  `.DISTO`, `.PORT`, `.WCASE`, `.OPTIMIZE`, `.NOISE` and `.MEASURE`
  ([netlist input](KNOWN_ISSUES.md#netlist-input)).
- [x] **`.OP` with `.DC` runs both** (S), the sweep first, as in
  ngspice-45.2's raw file.
- [x] **Analyses in `.control` blocks** (M): `op`, `dc` (also nested),
  `tran` and `ac`, read by their directives' parsers; the others are
  named in a warning.

## 2. Analyses

- [ ] **The transient's speed** (M). A ladder of 200 sections takes longer
  than in ngspice-47; profile it first
  ([analyses](KNOWN_ISSUES.md#analyses)).
- [ ] **The adaptive time step** (L): make the library's adaptive path
  correct (BDF2 and BDF3 of fixed order on a circuit at rest, the first
  step of the trapezoidal rule), then decide with a person whether the
  program uses it, which would also give `TRTOL`, `CHGTOL` and the `LTE_*`
  options an effect ([analyses](KNOWN_ISSUES.md#analyses)).
- [ ] **Convergence aids by default** (M): measure what ngspice-47 does
  when Newton's method alone fails, then decide with a person
  ([analyses](KNOWN_ISSUES.md#analyses)).
- [ ] **GMIN in the other devices' junctions** (M): only the diode and
  the bipolar transistor have it ([analyses](KNOWN_ISSUES.md#analyses)).
- [ ] **Input-referred noise** (S): divide by the gain from the input
  source ([analyses](KNOWN_ISSUES.md#analyses)).
- [ ] **A gradient for `LD_SLSQP`** (S)
  ([analyses](KNOWN_ISSUES.md#analyses)).

## 3. Test quality

Changes to existing tests need a person's approval, each in its own
commit.

- [ ] **The time-step and LTE tests whose circuits do not move** (M):
  about 34 tests start at rest, so they check nothing of the LTE code
  ([tests](KNOWN_ISSUES.md#tests)).
- [ ] **Tests that cannot fail or check little** (M): the VDMOS region
  tests, the transmission-line "validation" against DC values,
  `bsim4_dc_smoke`, `BSIM4NgspiceCompareTest.IdsVsNgspice_T27C` (errors up
  to 100 %), the BJT validation programs, the level-3 Meyer tests
  ([tests](KNOWN_ISSUES.md#tests)).
- [x] **Restore the six diode and BJT tests** (M). `45a7bf2` supplies
  the project-owned model cards. `91b4def` removes the passing cases from the known-failure list.

## 4. Device models

- [ ] **The bipolar transistor's parasitics** (M): RB, RE, RC, the
  substrate junction, XTF, VTF, ITF, PTF, XCJC, FC, the charges' cross
  derivative and the temperature of the junction potentials
  ([netlist input](KNOWN_ISSUES.md#netlist-input)).
- [ ] **The level-1 MOSFET's capacitances and bulk junctions** (M)
  ([netlist input](KNOWN_ISSUES.md#netlist-input)).
- [ ] **The diode's remaining parameters** (S): FC, KF, AF, TNOM, EG, XTI
  ([netlist input](KNOWN_ISSUES.md#netlist-input)).
- [x] **BSIM4 terminal charges for the default `CAPMOD=2`** (L).
  Implemented in `57a155a` and covered by the inverter-chain regression enabled in `6b334da`.
- [ ] **BSIM4 intrinsic capacitance for `CAPMOD=0` and `1`, and source
  and drain junction currents in DC** ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **BSIM4's noise and parameter extractor through the device's
  equations** (M), not a separate model
  ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **BSIM4's remaining parameters and limits** (M): MOBMOD 3 to 6,
  `RDSMOD`, `TEMPMOD`, `RDSWMIN`, `DROUT = 0`, `DSUB = 0`, `MAX_EXP`
  ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **BSIM4's threshold with body bias** (S): find the cause of the
  10 µV ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **BSIM3v3** (L), in place of the simplified BSIM3
  ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **BSIM1** (L): port ngspice's BSIM1 (its `LEVEL` 4) or remove the
  library's class ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **The FinFET** (M): the jump at the threshold, W<sub>eff</sub>, the
  reverse mode, `TFIN` and `VSAT`
  ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **The MESFET's gate junctions and charges** (M)
  ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **Noise** (S): the 8/3 of BSIM3, BSIM4 and the GaN HEMT, the
  level-1 flicker noise, the temperatures of level 3, the MESFET and the
  SCR ([device models](KNOWN_ISSUES.md#device-models)).
- [ ] **The level-1 overlap capacitances by the transient's method** (S),
  through `device_utils::CapacitorHistory`
  ([TRANSIENT_ANALYSIS](docs/TRANSIENT_ANALYSIS.md)).
- [ ] **The lossy line `O` without its approximation** (L): the LTRA
  convolution of Roychowdhury and Pederson
  ([device models](KNOWN_ISSUES.md#device-models)).

## 5. Output and tools

- [ ] **SSF** (M): a written specification, a scale for small values,
  exact block times, the version, and a file for DC sweeps
  ([command line and output](KNOWN_ISSUES.md#command-line-and-output)).
- [ ] **`run_netlist` of the Python tools with relative `.include`** (S)
  ([command line and output](KNOWN_ISSUES.md#command-line-and-output)).

## 6. Source code

- [ ] **The capacitor, the inductor and the mutual inductance through
  `device_utils::capacitor_companion`** (S), so that the companion is
  written once ([source code](KNOWN_ISSUES.md#source-code)).
- [ ] **Compiler warnings** (S): 150 in kirimaspice's own sources
  ([source code](KNOWN_ISSUES.md#source-code)).
- [ ] **Comments in Japanese** to English (S)
  ([source code](KNOWN_ISSUES.md#source-code)).
- [ ] **One directory of helpers** (S): `util/` and `utils/`
  ([source code](KNOWN_ISSUES.md#source-code)).
- [ ] **Split the large files** (M), `netlist_parser.cpp` first
  ([source code](KNOWN_ISSUES.md#source-code)).
- [ ] **The Monte Carlo "GPU" path** (S): remove the name or write the
  code ([source code](KNOWN_ISSUES.md#source-code)).
