# Moving a netlist from ngspice

[日本語](PORTING.ja.md) · [Project README](../README.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

**Updated 2026-11-14** for commit `fb50c51`. This guide is for running with
kirimaspice a netlist written for ngspice. It says what is the same, how
the commands map, and what to check in the netlist, in the order in which
it is usually met. Each difference was found by running kirimaspice and
ngspice-47; the details and measurements are in
[KNOWN_ISSUES](../KNOWN_ISSUES.md), and the syntax in the
[Reference Manual](REFERENCE_MANUAL.md).

## Contents

- [What is the same](#what-is-the-same)
- [Run it](#run-it)
- [Check the netlist](#check-the-netlist)
- [Compare the results](#compare-the-results)

## What is the same

Where SPICE dialects differ, kirimaspice follows ngspice in five rules,
each checked against ngspice-47
([NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.md)):

1. Scale factors are those of ngspice, in any case: `M` is milli, `MEG` is
   mega, `MIL` is 25.4×10⁻⁶ and `A` is atto.
2. Only sources written with `AC` are inputs of an AC analysis.
3. The current of `V`, `E`, `H` and voltage-form `B` sources is positive
   into the `+` terminal, so a supply that delivers current is negative.
4. In a nested `.DC`, the first source is the inner loop.
5. `gnd`, in any case, is ground.

Model names are not case-sensitive, and a `.MODEL` may stand after the
elements that use it. ngspice-47 loads the ASCII and binary raw files that
kirimaspice writes ([Reference Manual](REFERENCE_MANUAL.md#output-formats)).

## Run it

| With ngspice | With kirimaspice |
| --- | --- |
| `ngspice -b circuit.cir` | `build/kirimaspice circuit.cir` |
| `ngspice -b -r out.raw circuit.cir` | `build/kirimaspice circuit.cir --raw-out out.raw`, or `python3 tools/ngspice_shim.py -b -r out.raw circuit.cir` |
| analyses in a `.control` block (`op`, `dc`, `tran`, `ac`, `run`) | dot directives outside `.control`: `.op`, `.dc`, `.tran`, `.ac` |
| `print`, `plot`, `meas`, `wrdata` | the raw file, read with the [Python tools](USER_MANUAL.md#use-the-python-tools); the AC result is always in `<base>_ac.csv` |

The program runs in batch only. It prints the operating point and the
transient table, a summary of a DC sweep, and nothing for an AC analysis;
the values of a sweep are in the raw file.

## Check the netlist

### Analyses and directives

- [ ] **Analyses as dot directives.** The program runs `.OP`, `.DC`,
  `.TRAN` and `.AC`, and `.FOUR` after a transient. Analyses inside
  `.control` are not run as in ngspice.
- [ ] **No analysis words in comments.** The program finds `.op`, `.dc`
  and `.tran` by a text search of every line, so a comment that mentions
  `.tran` runs a transient, and a `.OPTIONS` line runs an operating point.
- [ ] **`.INCLUDE`, not `.INC`.** `.INC` is ignored without a message.
- [ ] **`.FOUR` with a lower-case `v(` or `i(`.** `.FOUR 1k V(out)` stops
  with a parse error.
- [ ] **Directives that do nothing here.** `.NOISE` does not exist;
  `.SENS`, `.TF`, `.PZ`, `.DISTO`, `.STEP`, `.MC`, `.PRINT` and `.SAVE`
  are read but not used; `.MEASURE` is ignored.
- [ ] **Nothing after `.END`.** Lines after it are read.

### Names and numbers

- [ ] **The case of node, element and subcircuit names.** They are
  case-sensitive here (`OUT` and `out` are two nodes); ngspice folds case.
- [ ] **No scale factors inside braces or in `.OPTIONS` values.** Write
  `{0.001}`, not `{1m}`.
- [ ] **`.IC` and `.NODESET` with an upper-case `V(`.** `v(out)=1` is
  skipped with a warning.

### Sources

- [ ] **`PULSE` only, after a DC value.** Write `V1 in 0 DC 0 PULSE(...)`.
  There are no `SIN`, `EXP`, `PWL` or `SFFM` waveforms.
- [ ] **`B` sources only in DC and transient.** They have no AC model, and
  their expressions cannot use `I(...)`.

### Subcircuits and parameters

- [ ] **Elements inside `.SUBCKT`.** Only `R`, `C`, `L`, `V` and `I` (DC
  value), `M` (four terminals, level 1 or BSIM4) and `X` are expanded;
  others are skipped with a warning. Move diodes, bipolar transistors and
  controlled sources to the top level.
- [ ] **Parameters of a subcircuit.** Write defaults as `NAME=value` after
  the pins; `PARAMS:` is not accepted. A subcircuit body cannot read a
  global `.PARAM`: pass the value on the `X` line, as in
  `X1 in out DIV RTOP={RG}`. A `.PARAM` inside a `.SUBCKT` is global.
- [ ] **`.FUNC` only in `.PARAM` expressions**, not in element values.
- [ ] **`.IF` conditions without `.PARAM` names.** `.IF` cannot read them,
  and keeps every branch when it cannot evaluate the condition.
- [ ] **One `.PARAM` per name.** A second one is ignored with a warning.

### Models

- [ ] **The parameters the models read.** The diode reads only `IS` and
  `N`, and the other models read part of their parameters; the rest is
  ignored without a warning ([KNOWN_ISSUES](../KNOWN_ISSUES.md#netlist-input)).
- [ ] **No `J` elements.** The library has a JFET, but the parser has no
  `J` element.

### Transient

- [ ] **The time step.** The program steps by `tstep`, fixed: `tmax` has
  no effect and there is no control by the truncation error. ngspice
  chooses its steps, so give kirimaspice a `tstep` small enough for the
  fastest change of the circuit.
- [ ] **`UIC` with `.IC`.** Without `UIC`, `.IC` is ignored; ngspice uses
  it to find the operating point.
- [ ] **The first step.** With the trapezoidal rule, the first step is a
  backward Euler step.

### DC sweeps

- [ ] **`.TEMP` with `.DC`.** The sweep runs at the first temperature only.

## Compare the results

Run the same netlist with both, and record the ngspice version
(`ngspice -v`). Compare node voltages from the raw files rather than
printed values, which are rounded differently. Where an analytic solution
exists, compare both with it: the [examples](../examples/README.md) show
how close kirimaspice is for a few circuits. When the two simulators
disagree, report it with the netlist, both results and the versions
([CONTRIBUTING](../CONTRIBUTING.md)). Commercial simulators are not used for
such comparisons ([FAQ](FAQ.md#do-you-check-against-commercial-simulators)).
