#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Validate that tools/ngspice_shim.py runs a deck and emits log/raw artifacts.

A second case runs a deck whose analyses write no raw file (AC alone): the
shim must not leave the raw file of an earlier run in place of the one asked
for, and must exit non-zero.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", help="directory for the log and raw files "
                        "(default: a temporary directory)")
    parser.add_argument("--binary", type=Path, help="the kirimaspice program to run "
                        "(default: the shim's, build/kirimaspice of the source tree)")
    args = parser.parse_args()
    if args.out_dir is None:
        with tempfile.TemporaryDirectory() as tmpdir:
            return run_all(Path(tmpdir), args.binary)
    return run_all(Path(args.out_dir), args.binary)


def run_all(bench_dir: Path, binary: Path | None) -> int:
    for case in (run, run_without_raw_output):
        rc = case(bench_dir, binary)
        if rc != 0:
            return rc
    return 0


def run(bench_dir: Path, binary: Path | None = None) -> int:
    repo = Path(__file__).resolve().parents[2]
    shim = repo / "tools" / "ngspice_shim.py"
    deck = repo / "tests" / "netlists" / "repl_rc.sp"
    bench_dir.mkdir(parents=True, exist_ok=True)

    log_path = bench_dir / "test_shim.log"
    raw_path = bench_dir / "test_shim.raw"
    for path in (log_path, raw_path):
        if path.exists():
            path.unlink()

    cmd = ["python3", str(shim), "-b", "-o", str(log_path), "-r", str(raw_path), str(deck)]
    if binary is not None:
        cmd[2:2] = ["--binary", str(binary.resolve())]
    result = subprocess.run(cmd, cwd=repo, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        return result.returncode

    if not log_path.exists() or not raw_path.exists():
        print("shim did not create expected log/raw files", file=sys.stderr)
        return 1
    log_text = log_path.read_text(encoding="utf-8")
    if deck.name not in log_text:
        print("log file missing deck header (shim did not record metadata)", file=sys.stderr)
        return 1
    raw_text = raw_path.read_text(encoding="utf-8")
    if "Variables:" not in raw_text:
        print("RAW file missing Variables header", file=sys.stderr)
        return 1
    return 0


def run_without_raw_output(bench_dir: Path, binary: Path | None = None) -> int:
    """An AC-only deck writes no raw file; a stale one must not pass for it."""
    repo = Path(__file__).resolve().parents[2]
    shim = repo / "tools" / "ngspice_shim.py"
    bench_dir.mkdir(parents=True, exist_ok=True)
    deck = bench_dir / "shim_ac_only.cir"
    deck.write_text("* AC alone: no analysis that writes a raw file\n"
                    "V1 in 0 AC 1\n"
                    "R1 in out 1k\n"
                    "C1 out 0 1u\n"
                    ".ac dec 5 10 1k\n"
                    ".end\n", encoding="utf-8")
    raw_path = bench_dir / "shim_ac_only.raw"
    raw_path.write_text("the raw file of an earlier run\n", encoding="utf-8")

    cmd = ["python3", str(shim), "-b", "-r", str(raw_path), str(deck)]
    if binary is not None:
        cmd[2:2] = ["--binary", str(binary.resolve())]
    result = subprocess.run(cmd, cwd=repo, capture_output=True, text=True)
    if result.returncode == 0:
        print("shim exited 0 although the deck wrote no raw file", file=sys.stderr)
        return 1
    if raw_path.exists():
        print("shim left the raw file of an earlier run in place", file=sys.stderr)
        return 1
    if "no raw file" not in result.stderr:
        sys.stderr.write(result.stderr)
        print("shim did not say that no raw file was written", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
