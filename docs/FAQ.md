# FAQ

[日本語](FAQ.ja.md) · [Project README](../README.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

**Updated 2026-11-14** for commit `35eb58a`. These answers describe what
kirimaspice does today; the [User Manual](USER_MANUAL.md) and the
[Reference Manual](REFERENCE_MANUAL.md) give the details,
[KNOWN_ISSUES](../KNOWN_ISSUES.md) the known problems, and the
[Glossary](GLOSSARY.md) the terms.

- [Start and inspect a run](#start-and-inspect-a-run)
- [Netlists and compatibility](#netlists-and-compatibility)
- [Results and accuracy](#results-and-accuracy)
- [Platforms and performance](#platforms-and-performance)
- [Development and contributions](#development-and-contributions)

## Start and inspect a run

### Can I use kirimaspice on my circuits now?

It is in early development and has no release yet (version 0.0.0). The
program runs the [operating point](GLOSSARY.md#operating-point),
[DC sweep](GLOSSARY.md#dc-sweep), [transient](GLOSSARY.md#transient) and
[AC](GLOSSARY.md#ac-analysis) analyses of a netlist. Several elements and
directives have limits that matter in practice, for example which elements
a subcircuit can hold and which device parameters the parser reads; read
[KNOWN_ISSUES](../KNOWN_ISSUES.md) before relying on a result.

### What should I record when a result needs reproducing?

The version line the program logs first (`kirimaspice v0.0.0 - SPICE
circuit simulator`), the commit you built, the netlist, the command line,
your operating system, compiler and SuiteSparse version. There is no
`--version` option yet.

### Why are log lines mixed with the results?

Both go to standard output. Log lines start with `[` (a time stamp and
`[info]`, `[warning]` or `[error]`), so filter them out to keep the results.
Read the warnings: some mistakes, such as an unknown `.OPTIONS` name, only
give a warning and the run goes on without that setting.

### Where is the result of an AC analysis?

It is not printed. It is always written to `<base>_ac.csv`, where `<base>`
is the `-o` path, or the netlist path, without its extension
([User Manual](USER_MANUAL.md#save-the-results)).

### Why is my DC sweep not printed?

The program prints only a summary of a sweep. Add `--raw-out <file>` to get
the values.

## Netlists and compatibility

### Does kirimaspice read netlists written for ngspice?

It reads SPICE netlists, and where SPICE dialects differ it follows
[ngspice](GLOSSARY.md#ngspice) for scale factors, AC inputs, the sign of
source currents, the order of a nested `.DC` and `gnd`
([NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.md)); each of these was
checked by running ngspice-47. Other differences remain: node names are
case-sensitive, a source needs a DC value before `PULSE`, `PULSE` is the
only waveform, analyses inside `.control` are not run as ngspice runs them,
and the parser reads only part of the device parameters. The document and
[KNOWN_ISSUES](../KNOWN_ISSUES.md) list them.

### Does it read netlists written for HSPICE or Spectre?

Not confirmed. Where SPICE dialects differ, kirimaspice follows ngspice
([NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.md)). No commercial
simulator has been used to check compatibility, and none will be (see
[the answer on commercial simulators](#do-you-check-against-commercial-simulators)).

### Why is `1M` one milliohm?

As in SPICE, `M` is milli whatever its case; mega is `MEG`. In the same
way, `10MHz` is 10 mHz, and `1A` is 10⁻¹⁸ because `A` is atto
([scale factor](GLOSSARY.md#scale-factor)).

### Why is the current of my supply negative?

The current of a voltage source is positive when it flows into its `+`
terminal through the source, as in SPICE. A supply delivering current
therefore shows a negative current ([branch current](GLOSSARY.md#branch-current)).

### Why does my supply not appear in the AC result?

Only sources written with `AC` are inputs of an AC analysis. Write
`AC 1` (or `AC` alone) on the source that drives the circuit.

### Why does an element inside my subcircuit disappear?

Inside a subcircuit only `R`, `C`, `L`, `V` and `I` (with a DC value), `M`
(with four terminals, level 1 or BSIM4) and `X` are expanded; other
elements are skipped with a warning. Place diodes, bipolar transistors and
controlled sources at the top level for now.

### Why does `{1m}` give an error?

Scale factors are not accepted inside braces. Write `{0.001}` or a plain
number.

### Why does `Undefined BJT model` (or MOSFET, diode, MESFET) appear?

No `.MODEL` in the netlist has that name. Model names are not
case-sensitive and a `.MODEL` may stand anywhere; check the spelling and
that the file with the model is read with `.INCLUDE` or `.LIB`.

## Results and accuracy

### How do I know a result is right?

Compare it with an analytic solution where one exists, or with ngspice,
recording its version. The project's own [expected values](GLOSSARY.md#expected-value)
come from model specifications, analytic solutions and recorded reference
runs, never from the implementation itself. The User Manual gives two
checks: an RC low-pass filter at 1 kHz (−16.072 dB, −80.96°) and an RC
charge at 1 ms (0.632105 for the exact 0.632121).

### Which integration method should I use?

The default, `trap`, is second order. On an RC discharge from 1 V with
`.TRAN 10u 1m` (RC = 1 ms) the value at 1 ms was 0.367895 with `trap`,
0.367913 with `gear` (BDF2) and 0.369711 with `euler` (backward Euler,
first order), for the exact 0.367879. Set the method with
`.OPTIONS METHOD=<name>`.

### Is the time step adaptive?

Not in the program: it steps by `tstep`, at most `tmax`. The library has an
adaptive path that chooses each step from the [LTE](GLOSSARY.md#lte); the
program does not use it yet.

### Do you check against commercial simulators?

No. The project does not hold licenses for commercial SPICE simulators
such as HSPICE (Synopsys) or Spectre (Cadence). kirimaspice has not been
verified with them, and they will not be run or compared for this project.
Their output, logs, waveforms and diagnostics are also kept out of test
expectations and of decisions on the implementation; keeping such results
private does not by itself make their use permitted. Expected values come
from model specifications, analytic solutions and open-source simulators
run with a recorded version, such as ngspice.

Two public examples of the terms that govern such tools, read on
2026-11-14 without logging in:

- The [Synopsys Cloud Service Agreement 2024](https://www.synopsys.com/cloud/service-agreement.html):
  sections 9.3.2, 9.3.5 and 9.3.7 prohibit reverse engineering a product
  or deriving the algorithms or ideas embodied in it, using a product or
  its output to develop or enhance a product that competes with Synopsys,
  and benchmarking a product or disclosing benchmark results.
- Cadence's [click-through terms for its Design Materials & Cloud Platform](https://www5.cadence.com/rs/070-BII-206/images/SOFTWARE%20LICENSE%20AGREEMENT_FNL.pdf)
  (dated 2020-07-29 in the document): use is for evaluation and
  demonstration only, and not for benchmarking design tools; reverse
  engineering the encrypted portions is prohibited; and benchmarking or
  competitive analysis derived from the materials is Cadence's
  confidential information.

Reading public contract text is separate from holding a product license.
These examples do not mean that the project has entered into these
agreements or that they apply to it, and the license terms of HSPICE and
Spectre themselves were not found in public (not confirmed). Terms vary by
product, contract and jurisdiction, and we do not claim that every ordinary
use is prohibited. Not using commercial simulators is this project's
operating policy. Compatibility with commercial simulators and matching
their results are not confirmed; see the
[posting policy](#may-i-include-commercial-simulator-output-in-an-issue).

### Why are some comparison tests skipped?

Twenty comparison tests look for reference data in a directory where it is
not, and one runs ngspice and is skipped when ngspice is not installed
([KNOWN_ISSUES](../KNOWN_ISSUES.md#tests)).

## Platforms and performance

### Which platforms does it run on?

It is built and tested on macOS 27.2 (arm64) with GCC 16.2.0, Homebrew's
clang 23.1.4 and Apple clang 21.0.0, and in CI on Ubuntu 24.04 (x86-64)
with GCC 13.3.0 and clang 18.1.3. Windows is not tested.

### Does it use more than one thread?

When OpenMP is found at configure time, the AC analysis solves its
frequency points in parallel and some vector operations of large systems
run in parallel (read in the code). Apple clang builds without OpenMP. The
speed-up has not been measured.

### How fast is it?

Not measured. No benchmark results are published yet.

## Development and contributions

### Is the code written by AI?

Most of it is generated by AI tools. People decide the specifications, the
expected values and what is published, and approve them; the rules are in
[AI_POLICY](spec/AI_POLICY.md). Commits made with AI carry an
`Assisted-by` trailer.

### Can I send a pull request?

Not yet. For now the project accepts issues and specification proposals
only ([CONTRIBUTING](../CONTRIBUTING.md)).

### May I include commercial simulator output in an issue?

No. Keep it out of issue bodies, comments and attachments: output, logs,
screenshots, waveforms and diagnostics, including anonymized excerpts,
summaries and results run by someone else. The same holds for netlists,
models and documents distributed with a commercial tool or under a
non-disclosure agreement, such as foundry model parameters. Do not run a
commercial simulator for a report, and do not ask another contributor to.

Describe the expected behaviour in your own words, with the section of the
model's specification or an analytic solution, a minimal netlist you wrote
yourself and, where it helps, the result of an open-source simulator with
its version. If such material is posted, do not quote, summarize or use
it; ask its author or the maintainers to remove it.

### How can I help?

Report a bug with a netlist as small as you can make it, the command, what
you expected and why (a model's specification, an analytic solution, or an
open-source simulator with its version), and what you got. Reports of wrong
expected values and proposals for the documents under `docs/spec/` are
welcome too. Do not paste text from standards, papers or manuals, or
confidential information such as foundry model parameters.

### How do I report a vulnerability?

Privately, through GitHub's private vulnerability reporting, as
[SECURITY](../SECURITY.md) says.

### How is kirimaspice related to kirimasim?

[kirimasim](https://github.com/kurochan001/kirimasim) is its sister project,
a SystemVerilog simulator. Sharing waveform formats and mixed-signal
co-simulation with it are goals for later.

### What is the license?

Apache License 2.0 with LLVM Exceptions. The files based on the BSIM4 model
code also carry ECL-2.0, and KLU and BTF, linked dynamically from the
system, are LGPL-2.1-or-later ([README](../README.md#license),
[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md)).
