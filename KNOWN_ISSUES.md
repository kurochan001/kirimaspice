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
    voltage is at least 0 V and gets -4.1e-25 V on macOS (arm64). It passes
    on Ubuntu 24.04 (x86-64) with GCC 13.3.0 and clang 18.1.3.
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
  reads `/tmp/phase_b7_temperature_refs.json`, which a script in `scripts/`
  writes.
- `BSIM4ComprehensiveTest.NgspiceComparison` runs `ngspice` and is skipped
  when ngspice is not installed.
- Some tests find their data relative to the build directory and pass only
  when it is `build/` directly under the source tree.

## Netlist input

These were found while writing the [User Manual](docs/USER_MANUAL.md) and
the [Reference Manual](docs/REFERENCE_MANUAL.md), by running the program
built from commit `91af284`. What this section says about SPICE and ngspice
comes from their manuals; none of it was compared by running ngspice.

- **`B` sources have no AC model and no `I(<source>)`.** In AC analysis a
  `B` source contributes nothing and logs `AC analysis not yet supported`;
  an expression with `I(...)` is an error.
- **Upper-case `M` is mega.** `1M` is 10⁶ and `1m` is 10⁻³; in SPICE both
  are milli. Scale suffixes are not accepted inside braces or in
  `.OPTIONS` values (there the option is ignored with a warning).
- **A voltage source without `AC` takes part in AC analysis with magnitude
  1.** In SPICE its AC magnitude is 0.
- **Source currents have the opposite sign to SPICE.** kirimaspice reports
  the current that leaves the `+` terminal; the SPICE3 and ngspice manuals
  define it as the current that enters the `+` terminal. `F` and `H` follow
  the kirimaspice sign, so they give the opposite result to SPICE for the
  same netlist.
- **A nested `.DC` sweep loops in the opposite order to SPICE.** The second
  source is swept for each value of the first; in SPICE the first source is
  swept for each value of the second.
- **`GND` is ground inside subcircuits but not at the top level.** Inside a
  subcircuit, `0` and `GND` in any case are ground; at the top level only
  `0` is, and `GND` is an ordinary node.
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
- **`.PRINT`, `.SAVE`, `.STEP`, `.SENS`, `.MC`, `.TF`, `.FOUR`, `.PZ`,
  `.DISTO`, `.PORT`, `.WCASE` and `.OPTIMIZE` are read, but the program
  does not use them.** `.MEASURE` and `.TITLE` are ignored.
- **Lines after `.END` are read.**

## Analyses

Measured with the program built from commit `91af284`.

- **The default integration method, `trap`, gives backward-Euler accuracy
  on an RC discharge.** For 1 kΩ and 1 µF starting at 1 V (`.IC`, `UIC`),
  with `.TRAN 10u 1m`, `V(out)` at 1 ms is:

  | `METHOD` | `V(out)` |
  | --- | --- |
  | `trap` (default) | 0.369725 |
  | `euler` | 0.369711 |
  | `tr_bdf2` | 0.369184 |
  | `gear` | 0.367913 |
  | `gear3` | 0.367905 |

  The exact value is e⁻¹ = 0.367879. Backward Euler with a fixed 10 µs
  step gives 0.369711, and the trapezoidal rule gives 0.367876.
- **A matched lossless line (`T`) reflects.** A 1 V step through 50 Ω into
  `T1 a 0 b 0 Z0=50 TD=10n` loaded with 50 Ω gives 0.25 V at the far end
  after 10 ns, and the near end rises from 0.5 V to 0.625 V after 20 ns. A
  matched line gives 0.5 V at both ends.
- **Coupled inductors (`K`) grow without bound.** `L1` and `L2` of 1 mH with
  `K1 L1 L2 0.9`, `L1` driven by a 1 V step through 10 Ω and `L2` loaded
  with 1 kΩ: the voltage across the load reaches 84 V after 20 µs and is
  still growing. The circuit is passive.

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
- Windows is not tested.
