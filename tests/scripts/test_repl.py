#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Regression test for tools/kirimaspice_repl.py (non-interactive backend)."""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.kirimaspice_repl import KirimaspiceRepl


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--netlist", required=True)
    args = parser.parse_args()

    repl = KirimaspiceRepl(Path(args.binary).resolve())
    repl.cmd_load(args.netlist)

    with tempfile.TemporaryDirectory() as tmpdir:
        raw_path = Path(tmpdir) / "repl_test.raw"
        repl.state.raw_path = raw_path
        repl.run_command("")  # run with default arguments
        assert raw_path.exists(), "REPL did not generate RAW output"
        assert repl.state.last_results, "REPL failed to parse RAW output"
        first = repl.state.last_results[0]
        assert "time" in first, "time axis missing from REPL output"
        assert any(key.startswith("v(") for key in first), "Voltage waveform missing from REPL output"
        vout = [row["v(out)"] for row in repl.state.last_results if "v(out)" in row]
        assert vout, "v(out) samples missing"
        print(f"REPL produced {len(vout)} v(out) samples")


if __name__ == "__main__":
    main()
