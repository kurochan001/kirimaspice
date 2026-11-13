#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Regression test for tools/kirimaspice_pyapi.py."""

from __future__ import annotations

import argparse
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.kirimaspice_pyapi import RawResult, KirimaspiceSession


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--netlist", required=True)
    args = parser.parse_args()

    binary = Path(args.binary).resolve()
    deck_path = Path(args.netlist).resolve()
    deck_text = deck_path.read_text()

    session = KirimaspiceSession(binary)
    result: RawResult = session.run_netlist(deck_text)

    signals = list(result.signals())
    assert signals, "No signals returned from RAW"
    assert any(sig.startswith("v(") for sig in signals), f"Expected voltage waveform in {signals}"
    vout = result.series("v(out)")
    time = result.series("time")
    assert len(vout) == len(time) > 0, "time/v(out) lengths mismatch or empty"
    # Basic sanity check: RC response should start near supply voltage
    assert abs(vout[-1] - 5.0) < 1e-6, "Final sample should settle near 5V"
    print(f"Loaded {len(vout)} samples with signals: {', '.join(signals)}")


if __name__ == "__main__":
    main()
