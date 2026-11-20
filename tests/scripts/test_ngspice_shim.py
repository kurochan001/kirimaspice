#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Validate that tools/ngspice_shim.py runs a deck and emits log/raw artifacts."""
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
    args = parser.parse_args()
    if args.out_dir is None:
        with tempfile.TemporaryDirectory() as tmpdir:
            return run(Path(tmpdir))
    return run(Path(args.out_dir))


def run(bench_dir: Path) -> int:
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


if __name__ == "__main__":
    raise SystemExit(main())
