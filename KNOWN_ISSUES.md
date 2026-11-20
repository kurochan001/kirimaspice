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
- Some tests find their data relative to the build directory and pass only
  when it is `build/` directly under the source tree.

## Netlist input

These were found while writing the [User Manual](docs/USER_MANUAL.md) and
the [Reference Manual](docs/REFERENCE_MANUAL.md), by running the program
built from commit `91af284`. What this section says about SPICE and ngspice
comes from their manuals. Five differences from ngspice (scale factors,
AC inputs, source currents, nested `.DC` sweeps and `gnd`) were checked by
running ngspice-47 and removed by following ngspice, as
[NETLIST_CONVENTIONS.md](docs/spec/NETLIST_CONVENTIONS.md) sets out; that
document also lists differences not decided there.

- **`B` sources have no `I(<source>)`:** an expression with `I(...)` is an
  error.
- **Scale factors are not accepted inside braces or in `.OPTIONS`
  values** (there the option is ignored with a warning).
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
- **`.IC` and `.NODESET` need an upper-case `V(`;** `v(out)=1` is skipped
  with a warning.
- **Waveform arguments that SPICE defaults from `.TRAN` must be given:**
  `PULSE` takes all seven, `SIN` at least `VO VA FREQ`, `EXP` all six,
  `SFFM` at least `VO VA FC` (read in the code). An `SFFM` source without
  a DC value has `VO` in the operating point, where ngspice-47 gives 0 V
  (measured at `eaf0629`).
- **The diode reads only `IS` and `N`; the bipolar transistor does not read
  `RE`, `RC`, `CJS`, `XTF`, `VTF`, `ITF`, `PTF`, `XCJC` and `FC`; the level-1
  MOSFET has no bulk terminal in its equations and no `LAMBDA`, and its
  `GAMMA` is a noise coefficient.** Other parameters on the cards of
  diodes, bipolar transistors, level 1 and BSIM4 are ignored without a
  warning; the other MOSFET levels warn about parameters they do not know
  ([Reference Manual](docs/REFERENCE_MANUAL.md#models)). Level 3 accepts
  `LAMBDA`, `RS`, `RD`, `CJ`, `CJSW` and `MJSW` without a warning and does
  not use them.
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
  AC and transient omit the derivative of B-E charge by VBC. The
  temperature adjustment scales currents and beta, without adjusting
  depletion capacitances or junction potentials (read in the code).
- **`S` and `Y` take a model name that is not defined before them without
  a message,** and use their defaults.
- **`.PRINT`, `.SAVE`, `.STEP`, `.SENS`, `.MC`, `.TF`, `.PZ`, `.DISTO`,
  `.PORT`, `.WCASE` and `.OPTIMIZE` are read, but the program does not use
  them.** `.NOISE`, `.MEASURE` and `.TITLE` are ignored.
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
  without analysis lines), and `dc V1 0 1 0.5 V2 0 1 1` swept only `V1`. Write analyses as dot
  directives (`.op`, `.dc`, `.tran`, `.ac`).

## Analyses

Measured with the program built from commit `91af284`, unless an entry
says otherwise.

- In the library's adaptive time-step path, which the program does not
  use, a fixed `TRAPEZOIDAL` method starts without the backward-Euler step
  that the fixed-step path takes (read in the code, not run).
- **`tmax` of `.TRAN` has no effect.** The parser reads it, but the
  program steps by `tstep`: `.TRAN 10u 1m 0 2u` gives 101 points, as
  `.TRAN 10u 1m` does. ngspice-47 limits the step to `tmax` and gives 508
  points with a largest step of 2 µs (measured at commit `d858190`).
- **`TRTOL`, `CHGTOL`, the `LTE_*` options, `ERROR_EST` and `MAXORD` have
  no effect**, because they act on the adaptive path that the program
  does not use (read in the code).
- **`.OP` with `.DC` runs only the sweep.**

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
- **BSIM4's capacitances are a simplified Meyer model with BSIM4's
  junctions,** not BSIM4's charge model (`BSIM4Core::compute_capacitances`).
  The junctions take their perimeters from `PS` and `PD` alone, 0 when the
  element line does not give them, where ngspice-47 takes a perimeter
  then: with no junction parameters on the card and AS = 1e-10 m², it gives
  a source capacitance of 4.331e-14 F at 1 V reverse, where the bottom
  junction alone is 3.536e-14 F; the rest fits the default sidewall
  capacitance over a perimeter of 2 W (run on 2026-11-15; ngspice's rule
  not read). The outer fringe capacitance CF is not modelled.
- **BSIM4's MOBMOD 3 (the high-k mobility model) to 6 are computed as
  MOBMOD 0,** without a warning. MOBMOD 0, 1 and 2 follow the BSIM4 4.8.3
  code.
- **BSIM4's threshold differs from ngspice-47's by up to 10 µV with body
  bias** (1 µV at V<sub>BS</sub> = 0, 6 to 10 µV from −0.5 V to −3 V, for
  the card of the reference test). The cause is not confirmed; the BSIM4
  code's k/q (8.617087·10<sup>−5</sup> V/K) differs from the one
  kirimaspice uses (CODATA, 8.617333·10<sup>−5</sup> V/K) by 2.8·10<sup>−5</sup>,
  which moves φ by about 12 µV (an estimate, not checked).
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
  transient uses, and the IGBT does so under the trapezoidal method (its
  `stamp_transient` takes (V − V<sub>prev</sub>)/dt for both).
- **The lossy line `O` is approximate in the transient.** DC and AC take
  its exact two-port; the transient takes the lossless line's model with
  the characteristic impedance sqrt(L/C), the delayed waves attenuated by
  exp(−α·length) with α = (R/Z0 + G·Z0)/2, and a low-pass filter whose time
  constant is made from L/R and C/G (`src/devices/tline_lossy.cpp`, read in
  the code). For the first TD of a transient its delayed waves are those of
  0 V, not those of the operating point (read in the code).
- **Three or more inductors coupled to each other are coupled pair by
  pair.** Each `K` adds what the inverse of its own 2 × 2 inductance matrix
  adds to the two inductors (`MutualInductance::inverse_inductance`), in AC
  and in the transient. That is exact for one pair, not for three inductors
  that `K` lines couple among themselves: for three of 1 H with k = 0.5
  between each two, the diagonal of the inverse of the 3 × 3 matrix is 3/2,
  and the pairs add up to 5/3 (computed in exact fractions).
- KLU is the only working linear solver. The SuperLU_DIST backend is a
  placeholder: asking for it falls back to KLU.

## Command line and output

- There is no `--version` option.
- Options the program does not know are ignored without a message, and the
  netlist must be the first argument.
- Log lines and results both go to standard output.
- No SSF file is written for a DC sweep.
- The SSF output format has no published specification in this repository.

## Platforms

- On macOS (arm64), GCC 16.2.0, Homebrew's clang 23.1.4 (libc++, OpenMP)
  and Apple clang 21.0.0 (no OpenMP) give the same test results.
- On Ubuntu 24.04 (x86-64), CI builds and tests with GCC 13.3.0 and clang
  18.1.3 (libstdc++), CMake 3.31.6 and SuiteSparse 7.6.1 from
  `libsuitesparse-dev`; the results differ from macOS only in
  `CMOS_Inverter.DCTransferCharacteristic_HighInput`, which passes.
- On 2026-11-14, the README's steps were followed in a fresh `ubuntu:24.04`
  container on arm64 (Colima on macOS): GCC 13.3.0, CMake 3.28.3, Ninja
  1.11.1, Python 3.12.3, Git 2.43.0 and `libsuitesparse-dev`
  1:7.6.1+dfsg-1build1. It built, and 1097 tests were registered: 1066
  passed, 23 were skipped, 1 is disabled, and the 7 listed tests failed,
  as on macOS (arm64).
- CMake 3.25, the minimum the README names, is not tested; the oldest
  tested is 3.28.3.
- Windows is not tested.
