# How the circuit solver works

[日本語](SOLVER_ALGORITHMS.ja.md) · [Documentation guide](README.md)

Follow one voltage source, resistor and capacitor from a netlist to a matrix and then to one
point on a waveform. Adding a diode to the same circuit shows where the Jacobian and Newton
iteration enter the calculation. The numerical examples were checked with kirimaspice at
`770db19`.

## Turn the circuit into three equations

```spice
RC charging from an initial voltage
V1 in 0 1
R1 in out 1k
C1 out 0 1u
.IC V(in)=1 V(out)=0
.OPTIONS METHOD=BE
.TRAN 100u 300u UIC
.END
```

`V1` holds `in` at 1 V. `R1` connects `in` to `out`, and `C1` connects `out` to ground. `UIC`
and `.IC` start the capacitor at 0 V. Save the netlist as `build/rc-algorithm.cir` and run the
following from the repository root. The raw file gives `v(in)`, `v(out)` and `i(v1)` at each
recorded time.

```sh
build/kirimaspice build/rc-algorithm.cir --raw-out build/rc-algorithm.raw
```

Modified nodal analysis (MNA) sets ground to 0 V and treats the remaining node voltages as
unknowns. An ideal voltage source sets the voltage across its terminals. The surrounding circuit
determines its current. MNA therefore adds the branch current `i_V` as an unknown and adds one
equation imposing the source voltage. Positive `i_V` flows into the positive terminal. Writing
`g = 1/R`, Kirchhoff's current law (KCL) and the source constraint give

```text
in     g(v_in − v_out) + i_V = 0
out    g(v_out − v_in) + i_C = 0
V1     v_in = V_s
```

The first two rows sum the currents leaving a node, in amperes. The third row is a constraint in
volts. There are three unknowns, `x = [v_in, v_out, i_V]ᵀ`. A standalone inductor in kirimaspice
computes its current from terminal voltage and stored current history, so it needs no extra
branch unknown. [Source-current convention](spec/NETLIST_CONVENTIONS.md) ·
[Voltage-source stamp](../src/devices/voltage_source.cpp)

## Add the device stamps

A stamp is the set of coefficients a device adds to the rows and columns for its terminals. For
a resistor between `p` and `q`, the current leaving `p` is `g(v_p − v_q)`. The current leaving
`q` has the opposite sign. Add `+g` at `(p,p)` and `(q,q)` and `−g` at `(p,q)` and `(q,p)`. Omit
any row or column for ground. For a current source delivering a constant `I` from `p` to `q`,
subtract `I` from the right-hand side `b` at `p` and add `I` at `q`.

To stamp `C1`, let `v_n` be its accepted voltage at time `t_n` and `h` the time interval to the
next point. Backward Euler (BE) replaces `i_C = C dv_out/dt` by `i_C ≈ (C/h)(v_out − v_n)`. For
this step the capacitor acts like a conductance `G_C = C/h` in parallel with a current source of
value `−G_C v_n`. This equivalent circuit is called its **companion model**. At `out`, `C1` adds
`G_C` to the diagonal and `+G_C v_n` to the right-hand side.

```math
\begin{bmatrix}
g & -g & 1\\
-g & g+G_C & 0\\
1 & 0 & 0
\end{bmatrix}
\begin{bmatrix}v_{\rm in}\\v_{\rm out}\\i_V\end{bmatrix}
=
\begin{bmatrix}0\\G_C v_n\\V_s\end{bmatrix}.
```

For the first step `g = 1 mS`, `G_C = 10 mS` and `v_n = 0`. Row three gives `v_in = 1 V`, row
two gives `v_out = 1/11 V ≈ 0.0909091 V`, and row one gives `i_V ≈ −0.909091 mA`. The negative
sign means the source supplies current to the circuit. The next step uses that `1/11 V` on its
right-hand side. Contributions at the same matrix position are summed, and the KLU linear solver
receives the sparse matrix in compressed-column form. [Resistor](../src/devices/resistor.cpp) ·
[Capacitor](../src/devices/capacitor.cpp) ·
[Sparse matrix assembly](../src/core/matrix_types.cpp)

## Add a diode and solve with Newton's method

Connect a diode from `out` (anode) to ground (cathode). For this derivation, keep the external
`C1` and the diode conduction current `i_D(v)` and omit the diode's own stored charge.
Substitute the known `v_in = V_s`. The current left unbalanced at `out` is the residual that the
solver must drive to zero.

```math
F(v)=g(v-V_s)+G_C(v-v_n)+i_D(v).
```

At iteration `k` take the tangent to the diode curve at the trial voltage `u`. Its slope is
`g_D = di_D/dv(u)`. The line `i_D(v) ≈ g_D v + I_eq` passes through `(u, i_D(u))` when
`I_eq = i_D(u) − g_D u`. The same stamp rules apply. Add `g_D` to the `out` diagonal and
subtract `I_eq` from its right-hand side. The implementation limits changes in junction voltage
and includes the current and slope from the parallel `GMIN`.
[Diode linearization](../src/devices/diode.cpp)

For this one row, the Newton update is `v_next = u − F(u)/(g + G_C + g_D)`. With several
unknowns, the **Jacobian matrix** `J` plays the role of the denominator. Its entry
`J[i,j] = ∂F_i/∂x_j` tells how much the residual of equation `i` changes for a small change of
unknown `j`. In the full MNA system, the derivative of the `out` residual with respect to `v_out` is
`g + G_C + g_D` and its derivative with respect to `v_in` is `−g`. In the voltage-constraint
row, `∂(v_in − V_s)/∂v_in = 1`.

Newton's method solves `J(x_k) Δx = −F(x_k)` and takes `x_next = x_k + Δx`. The code stamps the
rearranged equation `J(x_k) x_next = J(x_k) x_k − F(x_k)` to solve directly for the next values.
Slopes and right-hand-side values are updated at every iteration. On an ordinary transient step,
KLU first analyzes the sparse structure and factors the numerical matrix. Later iterations reuse
that structure and factor the new values. The numerical LU factors are lower and upper triangular,
so the linear solve uses forward and backward substitution.
[Transient Newton loop](../src/solvers/transient_solver.cpp) ·
[KLU interface](../src/solvers/klu_solver.cpp)

The convergence check compares node-voltage changes with `RELTOL × max(|old|, |new|) + VNTOL`.
Branch-current changes use `ABSTOL` for the absolute tolerance. The solver also re-stamps at the
candidate solution and checks the current residual at each node and the voltage residual of each
constraint against their respective tolerances. An iteration in which junction limiting is
active continues. Convergence here concerns the equations for the chosen time step.
[Convergence checks](../src/solvers/convergence_checker.cpp) ·
[Diode operating-point KCL test](../tests/unit/test_dc_convergence_kcl.cpp)

Try the diode in the example by inserting these two lines before `.END` and extending the
`.TRAN` stop time from `300u` to `2m`.

```spice
D1 out 0 Ddemo
.MODEL Ddemo D(IS=1p N=1 CJO=0 TT=0)
```

At 2 ms, `v(out)` is `0.8513564 V` for the original RC circuit and `0.5171735 V` with the diode.
As the voltage rises, the diode carries part of the current supplied by the resistor. In a
multi-terminal device with nonlinear charge, a BE terminal current `(Q_i(x) − Q_i(x_n))/h`
contributes `(∂Q_i/∂v_j)/h` to Jacobian entry `(i,j)`. Charge that depends on another terminal's
voltage produces an off-diagonal coefficient.
[BSIM4 terminal-charge and derivative tests](../tests/unit/test_bsim4_transient.cpp)

## Change the integration method

Choosing an approximation to the time derivative of charge or flux changes the matrix
coefficients and the history terms on the right-hand side. The connections, and hence the `out`
equation used in this circuit, stay the same.

- **Backward Euler (first order)** uses the endpoint current in `C(v_next − v_n) = h i_next`.
  The **trapezoidal rule (second order)** averages the currents at the two endpoints, giving
  `C(v_next − v_n) = h(i_n + i_next)/2`. Solving for current gives
  `i_next = (2C/h)(v_next − v_n) − i_n`. Its diagonal conductance is twice the BE value, and the
  previous current also enters the right-hand side.
- **The backward differentiation formulas (Gear, BDF2 and BDF3)** take the endpoint slope of a
  polynomial interpolating the new and past voltages. For constant steps BDF2 gives
  `i_next = (C/h)(3v_next/2 − 2v_n + v_{n−1}/2)`. BDF3 goes back one more point. When step
  lengths vary, the solver recalculates the coefficients using the previous step lengths.
- **TR-BDF2 (second order)** first takes a trapezoidal stage to `t_n + γh`, with `γ = 2 − √2`. A
  BDF2 stage uses the intermediate and starting values to reach `t_n + h`. Each stage evaluates
  sources at its own time and runs its own Newton iteration.

The CLI defaults to the trapezoidal rule with one BE startup step to establish history. The
example requests BE on every step. The transient guide covers startup for the other methods,
restarts at source breakpoints, and device support.
[Integration methods and startup](TRANSIENT_ANALYSIS.md#integration-methods) ·
[TR-BDF2 stage-time tests](../tests/unit/test_tr_bdf2_accuracy.cpp)

## Stiffness and numerical stability

A transient MNA model is a **differential-algebraic equation (DAE)**. It combines the time
evolution of charge and flux with KCL and the constraints imposed by voltage sources.
Diode currents and terminal charges that depend nonlinearly on voltage make these equations
nonlinear. **Stiffness** concerns time scales. Once a fast transient has decayed, a step can
be small enough for accuracy in the slow waveform of interest while still being too large for
a particular integration method to remain stable. Even a linear RC circuit can exhibit this.

Linearization at an operating point gives `C δx′ + G δx = 0` for an unforced perturbation.
Here `C` collects storage derivatives and `G` is the Jacobian of the static device terms.
A voltage constraint has no time derivative. A finite eigenvalue `λ` corresponds to a mode
`u` satisfying `(G + λC)u = 0`. Represent one decaying mode by `y′ = λy` with `Re(λ) < 0`, and let `z = hλ`. For an RC
discharge, `λ = −1/(RC)`. The exact solution is multiplied by `exp(z)` per step. A one-step
method with amplification factor `R(z)` is **A-stable** if `|R(z)| ≤ 1` throughout the left
half of the complex plane. For comparison, forward Euler gives `R(z) = 1 + z`. For real
negative `λ` its amplitude grows if `h > 2/|λ|`. Stability can therefore limit the step even
when the slow waveform would allow a larger one.

- **Backward Euler** has `R(z) = 1/(1−z)`. It is A-stable, and `R(z) → 0` as `|z| → ∞` in
  the left half-plane. This combination is called **L-stability**. Very fast decaying modes
  are strongly damped.
- **Trapezoidal integration** has `R(z) = (1+z/2)/(1−z/2)` and is also A-stable.
  Its factor changes sign for real `z < −2` and tends to `−1` as `z → −∞`. The amplitude
  stays bounded, but a physically extinguished signal can alternate in sign for many steps.
- **TR-BDF2** is L-stable with the `γ = 2 − √2` used here. It damps fast components at long
  steps and is second-order accurate for smooth solutions. [TR-BDF2 stability analysis](https://arxiv.org/abs/1510.04303)
- **Gear methods** use several past values, so stability is determined by all the roots of
  the recurrence. At a constant step **BDF2 is A-stable**. **BDF3 is A(α)-stable**. Its
  stability region includes a sector about the negative real axis with a half-angle of about
  86°. A weakly damped oscillation near the imaginary axis can fall outside the stable region
  at some step sizes. These results concern constant-step formulas. Startup and changes in
  step length also affect a computed waveform. [BDF stability regions](https://sundials.readthedocs.io/en/v6.3.0/cvode/Mathematics_link.html#bdf-stability-limit-detection) · [BDF stability angles](https://arxiv.org/abs/1901.08347)

### Try two time constants

In the original RC netlist, replace the `.IC` line with the three lines below. The `out`
node charges with a 1 ms time constant. The `fast` node discharges from 1 V with a 1 µs
time constant. These modes are independent, so each has an exact solution for comparison.

```spice
Rfast fast 0 1k
Cfast fast 0 1n
.IC V(in)=1 V(out)=0 V(fast)=1
```

Keep the 100 µs step and vary `METHOD`. These are measured voltages at 200 µs. All three
runs take their first step with BE.

| Solution or `METHOD` | `v(fast)` (fast decay) | `v(out)` (slow charge) |
| --- | ---: | ---: |
| Exact | 1.38 × 10⁻⁸⁷ V | 0.181269 V |
| `BE` | +0.0980 mV | 0.173554 V |
| `TRAP` | −9.5127 mV | 0.177489 V |
| `TR_BDF2` | −0.4362 mV | 0.177454 V |

For the fast mode `z = −100`. The first BE step leaves about 9.901 mV. A trapezoidal step
multiplies that by `−49/51`, producing a negative voltage in a passive RC discharge whose
exact voltage is always positive. On the slow node trapezoidal and TR-BDF2 differ by only
about 35 µV, while the fast node shows how differently they damp. To measure the fast
transient itself, reduce the step until its time scale is resolved and compare waveforms.
Stability concerns amplification of decaying modes, Newton convergence concerns the
equations at a given time, and the LTE estimate below concerns the accuracy of one step.

## Estimate the error and decide on a step

The exact RC response is `v(t) = V_s (1 − exp(−t/(RC)))`. At 100 µs the exact voltage is
`0.0951626 V`. The BE result above is about 4.25 mV away. Change only the `.TRAN` step to `50u`.
Two steps reach the same 100 µs at `0.0929705 V`, an error of about 2.19 mV. Repeating a run at
finer steps is a way to check waveform accuracy with the fixed-step CLI. Fixed stepping also
clips steps at breakpoints and can retry with BE and halve the step after Newton failure.
[Fixed-step control](TRANSIENT_ANALYSIS.md#one-fixed-step-and-convergence)

Local truncation error (LTE) is the error in the solution introduced by one integration step
started with exact values and history. For a smooth solution and a method of order `p`, this
error is typically `O(h^(p+1))`. With stable integration over a fixed time interval the
accumulated error is `O(h^p)`.

When the exact solution is unknown, two estimates at the same time give an indication of whether
the step is too large. In the C++ library, `config.transient.use_adaptive_timestep = true`
enables this decision. For voltage estimates `a` and `b` at a trial endpoint, the solver forms
the dimensionless quantity

```math
E_V=\max_{\text{nodes }j}
\frac{|a_j-b_j|}
     {\mathrm{lte\_abstol}+\mathrm{lte\_reltol}\max(|a_j|,|b_j|)}.
```

The absolute tolerance provides a scale near zero volts. The relative tolerance makes the scale
grow with signal amplitude. The maximum runs over node voltages. There are two ways to obtain
the voltage estimates.

- **Compare integration formulas.** Solve from the same time and device history to the same
  endpoint with, for example, BE and BDF2. The `PHYSICAL` setting uses this voltage comparison.
  During startup both solves can use BE and give a voltage difference of zero.
- **Predict and correct.** Extrapolate from accepted voltages and compare the prediction with
  the result of the implicit solve. With two stored points, the extrapolation scales the last
  voltage change by the trial step divided by the previous step. Pure `PREDICTOR_CORRECTOR` also
  uses the predicted solution as Newton's initial guess. While building its history it uses two
  solves for comparison. `COMBINED` uses the larger of the available estimates.

Run the example separately with `METHOD=BE` and `METHOD=TRAP`. Both take the same BE step to 100
µs. From that common history they reach 200 µs at `0.1735537 V` and `0.1774892 V` respectively.
Using the default scales of `1 µV` and `lte_reltol = 0.001` gives `E_V ≈ 22.05`. Applying the
default adaptive acceptance test `E ≤ TRTOL = 7` would reject this step. This calculation uses
two measured fixed-step runs to illustrate the decision.

After 20 accepted steps since startup or a source breakpoint, the solver also considers
estimates from dynamic devices that supply one. For example, the capacitor predicts charge from
its stored charge history and last current, `Q_pred = charge_prev_ + h × current_prev_`. It
compares that with `Q_corr = C v_next`, dividing the absolute difference by
`max(|Q_corr|, |Q_pred|) + CHGTOL`. The solver takes the maximum of the voltage and device
estimates as `E`. Rejection restores device state and retries from the same starting time.
Acceptance records the solution and the step actually taken. The basic multiplier for the
following step is `0.9 × max(E, 10⁻¹⁰)^(−1/(p+1))`, where `p` is the order used by the
controller. Multiplier and step bounds and an adjustment on order change also apply. A local
indicator cannot guarantee a maximum error for the whole waveform.
[Adaptive API example](TRANSIENT_ANALYSIS.md#adaptive-stepping-in-the-library) ·
[Prediction, rejection and step control](../src/solvers/transient_solver.cpp)

## When adaptive steps save simulation time

The normal CLI uses fixed stepping. Its nominal integration step is the `.TRAN` value `TSTEP`,
capped by `TMAX` when the latter is smaller. Source breakpoints, the stop time, a device's
maximum allowed step or a failed Newton solve can shorten a step. The fixed-step loop then
aims for its time grid again. A flat waveform does not cause it to skip several grid points
based on LTE. For example, `.TRAN 1u 10m 0 100u UIC` normally advances by 1 µs in the CLI.
[CLI step selection](../src/main.cpp) · [Fixed-step loop](../src/solvers/transient_solver.cpp)

The C++ library enables adaptive stepping with `config.transient.use_adaptive_timestep = true`.
Set the starting step `initial_step` and the ceiling `max_step`. In a parsed netlist, omitting
`TMAX` sets that ceiling to `TSTEP`, so allow a larger maximum to grow beyond the starting
step. The line above sets a 1 µs start and a 100 µs ceiling. At this revision the CLI and
netlist have no switch to enable adaptive stepping.
[Library configuration example](TRANSIENT_ANALYSIS.md#adaptive-stepping-in-the-library) ·
[Parsing the time settings](../src/parser/netlist_parser.cpp)

Where the waveform changes rapidly and the estimated error is large, the adaptive solver
takes short steps. During smooth intervals it can increase the next step. Reaching the same
stop time in fewer steps can save device stamps, numerical factorizations and Newton
iterations. There is also work per trial. The default error estimator solves to the same
endpoint with two integration formulas, and rejected trials consume time. A waveform with
persistent fast changes or a small step ceiling leaves less room for savings.

### Measure cost and accuracy on the same RC circuit

Use the original charging circuit (1 ms time constant, no diode) and extend the stop time to
10 ms. Both paths ran through the same C++ library with `GEAR2`. The adaptive path used
`adaptive_order=true`, `max_order=2`, a 1 µs initial step, a 100 µs ceiling and default error
tolerances. Two fixed steps were measured, 1 µs and 15 µs. The latter achieved a waveform
error comparable to the adaptive run.

| Step selection | Accepted steps | Newton iterations | Max voltage error | Median solve time |
| --- | ---: | ---: | ---: | ---: |
| Fixed 1 µs | 10,000 | 10,692 | 0.00124 mV | 19.51 ms |
| Fixed 15 µs | 667 | 1,334 | 0.282 mV | 2.01 ms |
| Adaptive, 1 µs start and 100 µs ceiling | 156 | 624 | 0.295 mV | 0.95 ms |

The adaptive run reached 100 µs and recorded zero LTE rejections. Against fixed 15 µs it took
about a quarter as many accepted steps. Its steps include error-estimation solves, so the
iteration count and elapsed time were both about half. To measure waveform error, linearly
interpolate `v(out)` for each run onto the same 1 µs grid and take the largest absolute
difference from the exact `1 − exp(−t/(1 ms))` V. Timing used a macOS arm64 Release build
with GCC 16.2.0 at `770db19`. Each run used a fresh circuit and solver, with logging off.
Only `solve()` was timed. The reported times are medians of 21 runs per setting, rotating
the execution order after an initial warm-up for each setting.

## Solve the same circuit in DC and AC

At a DC operating point the capacitor's steady current is zero. The example settles at
`v_out = 1 V` and `i_V = 0 A`. With the diode added, Newton's method finds a voltage satisfying
`g(v_out − V_s) + i_D(v_out) = 0`. Small-signal AC analysis first obtains a DC operating point,
evaluates device slopes there, and solves a complex matrix at each frequency. The capacitor
stamps `jωC` and this RC circuit has `V_out/V_in = 1/(1+jωRC)`. The independent variable is now
angular frequency `ω = 2πf`, and no time-step history is needed.
[DC solver](../src/solvers/dc_solver.cpp) · [AC solver](../src/solvers/ac_solver.cpp)

Ho, Ruehli and Brennan introduced the MNA formulation. For the relationship between equation
assembly, linear and nonlinear solves, and numerical integration in SPICE, see Nagel's technical
report.
[Original MNA paper (IBM Research)](https://research.ibm.com/publications/the-modified-nodal-approach-to-network-analysis) ·
[SPICE2 technical report (UC Berkeley)](https://www2.eecs.berkeley.edu/Pubs/TechRpts/1975/9602.html)
