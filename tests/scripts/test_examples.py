#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Runs one netlist of examples/ and checks its result against the analytic
solution quoted in the netlist, examples/README.md and the manuals.

  test_examples.py --binary build/kirimaspice --examples examples --case rc_step

Each case runs the program in a temporary directory, so no output is written
next to the examples. The expected values come from analytic solutions; the
tolerances are those of the method, not of the implementation: AC and DC
results are exact up to rounding, and a transient result differs from the
exact solution by the local truncation error of the integration method.
"""

from __future__ import annotations

import argparse
import cmath
import csv
import math
import re
import subprocess
import sys
import tempfile
from pathlib import Path

BOLTZMANN = 1.380649e-23  # J/K, exact in the SI since 2019
CHARGE = 1.602176634e-19  # C, exact in the SI since 2019
KELVIN_27C = 300.15


def run(binary: Path, netlist: Path, *args: str) -> str:
    done = subprocess.run([str(binary), str(netlist), *args],
                          capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(f"{netlist.name}: exit code {done.returncode}\n{done.stdout}{done.stderr}")
    return done.stdout


def read_raw(path: Path, plotname: str) -> list[dict[str, float]]:
    """Reads the plot named plotname of an ASCII SPICE3 raw file."""
    lines = path.read_text().splitlines()
    starts = [i for i, line in enumerate(lines) if line == f"Plotname: {plotname}"]
    if not starts:
        sys.exit(f"{path.name}: no plot '{plotname}'")
    lines = lines[starts[-1]:]
    first = lines.index("Variables:")
    values = lines.index("Values:")
    names = [line.split()[1] for line in lines[first + 1:values]]
    rows, current = [], None
    for line in lines[values + 1:]:
        fields = line.split()
        if not fields:
            continue
        if line.startswith("Title:"):
            break
        if len(fields) == 2:  # "<index> <first value>" starts a point
            if current:
                rows.append(current)
            current = [float(fields[1])]
        else:
            current.append(float(fields[0]))
    rows.append(current)
    return [dict(zip(names, row)) for row in rows]


def printed(stdout: str, name: str) -> float:
    match = re.search(rf"^{re.escape(name)} = (\S+) [VA]$", stdout, re.M)
    if not match:
        sys.exit(f"'{name}' not printed:\n{stdout}")
    return float(match.group(1))


def check(label: str, got: float, expected: float, tol: float) -> bool:
    ok = abs(got - expected) <= tol
    print(f"{'ok  ' if ok else 'FAIL'} {label}: got {got:.9g}, expected {expected:.9g} (tolerance {tol:g})")
    return ok


def case_divider(binary: Path, examples: Path, tmp: Path) -> bool:
    out = run(binary, examples / "test_simple_divider.sp")
    # Two equal resistors across 3.3 V.
    return all([
        check("V(n2)", printed(out, "V(n2)"), 1.65, 1e-6),
        check("I(VDD)", printed(out, "I(VDD)"), -3.3 / 2000.0, 1e-9),
    ])


def case_params_subckt(binary: Path, examples: Path, tmp: Path) -> bool:
    out = run(binary, examples / "params_subckt.sp")
    vh = 4.0 / 2.0
    return all([
        check("V(out)", printed(out, "V(out)"), vh * 2e3 / 3e3, 1e-5),
        check("V(xout)", printed(out, "V(xout)"), vh * 1e3 / 4e3, 1e-5),
        check("I(V1)", printed(out, "I(V1)"), -(vh / 3e3 + vh / 4e3), 1e-8),
    ])


def case_rc_step(binary: Path, examples: Path, tmp: Path) -> bool:
    raw = tmp / "rc_step.raw"
    run(binary, examples / "rc_step.sp", "--raw-out", str(raw))
    rows = read_raw(raw, "Transient Analysis")
    at_1ms = next(r for r in rows if abs(r["time"] - 1e-3) < 1e-12)
    # h = 10 us, tau = 1 ms. The first step is backward Euler and the rest
    # trapezoidal: the first step's error, (h/tau)^2 / 2 = 5e-5 decayed by
    # e^-1, is 1.8e-5 at 1 ms, and the trapezoidal steps add about
    # (h/tau)^2 / 12 * e^-1 = 3e-6. Allow 5e-5.
    return check("V(out) at 1 ms", at_1ms["v(out)"], 1.0 - math.exp(-1.0), 5e-5)


def case_rc_discharge(binary: Path, examples: Path, tmp: Path) -> bool:
    text = (examples / "rc_discharge.sp").read_text()
    exact = math.exp(-1.0)
    # h/tau = 0.01. Backward Euler is first order: its error at 1 ms is about
    # (h/tau) / 2 * e^-1 = 1.8e-3. The trapezoidal rule (whose first step is
    # backward Euler) and BDF2 are second order, with errors of the order of
    # (h/tau)^2 = 1e-4 times a constant below one.
    bounds = {"trap": 5e-5, "gear": 1e-4, "euler": 3e-3}
    results = []
    for method, tol in bounds.items():
        deck = tmp / f"rc_discharge_{method}.sp"
        deck.write_text(text.replace("METHOD=trap", f"METHOD={method}"))
        raw = tmp / f"rc_discharge_{method}.raw"
        run(binary, deck, "--raw-out", str(raw))
        last = read_raw(raw, "Transient Analysis")[-1]
        results.append(check(f"V(out) at 1 ms, METHOD={method}", last["v(out)"], exact, tol))
    # Without UIC, .IC holds V(out) at 1 V for the operating point that starts
    # the transient, as in SPICE, so the discharge is the same.
    deck = tmp / "rc_discharge_no_uic.sp"
    deck.write_text(text.replace(" UIC\n", "\n"))
    raw = tmp / "rc_discharge_no_uic.raw"
    run(binary, deck, "--raw-out", str(raw))
    rows = read_raw(raw, "Transient Analysis")
    results.append(check("V(out) at 0 without UIC", rows[0]["v(out)"], 1.0, 1e-9))
    results.append(check("V(out) at 1 ms without UIC", rows[-1]["v(out)"], exact, bounds["trap"]))
    return all(results)


def case_rc_lowpass_ac(binary: Path, examples: Path, tmp: Path) -> bool:
    run(binary, examples / "rc_lowpass_ac.sp", "-o", str(tmp / "rc_lowpass"))
    with open(tmp / "rc_lowpass_ac.csv", newline="") as handle:
        rows = list(csv.DictReader(line for line in handle if not line.startswith("#")))
    row = next(r for r in rows if abs(float(r["frequency"]) - 1e3) < 1e-6)
    h = 1.0 / (1.0 + 2j * math.pi * 1e3 * 1e3 * 1e-6)
    return all([
        check("|V(out)| at 1 kHz [dB]", float(row["v(out)_mag_dB"]), 20 * math.log10(abs(h)), 1e-6),
        check("phase of V(out) at 1 kHz [deg]", float(row["v(out)_phase_deg"]),
              math.degrees(cmath.phase(h)), 1e-6),
    ])


def case_diode_iv(binary: Path, examples: Path, tmp: Path) -> bool:
    raw = tmp / "diode_iv.raw"
    run(binary, examples / "diode_iv.sp", "--raw-out", str(raw))
    vt = BOLTZMANN * KELVIN_27C / CHARGE
    results = []
    for row in read_raw(raw, "DC Sweep"):
        v = row["V1"]
        diode = 1e-14 * (math.exp(v / vt) - 1.0)
        # Relative 1e-6 is far above rounding and far below any model change.
        tol = max(1e-6 * abs(diode), 1e-18)
        results.append(check(f"I(V1) at {v:.2f} V", row["i(V1)"], -diode, tol))
    return all(results)


CASES = {
    "divider": case_divider,
    "params_subckt": case_params_subckt,
    "rc_step": case_rc_step,
    "rc_discharge": case_rc_discharge,
    "rc_lowpass_ac": case_rc_lowpass_ac,
    "diode_iv": case_diode_iv,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--examples", required=True, type=Path)
    parser.add_argument("--case", required=True, choices=sorted(CASES))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        ok = CASES[args.case](args.binary.resolve(), args.examples.resolve(), Path(tmp))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
