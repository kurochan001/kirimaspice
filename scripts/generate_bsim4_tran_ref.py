# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Generate the inverter-chain reference from the same netlist as the C++ test."""

import argparse
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DECK = ROOT / "validation/bsim4_inv_chain.cir"
sys.path.insert(0, str(ROOT / "tools"))
from kirimaspice_pyapi import parse_ascii_raw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DECK.with_name("bsim4_inv_chain_tran_ref.txt"))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        raw = Path(directory) / "chain.raw"
        subprocess.run(["ngspice", "-n", "-b", "-r", str(raw), str(DECK)], cwd=directory,
                       env={**os.environ, "SPICE_ASCIIRAWFILE": "1"}, check=True)
        rows = parse_ascii_raw(raw)
        # The raw header identifies the ngspice binary that produced these samples.
        with raw.open() as source:
            command = next(line.strip() for line in source if line.startswith("Command:"))
    columns = ("time", "v(vin)", "v(n1)", "v(n2)", "v(n3)")
    if not rows or rows[0]["time"] != 0 or not math.isclose(rows[-1]["time"], 10e-9):
        raise ValueError("Expected the complete 0 to 10 ns inverter-chain transient")
    if any(b["time"] <= a["time"] for a, b in zip(rows, rows[1:])):
        raise ValueError("Expected increasing sample times")
    if any(not math.isfinite(row[name]) for row in rows for name in columns):
        raise ValueError("Nonfinite sample in the inverter-chain transient")
    samples = (" ".join(f"{row[name]:.12e}" for name in columns) for row in rows)
    output = f"# {command}\n# Time Vin V_n1 V_n2 V_n3\n" + "\n".join(samples) + "\n"
    args.output.write_text(output, encoding="ascii")


if __name__ == "__main__":
    main()
