#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Checks how the program runs a netlist with .TEMP.

  test_cli_temperature.py --binary build/kirimaspice --case single

- single: .TEMP with one value means .OPTIONS TEMP with that value, as in
  ngspice-47 (which takes one value only): the operating point and the DC
  sweep of a diode at .TEMP 100 equal those at .OPTIONS TEMP=100 and differ
  from 27 C, and the files are written under their own names.
- list: with several values every analysis runs at each of them. The raw file holds one plot per temperature, each equal to a run at
  .OPTIONS TEMP of that value, and the SSF and AC CSV files carry the
  temperature in their names (out_27C.ssf, out_ac_27C.csv).

Each case runs the program in a temporary directory.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

DIODE = """temperature
V1 a 0 DC 0.6
R1 a b 1
D1 b 0 dm
C1 b 0 1n
.model dm D IS=1e-14
{directive}
{analyses}
.end
"""


def run(binary: Path, workdir: Path, name: str, directive: str, analyses: str,
        *args: str) -> str:
    netlist = workdir / f"{name}.cir"
    netlist.write_text(DIODE.format(directive=directive, analyses=analyses))
    done = subprocess.run([str(binary), str(netlist), *args], capture_output=True, text=True,
                          cwd=workdir)
    if done.returncode != 0:
        sys.exit(f"{name}: exit code {done.returncode}\n{done.stdout}{done.stderr}")
    return done.stdout


def printed_currents(stdout: str) -> list[float]:
    return [float(v) for v in re.findall(r"^I\(V1\) = (\S+) A$", stdout, re.M)]


def raw_plots(path: Path) -> list[tuple[str, list[str]]]:
    """The plots of an ASCII raw file: their names and their value lines."""
    plots = []
    for chunk in path.read_text().split("Title:")[1:]:
        name = re.search(r"^Plotname: (.*)$", chunk, re.M).group(1)
        values = chunk.split("Values:\n", 1)[1].split()
        plots.append((name, values))
    return plots


def expect(label: str, ok: bool) -> bool:
    print(f"{'ok  ' if ok else 'FAIL'} {label}")
    return ok


def case_single(binary: Path, workdir: Path) -> bool:
    temp = run(binary, workdir, "temp", ".TEMP 100", ".op", "-o", "temp.ssf",
               "--raw-out", "temp.raw")
    option = run(binary, workdir, "option", ".OPTIONS TEMP=100", ".op")
    room = run(binary, workdir, "room", "", ".op")
    ok = expect("one operating point is printed", len(printed_currents(temp)) == 1)
    ok &= expect(".TEMP 100 gives the current of .OPTIONS TEMP=100",
                 printed_currents(temp) == printed_currents(option))
    ok &= expect("which differs from the current at 27 C",
                 printed_currents(temp) != printed_currents(room))
    ok &= expect("the SSF and raw files are written under their names",
                 (workdir / "temp.ssf").exists() and (workdir / "temp.raw").exists())
    run(binary, workdir, "sweep_temp", ".TEMP 100", ".dc V1 0.5 0.7 0.1", "--raw-out",
        "sweep_temp.raw")
    run(binary, workdir, "sweep_option", ".OPTIONS TEMP=100", ".dc V1 0.5 0.7 0.1",
        "--raw-out", "sweep_option.raw")
    ok &= expect("a DC sweep under .TEMP 100 equals one under .OPTIONS TEMP=100",
                 raw_plots(workdir / "sweep_temp.raw")[0][1] ==
                 raw_plots(workdir / "sweep_option.raw")[0][1])
    return ok


def case_list(binary: Path, workdir: Path) -> bool:
    analyses = ".dc V1 0.5 0.7 0.1\n.ac dec 1 1k 10k"
    out = run(binary, workdir, "list", ".TEMP 27 100", analyses, "-o", "out.ssf",
              "--raw-out", "list.raw")
    ok = expect("a header per temperature is printed",
                re.findall(r"^=== Temperature (\S+) °C ===$", out, re.M) == ["27", "100"])
    plots = raw_plots(workdir / "list.raw")
    ok &= expect("the raw file holds a DC sweep per temperature",
                 [name for name, _ in plots] == ["DC Sweep", "DC Sweep"])
    for index, celsius in enumerate(["27", "100"]):
        run(binary, workdir, f"at{celsius}", f".OPTIONS TEMP={celsius}", analyses,
            "--raw-out", f"at{celsius}.raw")
        ok &= expect(f"the sweep at {celsius} C equals one under .OPTIONS TEMP={celsius}",
                     len(plots) > index and
                     plots[index][1] == raw_plots(workdir / f"at{celsius}.raw")[0][1])
        ok &= expect(f"out_ac_{celsius}C.csv is written",
                     (workdir / f"out_ac_{celsius}C.csv").exists())
    ok &= expect("no AC CSV without a temperature is written",
                 not (workdir / "out_ac.csv").exists())

    run(binary, workdir, "oplist", ".TEMP 27 100", ".op", "-o", "op.ssf")
    ok &= expect("op_27C.ssf and op_100C.ssf are written",
                 (workdir / "op_27C.ssf").exists() and (workdir / "op_100C.ssf").exists())
    return ok


CASES = {"single": case_single, "list": case_list}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--case", choices=sorted(CASES), required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        ok = CASES[args.case](args.binary.resolve(), Path(tmp))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
