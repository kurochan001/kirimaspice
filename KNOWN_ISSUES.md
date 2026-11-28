# Known issues

[日本語](KNOWN_ISSUES.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

This list holds what has been measured or read in the code. Measurements
were made on 2026-11-14 on macOS 27.2 (arm64) with GCC 16.2.0, CMake 4.4.4
and SuiteSparse 7.6.1 unless an entry says otherwise.

## Tests

At `6b334da` the macOS run passed all 1505 tests. The five
`ManufacturerModels.*` tests and `BJTModelTest.SimpleCommonEmitter`
use the diode and NPN example cards in [tests/models](tests/models).
The known-failure list is empty.

| Comparison | State | Reference |
| --- | --- | --- |
| BSIM4 temperature reference (1) | runs | reference from ngspice-45.2 is committed |
| BSIM4 VDSAT branch (1) | runs | reference verified with ngspice-45.2 |
| BSIM4 transient inverter chain (1) | runs | stored ngspice-47 waveform |
| Live ngspice comparison | conditional | runs only when ngspice is installed |

- `CMOS_Inverter.DCTransferCharacteristic_HighInput` used to require an output
  of at least 0 V and saw -4.1e-25 V on arm64, which is roundoff around the
  exact value of 0 V; its lower bound now allows -1e-12 V.
- The ngspice version used to make most of the reference data in
  `validation/` and `tests/validation/` is not recorded. Recorded examples include
  `validation/mosfet_2stage_amplifier_ac_ngspice.csv`, written by
  ngspice-45.2, and the inverter-chain reference described below. The twenty comparison tests that read `validation/` were
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
- `BSIM4Transient.InverterChain_MatchesNgspiceReference` and the reference
  generator read the [same netlist](validation/bsim4_inv_chain.cir) (Gear,
  1 ps step, 10 ns duration). On macOS at `6b334da`, the maximum absolute
  voltage errors against ngspice-47 were 2.51 mV at n1, 2.07 mV at n2
  and 2.39 mV at n3. The limit is 50 mV at each node.
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
  source fixes; of the MESFET's drain currents only `LEVEL=2`'s is checked
  against a value (`MesfetStatz`); the "ngspice validation" references of the transmission-line tests are DC values;
  `bsim4_dc_smoke` checks only that Id is finite;
  `BSIM4NgspiceCompareTest.IdsVsNgspice_T27C` passes with errors up to
  100 % (its comment says 30 %) and sets DVT0, DVT1, ETA0 and K3 to 0,
  which are not BSIM4's defaults; several BJT validation programs check
  another region than they name, or allow 5 to 21 %; the level-3 "Meyer"
  capacitance tests expect Cgs = Cgd = 2/3 Cox W L in the linear region
  from helpers that no stamp uses.

## Netlist input

These were found while writing the [User Manual](docs/USER_MANUAL.md) and
the [Reference Manual](docs/REFERENCE_MANUAL.md), by running the program
built from commit `91af284`. What this section says about SPICE and ngspice
comes from their manuals. Five differences from ngspice (scale factors,
AC inputs, source currents, nested `.DC` sweeps and `gnd`) were checked by
running ngspice-47 and removed by following ngspice, as
[NETLIST_CONVENTIONS.md](docs/spec/NETLIST_CONVENTIONS.md) sets out; that
document also lists differences not decided there.

- **A `B` expression that fails when it is evaluated gives 0, with an
  error message at each evaluation, and the run goes on.** It is checked
  once when the line is read, at `TIME` 0 with every voltage and current
  0, so this is left only for a failure that that evaluation does not
  reach, such as a failing `.FUNC` in a branch of `? :` not taken there.
- **`I(<name>)` in a `B` expression reads only the current of a voltage
  source or of an `E`, `H` or voltage-form `B` source;** for any other
  element it is an error (`R1 is not one`). ngspice-45.2 also reads the
  current of a resistor: `B1 b 0 V={I(R1)*1k}` with 1 mA through R1
  gives 1 V (run 2026-11-28).
- **`E`, `F`, `G` and `H` have no `POLY` form;** a line with `POLY(...)`
  after the output nodes is an error.
- **The diode reads `IS`, `N`, `CJO`, `VJ`, `M`, `RS`, `TT`, `BV` and
  `IBV` only, not `FC`, `KF`, `AF` and the others, which it names in a
  warning (its FC is 0.5); the bipolar
  transistor does not read `RE`, `RC`, `CJS`, `XTF`, `VTF`, `ITF`, `PTF`,
  `XCJC` and `FC`; the level-1 MOSFET has no capacitances and no bulk
  junctions.** Other parameters on the cards of these devices and of the
  MOSFET levels are named in a warning
  ([Reference Manual](docs/REFERENCE_MANUAL.md#models)). Among them are
  `TNOM` of the diode and of the level-1 MOSFET, which take 27 °C, and
  `EG` and `XTI` of the diode, which takes 1.11 eV and 3 (run on
  2026-11-21 at `21bb991`). Level 3 accepts `LAMBDA`, `RS`, `RD`, `CJ`,
  `CJSW` and `MJSW` without a warning and does not use them.
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
- **`.MODEL` has no SCR or TRIAC type:** `S` and `Y` take `NAME=value`
  parameters on their lines only, and a model name there is an error.
- **`.STEP`, `.SENS`, `.MC`, `.TF`, `.PZ`, `.DISTO`, `.PORT`, `.WCASE`
  and `.OPTIMIZE` are read but not run, each named in a warning;
  `.PRINT DC`, `.PRINT TRAN` and `.PRINT AC` are printed, and `.SAVE`
  limits the output files.** `.NOISE`,
  `.MEASURE`, `.TITLE` and unknown directives are ignored with a warning.
- **Empty lines before the title are skipped: the first line that is not
  empty is the title.** ngspice-47 takes the first line as the title even
  when it is empty, so a netlist that starts with an empty line loses its
  first element here. Checked on 2026-11-15 by running both on the same
  netlist: with `V1 a 0 1` on the second line, ngspice-47 printed
  `i(v1) = -1.00000e-03` and this program had no `V1`. Many tests write
  their netlists starting with an empty line.
- **A `.control` block runs `op`, `dc`, `tran` and `ac` in the program's
  order, each kind once,** not in the order of the block as ngspice does,
  and the block's other commands (`noise`, `pz`, `sens`, `plot`, `set` and
  others) are named in a warning and not run.

## Analyses

Measured with the program built from commit `91af284`, unless an entry
says otherwise.

- In the library's adaptive time-step path, which the program does not
  use, a fixed `TRAPEZOIDAL` method starts without the backward-Euler step
  that the fixed-step path takes (read in the code, not run).
- **`TRTOL`, `CHGTOL`, the `LTE_*` options, `ERROR_EST` and `MAXORD` have
  no effect**, because they act on the adaptive path that the program
  does not use (read in the code).
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
- **Of the devices, only the diode (since `36d8f96`) and the bipolar
  transistor carry a GMIN across their junctions.** The level-1 and
  level-3 MOSFETs, BSIM3, the SiC MOSFET, the VDMOS and the JFET stamp
  theirs in `stamp_dc_linear`, which no solver calls for a nonlinear
  device, so they have none. SPICE puts it across the bulk junctions,
  which these models mostly do not have (read in the code at `63ce9a6`).
- **`.AC LIN 2` gives both ends; ngspice-47 gives the start only.**
  `.ac lin 2 1 10` gives 1 and 10 Hz here and 1 Hz in ngspice-47, whose
  linear step is 0 for fewer than three points (run on 2026-11-21). The
  two points `<n>` asks for are kept. `DEC` and `OCT` take ngspice-47's
  grid.
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
- **BSIM1 is not offered from a netlist; `LEVEL=9` is an error.**
  ngspice's `LEVEL` 9 is MOS9 and its BSIM1 is `LEVEL` 4, and the
  library's BSIM1 (`MOSFET_BSIM1`) is a simplified model whose P device is
  not the mirror of its N device: with VFB = −0.8 V (N) or +0.8 V (P), PHI = 0.7,
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
- **BSIM4 uses simplified Meyer capacitances for `CAPMOD=0` and `1`.**
  The default `CAPMOD=2` uses `BSIM4Core::intrinsic_charges` for AC and
  transient analysis. Overlap, outer fringe (`CF`) and junction
  capacitances are calculated separately. When `PS` or `PD` is omitted,
  the isolation perimeter follows the finger layout for the supported
  default geometry (`GEOMOD=0`, zero diffusion lengths). Each finger
  also contributes a gate-edge junction capacitance on both sides.
- **BSIM4's MOBMOD 3 (the high-k mobility model) to 6 are computed as
  MOBMOD 0,** and the card is named in a warning. MOBMOD 0, 1 and 2 follow
  the BSIM4 4.8.3 code.
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
- **Some noise sources differ from SPICE's** (library only: the program
  does not run `.NOISE`; read in the code). The channel thermal noise of
  BSIM3, BSIM4 and the GaN HEMT is 4kT·(8/3)·g<sub>m</sub>, where level 1,
  level 3, BSIM1, the MESFET, the FinFET and the IGBT take SPICE's
  4kT·(2/3)·g<sub>m</sub> (level 3 and BSIM1 since 2026-11-21). The
  level-1 flicker noise lacks C<sub>ox</sub> and takes `EF` on the area,
  where ngspice-47 has
  KF·I<sub>d</sub><sup>AF</sup>/(f<sup>EF</sup>·C<sub>ox</sub>·W·L) (not
  compared by running it). Level 3, which has no temperature model, takes
  300.15 K as its noise temperature, and the MESFET and the SCR their TNOM;
  the resistor and level 1 take their own temperature. The diode gives no
  shot noise below 1 fA.
- **The MESFET has ngspice's `LEVEL=1` only,** Statz's model (0.240741 A
  for BETA 0.1, VTO −2 V, B 0.3, ALPHA 2, VGS = 0 and VDS = 1 V, as
  ngspice-47 gives, run on 2026-11-21); its other levels, other models,
  are an error. The gate junctions and the charge model of ngspice's
  MESFET are not modelled; `CGS`, `CGD` and `CDS` are constant. Curtice's
  model is the library's only (`MESFETModel::CURTICE`).
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
- **The lossy line `O`'s transient is an approximation.** DC and AC take
  its exact two-port; the transient takes up to 16 sections of the
  constant-parameter (Bergeron) line, each lossless with its loss lumped
  at its ends (`TLineLossy`), so it does not follow a line whose loss per
  16 sections is large, such as an RC line. For a 1 V step through 50 Ω
  into L = 250 nH/m, C = 100 pF/m and TD = 10 ns loaded by 50 Ω, the far
  end is within 2·10<sup>−5</sup> V (R = 0.5 Ω/m) and 1·10<sup>−4</sup> V
  (R = 5 Ω/m) of ngspice-47's LTRA, and the near end within
  1.3·10<sup>−3</sup> V; on an edge the delays of the sections, read by
  linear interpolation, spread it over about √M steps (M sections): with
  steps of 0.1 ns and R = 5 Ω/m the far end is 0.37 V 0.2 ns after the
  edge arrives, where LTRA gives 0.45 V, and 0.02 ns steps agree (run on
  2026-11-21). Each section's delay TD/M bounds the step.
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
