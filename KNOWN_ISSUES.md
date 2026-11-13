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
    voltage is at least 0 V and gets -4.1e-25 V.
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

## Solvers

- KLU is the only working linear solver. The SuperLU_DIST backend is a
  placeholder: asking for it falls back to KLU.

## Command line and output

- There is no `--version` option.
- With `-o <path>`, the CSV of an AC analysis is written to a name made by
  removing everything after the last dot of the whole path, so a dot in a
  directory name moves the file to the parent directory.
- The SSF output format has no published specification in this repository.

## Platforms

- Builds and tests have been run only on macOS (arm64) with GCC 16.2.0.
  Ubuntu 24.04 with its own compilers is the target and is not confirmed
  yet.
- With Apple clang 21, the pinned {fmt} 9.1.0 and spdlog 1.13.0 do not
  compile (a `consteval` error in spdlog).
- Windows is not tested.
