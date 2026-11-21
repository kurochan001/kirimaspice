# Glossary

[日本語](GLOSSARY.ja.md) · [Project README](../README.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

Terms used in the [README](../README.md), the [User Manual](USER_MANUAL.md),
the [Reference Manual](REFERENCE_MANUAL.md) and the other documents, with
what they mean in kirimaspice. **Updated 2026-11-15** for commit `eaf0629`.
For practical questions see the [FAQ](FAQ.md).

- [Netlists](#netlists) · [Sources and devices](#sources-and-devices) ·
  [Analyses](#analyses) · [Numerical methods](#numerical-methods) ·
  [Output](#output) · [Project and the way of working](#project-and-the-way-of-working)

## Netlists

| Term | Meaning |
| --- | --- |
| <a id="netlist"></a>netlist | The text file that describes a circuit and the analyses to run. kirimaspice reads SPICE netlists and follows ngspice where SPICE dialects differ ([NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.md)) |
| title line | The first non-empty line of a netlist, whatever it contains |
| element | A line that places a device, named by its first letter: `R1`, `V1`, `M3`, `X2`. Element names are not case-sensitive |
| <a id="directive"></a>directive | A line that starts with `.`: `.MODEL`, `.TRAN`, `.PARAM`. Some are read but not used by the program ([Reference Manual](REFERENCE_MANUAL.md#directives)) |
| <a id="node"></a>node | A connection point named on element lines. Node names are not case-sensitive, and are written in lower case |
| <a id="ground"></a>ground | The reference node, 0 V: `0`, or `gnd` in any case. Inside kirimaspice it is node 0, `NODE_GND` |
| continuation line | A line that starts with `+` and continues the line before it |
| <a id="model-card"></a>model card | A `.MODEL` line: a named set of device parameters. Its name is not case-sensitive, and it may stand after the `D`, `Q`, `M` and `Z` elements that use it |
| `LEVEL` | The model-card parameter that selects the MOSFET model: 1, 3, BSIM1, BSIM3, BSIM4 and others ([Reference Manual](REFERENCE_MANUAL.md#models)) |
| <a id="subcircuit"></a>subcircuit | A circuit defined between `.SUBCKT` and `.ENDS` and placed with an `X` element. Its name is not case-sensitive. Its internal nodes and elements get names such as `x1.n1` and `X1.R1` ([Reference Manual](REFERENCE_MANUAL.md#elements)) |
| <a id="parameter"></a>parameter | A name defined with `.PARAM`, used in `{...}` expressions; `.FUNC` defines a function for `.PARAM` expressions |
| expression | A value in braces, `{...}`, evaluated with the ExprTk library. Scale factors are not accepted inside it |
| <a id="scale-factor"></a>scale factor | A suffix that scales a number, in any case: `T`, `G`, `MEG`, `K`, `MIL`, `M` (milli), `U`, `N`, `P`, `F`, `A` (atto). Mega is `MEG` |

## Sources and devices

| Term | Meaning |
| --- | --- |
| independent source | A voltage (`V`) or current (`I`) source whose value does not depend on the circuit. It has a DC value and may have an AC input and a transient waveform (`PULSE`, `SIN`, `EXP`, `PWL`, `SFFM`) ([Reference Manual](REFERENCE_MANUAL.md#sources)) |
| <a id="ac-input"></a>AC input | A source whose line has `AC [<magnitude> [<phase>]]`; only such sources drive an AC analysis |
| controlled source | A source whose value depends on a voltage (`E` gives a voltage, `G` a current) or on the current of a voltage source (`H` gives a voltage, `F` a current) |
| <a id="behavioural-source"></a>behavioural source | A `B` element whose voltage or current is an expression of node voltages and `TIME`, the time of the transient |
| <a id="branch-current"></a>branch current | The current of a voltage source, `E`, `H` or voltage-form `B` source, solved as its own unknown. It is positive when it flows into the `+` terminal through the source, so a source delivering current has a negative current |
| level-1 MOSFET | SPICE's square-law MOSFET model (Shichman–Hodges): parameters `KP`, `W`, `L`, `VTO`, `GAMMA` (body effect), `PHI`, `LAMBDA` (channel-length modulation) and noise parameters, with the defaults of ngspice-47 |
| BSIM3, BSIM4 | The Berkeley short-channel MOSFET models. The BSIM4 code is based on BSIM4 4.8.3 and keeps its ECL-2.0 notice ([THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md)) |
| <a id="mutual-inductance"></a>mutual inductance | A `K` element coupling two inductors with coefficient k, so that M = k·√(L1·L2) |
| <a id="lossless-line"></a>lossless line | A `T` element: a transmission line with characteristic impedance `Z0` and delay `TD`, simulated by the method of characteristics |
| lossy line | An `O` element: a transmission line with resistance, inductance, capacitance and conductance per unit length |

## Analyses

| Term | Meaning |
| --- | --- |
| <a id="operating-point"></a>operating point | The DC solution of the circuit, with capacitors open and inductors shorted (`.op`) |
| <a id="dc-sweep"></a>DC sweep | Operating points while a source steps through a range (`.DC`). With two sources, the first is swept for each value of the second |
| <a id="transient"></a>transient analysis | The solution over time (`.TRAN`), from an operating point or, with `UIC`, from the values of `.IC` |
| <a id="ac-analysis"></a>AC analysis | The small-signal response over frequency (`.AC`), linearized at the operating point and driven by the AC inputs |
| temperature sweep | Every analysis at each temperature of a `.TEMP` list; one value is `.OPTIONS TEMP` ([Reference Manual](REFERENCE_MANUAL.md#directives)) |
| <a id="fourier"></a>Fourier analysis | The harmonics and the THD of a signal over the last period of a transient (`.FOUR`), printed after the transient |
| <a id="uic"></a>`UIC` | "Use initial conditions": on a `.TRAN` line, start the transient from the `.IC` values instead of from an operating point. Without `UIC`, the operating point holds the `.IC` nodes at their values |
| library-only analysis | Noise, transfer function, pole-zero, distortion, sensitivity, S-parameters, Monte Carlo, worst case and optimization: implemented in the library and its tests, not run by the program ([Architecture](ARCHITECTURE.md#other-analyses-in-the-library)) |

## Numerical methods

| Term | Meaning |
| --- | --- |
| <a id="mna"></a>MNA | Modified nodal analysis: the system of equations with one unknown per node and one per [branch current](#branch-current) |
| <a id="stamp"></a>stamp | What a device writes into the MNA equations for the present solution: conductances into the matrix and currents into the right-hand side. Each analysis has its stamp method (`stamp_dc_nonlinear`, `stamp_transient`, `stamp_ac`), and all of them evaluate the same model code ([Architecture](ARCHITECTURE.md#devices)) |
| Newton-Raphson iteration | Solving a nonlinear circuit by repeatedly linearizing it at the latest guess; `RELTOL`, `ABSTOL` and `VNTOL` decide when it has converged, and `ITL1` and `ITL4` limit it |
| `GMIN` | A small conductance (10⁻¹² S) that keeps the system solvable: in parallel with each junction of a diode or a bipolar transistor. The `GMIN` option (default 10⁻¹² S) is the least shunt of dynamic gmin, a convergence aid of the operating point |
| convergence aids | Gmin stepping, source stepping, dynamic gmin, continuation and pseudo-transient: ways to find an operating point when plain iteration fails, each switched on with `.OPTIONS` |
| <a id="klu"></a>KLU | The sparse LU solver of SuiteSparse that solves the linear system at each iteration; taken from the system and linked dynamically |
| <a id="integration-method"></a>integration method | How a transient step turns the derivatives of capacitors and inductors into equations: backward Euler (first order), trapezoidal (`trap`, the default), BDF2 (`gear`), BDF3 (`gear3`) and TR-BDF2. The program takes the first step, and the first step after each corner of a source, by backward Euler with every method (the first two with BDF2), and BDF3 then starts with BDF2 steps |
| companion model | A capacitor or inductor replaced, for one time step, by a conductance and a current source that follow from the integration method |
| <a id="lte"></a>LTE | Local truncation error: the error one step adds, estimated from the charge of capacitors and the flux of inductors (for coupled inductors, their coupled fluxes) |
| <a id="time-step"></a>fixed and adaptive time step | The program steps by `tstep`, or `tmax` when it is shorter, and onto the corners of the sources. The library also has an adaptive path that chooses each step from the LTE; the program does not use it |

## Output

| Term | Meaning |
| --- | --- |
| <a id="raw-file"></a>SPICE3 raw file | The waveform file of SPICE3 and ngspice, in ASCII (`--raw-out`) or binary (`--raw-binary-out`). Each analysis is a plot: a header listing the variables, then one value per variable for each point |
| scale variable | The first variable of a plot, against which the others are given: `time`, or the swept source |
| <a id="ssf"></a>SSF | kirimaspice's own binary waveform format (`-o`), with zstd-compressed data blocks. Its specification is not published yet |
| AC CSV | The text file of an AC analysis, `<base>_ac.csv`: real part, imaginary part, magnitude in dB and phase of each node voltage per frequency |
| WAV output | A transient result written as audio (`--wav-out`), one channel per node, ±1 V full scale |

## Project and the way of working

| Term | Meaning |
| --- | --- |
| kirimasim | The sister project, a SystemVerilog simulator. Shared waveform formats and mixed-signal co-simulation with it are goals for later |
| <a id="ngspice"></a>ngspice | The open-source SPICE simulator that kirimaspice follows where SPICE dialects differ and may run for reference, recording its version (ngspice-47 for [NETLIST_CONVENTIONS](spec/NETLIST_CONVENTIONS.md)) |
| <a id="expected-value"></a>expected value | The value a test checks. It comes from a model's specification, an analytic solution or a recorded reference run, and changing an existing one needs its own commit and a person's approval ([AI_POLICY](spec/AI_POLICY.md)) |
| <a id="known-failures"></a>known failures | `tests/known_failures.txt`: the tests that fail today. `tools/local_ci.py` fails on other failures, and on any test that could not start |
| `local_ci.py` | The script that configures, builds and tests in `build/`, compares the failures with the known failures, and runs `check_banned.py` and `check_spdx.py` |
| `check_banned.py` | The check that tracked files (and, with `--history`, every commit's patch, message and author) hold no home-directory paths, e-mail addresses other than noreply ones, foundry names, binary files or files over 5 MB, and none of the private names given with `--private` |
| `check_spdx.py` | The check that every file of the project's own code starts with the copyright line and the SPDX license line |
| `Assisted-by`, `Approved-by`, `Reviewed-by` | Commit trailers: an AI tool helped; a person approved a specification or expected value; a person read the commit before it was pushed ([AI_POLICY](spec/AI_POLICY.md)) |
| clean room | The rule that code under the GPL or LGPL is neither read nor ported; KLU and BTF are used only through their public API |
