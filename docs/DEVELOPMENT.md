# Developing kirimaspice

[日本語](DEVELOPMENT.ja.md) · [Project README](../README.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

**Updated 2026-11-15** for commit `02261a2`. This guide is for people who
change kirimaspice: how to build it for development, how the tests are laid
out and run, what to check before a push, and how to add a device model or
fix a bug within the project's rules. [CONTRIBUTING](../CONTRIBUTING.md)
says what contributions are accepted, [CLAUDE.md](../CLAUDE.md) holds the
rules that bind people and agents alike, and
[ARCHITECTURE](ARCHITECTURE.md) describes the code.

## Contents

- [Build for development](#build-for-development)
- [Tests](#tests)
- [Before a push](#before-a-push)
- [Continuous integration](#continuous-integration)
- [Add or change a device model](#add-or-change-a-device-model)
- [Fix a bug](#fix-a-bug)
- [Expected values and reference data](#expected-values-and-reference-data)
- [Benchmarks](#benchmarks)
- [Debugging aids](#debugging-aids)

## Build for development

The commands are those of the [README](../README.md#building):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- The build directory must be `build/` directly under the source tree:
  some tests find their data relative to it.
- SuiteSparse (KLU) comes from the system. For another installation, pass
  `-DCMAKE_PREFIX_PATH=<prefix>`; `tools/local_ci.py` takes it as
  `--cmake-arg=-DCMAKE_PREFIX_PATH=<prefix>`.
- The CMake options (`BUILD_TESTS`, `BUILD_BENCHMARKS`, `SANITIZE_ADDRESS`,
  `ENABLE_COVERAGE` and others) are listed in the
  [Reference Manual](REFERENCE_MANUAL.md#build-options).
  `-DSANITIZE_ADDRESS=ON` is what the nightly AddressSanitizer job uses.
- On macOS the project is built with Homebrew's GCC
  (`-DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16`), Homebrew's
  clang or Apple clang. Apple clang builds without OpenMP.
- Format code with the repository's `.clang-format`. Each file of the
  project's own code starts with the copyright line and the SPDX line;
  `python3 tools/check_spdx.py --add` writes them.

## Tests

### Layout

| Directory | What is there | Run by CTest |
| --- | --- | --- |
| `tests/unit/` | GoogleTest programs, one per source file, for one class or one function each | yes, every case |
| `tests/integration/` | GoogleTest programs that parse a netlist and run an analysis | yes |
| `tests/validation/` | comparisons with ngspice reference data in `validation/`, and standalone investigation programs | 5 of the 47 programs (GoogleTest); the other 42 are built but not run |
| `tests/scripts/` | Python tests: the Python tools, the [examples](../examples/README.md), a BSIM4 DC smoke test | yes |
| `tests/netlists/` | netlists that the command-line and Python tests use | — |
| `tests/benchmarks/` | Google Benchmark programs, built only with `BUILD_BENCHMARKS=ON` | no |
| `tests/debug/`, `tests/debug_*.cpp` | debugging programs | no |

GoogleTest programs are registered with `gtest_discover_tests`, so CTest
lists each test case as `Suite.Name`. The command-line tests (`cli_*`,
among them the `.TEMP` runs `cli_temperature_*`), the examples
(`example_*`), the Python tools (`python_tools_*`), `bsim4_dc_smoke` and `MutualInductanceFixTest` are registered with
`add_test`, one CTest test each. At commit `02261a2` CTest has 1188 tests
on macOS.

### Run a part of the tests

```sh
ctest --test-dir build -R '^example_'          # by regular expression
ctest --test-dir build -R 'BSIM4' -j 8          # in parallel
build/tests/test_parser --gtest_filter='*Ground*' # one GoogleTest program directly
```

A test whose expected data is missing skips instead of failing; the skips
and their reasons are in [KNOWN_ISSUES](../KNOWN_ISSUES.md#tests).

### Known failures

`tests/known_failures.txt` lists the tests that fail today, one name per
line, with the reason as a comment. `tools/local_ci.py` and CI fail on any
failure that is not listed, and report a listed test that passes so that
it can be removed. Adding a name to the list needs the same approval as
changing an expected value (see [below](#expected-values-and-reference-data)).

## Before a push

```sh
python3 tools/local_ci.py
python3 tools/local_ci.py --history   # also scan every commit
```

`tools/local_ci.py` configures, builds and tests in `build/` (Release,
Ninja), compares the failures with `tests/known_failures.txt`, and runs
`tools/check_banned.py` and `tools/check_spdx.py`. It prints a summary and
writes the logs to `build/local-ci/`; attach the summary when you report a
change.

`tools/check_banned.py` looks for what must not be published: paths into a
home directory, e-mail addresses other than GitHub noreply addresses,
names of semiconductor foundries, binary and large files, and, with
`--history`, commit authors without a noreply address. Names that must not
appear cannot be written in the repository; keep them in an untracked
file, one per line, and pass it with `--private <file>` or the
`KIRIMASPICE_PRIVATE_PATTERNS` environment variable.

## Continuous integration

| Workflow | When | Jobs |
| --- | --- | --- |
| `.github/workflows/ci.yml` | a push to `main`, or by hand | `tools/local_ci.py` on Ubuntu 24.04 with GCC and with clang; a history scan with `tools/check_banned.py --history` |
| `.github/workflows/nightly.yml` | 18:17 UTC every day, or by hand | `tools/local_ci.py` with AddressSanitizer; `tools/local_ci.py` with ngspice installed, so that the ngspice comparisons run |

The workflows run on GitHub's standard runners; they do not use
self-hosted runners, as the public logs of a job name the machine that ran
it. The private patterns of the history scan come from the
`PRIVATE_PATTERNS` repository secret.

## Add or change a device model

A device's equations are written once. Every analysis (operating point, DC
sweep, AC, transient, noise) evaluates a device through the same model
code, so do not add a second copy of a formula for one analysis. Where
this is not yet so (BSIM4's noise sources),
[KNOWN_ISSUES](../KNOWN_ISSUES.md#device-models) says so; new code must not
add more such cases. `tests/unit/test_small_signal_consistency.cpp` checks
that a device's AC gain equals the slope of its DC transfer curve; add a
case there for a new device.

The steps, with the classes and files described in
[ARCHITECTURE](ARCHITECTURE.md#devices):

1. Implement the device in `include/kirimaspice/devices/` and
   `src/devices/`, deriving from the device base class and implementing
   the stamps of the analyses it takes part in.
2. Add it to the circuit (`CircuitGraph` in `src/circuit/`) and, if a
   netlist should reach it, to the parser (`src/parser/netlist_parser.cpp`)
   and to subcircuit expansion.
3. Write tests whose expected values come from the model's specification
   or an analytic solution, or from an ngspice run with its version
   recorded.
4. Document the element in the [Reference Manual](REFERENCE_MANUAL.md#elements).

The places a new device touches:

| Place | What to add |
| --- | --- |
| `include/kirimaspice/core/types.hpp` | a value of `DeviceType` for each polarity |
| `include/kirimaspice/devices/`, `src/devices/` | the class; an explicit instantiation in the `.cpp` file for a template that is not header-only |
| `CMakeLists.txt` | the `.cpp` file in the sources of `kirimaspice_lib` (they are listed, not globbed) |
| `src/parser/netlist_parser.cpp` | the element or the `LEVEL` of a `.MODEL` card, and its parameters |
| `CircuitGraph::expand_subcircuit_instance` (`src/circuit/circuit_graph.cpp`) | the element inside a `.SUBCKT`, if it should work there |
| `apply_temperature_to_devices` (`src/solvers/temperature_utils.cpp`) | a call of the device's `set_temperature`, if it depends on temperature |
| `tests/unit/`, `tests/CMakeLists.txt` | the tests and their registration |
| `ECL_FILES` in `tools/check_spdx.py`, [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md) | only for code based on BSIM model code |
| [Reference Manual](REFERENCE_MANUAL.md#elements) and its Japanese version | the element, its parameters and their defaults |

Code based on BSIM model code keeps its notice (ECL-2.0), carries the
SPDX expression of `ECL_FILES` in `tools/check_spdx.py`, and is listed in
[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md). Do not read or port
source code under the GPL or LGPL; check the license of each ngspice file
before reading it ([CLAUDE.md](../CLAUDE.md#code-under-the-gpl-or-lgpl)).

## Fix a bug

Do not make the shape of a symptom a condition: no test circuit, node
name, file name or issue number in an `if`. A commit that fixes a bug
states

1. the rule it broke: the section of the model's specification, or the
   analytic solution;
2. the one place that implements that rule, by function name;
3. the other paths that go through the same place.

The simulator does not change behaviour by file names or paths, and
holds no knowledge of the tests.

## Expected values and reference data

- Expected values come from the model's specification, from analytic
  solutions, or from reference runs of ngspice whose version is recorded.
  They never come from the implementation itself.
- Do not rewrite expected values or tests to make tests pass. When the
  implementation and an expected value disagree, first show a person which
  of them follows the specification or the analytic solution.
- A change that alters, removes or loosens an existing expected value,
  deletes or renames a test, or adds a test to `tests/known_failures.txt`
  goes in its own commit, separate from changes to `src/`, states its
  reason, and needs a person's approval, recorded as an `Approved-by`
  trailer. New tests with their expected values may go with the
  implementation.
- The reference data in `validation/` came from ngspice runs whose version
  is not recorded. New reference data records the ngspice version, the
  command and the netlist. The scripts in `scripts/` run ngspice to make
  BSIM4 reference data:

  | Script | Writes |
  | --- | --- |
  | `scripts/bench/generate_bsim4_bias_refs.py` | `/tmp/bsim4_bias_refs.json`, 13 bias points |
  | `scripts/bench/generate_bsim4_temperature_refs.py` | `/tmp/bsim4_temperature_refs.json`, read by `BSIM4TemperatureValidationTest` |
  | `scripts/generate_bsim4_tran_ref.py` | `validation/bsim4_inv_chain_tran_ref.txt`, read by `test_bsim4_transient.cpp` |

- Output, logs and waveforms of commercial simulators are never used
  ([FAQ](FAQ.md#do-you-check-against-commercial-simulators)).

## Benchmarks

```sh
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON
cmake --build build-bench --target bench_device_stamping
```

`BUILD_BENCHMARKS=ON` fetches Google Benchmark and builds the programs of
`tests/benchmarks/`: `bench_large_circuit`, `bench_solver_backends_ac`,
`bench_sparse_refactorize`, `bench_sparse_matrix_zero`,
`bench_device_stamping` and `bench_dc_thread_profile`. CTest does not run
them, CI does not build them, and no results are published.

## Debugging aids

- The program logs at the `info` level (`src/main.cpp`); there is no
  option to change the level. The `debug` and `trace` messages in the code
  need a change to `main.cpp` to appear.
- `KIRIMASPICE_DEBUG_DUMP`, set to any value, writes the MNA system of
  the second Newton iteration of each DC solve to `debug_coo.txt`,
  `debug_csc.txt`, `debug_b.txt` and `debug_x.txt` in the current
  directory. `KIRIMASPICE_BSIM1_DEBUG` turns on BSIM1 debug logs, for the
  device of that name only when its value is not empty
  ([Reference Manual](REFERENCE_MANUAL.md#environment-variables)).
- `--raw-out` writes every node voltage and branch current of the
  operating point, a DC sweep or a transient;
  [Python tools](USER_MANUAL.md#use-the-python-tools) read it.
