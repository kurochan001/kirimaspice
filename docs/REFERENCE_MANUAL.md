# kirimaspice Reference Manual

[日本語](REFERENCE_MANUAL.ja.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

This is a reference to the command line, the netlist language and the
output files of the program `kirimaspice`. It was written on
**2026-11-14** from the source at commit `91af284`, and the behaviour it
describes was checked by running that build (macOS 27.2, arm64, GCC 16.2.0,
`kirimaspice v0.0.0`). Model lookup, the raw file of an operating point,
the output of a node named `GND`, `B` sources, `NAME={expr}` parameters,
the lossless line, the trapezoidal rule and coupled inductors were checked
again after the fixes in commits `6a007ab` to `6edd93a`, and the netlist
conventions of [NETLIST_CONVENTIONS.md](spec/NETLIST_CONVENTIONS.md) after
commit `282ae9f`. Where an entry
was read in the code but not run, it says so. For a guided first run, use
the [User Manual](USER_MANUAL.md); for what is wrong or missing, see
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

## Contents

- [Command line](#command-line)
- [Environment variables](#environment-variables)
- [Exit codes](#exit-codes)
- [Netlist syntax](#netlist-syntax)
- [Elements](#elements)
- [Models](#models)
- [Directives](#directives)
- [Options](#options)
- [Output formats](#output-formats)
- [Build options](#build-options)
- [Sources](#sources)

## Command line

```text
kirimaspice <netlist> [options]
```

The netlist must be the first argument. Options that take a value accept
both `--opt value` and `--opt=value`, except `-o`, which takes only
`-o value`, and the solver options, which take only `--opt=value`.
Options the program does not know are ignored without a message.

| Option | Effect |
| --- | --- |
| `-h`, `--help` | print the usage and exit with 0 |
| `-o <file>` | write the operating point or transient result to `<file>` as SSF, and name the AC CSV after `<file>` |
| `--raw-out <file>` | write the operating point, DC sweep and transient results as an ASCII SPICE3 raw file |
| `--raw-binary-out <file>` | the same as a binary SPICE3 raw file |
| `--wav-out <file>` | write the transient result as a WAV file |
| `--wav-sample-rate <Hz>` | WAV sample rate (default 44100) |
| `--wav-bits <n>` | WAV sample size: 8, 16, 24 or 32 (default 16) |
| `--solver-backend=<name>` | linear solver: `KLU` (default) or `SUPERLU_DIST` |
| `--solver-fallback-order=<a>,<b>` | backends to try when the requested one is not built |
| `--strict-backend` | stop with an error instead of falling back |
| `--superlu-dist-topology=<t>`, `--superlu-dist-grid=<g>` | hints for SuperLU_DIST |

SuperLU_DIST is not built by default and its backend is a placeholder:
asking for it logs `Falling back to klu ...`, or, with `--strict-backend`,
stops with exit code 1.

## Environment variables

| Variable | Effect |
| --- | --- |
| `KIRIMASPICE_SOLVER_BACKEND` | backend, as `--solver-backend`; the command line takes precedence |
| `KIRIMASPICE_SOLVER_FALLBACK_ORDER` | comma-separated fallback order for the variable above (default `klu`) |
| `KIRIMASPICE_SOLVER_STRICT` | `1` makes an unavailable backend from the variable above an error |
| `KIRIMASPICE_TLINE_LOSSY_IIR` | `1` turns on a second-order IIR loss filter in `O` lines (read in the code, not run) |
| `KIRIMASPICE_DEBUG_DUMP` | for development: writes the MNA matrix of each DC iteration to files such as `debug_coo.txt` in the current directory |
| `KIRIMASPICE_BSIM1_DEBUG` | for development: debug logs for BSIM1 devices (optionally only those whose name is the value) |

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | the requested analyses finished, or `--help` |
| 1 | no netlist, unreadable file, parse error, failed analysis, rejected option value, unavailable backend in strict mode |
| other | uncaught error, for example a non-numeric `--wav-sample-rate` (134 on macOS) |

A netlist that asks for no analysis returns 0 even when its operating point
does not converge.

## Netlist syntax

### Lines

- The netlist is read in this order: `.INCLUDE` files are inserted, `.LIB`
  sections are inserted, `+` continuation lines are joined to the line
  before them and empty lines are dropped, `.IF` blocks are resolved, and
  `.CONTROL` blocks are taken out. The lines that remain are read one by
  one.
- The first remaining line is the title.
- A line whose first non-blank character is `*` is a comment. `;` starts a
  comment that runs to the end of the line. There is no other comment
  character.
- A line that starts with `.` is a directive; any other line is an element,
  identified by its first letter in either case.
- Lines after `.END` are read like any other. Unknown directives are
  ignored without a message; an unknown element letter gives the warning
  `Unknown device type`.
- Line numbers in messages count the remaining lines, not the lines of the
  file.

### Names

- Node, element and subcircuit names are case-sensitive; model names are
  not.
- `0` is ground, and so is a node named `gnd` in any case, at the top
  level and inside subcircuits.
- Nodes inside a subcircuit instance are named `<instance>.<node>`, for
  example `X1.n1`.

### Numbers

A number is a decimal or scientific number followed by an optional scale
factor, in any case, as in ngspice. Letters after the number that are not
a scale factor, and letters after a scale factor, are ignored (`10mA` is
0.01, `10MHz` is 0.01).

| Scale factor | Scale |
| --- | --- |
| `T` | 10¹² |
| `G` | 10⁹ |
| `MEG` | 10⁶ |
| `K` | 10³ |
| `MIL` | 25.4×10⁻⁶ |
| `M` | 10⁻³ |
| `U` | 10⁻⁶ |
| `N` | 10⁻⁹ |
| `P` | 10⁻¹² |
| `F` | 10⁻¹⁵ |
| `A` | 10⁻¹⁸ |

`M` is milli in either case; mega is `MEG`.

### Expressions

A value written in braces, `{...}`, is evaluated with ExprTk. It can use
the parameters defined so far, the constants `pi` and `e`, ExprTk's
built-in functions, and, in `.PARAM` only, functions defined by `.FUNC`.
Scale suffixes are not accepted inside braces (`{1m}` is an error). A brace
group is one token even if it contains spaces, and one that follows `=`
stays in the same token, so `IS={ISAT}` is one parameter.

## Elements

`<n+>` and `<n->` are nodes; `<value>` is a number or an expression in
braces. Parameters written `NAME=value` may appear in any order; names are
not case-sensitive.

| Element | Syntax | Notes |
| --- | --- | --- |
| Resistor | `R<name> <n+> <n-> <value>` | further tokens are ignored |
| Capacitor | `C<name> <n+> <n-> <value>` | further tokens (such as `IC=`) are ignored |
| Inductor | `L<name> <n+> <n-> <value>` | further tokens are ignored |
| Mutual inductance | `K<name> <L1> <L2> <k>` | `<L1>`, `<L2>` are inductor names |
| Voltage source | `V<name> <n+> <n-> [DC] <value> [AC <mag> [<phase>]] [PULSE(<v1> <v2> <td> <tr> <tf> <pw> <per>)]` | see below |
| Current source | `I<name> <n+> <n-> [DC] <value> [AC <mag> [<phase>]]` | no waveforms |
| VCVS | `E<name> <n+> <n-> <nc+> <nc-> <gain>` | |
| CCCS | `F<name> <n+> <n-> <Vcontrol> <gain>` | |
| VCCS | `G<name> <n+> <n-> <nc+> <nc-> <gm>` | |
| CCVS | `H<name> <n+> <n-> <Vcontrol> <r>` | |
| Behavioural source | `B<name> <n+> <n-> I=<expr>` or `V=<expr>` | see below |
| Diode | `D<name> <anode> <cathode> [<model>] [IS=<v>] [N=<v>]` | |
| Bipolar transistor | `Q<name> <c> <b> <e> <NPN\|PNP\|model> [parameters]` | |
| MOSFET and related | `M<name> <d> <g> <s> [<b>] <NMOS\|PMOS\|model> [parameters]` | model `LEVEL` selects the device |
| MESFET | `Z<name> <d> <g> <s> [<NMF\|PMF\|model>] [parameters]` | |
| SCR | `S<name> <anode> <cathode> <gate> [parameters]` | |
| TRIAC | `Y<name> <mt2> <mt1> <gate> [parameters]` | |
| Lossless line | `T<name> <a+> <a-> <b+> <b-> Z0=<v> TD=<v>` | both required and positive; `ZO` is accepted for `Z0` |
| Lossy line | `O<name> <a+> <a-> <b+> <b-> R=<v> L=<v> C=<v> G=<v> TD=<v>` | per unit length; `TD` required and positive |
| Subcircuit instance | `X<name> <nodes...> <subckt> [NAME=<v> ...]` | |

### Sources

- A voltage source needs a value: `V1 a b` alone is an error. `DC` before
  it is optional.
- `PULSE(...)` must come after the DC value and needs all seven arguments;
  commas between them are allowed. There are no `SIN`, `EXP`, `PWL` or
  `SFFM` waveforms.
- Only a source whose line has `AC` is an input of AC analysis; `AC`
  without a magnitude means 1, and the phase, in degrees, defaults to 0. A
  source without `AC` has magnitude 0. Inside subcircuits the same holds.
- The current of a voltage source, and of `E`, `H` and voltage-form `B`
  sources, as printed and written, is positive when it flows into the `+`
  terminal through the source, so a source delivering current has a
  negative current. `F` and `H` use that current as their control.
- `F` and `H` name a voltage source that may appear later in the netlist.

### Behavioural sources

`B` takes the first token that starts with `V=` or `I=`; the expression may
be in braces (`V={2*V(in)}`), and without braces it must have no spaces.
It can use `V(<node>)` or `v(<node>)`, `TIME`, the constants and ExprTk's
built-in functions; `I(<source>)` is not available. An expression that
cannot be compiled is an error at the `B` line. Both forms work in the
operating point and the transient; in AC analysis a `B` source contributes
nothing and logs `AC analysis not yet supported`.

### Parameters of the simpler devices

These are the parameters the parser reads; any other parameter in the
element line or the `.MODEL` card is ignored. An instance parameter
overrides the model's.

| Element | Parameters (default) |
| --- | --- |
| `D` | `IS` (1e-14 A), `N` (1) |
| `Q` | `IS` (1e-14 A), `BF` (100), `NF` from a model or `N` on the instance (1), `ISE` (0), `NE` (1.5), `ISC` (0), `NC` (2), noise `KF` (0), `AF` (1), `FF` (1) |
| `M`, level 1 | `KP` (2e-5 A/V²), `W` (10 µm), `L` (1 µm), `VTO` or `VTH` (0.7 V for NMOS, −0.7 V for PMOS), noise `KF` (0), `AF` (1), `EF` (1), `GAMMA` (0.667) |
| `Z` | `LEVEL`/`MODEL` (formula), `BETA` (0.1), `VTO` (−2 V), `LAMBDA` (0.05), `ALPHA` (2), `B` (0.3), `VP` (3 V), `CGS` (1 pF), `CGD` (0.5 pF), `CDS` (0.1 pF), `KF`, `AF`, `EF`, `TNOM` (27 °C) |
| `S` | `VBF` (400 V), `IGT` (20 mA), `IH` (10 mA), `IL` (30 mA), `VGT` (1 V), `RON` (0.1 Ω), `VTM` (1.5 V), `DV_DT` (10), `TNOM` (27 °C) |
| `Y` | `VBO` (400 V), `IGT` (20 mA), `IH` (10 mA), `VGT` (1 V), `RON` (0.1 Ω), `VTM` (1.5 V), `DV_DT` or `DVDT` (10), `TNOM` (27 °C) |

- The level-1 MOSFET has no bulk terminal in its equations, no channel
  length modulation and no capacitances. Its `GAMMA` is the coefficient of
  channel thermal noise, not the body-effect coefficient of SPICE.
- `S` and `Y` take parameters only on the element line: `.MODEL` has no
  SCR or TRIAC type.
- With three terminals, `M` ties the bulk to the source. The parser treats
  a line as four-terminal when its sixth token starts with `NMOS` or
  `PMOS` or names a model of the netlist.

## Models

```text
.MODEL <NAME> <type>(<param>=<value> ...)
.MODEL <NAME> <type> <param>=<value> ...
```

- `<type>` is `NPN`, `PNP`, `D` (or `DIODE`), `NMOS`, `PMOS`, `NMF` or
  `PMF`, in any case. Any other type is an error.
- Parameter names are not case-sensitive; `NAME = value` with spaces is
  accepted; values may be expressions in braces.
- `TNOM`, on a card or an element line, is in °C, as in SPICE, for every
  device; so are `.TEMP` and `.OPTIONS TEMP`. The default `TNOM` is 27 °C.
- Model names are compared without regard to case; a `.MODEL` whose name
  differs from an earlier one only in case replaces it.
- A `.MODEL` line applies to the whole netlist: an element may come before
  its model. An element whose model is not defined anywhere is an error
  (`Undefined diode model`, `Undefined BJT model`, `Undefined MESFET
  model`, `Undefined MOSFET model`), given at the element's line.

For `NMOS` and `PMOS`, `LEVEL` selects the device:

| `LEVEL` | Device |
| --- | --- |
| 1 (default), and any value not listed below | level 1 (square law) |
| 3 | level 3 |
| 9 | BSIM1 |
| 8, 49 | BSIM3 |
| 14, 54 | BSIM4 (a model card with a `VERSION` other than 4.83 gives a warning) |
| 10 | SiC MOSFET |
| 11 | GaN HEMT |
| 12 | FinFET |
| 20 | VDMOS |
| 21 | IGBT (`M` terminals are collector, gate, emitter) |

The parameter names of levels 3, 9, 8/49 and 14/54 follow the respective
Berkeley model manuals. For these levels the parser passes on the names it
knows and ignores the others without a warning; the lists are in
`parse_mosfet_device` in `src/parser/netlist_parser.cpp`. Levels 10, 11,
12, 20 and 21 start from the device's own defaults, those of its polarity,
and take every parameter of the card and then of the element line by name;
a name the device does not have, or a value out of range, gives a warning.
Inside a subcircuit, only level 1 and BSIM4 MOSFETs are expanded.

### FinFET (`LEVEL=12`)

The FinFET is kirimaspice's own simplified square-law model
(`src/devices/finfet.cpp`), not BSIM-CMG. Its terminals are drain, gate and
source; a bulk node on the `M` line is accepted and not used. Every
parameter of the `.MODEL` card goes to the device, and one that the FinFET
does not have, or a value out of range, gives a warning and is ignored. `L`
may also be given on the element line, which overrides the card.

| Parameter | Meaning | NMOS default | PMOS default |
| --- | --- | --- | --- |
| `NFIN` | number of fins | 1 | 1 |
| `HFIN` | fin height [m] | 30e-9 | 30e-9 |
| `TFIN` | fin thickness [m]; read, not used by the current | 7e-9 | 7e-9 |
| `L` | gate length [m] | 1e-6 | 1e-6 |
| `VTH`, `VTH0` or `VTO` | threshold [V], with its sign | 0.4 | −0.4 |
| `U0` | low-field mobility [m²/Vs] | 0.03 | 0.025 |
| `VSAT` | saturation velocity [m/s]; read, not used by the current | 1e5 | 8e4 |
| `SS` | subthreshold swing [V/decade] | 0.07 | 0.075 |
| `DIBL` | threshold lowering per volt of V<sub>DS</sub> [V/V] | 0.05 | 0.06 |
| `TOX` | gate oxide thickness [m] | 1e-9 | 1e-9 |
| `CGS`, `CGD` | gate–source and gate–drain capacitance [F] | 1e-15, 1e-16 | 1e-15, 1e-16 |
| `KF`, `AF` | flicker noise coefficient and exponent | 0, 1 | 0, 1 |
| `KT1` | temperature coefficient of the threshold, with its sign [V/K] | −1e-3 | 1e-3 |
| `TNOM` | nominal temperature [°C] | 27 | 27 |

The current from drain to source of an NMOS, with V<sub>GS</sub> and
V<sub>DS</sub>; a PMOS has the same equations in V<sub>SG</sub> and
V<sub>SD</sub>, with its current from source to drain:

- W<sub>eff</sub> = `NFIN`·`HFIN`, C<sub>ox</sub> = 3.9 ε<sub>0</sub>/`TOX`,
  β = `U0`·(`TNOM`/T)<sup>1.5</sup>·C<sub>ox</sub>·W<sub>eff</sub>/`L`.
- V<sub>th</sub>(T) = `VTH` + `KT1`·(T − `TNOM`), its magnitude for a PMOS;
  V<sub>th,eff</sub> = V<sub>th</sub>(T) − `DIBL`·V<sub>DS</sub> for
  V<sub>DS</sub> > 0.
- At or above the threshold, with V<sub>ov</sub> = V<sub>GS</sub> −
  V<sub>th,eff</sub>: β (V<sub>ov</sub> V<sub>DS</sub> −
  V<sub>DS</sub><sup>2</sup>/2) for V<sub>DS</sub> < V<sub>ov</sub>, and
  β V<sub>ov</sub><sup>2</sup>/2 above.
- Below the threshold: β V<sub>t</sub><sup>2</sup> exp(V<sub>ov</sub> /
  (n V<sub>t</sub>)) (1 − exp(−V<sub>DS</sub>/V<sub>t</sub>)), with
  n = `SS`/(V<sub>t</sub> ln 10), so the current changes by one decade per
  `SS` of gate voltage.
- 10<sup>−12</sup> S lies between drain and source. `CGS` and `CGD` are
  constant, and the transient integrates them by backward Euler. The noise
  sources are 4kT·(2/3)·g<sub>m</sub> and `KF`·|I<sub>D</sub>|<sup>`AF`</sup>/f.

[KNOWN_ISSUES](../KNOWN_ISSUES.md#device-models) lists what this model
leaves out.

## Directives

"Used" means the program acts on the directive. "Read only" means the
parser stores it, but the program does not use it (the library can).

| Directive | Syntax | Status |
| --- | --- | --- |
| `.OP` | `.OP` | used: operating point (found by a text search, see [below](#how-the-program-chooses-analyses)) |
| `.DC` | `.DC <src> <start> <stop> <step> [<src2> <start2> <stop2> <step2>]` | used; `<src>` is a voltage or current source and is swept for each value of `<src2>`; a zero step is an error |
| `.TRAN` | `.TRAN <tstep> <tstop> [<tstart> [<tmax>]] [UIC]` | used; the program steps by `tstep`, and `tmax` is read but has no effect |
| `.AC` | `.AC DEC\|OCT\|LIN <n> <fstart> <fstop>` | used; `DEC`, `OCT`: `<n>` points per decade or octave, both ends included; `LIN`: `<n>` points in all |
| `.TEMP` | `.TEMP <t1> [<t2> ...]` (°C) | used for the operating point and transient; a DC sweep and AC use the first value |
| `.OPTIONS`, `.OPTION` | `.OPTIONS <name>=<value> ...` | used; see [Options](#options) |
| `.IC` | `.IC V(<node>)=<value> ...` | used by a transient: with `UIC` it starts from these values, without `UIC` its operating point holds the nodes at them; `V` must be upper case |
| `.NODESET` | `.NODESET V(<node>)=<value> ...` | used as the initial guess; `V` must be upper case |
| `.PARAM` | `.PARAM <name>=<value> ...` | used; the first definition of a name is kept |
| `.FUNC` | `.FUNC <name>(<a>[,<b>[,<c>]]) {<expr>}` | used in `.PARAM` expressions only; up to three arguments |
| `.MODEL` | see [Models](#models) | used |
| `.SUBCKT` / `.ENDS` | `.SUBCKT <name> <pins...> [NAME=<default> ...]` | used; no nesting of definitions; no `PARAMS:` |
| `.GLOBAL` | `.GLOBAL <node> ...` | used in subcircuit expansion |
| `.INCLUDE`, `.INC` | `.INCLUDE <file>` | used; relative to the netlist's directory |
| `.LIB` / `.ENDL` | `.LIB <file> <section>` | used |
| `.IF` / `.ELIF` / `.ELSE` / `.ENDIF` | `.IF (<expr>)` | used, but `.PARAM` values are not visible; an expression that cannot be evaluated keeps all branches |
| `.END` | `.END` | ignored; lines after it are read |
| `.PRINT`, `.SAVE` | `.PRINT DC\|TRAN\|AC <signals>`, `.SAVE <signals>` | read only |
| `.FOUR` | `.FOUR <frequency> v(<node>)\|i(<source>) ...` | used with `.TRAN`: prints the harmonics and the THD of the last period after the transient |
| `.STEP`, `.SENS`, `.MC`, `.TF`, `.PZ`, `.DISTO`, `.PORT`, `.WCASE`, `.OPTIMIZE` | | read only |
| `.CONTROL` / `.ENDC` | | taken out and run by the parser; the program does not take its analyses from it (see [KNOWN_ISSUES.md](../KNOWN_ISSUES.md)) |
| `.TITLE`, `.MEASURE`, others | | ignored |

### How the program chooses analyses

- The DC sweep runs when the parser has read a complete `.DC` line, and AC
  when it has read `.AC`.
- The operating point and the transient are chosen by searching each raw
  line of the netlist file (not of included files) for `.op`, `.dc` or
  `.tran`, in any case and anywhere in the line, comments included. A
  `.OPTIONS` line therefore selects the operating point. A `.DC` line
  selects the sweep instead of the operating point.
- With none of these, the program computes the operating point, prints the
  node voltages only and writes no file.

## Options

Each option is `<name>=<value>`; names are not case-sensitive. A value is a
plain number (no scale suffix) unless the table says otherwise. A malformed
or unknown option gives a warning and is ignored.

| Option | Default | Meaning |
| --- | --- | --- |
| `TEMP` | 27 | temperature in °C |
| `GMIN` | 1e-12 | minimum conductance (S) |
| `ABSTOL` | 1e-12 | absolute current tolerance (DC and transient) |
| `RELTOL` | 1e-3 | relative tolerance (DC and transient) |
| `VNTOL` | 1e-6 | voltage tolerance (DC and transient) |
| `ITL1` | 100 | DC iteration limit |
| `ITL2` | 0 (use `ITL1`) | iteration limit per DC sweep point |
| `ITL4` | 20 | iteration limit per transient step |
| `ITL5` | 100000 | limit on transient steps |
| `ITL6` | 0 (use `ITL1`) | iteration limit per source-stepping step |
| `METHOD` | `trap` | `trap`/`trapezoidal`, `euler`/`be`/`bdf1`, `gear`/`gear2`/`bdf2`, `gear3`/`bdf3`, `tr_bdf2`/`trbdf2` |
| `MAXORD`, `MAXORDER` | 3 | highest order, 1 to 6 |
| `TRTOL` | 7 | truncation error tolerance factor |
| `CHGTOL` | 1e-14 | charge tolerance (C) |
| `LTE_ABSTOL`, `LTE_RELTOL` | 1e-6, 1e-3 | local truncation error tolerances |
| `ERROR_EST`, `ERRMETHOD` | `physical` | `physical`/`charge`/`embedded`, `predictor_corrector`/`pc`/`dvdt`, `combined`/`both` |
| `GMIN_STEPPING` | 0 | 1 turns gmin stepping on |
| `GMIN_STEPS`, `GMIN_START`, `GMIN_END` | 5, 1e-6, 1e-12 | its steps and range |
| `SOURCE_STEPPING` | 0 | 1 turns source stepping on |
| `SOURCE_STEPS`, `SOURCE_START` | 10, 0.01 | its steps and starting fraction |
| `DYN_GMIN` | 0 | 1 turns dynamic gmin on |
| `DYN_GMIN_START`, `DYN_GMIN_MIN`, `DYN_GMIN_MAX` | 1e-12, 1e-12, 1e-3 | its range |
| `DYN_GMIN_INC`, `DYN_GMIN_DEC` | 10, 0.2 | its factors |
| `DYN_GMIN_STALL`, `DYN_GMIN_IMPROVE`, `DYN_GMIN_TARGET` | 4, 0.1, 1e-9 | its control |
| `CONTINUATION` | 0 | 1 turns continuation on |
| `CONTINUATION_STEPS`, `CONTINUATION_START` | 8, 0.05 | its steps and starting value |
| `PSEUDO_TRANSIENT` | 0 | 1 turns the pseudo-transient on |
| `PSEUDO_DT`, `PSEUDO_STEPS`, `PSEUDO_CAP` | 1e-9, 30, 1e-12 | its step, count and capacitance |
| `VOLTAGE_LIMITING` | 1 | junction voltage limiting; 0 turns it off |

How each method is applied when the operating point does not converge is
in `src/solvers/dc_solver.cpp`; this manual does not describe it.

## Output formats

### Standard output

Log lines (`[<time>] [info]`, `[warning]`, `[error]`) and results are both
written to standard output. The results are:

- operating point: `=== DC Operating Point ===`, then `V(0) = 0 V`, one
  `V(<node>) = <value> V` per node and one `I(<source>) = <value> A` per
  voltage, `E` and `H` source;
- under `.TEMP`: the same block per temperature, headed
  `=== DC Operating Point @ <t> °C ===`;
- DC sweep: a summary only;
- transient: `=== Transient Analysis Results ===` and a tab-separated table
  with a `Time (s)` column and one `V(<node>)` column per node, `V(0)`
  included, one row per time point;
- AC: nothing.

### SPICE3 raw, ASCII (`--raw-out`)

The header has `Title`, `Date`, `Plotname`, `Flags: real`,
`No. Variables`, `No. Points` and `Variables`, followed by `Values`.

| Analysis | `Plotname` | Variables |
| --- | --- | --- |
| operating point | `DC Operating Point` | `v(<node>)`, then `i(<source>)` |
| DC sweep | `DC Sweep` or `DC Sweep (2D)` | the swept source(s) (type `sweep`), `v(<node>)`, `i(<source>)` |
| transient | `Transient Analysis` | `time`, `v(<node>)`, `i(<source>)` |

Ground (the node `0`) is not a variable. Each analysis run appends one plot
to the same file. Each point holds one value per variable, the first on the
line with the point index.

### SPICE3 raw, binary (`--raw-binary-out`)

The same header, with the names in upper case (`V(<node>)`, `I(<source>)`),
then `Binary:` and the values as 8-byte IEEE doubles in the byte order of
the machine (little-endian on x86-64 and arm64), point after point.

ngspice-47 loads both raw files: on 2026-11-14 its `load` command read a
transient plot of 502 points written by kirimaspice, ASCII and binary,
with the same values. Other plots and other readers are not confirmed.

### SSF (`-o`)

A binary file with a fixed header (`SSF`), a list of `key=value` metadata
(`file.format`, `file.version` 4.3, `file.generator`, `file.analysis`,
`axis.0.*`, and `signal.<n>.name`, `.node` or `.device`, `.quantity`,
`.unit`), and data blocks compressed with zstd. Its specification is not
published in this repository; the writer is `src/output/ssf_writer.cpp`.

### AC CSV

The file `<base>_ac.csv`, where `<base>` is the `-o` path, or the netlist
path, without the part from its last dot. Lines that start with `#` give
the title, date, simulator, number of points and nodes, frequency range,
status, number of linear solves and time. Then a header and one row per
frequency:

```text
frequency,v(<node>)_real,v(<node>)_imag,v(<node>)_mag_dB,v(<node>)_phase_deg,...
```

Phases are in degrees; magnitudes in dB are 20·log₁₀|V|. Branch currents
are not written.

### WAV (`--wav-out`)

PCM, one channel per node (ground excluded). The transient result is
interpolated linearly at the sample rate. A voltage of ±1 V is full scale
and larger values are clipped. 8-bit samples are unsigned; 16-, 24- and
32-bit samples are signed integers.

## Build options

| CMake option | Default | Effect |
| --- | --- | --- |
| `CMAKE_CXX_STANDARD` | 23 | 23, or 26 to try the next standard; other values are an error |
| `BUILD_TESTS` | ON | build the tests |
| `BUILD_BENCHMARKS` | OFF | build the benchmarks |
| `ENABLE_COVERAGE` | OFF | add `--coverage` (GCC and Clang) |
| `SANITIZE_ADDRESS` | OFF | build with AddressSanitizer (GCC and Clang) |
| `KIRIMASPICE_WITH_SUPERLUDIST` | OFF | the SuperLU_DIST placeholder |
| `KIRIMASPICE_WITH_SUNDIALS` | OFF | sets a macro only |
| `KIRIMASPICE_SOLVER` | `KLU` | default backend name |

By `CMakeLists.txt`, `cmake --install build` installs `kirimaspice`, the
static library `kirimaspice_lib` and the headers under `include/` (read in
the file, not run).

## Sources

- The command line: `src/main.cpp`.
- The netlist parser: `src/parser/netlist_parser.cpp`; numbers:
  `include/kirimaspice/util/units.hpp`; defaults:
  `SimulationConfig` in `include/kirimaspice/core/types.hpp`.
- Subcircuit expansion: `src/circuit/circuit_graph.cpp`.
- Output writers: `src/output/`.
