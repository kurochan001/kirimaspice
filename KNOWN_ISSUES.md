# Known issues

[日本語](KNOWN_ISSUES.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

This list holds what has been measured or read in the code. Measurements
were made on 2026-11-14 on macOS 27.2 (arm64) with GCC 16.2.0, CMake 4.4.4
and SuiteSparse 7.6.1 unless an entry says otherwise.

## Tests

The groups below summarize the exceptions described in detail after the
table. They are not an additional set of tests.

| Group | State | Main obstacle |
| --- | --- | --- |
| Manufacturer models and common-emitter test (6) | known failures | model files are absent |
| BSIM4 temperature reference (1) | runs | reference from ngspice-45.2 is committed |
| BSIM4 VDSAT branch (1) | runs | reference verified with ngspice-45.2 |
| BSIM4 transient inverter chain (1) | disabled | edges differ; simplified capacitance model |
| Live ngspice comparison | conditional | runs only when ngspice is installed |

- **Six tests fail** and are listed in `tests/known_failures.txt`:
  - `ManufacturerModels.*` (five tests) and `BJTModelTest.SimpleCommonEmitter`
    include `tests/models/2N3904.lib` and `tests/models/1N4148.lib`, which are
    not in the repository. The tests expect `BF=200` for the 2N3904 and
    `IS=2.52e-9`, `N=1.752` for the 1N4148. The files must not be added
    before their origin and redistribution terms are checked; their origin
    is not confirmed.
- `CMOS_Inverter.DCTransferCharacteristic_HighInput` used to require an output
  of at least 0 V and saw -4.1e-25 V on arm64, which is roundoff around the
  exact value of 0 V; its lower bound now allows -1e-12 V.
- The ngspice version used to make most of the reference data in
  `validation/` and `tests/validation/` is not recorded. The exception is
  `validation/mosfet_2stage_amplifier_ac_ngspice.csv`, written by
  ngspice-45.2. The twenty comparison tests that read `validation/` were
  skipped until their data paths were corrected; they have run since then
  on Linux (x86-64) with clang 20.1.8 and GCC 15.2.0, and CI run
  44613253295 passed them with GCC 13.3.0 and clang 18.1.3; other
  platforms are not confirmed. The decks that write the AC reference
  CSVs are in `validation/`. No deck in the repository writes
  `validation/diode_ac_ngspice.csv` or the `validation/resistor_mesh_*_dc.csv`
  files, so how those were produced is not confirmed.
- `BSIM4TemperatureValidationTest.TemperatureSweepValidation` reads
  `tests/validation/bsim4_temperature_refs.json`, which
  `scripts/bench/generate_bsim4_temperature_refs.py` generated with
  ngspice-45.2. On Linux (x86-64) the model matches that reference:
  average errors are 0.09% (Vth), 1.99% (Ids) and 1.66% (Vdsat); the
  worst points are 0.65%, 2.41% and 2.44%.
- `BSIM4ComprehensiveTest.NgspiceComparison` runs `ngspice` and is skipped
  when ngspice is not installed. On macOS with ngspice-47 from Homebrew it
  runs and passes.
- `BSIM4ComprehensiveTest.VdsatBranchSelection` reads the reference from
  `validation/bsim4_vdsat_branch.cir`. With ngspice-45.2 that deck gives
  id = 2.1016 A and vdsat = 0.8418 V; the test passes, 0.08% and 0.45%
  from the stored values.
- `BSIM4Transient.InverterChain_MatchesNgspiceReference` is disabled
  (`DISABLED_` in its name). Its comment says that the transient does not
  converge near 1 ns, but on Linux (x86-64) it now completes: with the
  reference's 50 mV bound the worst errors are 0.40 V (n1), 0.83 V (n2)
  and 1.83 V (n3), all during the switching edges. The DC levels match;
  the simulated edges come earlier, consistent with the simplified
  capacitance model described below. The reference deck uses `.options method=gear`
  while the test uses trapezoidal; repeating the run with Gear2 changes
  the errors only slightly.
- At commit `02261a2` on macOS, CTest had 1188 tests: 1158 passed, 22 were
  skipped (the twenty comparison tests, which looked for their data in the
  wrong directory then, `TemperatureSweepValidation` and
  `VdsatBranchSelection`), 7 failed and 1 was disabled.
- **About 34 tests of the time step and the LTE check circuits that do
  not move.** Those of `test_predictor_corrector_lte`,
  `test_trapezoidal_variable_step`, `test_multistage_rc`,
  `test_lte_tolerance` and `test_initial_timestep_control` start from the
  operating point of a DC source without `UIC`, so their circuits stay at
  rest and the checks hold whatever the LTE code does (read in the tests
  at `63ce9a6`).
- **Some tests cannot fail, or check little** (read at `63ce9a6`). The
  VDMOS tests `LinearRegion` and `SaturationRegion` read a node that a
  source fixes; no MESFET test checks the value of a drain current; the
  "ngspice validation" references of the transmission-line tests are DC values;
  `bsim4_dc_smoke` checks only that Id is finite;
  `BSIM4NgspiceCompareTest.IdsVsNgspice_T27C` passes with errors up to
  100 % (its comment says 30 %) and sets DVT0, DVT1, ETA0 and K3 to 0,
  which are not BSIM4's defaults; several BJT validation programs check
  another region than they name, or allow 5 to 21 %; the level-3 "Meyer"
  capacitance tests expect Cgs = Cgd = 2/3 Cox W L in the linear region
  from helpers that no stamp uses.
- **Some tests write into the source tree or into fixed names in the
  temporary directory:** `ParameterSweepPerf.NoStepFallsBackSingleRun`
  (`tmp_param_perf.cir` in the source tree), the `NetlistParser` include
  tests (`$TMPDIR/kirimaspice_test/`) and `test_rc_ac_validation`
  (`kirimaspice_ac_reference_test.csv`), so two test runs at the same
  time share these files (read at `63ce9a6`).

## Netlist input

These were found while writing the [User Manual](docs/USER_MANUAL.md) and
the [Reference Manual](docs/REFERENCE_MANUAL.md), by running the program
built from commit `91af284`. What this section says about SPICE and ngspice
comes from their manuals. Five differences from ngspice (scale factors,
AC inputs, source currents, nested `.DC` sweeps and `gnd`) were checked by
running ngspice-47 and removed by following ngspice, as
[NETLIST_CONVENTIONS.md](docs/spec/NETLIST_CONVENTIONS.md) sets out; that
document also lists differences not decided there.

- **`B` sources have no `I(<source>)` and cannot read `.PARAM` names:**
  an expression with either is an error (`Undefined symbol`).
- **Scale factors are not accepted in expressions, with or without
  braces, or in `.OPTIONS` values** (there the option is ignored with a
  warning). A `.PARAM` value without braces that is not a number is read
  as an expression, so `.param r = 2k` is 2000 but `.param r = 2*1k` is an
  error, where ngspice-47 gives 2000 (run on 2026-11-21 at `21bb991`).
- **`PARAMS:` is not accepted** on `.SUBCKT` and `X` lines; parameters are
  written `NAME=value` after the pins.
- **`E`, `F`, `G` and `H` have no `POLY` form;** a line with `POLY(...)`
  after the output nodes is an error.
- **`.FUNC` cannot be called in an element value,** only in `.PARAM`
  expressions (`Expression parse error`).
- **`.IF` cannot read `.PARAM` values.** When the condition cannot be
  evaluated, the parser warns and keeps every branch.
- **A second `.PARAM` for the same name is ignored** with the warning
  `Failed to add ... to symbol table`.
- **In a subcircuit body, `.param x={1k` without its closing brace sets
  nothing, without a message;** an element that uses `x` is then an
  error (run on 2026-11-21 at `21bb991`).
- **A `.MODEL` inside a `.SUBCKT` is not local to it.** A second
  definition of a model name is named in a warning, and the elements after
  it and every subcircuit body use it. ngspice-47 keeps a `.MODEL` of a
  `.SUBCKT` to that subcircuit, and of two top-level definitions uses the
  first: with `dm` IS = 1e-14 at the top level and IS = 1e-10 in the
  subcircuit, its diode at 0.6 V draws 0.119 mA outside and 1.19 A inside
  (ngspice-47 run on 2026-11-21; the warning is given since `4487d91`).
- **`.LIB` in an included file is looked up from the main netlist's
  directory,** not from the included file's: `.include sub/inc.cir`
  whose `.lib models.lib tt` names `sub/models.lib` fails with
  `Cannot resolve library path`, where ngspice-47 finds the file (run on
  2026-11-21 at `21bb991`).
- **The default `W` and `L` of MOSFET levels 3, 8/49, 9 and 14/54 are
  10 µm and 1 µm.** ngspice-47 takes 100 µm by 100 µm for level 3 and
  5 µm by 5 µm for BSIM3 and BSIM4: a level-3 card without parameters at
  VD = VG = 2 V carries 4.144e-4 A here and 4.144e-5 A there (run on
  2026-11-21 at `21bb991`).
- **`.IC` and `.NODESET` need an upper-case `V(`;** `v(out)=1` is skipped
  with a warning.
- **Waveform arguments that SPICE defaults from `.TRAN` must be given:**
  `PULSE` takes all seven, `SIN` at least `VO VA FREQ`, `EXP` all six,
  `SFFM` at least `VO VA FC` (read in the code). An `SFFM` source without
  a DC value has `VO` in the operating point, where ngspice-47 gives 0 V
  (measured at `eaf0629`).
- **The diode reads `IS`, `N`, `CJO`, `VJ` and `M` only, not `RS`, `TT`,
  `BV`, `IBV` and the others, which it names in a warning; the bipolar
  transistor does not read `RE`, `RC`, `CJS`, `XTF`, `VTF`, `ITF`, `PTF`,
  `XCJC` and `FC`; the level-1 MOSFET has no capacitances and no bulk
  junctions.** Other parameters on the cards of these devices and of the
  MOSFET levels are named in a warning
  ([Reference Manual](docs/REFERENCE_MANUAL.md#models)). Among them are
  `TNOM` of the diode and of the level-1 MOSFET, which take 27 °C, and
  `EG` and `XTI` of the diode, which takes 1.11 eV and 3 (run on
  2026-11-21 at `21bb991`). Level 3 accepts `LAMBDA`, `RS`, `RD`, `CJ`,
  `CJSW` and `MJSW` without a warning and does not use them. On the
  element line, levels 3, 8/49, 9 and 12 drop the
  names they do not read, `M=` and `AD=` among them, without a message
  (run at `2826a81`).
- **The bipolar transistor implements the Gummel–Poon currents in one
  device for NPN and PNP, with limits on its parasitics.** The currents
  and their analytic derivatives share `BJT::evaluate`. Forward-active,
  saturation, reverse-active and high-injection currents agree with
  ngspice-47 within 2e-5 relative error (run on 2026-11-15;
  `tests/unit/test_bjt_gummel_poon.cpp`). RB, RBM and IRB give the base
  resistance through an accessor, but that resistance is not stamped;
  RE, RC, the substrate junction, XTF, VTF, ITF, PTF and XCJC are not
  modelled, and FC is fixed at 0.5. The transient integrates the
  voltage-dependent capacitances rather than the junction charges, and
  AC and transient omit the derivative of B-E charge by VBC. Temperature
  scales IS, ISE, ISC, BF and BR by SPICE's Gummel–Poon laws (since
  `ba291cd`), but not the junction potentials or the depletion
  capacitances (read in the code at `ba291cd`).
- **`S` and `Y` take a model name that is not defined before them without
  a message,** and use their defaults.
- **`.SAVE`, `.STEP`, `.SENS`, `.MC`, `.TF`, `.PZ`, `.DISTO`, `.PORT`,
  `.WCASE`, `.OPTIMIZE`, `.PRINT DC` and `.PRINT AC` are read but not run,
  each named in a warning; `.PRINT TRAN` is printed.** `.NOISE`,
  `.MEASURE`, `.TITLE` and unknown directives are ignored with a warning.
- **Lines after `.END` are read.**
- **Empty lines before the title are skipped: the first line that is not
  empty is the title.** ngspice-47 takes the first line as the title even
  when it is empty, so a netlist that starts with an empty line loses its
  first element here. Checked on 2026-11-15 by running both on the same
  netlist: with `V1 a 0 1` on the second line, ngspice-47 printed
  `i(v1) = -1.00000e-03` and this program had no `V1`. Many tests write
  their netlists starting with an empty line.
- **Analyses in a `.control` block are not run as in ngspice.** In runs at
  commit `7434904`, `op` inside `.control` did not select the operating
  point (the program ran the operating point it runs for a netlist
  without analysis lines), and `dc V1 0 1 0.5 V2 0 1 1` swept only `V1`.
  At `2826a81`, `tran` inside `.control` is not run either; `dc` and `ac`
  are, and at `21bb991` `dc` still sweeps its first source only, without
  a message. Write analyses as dot
  directives (`.op`, `.dc`, `.tran`, `.ac`).

## Analyses

Measured with the program built from commit `91af284`, unless an entry
says otherwise.

- In the library's adaptive time-step path, which the program does not
  use, a fixed `TRAPEZOIDAL` method starts without the backward-Euler step
  that the fixed-step path takes (read in the code, not run).
- **`TRTOL`, `CHGTOL`, the `LTE_*` options, `ERROR_EST` and `MAXORD` have
  no effect**, because they act on the adaptive path that the program
  does not use (read in the code).
- **`.OP` with `.DC` runs only the sweep.**
- **The library's adaptive path fails on a circuit at rest with BDF2 or
  BDF3 of fixed order.** A 1 kΩ, 1 µF RC circuit started from its
  operating point (no `UIC`) fails at t = 0, the step shrinking to
  10<sup>-12</sup> s; BDF1 and the order control finish.
  `BDFOrderComparisonTest.FinalValueConvergence` passes over these two
  runs (measured on 2026-11-20).
- **No convergence aid is on by default.** Source stepping, Gmin
  stepping, dynamic Gmin, the continuation method and the
  pseudo-transient fallback run only when `.OPTIONS` asks for them, so a
  DC solve that Newton's method alone does not finish fails. What
  ngspice-47 does by default is not confirmed.
- **A transient takes longer than in ngspice-47.** A ladder of 200
  sections of 100 Ω and 10 pF (201 unknowns), `.TRAN 1n 10u` with 10 001
  points, took 2.1 s; ngspice-47 with `tmax` 1 ns, 10 024 points, took
  0.36 to 0.40 s (macOS arm64, 2026-11-21). The cause is not
  investigated.
- **AC and noise add `GMIN` from every node to ground; DC does not.**
  `I1 0 a DC 1m AC 1m` and `R1 a 0 1k` with `.options gmin=1e-4` give
  1 V in the operating point and 0.909091 V in AC; ngspice-47 gives 1 V
  in both (both run on 2026-11-21, this program at `21bb991`).
- **Of the devices, only the diode (since `36d8f96`) and the bipolar
  transistor carry a GMIN across their junctions.** The level-1 and
  level-3 MOSFETs, BSIM3, the SiC MOSFET, the VDMOS and the JFET stamp
  theirs in `stamp_dc_linear`, which no solver calls for a nonlinear
  device, so they have none. SPICE puts it across the bulk junctions,
  which these models mostly do not have (read in the code at `63ce9a6`).
- **`DEC` and `OCT` sweeps over a fractional number of decades or octaves
  stretch the points to end on the stop frequency.** `.ac dec 10 10 25`
  gives 5 points 1.2574 apart, ending at 25 Hz, where ngspice-47 gives 10,
  12.59, 15.85 and 19.95 Hz; `.ac oct 1 1 3` gives 1, 1.73 and 3 Hz,
  ngspice-47 1 and 2 Hz. Whole decades and octaves agree (both run on
  2026-11-21, this program at `21bb991`).
- **The library's input-referred noise (`NoiseSolver`) divides by the
  transimpedance from the input node,** not by the voltage gain from the
  input source (read in the code at `63ce9a6`; no test checks it).
- **The optimizer's `LD_SLSQP` gets no gradient:**
  `nlopt_objective_wrapper` ignores `grad` (read in the code at
  `63ce9a6`; not confirmed by a run).

## Device models

- **BSIM4 noise and the BSIM4 parameter extractor use a separate,
  simplified current model.** The operating point, DC, transient and AC
  analyses evaluate BSIM4 through `BSIM4Core` (`src/devices/bsim4_core.cpp`).
  The noise sources (`MOSFET_BSIM4::get_noise_sources`) and
  `BSIM4ParameterExtractor` instead use `compute_drain_current` and
  `compute_threshold_voltage` in `src/devices/mosfet_bsim4.cpp`.
- **BSIM4 has no DC model of its source and drain junctions.** ngspice-47's
  `i(vd)` holds their GMIN currents as well (10<sup>−12</sup> S times the
  junction voltage), which shows below the threshold: for the card of
  `tests/unit/test_bsim4_reference.cpp` at V<sub>GS</sub> = 0.2 V the
  channel currents agree and `i(vd)` is larger by GMIN·V<sub>DB</sub>.
- **BSIM1 (`LEVEL` 9) is a simplified model whose P device is not the
  mirror of its N device.** With VFB = −0.8 V (N) or +0.8 V (P), PHI = 0.7,
  K1 = 0.5, TOX = 0.02 µm, MUZ = 600 and W/L = 10 µm / 1 µm at
  |VD| = |VG| = 1 V, the N device carries 1.033e-4 A, ngspice-47's BSIM1 (its
  LEVEL 4) 2.087e-4 A, and the P device 0 A (run on 2026-11-15). The P
  device's stamp does not linearize its current, its threshold takes |VFB|
  and PHI + V<sub>SB</sub>, and the junction capacitances take a negative
  V<sub>BD</sub> of the N device for forward bias (read in the code).
- **BSIM3 (`LEVEL` 8 and 49) is a simplified model, not BSIM3v3**
  (`src/devices/bsim3_core.cpp`, about 450 lines). For TOX = 3 nm,
  VTH0 = 0.5 V, U0 = 670 cm²/Vs, VSAT = 8e4 m/s and W/L = 10 µm / 1 µm at
  VD = VG = 1 V it gives 6.230e-4 A, where ngspice-47's BSIM3 gives
  3.151e-4 A (run on 2026-11-15). It takes U0 in cm²/Vs whatever its size,
  where ngspice-47 takes a U0 up to 1 in m²/Vs (`u0=0.067` and `u0=670`
  give it the same current). It carries no current when V<sub>DS</sub> < 0
  (no reverse mode and no junction diodes: 0 A at VD = −1 V, VG = 1 V).
  With a realistic mobility its operating point does not always converge:
  VDD = 1.8 V through 10 kΩ, VG = 1 V, L = 0.18 µm, W = 1 µm and U0 = 670
  stop after 100 iterations, where ngspice-47 gives V(d) = 0.6291 V.
  Its threshold is not BSIM3's either: with the card above, K1 = 0.5,
  K2 = 0 and VD = 1 V, the term −DVT0·atan(DVT2·V<sub>BS</sub> /
  √(DVT1·L<sub>eff</sub>)) moves it by about ±3.3 V at
  |V<sub>BS</sub>| = 0.3 V, √(|V<sub>BS</sub>| + φ) makes a forward body
  bias raise it, and the current falls by six decades as VG rises past
  it: 1.90e-4 A at VG = 0.4999 V, 4.1e-11 A at 0.5001 V (run on
  2026-11-21 at `21bb991`; the body-bias terms read in the code).
- **BSIM4's capacitances are a simplified Meyer model with BSIM4's
  junctions,** not BSIM4's charge model (`BSIM4Core::compute_capacitances`).
  The junctions take their perimeters from `PS` and `PD` alone, 0 when the
  element line does not give them, where ngspice-47 takes a perimeter
  then: with no junction parameters on the card and AS = 1e-10 m², it gives
  a source capacitance of 4.331e-14 F at 1 V reverse, where the bottom
  junction alone is 3.536e-14 F; the rest fits the default sidewall
  capacitance over a perimeter of 2 W (run on 2026-11-15; ngspice's rule
  not read). The outer fringe capacitance CF is not modelled. In the
  linear region Cgs = Cgd = 2/3 Cox W L (4/3 Cox W L in all). The
  saturation velocity of the capacitances is VSAT − AT·(T − TNOM), in
  kelvin, where the current takes AT·(T/TNOM − 1), so with the default
  AT = 3.3e4 it reaches 0 at TNOM + 2.4 K: the input capacitance is
  7.67e-14 F at 27 and 28 °C and 1.535e-13 F at 30 °C. `MULT_Q` is not
  used; a card's is named so, the element line's is not (run on
  2026-11-21 at `63ce9a6`).
- **BSIM4's MOBMOD 3 (the high-k mobility model) to 6 are computed as
  MOBMOD 0,** without a warning. MOBMOD 0, 1 and 2 follow the BSIM4 4.8.3
  code.
- **BSIM4's threshold differs from ngspice-47's by up to 10 µV with body
  bias** (1 µV at V<sub>BS</sub> = 0, 6 to 10 µV from −0.5 V to −3 V, for
  the card of the reference test). The cause is not confirmed; the BSIM4
  code's k/q (8.617087·10<sup>−5</sup> V/K) differs from the one
  kirimaspice uses (CODATA, 8.617333·10<sup>−5</sup> V/K) by 2.8·10<sup>−5</sup>,
  which moves φ by about 12 µV (an estimate, not checked). VBSC, the lower
  limit of V<sub>bseff</sub>, which was 0 before `74ba4d2`, was one
  cause: with it the current at V<sub>BS</sub> = −1 V went from −5.0e-5 to
  +1.1e-5 relative to ngspice-47's. How much of the threshold difference
  above remains is not confirmed (measure V<sub>th</sub> again with the
  reference test's card).
- **Some BSIM4 parameters and limits are not BSIM4's.** `RDSWMIN`,
  `RDSMOD` and `TEMPMOD` are not read from a card. A card with
  `DROUT = 0` gives −8.7e5 A and one with `DSUB = 0` 1.2e-3 A, where
  ngspice-47 turns the device off. Other exponential limits (AGS, the
  smoothing of V<sub>gsteff</sub>) take `MAX_EXP` = 2.353853e15, not
  BSIM4's 5.83e14 (not compared with ngspice-47). The guard for
  `DVT1 = 0` and a clamp of the DIBL shift to ±0.5 V are not BSIM4's (run
  and read on 2026-11-21 at `63ce9a6`).
- **BSIM4's gm, gds and gmb are central differences of its drain current**
  (`BSIM4Core::evaluate`), so a stamp evaluates the current seven times.
  `BSIM4Core::compute_cdrain` still computes analytic derivatives, which are
  not used.
- **The level 3 MOSFET differs from ngspice-47 where ngspice-47 departs
  from the published equations.** Level 3 follows SPICE's level 3 as
  published (Vladimirescu and Liu, UCB/ERL M80/7, 1980), and agrees with
  ngspice-47 to 2·10<sup>−5</sup> wherever channel-length modulation does
  not act. With KAPPA and NSUB, ngspice-47 modulates the channel length
  below VDSAT as well and shifts it above: its dL<sup>2</sup>/(KAPPA
  X<sub>d</sub><sup>2</sup>) is V<sub>DS</sub> − 7/8 V<sub>DSAT</sub> above
  VDSAT and V<sub>DSAT</sub>/8 (V<sub>DS</sub>/V<sub>DSAT</sub>)<sup>8</sup>
  below (inferred from its results; not found in its documentation), where
  the published equations have V<sub>DS</sub> − V<sub>DSAT</sub> above
  VDSAT and nothing below. The difference is up to 4.2 % next to VDSAT and
  below 1 % far above it. A card without PHI gets PHI = 2 V<sub>t</sub>
  ln(NSUB/n<sub>i</sub>) with n<sub>i</sub> = 1.45·10<sup>10</sup>
  cm<sup>−3</sup>, which is 0.08 % above ngspice-47's (whose results
  correspond to n<sub>i</sub> ≈ 1.466·10<sup>10</sup> cm<sup>−3</sup>; the
  cause is not confirmed). Level 3 has no DC junction diodes (ngspice-47's
  current includes them when a junction is forward biased), no
  temperature dependence (it reads TNOM), and no capacitances in the
  transient. BSIM1 reads TNOM without a temperature dependence too.
  With VMAX and KAPPA together, level 3 differs from ngspice-47 by up to
  12 % at V<sub>DS</sub> = 10 V; the cause is not confirmed.
- **With a forward-biased body, ngspice-47's level-1 g<sub>mbs</sub> is
  not the derivative of its current; here g<sub>mb</sub> is.** For
  `vto=0.7 kp=1e-4 gamma=0.5 phi=0.6`, W/L = 10 µm / 1 µm, VGS = 1.5 V,
  VDS = 2 V and VBS = 0.2 V both give 373.723 µA, rising by 2.79e-4 A/V
  with VBS, but ngspice-47 reports g<sub>mbs</sub> = 3.348e-4 S, which is
  g<sub>m</sub>·GAMMA/(2·sarg) with sarg the expression that replaces the
  root for a forward bias (run on 2026-11-21 at `21bb991`).
- **The noise sources differ from SPICE's** (library only: the program
  does not run `.NOISE`; read in the code at `63ce9a6`). The channel
  thermal noise of level 3 is 4kT·(8/3)·g<sub>m</sub>, four times SPICE's
  4kT·(2/3)·g<sub>m</sub>, and 0 in reverse mode; BSIM3, BSIM4, the GaN
  HEMT and the BSIM1 P device take 8/3 too, and level 1, the MESFET, the
  FinFET, the IGBT and the BSIM1 N device 2/3. The level-1 flicker noise
  lacks C<sub>ox</sub> and takes `EF` on the area, where ngspice-47 has
  KF·I<sub>d</sub><sup>AF</sup>/(f<sup>EF</sup>·C<sub>ox</sub>·W·L).
  The resistor, level 1 and level 3 take 300.15 K as their noise
  temperature, and the MESFET and the SCR their TNOM. The diode gives no
  shot noise below 1 fA.
- **The MESFET's `LEVEL=2` is not Statz's model.** It computes
  β·v<sub>ov</sub><sup>3</sup>·(1 + λ V<sub>DS</sub>)·(1 − (1 −
  V<sub>DS</sub>/VP)<sup>3</sup>)/3 and does not use `B`, where Statz's is
  β·v<sub>ov</sub><sup>2</sup>/(1 + B v<sub>ov</sub>)·(1 − (1 − α
  V<sub>DS</sub>/3)<sup>3</sup>)·(1 + λ V<sub>DS</sub>): with BETA 0.1,
  VTO −2 V, B 0.3, ALPHA 2, LAMBDA 0, VGS = 0 and VDS = 1 V it gives
  0.187654 A, ngspice-47 0.24074 A (both run on 2026-11-21, this program
  at `21bb991`).
- **The IGBT's tail current depends on the time step.** It starts from
  the collector current of the step after the turn-off: 6.12 A at 1.5 µs
  with `tmax` 100 ns, 1.22 A with 1 ns (run on 2026-11-21 at `63ce9a6`).
- **Above `VBD` the GaN HEMT adds 1 mS to g<sub>ds</sub> without the
  current that goes with it** (read in the code at `63ce9a6`). **The
  VDMOS warns "entered breakdown region" at every Newton iteration above
  `VBD`:** 700 V through 1 MΩ with `VBD = 600` settles at 600.01 V after
  97 such warnings (run on 2026-11-21 at `abb6804`).
- **JFET cannot be used from a netlist.** The library has a JFET model
  (`include/kirimaspice/devices/jfet.hpp`), but the parser has no `J`
  element. Its gate capacitances put C/dt (V − V<sub>prev</sub>) of the
  present iterate into the right-hand side, the form that the GaN HEMT,
  the VDMOS and the FinFET had before their fixes (read in the code; not
  run).
- **The FinFET is a simplified square-law model** (`src/devices/finfet.cpp`).
  Below the threshold its current is β·V<sub>t</sub><sup>2</sup>·exp(…)·(1 −
  exp(−V<sub>DS</sub>/V<sub>t</sub>)), which at the threshold is about
  β·V<sub>t</sub><sup>2</sup> where the square law above it starts at 0, so
  the current jumps there (read in the code). W<sub>eff</sub> is
  NFIN·HFIN, without the fin's other side and top. Drain and source are not
  swapped when V<sub>DS</sub> < 0, and `TFIN` and `VSAT` are read but not
  used ([reference manual](docs/REFERENCE_MANUAL.md#finfet-level12)).
- **The SiC MOSFET, GaN HEMT, VDMOS, FinFET and MESFET integrate their
  capacitances by backward Euler** whatever integration method the
  transient uses (read in the code).
- **The lossy line `O` is wrong in the transient.** DC and AC take its
  exact two-port. In the transient (`src/devices/tline_lossy.cpp`) the
  wave that arrives at a port is the delayed voltage of the other port
  alone, attenuated by exp(−α·length) with α = (R/Z0 + G·Z0)/2 and passed
  through a low-pass filter; it lacks the term of the delayed current,
  Z0·I(t − TD), that the lossless line has. So even a line without loss is
  wrong: with R = 0, L = 250 nH/m, C = 100 pF/m and TD = 10 ns between
  50 Ω and 50 Ω, a 1 V step settles at v(a) = 2/3 V and v(b) = 1/3 V,
  where its operating point gives 0.5 V at both; with R = 0.5 Ω/m, v(b)
  is 0.0144 V at 40 ns, ngspice-47's LTRA 0.495 V (both run on
  2026-11-21, this program at `21bb991`). For the first TD of a transient its
  delayed waves are those of 0 V, not those of the operating point (read
  in the code).
- **The lossless line `T` delays by the step when the step is longer than
  `TD`.** A 10 MHz `SIN` of 1 V between 50 Ω and 50 Ω through `TD = 1n`,
  `.tran 5n 200n`, gives v(b) = 0.1545 V at 150 ns, the wave of 5 ns
  before; the analytic value, which ngspice-47 gives, is 0.0314 V. The
  fixed step is not limited to the shortest delay of a line (both run on
  2026-11-21, this program at `21bb991`).
- **Three or more inductors coupled to each other are coupled pair by
  pair.** Each `K` adds what the inverse of its own 2 × 2 inductance matrix
  adds to the two inductors (`MutualInductance::inverse_inductance`), in AC
  and in the transient. That is exact for one pair, not for three inductors
  that `K` lines couple among themselves: for three of 1 H with k = 0.5
  between each two, the diagonal of the inverse of the 3 × 3 matrix is 3/2,
  and the pairs add up to 5/3 (computed in exact fractions).
- KLU is the only working linear solver. The SuperLU_DIST backend of the
  library is a placeholder, and the program has no option to choose it.

## Command line and output

- No SSF file is written for a DC sweep.
- The SSF output format has no published specification in this repository.
- **SSF stores voltages and currents as integers of 1 µV and 1 µA, and
  writes no scale into the file,** so 50 pA is stored as 0. The times of
  its blocks are the time times 10⁹, truncated (`.tran 0.3n 3n` gives 0,
  0, 0, 0, 1, 1, ...), and its header says version 3.4 where
  `file.version` says 4.3 (run and read on 2026-11-21 at `63ce9a6`).
- **`run_netlist` of `tools/kirimaspice_pyapi.py` writes the netlist's
  text into a temporary directory and runs it there,** so a relative
  `.include` fails through it and kirimaspice exits with 1 (run on
  2026-11-21 at `63ce9a6`).

## Platforms

- On macOS (arm64), GCC 16.2.0, Homebrew's clang 23.1.4 (libc++, OpenMP)
  and Apple clang 21.0.0 (no OpenMP) give the same test results.
- On Ubuntu 24.04 (x86-64), CI builds and tests with GCC 13.3.0 and clang
  18.1.3 (libstdc++), CMake 3.31.6 and SuiteSparse 7.6.1 from
  `libsuitesparse-dev`. Whether its results now equal those on macOS
  is not confirmed; CI's test summary shows it.
- On 2026-11-14, the README's steps were followed in a fresh `ubuntu:24.04`
  container on arm64 (Colima on macOS): GCC 13.3.0, CMake 3.28.3, Ninja
  1.11.1, Python 3.12.3, Git 2.43.0 and `libsuitesparse-dev`
  1:7.6.1+dfsg-1build1. It built, and 1097 tests were registered: 1066
  passed, 23 were skipped, 1 is disabled, and the 7 listed tests failed,
  as on macOS (arm64).
- CMake 3.25, the minimum the README names, is not tested; the oldest
  tested is 3.28.3.
- Windows is not tested.

## Source code

Counted on 2026-11-21 at commit `9aa304c`, the warnings at `81bfb2a`.

- **Compiler warnings.** A full build with GCC 16.2.0 on macOS gives 559
  warning lines, at 153 places once those repeated by the headers of
  several files are counted once; 150 are in kirimaspice's own sources:
  125 `-Wunused-parameter`, 17 `-Wunused-variable`, 4
  `-Wunused-but-set-variable`, 3 `-Woverloaded-virtual` and 1
  `-Wunused-function`. Other compilers are not counted.
- **Large files.** Five source files have more than 2000 lines:
  `src/parser/netlist_parser.cpp` (6509), `tests/unit/test_parser.cpp`
  (4657), `src/devices/bsim4_core.cpp` (3082),
  `src/circuit/circuit_graph.cpp` (2135) and `tests/unit/test_bsim3.cpp`
  (2009).
- **Comments in Japanese.** 46 lines in 11 files of `src/` and `include/`
  hold Japanese text, most in `mosfet_bsim4_params.hpp` (19) and
  `mosfet_bsim3_params.hpp` (13); with the tests, 56 lines in 17 files.
- **Two directories of helpers**, `include/kirimaspice/util/` and
  `include/kirimaspice/utils/`.
- **The capacitor, the inductor and the mutual inductance write their own
  companion formulas** instead of `device_utils::capacitor_companion`,
  which the diode, the BJT, BSIM3, BSIM4 and the IGBT use; they give the
  same numbers, but the equations are not written once (read in the code
  at `63ce9a6`).
- **`MonteCarloBatchExecutor::execute_dc_monte_carlo_gpu` has no GPU
  code.** `src/analysis/monte_carlo_gpu.cpp` logs "Using fast CPU path
  (CUDA off)" and runs the CPU path.
