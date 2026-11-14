# kirimaspice

[日本語](README.ja.md)

[User manual](docs/USER_MANUAL.md) · [Reference manual](docs/REFERENCE_MANUAL.md) · [Glossary](docs/GLOSSARY.md) · [FAQ](docs/FAQ.md) · [Known issues](KNOWN_ISSUES.md) · [Contributing](CONTRIBUTING.md) · [Third-party notices](THIRD_PARTY_NOTICES.md)

[![CI](https://github.com/kurochan001/kirimaspice/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/kurochan001/kirimaspice/actions/workflows/ci.yml?query=branch%3Amain)
[![Nightly](https://github.com/kurochan001/kirimaspice/actions/workflows/nightly.yml/badge.svg?branch=main)](https://github.com/kurochan001/kirimaspice/actions/workflows/nightly.yml?query=branch%3Amain)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](#building)
[![Linux](https://img.shields.io/badge/OS-Linux-FCC624?logo=linux&logoColor=black)](#building)
[![macOS](https://img.shields.io/badge/OS-macOS-000000?logo=apple&logoColor=white)](#building)
[![License: Apache-2.0 WITH LLVM-exception](https://img.shields.io/badge/license-Apache--2.0%20WITH%20LLVM--exception-blue)](LICENSE)

kirimaspice is an open-source SPICE circuit simulator in early development,
and a sister project of [kirimasim](https://github.com/kurochan001/kirimasim),
the SystemVerilog simulator. It reads a SPICE netlist, runs operating-point,
DC-sweep, transient and AC analyses, and writes the results as SPICE3 raw
files, CSV or SSF. It has made no release yet. AI assists implementation;
people approve specifications, test expectations and reviews under the
[AI policy](docs/spec/AI_POLICY.md).

## Status

Measured on 2026-11-14 on macOS 27.2 (arm64) with GCC 16.2.0, CMake 4.4.4
and SuiteSparse 7.6.1; Homebrew's clang 23.1.4 and Apple clang 21.0.0 give
the same results:

| CTest tests | Count |
| --- | --- |
| Registered | 1073 |
| Passed | 1042 |
| Skipped | 23 |
| Failed (listed in `tests/known_failures.txt`) | 7 |
| Disabled | 1 |

In CI on Ubuntu 24.04 (x86-64), GCC 13.3.0 and clang 18.1.3 build
kirimaspice, and only six of the listed tests fail:
`CMOS_Inverter.DCTransferCharacteristic_HighInput` passes there.
[KNOWN_ISSUES.md](KNOWN_ISSUES.md) lists the failing and
skipped tests and the other known problems.

## What it handles

The parser accepts these elements:

| Letter | Element |
| --- | --- |
| `R`, `C`, `L` | Resistor, capacitor, inductor |
| `K` | Mutual inductance |
| `V`, `I` | Independent voltage and current sources |
| `E`, `F`, `G`, `H` | Controlled sources (VCVS, CCCS, VCCS, CCVS) |
| `B` | Behavioral source |
| `D` | Diode |
| `Q` | Bipolar transistor (NPN, PNP) |
| `M` | MOSFET and related devices, chosen by the model's `LEVEL` (below) |
| `Z` | MESFET (`NMF`, `PMF`) |
| `S`, `Y` | SCR, TRIAC |
| `T`, `O` | Lossless and lossy transmission lines |
| `X` | Subcircuit instance |

For `M`, the `LEVEL` of the `.model` selects the model: 1 (default), 3,
9 (BSIM1), 8 or 49 (BSIM3), 14 or 54 (BSIM4), 10 (SiC MOSFET), 11 (GaN
HEMT), 12 (FinFET), 20 (VDMOS) and 21 (IGBT). The library also has a JFET
model, but the parser has no `J` element yet. How complete each model is
varies; each has its own tests.

The parser accepts these directives: `.AC`, `.DC`, `.TRAN`, `.OPTIONS`,
`.IC`, `.NODESET`, `.PRINT`, `.SAVE`, `.GLOBAL`, `.PARAM`, `.FUNC`,
`.MODEL`, `.SUBCKT`/`.ENDS`, `.INCLUDE`, `.LIB`/`.ENDL`, `.IF`/`.ELSE`/`.ENDIF`,
`.STEP`, `.TEMP`, `.SENS`, `.MC`, `.TF`, `.FOUR`, `.PZ`, `.DISTO`, `.PORT`,
`.WCASE` and `.OPTIMIZE`.

The program `kirimaspice` runs the operating point and the transient
analysis (each also over the temperatures of `.TEMP`), DC sweeps (also
nested), the Fourier analysis of `.FOUR` after a transient, and AC
analysis. The library also implements noise, transfer function,
pole-zero, distortion, sensitivity, S-parameter, Monte Carlo, worst-case
and optimization analyses, which the tests exercise; the program does not
run them yet.

The linear solver is KLU from SuiteSparse.

## Building

You need CMake 3.25 or newer, a C++23 compiler, Ninja, Python 3, and the
SuiteSparse development files (KLU 2.3.2 or newer, as in SuiteSparse
7.6.1), and Git. The other dependencies are downloaded by CMake with Git at
configure time, pinned to fixed versions, so the first configure needs
network access.

On Ubuntu 24.04 (the packages CI installs, and Git, which its runners
already have):

```sh
sudo apt install build-essential cmake ninja-build python3 libsuitesparse-dev git
```

Then, from the source tree:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Use `build/` as the build directory: some tests find their data relative to
it. If SuiteSparse is installed elsewhere, add
`-DCMAKE_PREFIX_PATH=<its prefix>` to the first command.

## Running

```sh
build/kirimaspice circuit.cir --raw-out circuit.raw
```

| Option | Output |
| --- | --- |
| `--raw-out <file>` | SPICE3 raw file in ASCII (DC and transient) |
| `--raw-binary-out <file>` | SPICE3 raw file in binary (DC and transient) |
| `-o <file>` | SSF (operating point and transient); also names the CSV of an AC analysis |
| `--wav-out <file>` | WAV audio of a transient result |

`build/kirimaspice --help` lists all options. The AC result is always written
to a CSV file; the [User Manual](docs/USER_MANUAL.md#save-the-results) says
where.

## Documents

- [User Manual](docs/USER_MANUAL.md): building, writing a netlist, running and reading the results
- [Reference Manual](docs/REFERENCE_MANUAL.md): options, elements, directives and output formats
- [Glossary](docs/GLOSSARY.md): the terms the documents use
- [FAQ](docs/FAQ.md): common questions, from running a netlist to contributing
- [KNOWN_ISSUES.md](KNOWN_ISSUES.md): known problems
- [CONTRIBUTING.md](CONTRIBUTING.md): what is accepted, and how to change the code
- [CLAUDE.md](CLAUDE.md) and [AGENTS.md](AGENTS.md): rules for coding agents (and people)
- [SECURITY.md](SECURITY.md): reporting a vulnerability
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md): Contributor Covenant 2.1
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md): dependencies and their licenses

## License

kirimaspice is licensed under the Apache License 2.0 with LLVM Exceptions
(`Apache-2.0 WITH LLVM-exception`; see [LICENSE](LICENSE)). Six files that
are based on the BSIM4 4.8.3 model code are also under ECL-2.0, as their
headers say. KLU and BTF, which are linked dynamically from the system, are
under LGPL-2.1-or-later. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
lists every dependency.
