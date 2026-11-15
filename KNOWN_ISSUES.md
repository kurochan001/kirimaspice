# Known issues

[日本語](KNOWN_ISSUES.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

This list holds what has been measured or read in the code. Measurements
were made on 2026-11-14 on macOS 27.2 (arm64) with GCC 16.2.0, CMake 4.4.4
and SuiteSparse 7.6.1 unless an entry says otherwise.

## Tests

- **Seven tests fail** and are listed in `tests/known_failures.txt`:
  - `ManufacturerModels.*` (five tests) and `BJTModelTest.SimpleCommonEmitter`
    include `tests/models/2N3904.lib` and `tests/models/1N4148.lib`, which are
    not in the repository.
  - `CMOS_Inverter.DCTransferCharacteristic_HighInput` checks that the output
    voltage is at least 0 V and gets -4.1e-25 V on macOS (arm64) and on
    Ubuntu 24.04 (arm64). It passes on Ubuntu 24.04 (x86-64) with GCC 13.3.0
    and clang 18.1.3.
- **Twenty comparison tests are always skipped.** They look for reference
  data under `tests/validation/` (for example
  `tests/validation/rc_lowpass_ac_ngspice.csv`), but the files are in
  `validation/`. When the files are placed where the tests look, 19 of the
  20 pass and `MOSFET2StageAmplifierValidation.MatchesNgspice` fails: the
  phase differs from the reference by up to 11.7 degrees near 10 Hz, against
  a tolerance of 5 degrees. Which side is right is not confirmed.
- The ngspice version used to make the reference data in `validation/` and
  `tests/validation/` is not recorded.
- `BSIM4TemperatureValidationTest.TemperatureSweepValidation` is skipped: it
  reads `/tmp/bsim4_temperature_refs.json`, which
  `scripts/bench/generate_bsim4_temperature_refs.py` writes.
- `BSIM4ComprehensiveTest.NgspiceComparison` runs `ngspice` and is skipped
  when ngspice is not installed. On macOS with ngspice-47 from Homebrew it
  runs and passes.
- `BSIM4ComprehensiveTest.VdsatBranchSelection` always skips itself: its
  comment says that its reference current of 2.1 A is not verified.
- `BSIM4Transient.InverterChain_MatchesNgspiceReference` is disabled
  (`DISABLED_` in its name); its comment says that the BSIM4 transient
  does not converge near 1 ns (not checked again).
- At commit `02261a2` on macOS, CTest has 1188 tests: 1158 pass, 22 are
  skipped (the twenty comparison tests above, `TemperatureSweepValidation`
  and `VdsatBranchSelection`), 7 fail and 1 is disabled.
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
- **Inside `.SUBCKT`, only `R`, `C`, `L`, `V` and `I` (DC and AC values,
  no waveform), `M` (four terminals, level 1 or BSIM4) and `X` are
  expanded.** Other elements
  are skipped with a warning. `PARAMS:` is not accepted.
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
- **The diode reads only `IS` and `N`; the bipolar transistor reads `IS`,
  `BF`, `NF`, `ISE`, `NE`, `ISC`, `NC` and noise parameters; the level-1
  MOSFET has no bulk terminal in its equations and no `LAMBDA`, and its
  `GAMMA` is a noise coefficient.** Other parameters on the cards of
  diodes, bipolar transistors, level 1 and BSIM4 are ignored without a
  warning; the other MOSFET levels warn about parameters they do not know
  ([Reference Manual](docs/REFERENCE_MANUAL.md#models)). Level 3 accepts
  `LAMBDA`, `RS`, `RD`, `CJ`, `CJSW` and `MJSW` without a warning and does
  not use them.
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
  point (the program fell back to its default, which prints no currents),
  and `dc V1 0 1 0.5 V2 0 1 1` swept only `V1`. Write analyses as dot
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
- **The program chooses the operating point and the transient by a text
  search.** It looks for `.op`, `.dc` and `.tran` anywhere in each line of
  the netlist file, comments included, so a `.OPTIONS` line runs an
  operating point and a comment that mentions `.tran` runs a transient.
- Options the program does not know are ignored without a message, and the
  netlist must be the first argument.
- A non-numeric value of `--wav-sample-rate` ends the program with an
  uncaught `std::invalid_argument` (exit code 134 on macOS).
- A netlist with no analysis line whose operating point does not converge
  prints error messages and exits with 0.
- The CSV of an AC analysis says `# Simulator: kirimaspice v0.27`; the
  version is written there as fixed text.
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
