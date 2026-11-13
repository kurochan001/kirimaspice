# CLAUDE.md: rules for Claude Code working on kirimaspice

[日本語](CLAUDE.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

## Purpose of the project

kirimaspice is an open-source SPICE circuit simulator written in C++20. The
static library `kirimaspice_lib` holds the netlist parser, the circuit, the
device models, the solvers, the analyses and the output writers; the program
`kirimaspice` is a thin command-line front end over it. It is a sister
project of kirimasim, a SystemVerilog simulator: sharing waveform formats and
mixed-signal co-simulation with it are goals for later. The README gives the
status.

## Layout

| Directory (`src/` and `include/kirimaspice/`) | Content |
| --- | --- |
| `parser` | Netlist parsing (PEGTL; expressions with ExprTk) |
| `circuit` | Circuit graph and subcircuit expansion (Boost.Graph) |
| `devices` | Device models |
| `solvers` | DC, AC, transient, noise and the other analyses; the sparse linear solver (KLU) |
| `analysis` | Monte Carlo, worst case, optimization (NLopt), BSIM4 parameter extraction |
| `measure` | `.measure` |
| `output` | SPICE3 raw (ASCII and binary), SSF, CSV, Touchstone, WAV |
| `core`, `util`, `utils` | Types, matrices, constants, helpers |

- Keep the library and the program apart: `src/main.cpp` reads the command
  line and calls the library, and holds no simulation logic.
- Namespaces are `kirimaspice::<directory>`. Names on a future C boundary
  start with `kirimaspice_` and `KIRIMASPICE_`.

## Build and test commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/local_ci.py        # build, tests, check_banned and check_spdx, with a summary
```

- SuiteSparse (KLU) comes from the system, for example `libsuitesparse-dev`
  on Ubuntu; pass `-DCMAKE_PREFIX_PATH=<prefix>` for another installation.
  Python 3 is needed for the tests.
- The build directory must be `build/` directly under the source tree: some
  tests find their data relative to it.
- Tests that fail today are listed in `tests/known_failures.txt`;
  `tools/local_ci.py` fails on any other failure.
- Before pushing, run `tools/local_ci.py` and attach its summary to the
  report. CI runs on GitHub's standard runners; workflows do not use
  self-hosted runners, as the public logs of a job name the machine that
  ran it.

## Decide meaning in one place, and do not fix by the shape of the symptom

- **A device's equations are written once.** Every analysis (operating
  point, DC sweep, AC, transient, noise) evaluates a device through the same
  model code. Do not add a second copy of a formula for one analysis. Where
  this is not yet so (BSIM4's noise sources), `KNOWN_ISSUES.md` says so; new code
  must not add more such cases.
- **Do not make the shape of a symptom a condition.** Do not put a test's
  circuit, node names, file names or issue numbers in `if` conditions. A
  commit that fixes a bug states:
  1. the rule it broke (the section of the model's specification, such as
     the BSIM4 manual, or the analytic solution);
  2. the one place that implements that rule (the function's name);
  3. the other paths that go through the same place.
- **Do not put knowledge of the tests in the simulator.** The program does
  not change behaviour by file names or paths.

## Expected values

- Expected values come from the model's specification, from analytic
  solutions, or from reference runs of ngspice whose version is recorded.
- Do not rewrite expected values or tests to make tests pass. When the
  implementation and an expected value disagree, first show a person which
  of them follows the model's specification or the analytic solution.
- A change that alters, removes or loosens an existing expected value,
  deletes or renames a test, or adds a test to `tests/known_failures.txt`
  goes in its own commit, separate from changes to `src/`, states its
  reason, and needs a person's approval. New tests with their expected
  values may go with the implementation.

## How to write commits

- One commit per purpose. Messages in English, the first line at most 72
  characters.
- When an AI helped, add the trailer `Assisted-by: Claude Code:<model name
  and version>`. Do not add `Co-Authored-By`.
- Do not add `Signed-off-by`: the project does not use the DCO.
- The author email of commits is the GitHub noreply address.

## What must not go in the public repository

- The name, code, documents or work records of private projects, including
  the private code base this project started from.
- Absolute paths of home directories, user names, machine names, and e-mail
  addresses (except GitHub noreply addresses, the contact in
  `CODE_OF_CONDUCT.md`, and those in third-party copyright notices, which
  `THIRD_PARTY_NOTICES.md` reproduces as they are).
- Information about workplaces or work, and foundry process data.
- Output, logs or waveforms of commercial simulators. Do not run commercial
  simulators.
- Copies of the text or figures of standards, papers or manuals (giving
  section numbers and summarizing in your own words is fine).
- Build outputs, profiles and debug executables.

`tools/check_banned.py` checks what it can. Names that must not appear are
kept in an untracked file and passed with `--private`.

## Code under the GPL or LGPL

- Do not read or port source code under the GPL or LGPL (any version):
  Xyce, Qucs, Gnucap and others. KLU and BTF (LGPL) are used only through
  their public API (`klu.h` and the KLU user guide); their implementation is
  not read.
- ngspice is mostly under a Modified BSD license, but some of its files are
  not (its `COPYING` lists them, for example `src/maths/KLU`,
  `src/frontend/numparam` and `src/xspice/icm/table`). Check the license of
  each file before reading it, and stop and report if it is GPL or LGPL.
- Learn the behaviour of other tools from their manuals and from running
  them. ngspice may be run for reference; record its version.
- Do not copy other projects' tests or the reproducing code of their issues.
  Write the test anew from what the bug is.
- Code based on BSIM model code keeps its notice (ECL-2.0) and is listed in
  `THIRD_PARTY_NOTICES.md`.

## Write "not confirmed" for what is not known

Do not fill in by guessing what has not been checked: version numbers,
licenses, the behaviour of other tools, performance figures. Write "not
confirmed", with a way to check it.

## What to ask a person first

Show the proposal and its diff, and wait, before: adding or removing a
dependency, installing a tool, deciding a specification or published text,
changing CI policy, changing an existing expected value, or pushing. Texts
with official wording (licenses, codes of conduct) are fetched from the
official source, not written from memory.
