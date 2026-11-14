# Examples

[日本語](README.ja.md) · [Project README](../README.md) · This English text is the main version; the Japanese version follows it, and where they differ, this text holds.

Small netlists that show one feature each. Every one has an analytic
solution, and CTest runs each one and compares the result with it
(`tests/scripts/test_examples.py`, tests `example_*`), so the numbers below
are checked on every test run. Run them from the top of the source tree
after [building](../docs/USER_MANUAL.md#build).

| File | Shows | Analysis |
| --- | --- | --- |
| [test_simple_divider.sp](test_simple_divider.sp) | the first run of the [User Manual](../docs/USER_MANUAL.md#run-your-first-simulation) | operating point |
| [params_subckt.sp](params_subckt.sp) | `.PARAM`, `.FUNC` and a subcircuit with a parameter | operating point |
| [diode_iv.sp](diode_iv.sp) | a `.MODEL`, a DC sweep and the sign of a source current | DC sweep |
| [rc_step.sp](rc_step.sp) | a `PULSE` source and the raw output | transient |
| [rc_discharge.sp](rc_discharge.sp) | `.IC` with `UIC`, and the integration methods | transient |
| [rc_lowpass_ac.sp](rc_lowpass_ac.sp) | an AC input and the AC CSV | AC |

## Voltage divider

```sh
build/kirimaspice examples/test_simple_divider.sp
```

Two 1 kΩ resistors across 3.3 V: `V(n2) = 1.65 V`, and `I(VDD) =
-0.00165 A`. The current of a voltage source is positive into its `+`
terminal, so a source that delivers current shows a negative value
([NETLIST_CONVENTIONS](../docs/spec/NETLIST_CONVENTIONS.md), rule 3).

## Parameters, a function and a subcircuit

```sh
build/kirimaspice examples/params_subckt.sp
```

`VH = HALF(VIN) = 2 V` drives a 1 kΩ / 2 kΩ divider, so `V(out) =
1.33333 V`. `X1` is the subcircuit `DIV` with `RTOP=3k`, a 3 kΩ / 1 kΩ
divider, so `V(xout) = 0.5 V`. `V1` delivers 2/3 mA + 1/2 mA, so `I(V1) =
-0.00116667 A`. A parameter given on the `X` line replaces the default of
the `.SUBCKT` line.

## Diode I–V curve

```sh
build/kirimaspice examples/diode_iv.sp --raw-out diode_iv.raw
```

The program prints only a summary of a sweep; the values are in the raw
file. With `IS=1e-14` and `N=1` at 27 °C the current follows
I = IS·(exp(V/V<sub>T</sub>) − 1), V<sub>T</sub> = kT/q = 25.865 mV, and
`i(V1)` is −I. The program agrees with this to better than one part in a
million at every point of the sweep, for example −1.18719×10⁻⁴ A at 0.6 V.

Two known issues show in this example
([KNOWN_ISSUES](../KNOWN_ISSUES.md#analyses)):

- The sweep `0 0.7 0.05` ends at 0.65 V: the end point is lost when
  (stop − start)/step is not exactly an integer in floating point.
- The example stops at 0.7 V because above about 0.73 V the diode current
  is too small: at 0.8 V the program gives −6.76×10⁻² A where the equation,
  and ngspice-47, give −0.2708 A.

## RC step response

```sh
build/kirimaspice examples/rc_step.sp --raw-out rc_step.raw
```

1 kΩ and 1 µF (RC = 1 ms) driven by a 0-to-1 V step. The exact value at
1 ms is 1 − e⁻¹ = 0.632121; the program gives 0.632105 with the default
trapezoidal rule and `tstep` = 10 µs. The first step is a backward Euler
step, which accounts for most of the difference.

## RC discharge and the integration methods

```sh
build/kirimaspice examples/rc_discharge.sp --raw-out rc_discharge.raw
```

The capacitor starts at 1 V from `.IC V(out)=1`; `UIC` on the `.TRAN` line
makes the transient start from `.IC` instead of from an operating point.
Without `UIC` the program ignores `.IC` and the capacitor stays at 0 V,
whereas ngspice uses `.IC` in both cases
([KNOWN_ISSUES](../KNOWN_ISSUES.md#analyses)).

The exact value at 1 ms is e⁻¹ = 0.367879. Change `METHOD` on the
`.OPTIONS` line to compare the methods:

| `METHOD` | Method | Order | V(out) at 1 ms |
| --- | --- | --- | --- |
| `trap` (default) | trapezoidal | 2 | 0.367895 |
| `gear` | BDF2 | 2 | 0.367913 |
| `euler` | backward Euler | 1 | 0.369711 |

## RC low-pass filter

```sh
build/kirimaspice examples/rc_lowpass_ac.sp -o rc_lowpass
```

The AC result is written to `rc_lowpass_ac.csv`, not printed. `AC 1` makes
`V1` the input. At 1 kHz the response 1/(1 + j2πfRC) is −16.0722 dB and
−80.957°, and the program gives the same to the digits printed in the CSV.
The CSV's header line `Simulator: kirimaspice v0.27` is a known issue: the
version is 0.0.0.
