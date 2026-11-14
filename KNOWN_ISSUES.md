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

- **`B` sources have no AC model and no `I(<source>)`.** In AC analysis a
  `B` source contributes nothing and logs `AC analysis not yet supported`;
  an expression with `I(...)` is an error.
- **Scale factors are not accepted inside braces or in `.OPTIONS`
  values** (there the option is ignored with a warning).
- **Inside `.SUBCKT`, only `R`, `C`, `L`, `V` and `I` (DC value), `M`
  (four terminals, level 1 or BSIM4) and `X` are expanded.** Other elements
  are skipped with a warning. `PARAMS:` is not accepted.
- **`.FUNC` cannot be called in an element value,** only in `.PARAM`
  expressions (`Expression parse error`).
- **`.IF` cannot read `.PARAM` values.** When the condition cannot be
  evaluated, the parser warns and keeps every branch.
- **A second `.PARAM` for the same name is ignored** with the warning
  `Failed to add ... to symbol table`.
- **`.IC` and `.NODESET` need an upper-case `V(`;** `v(out)=1` is skipped
  with a warning.
- **`PULSE` is the only waveform, and only after a DC value.**
  `V1 in 0 PULSE(...)` stops with `Invalid number format: missing digits`.
- **The diode reads only `IS` and `N`; the bipolar transistor reads `IS`,
  `BF`, `NF`, `ISE`, `NE`, `ISC`, `NC` and noise parameters; the level-1
  MOSFET has no bulk terminal in its equations and no `LAMBDA`, and its
  `GAMMA` is a noise coefficient.** Other parameters on a `.MODEL` card are
  ignored without a warning, as are unknown BSIM4 parameter names.
- **`.PRINT`, `.SAVE`, `.STEP`, `.SENS`, `.MC`, `.TF`, `.PZ`, `.DISTO`,
  `.PORT`, `.WCASE` and `.OPTIMIZE` are read, but the program does not use
  them.** `.MEASURE` and `.TITLE` are ignored.
- **Lines after `.END` are read.**
- **Analyses in a `.control` block are not run as in ngspice.** In runs at
  commit `7434904`, `op` inside `.control` did not select the operating
  point (the program fell back to its default, which prints no currents),
  and `dc V1 0 1 0.5 V2 0 1 1` swept only `V1`. Write analyses as dot
  directives (`.op`, `.dc`, `.tran`, `.ac`).

## Analyses

Measured with the program built from commit `91af284`, unless an entry
says otherwise.

- **A lossless line (`T`) is an open circuit in the operating point.** In
  DC a lossless line connects its ports (equal voltages, equal and opposite
  currents), but its DC stamp is empty: 1 V through 50 Ω into
  `T1 a 0 b 0 Z0=50 TD=10n` loaded with 50 Ω gives `V(a)` = 1 V and `V(b)` =
  0 V, where both are 0.5 V. A transient that starts from such an operating
  point starts from that state (measured at commit `5e3008d`).
- In the library's adaptive time-step path, which the program does not
  use, a fixed `TRAPEZOIDAL` method starts without the backward-Euler step
  that the fixed-step path takes (read in the code, not run).
- **`tmax` of `.TRAN` has no effect.** The parser reads it, but the
  program steps by `tstep`: `.TRAN 10u 1m 0 2u` gives 101 points, as
  `.TRAN 10u 1m` does. ngspice-47 limits the step to `tmax` and gives 508
  points with a largest step of 2 µs (measured at commit `d858190`).

## Device models

- **BSIM4 noise uses a separate, simplified current model.** The operating
  point, DC, transient and AC analyses evaluate BSIM4 through `BSIM4Core`
  (`src/devices/bsim4_core.cpp`). The noise sources
  (`MOSFET_BSIM4::get_noise_sources`) instead use `compute_drain_current`
  and `compute_threshold_voltage` in `src/devices/mosfet_bsim4.cpp`, with a
  numerical derivative for gm.
- **The default of BSIM4's UB is under review.** It is 2.5e-21 (m/V)², and
  the comment beside it says it was changed from 1.0e-19 to follow a
  0.18 µm process model card. Whether this matches the default in the BSIM4
  manual is not confirmed.
- **JFET cannot be used from a netlist.** The library has a JFET model
  (`include/kirimaspice/devices/jfet.hpp`), but the parser has no `J`
  element.
- **The FinFET's current has no channel length.** Its gain factor is
  β = C<sub>ox</sub>·W<sub>eff</sub>·µ (`beta_` in `src/devices/finfet.cpp`),
  where a MOSFET's is µ·C<sub>ox</sub>·W/L, so a FinFET with the defaults
  of `CircuitGraph::add_finfet_n` carries about 3 pA at
  V<sub>GS</sub> = V<sub>DS</sub> = 0.8 V (read in the code; the current
  measured at the commit that gave the FinFET its temperature).
- **BSIM3 and BSIM4 do not keep a DC operating point in a transient.** A
  circuit with only DC sources must stay at its operating point. An NMOS
  with 10 kΩ from 1.8 V to its drain and 10 kΩ from 1 V to its gate
  (`L=0.18u W=1u`, `.TRAN 1n 200n`) moves from `V(d)` = 1.7837 V to
  1.7996 V with BSIM3 (`level=8`) and from 0.3260 V to 0.2987 V with BSIM4
  (`level=14`), so the transient models of these MOSFETs differ from their
  DC models (measured at the commit that fixed the history of accepted
  time points; before it, BSIM3 fell to 0 V).
- **BSIM4's operating point differs from ngspice-47 for a small model
  card.** For the BSIM4 circuit above (`toxe=3e-9 vth0=0.5 u0=0.067
  vsat=8e4` and overlap capacitances), the program gives `V(d)` = 0.3260 V
  and a DC gate current of 1.7 µA through the 10 kΩ, where ngspice-47
  gives `V(d)` = 0.6785 V and no gate current. Which default parameters
  cause it is not confirmed.

## Solvers

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
- No file is written for an operating point or transient under `.TEMP`,
  and no SSF for a DC sweep.
- In some runs the transient table on standard output lists the last time
  point twice (seen with `.TRAN 10u 5m` and `.TRAN 1n 30n`, not with
  `.TRAN 10u 1m`).
- With `-o <path>`, the CSV of an AC analysis is written to a name made by
  removing everything after the last dot of the whole path, so a dot in a
  directory name moves the file to the parent directory.
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
