#!/usr/bin/env python3
"""Minimal ngspice CLI shim that routes to KirimaSpice for GUI integrations."""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = REPO_ROOT / "build" / "kirimaspice"


@dataclass
class ShimArgs:
    binary: Path
    raw: Path | None
    log: Path | None
    netlists: List[Path]
    leftover: Sequence[str]
    verbose: bool


def parse_cli(argv: Sequence[str]) -> ShimArgs:
    parser = argparse.ArgumentParser(
        description=(
            "Drop-in replacement for the ngspice CLI subset used by KiCad/Qucs-S. "
            "It accepts -b/-r/-o flags and forwards netlists to KirimaSpice."
        )
    )
    parser.add_argument("-b", "--batch", action="store_true", help="Ignored (KirimaSpice runs batch-only).")
    parser.add_argument("-o", "--log", type=Path, help="Write combined stdout/stderr to this log file.")
    parser.add_argument("-r", "--raw", type=Path, help="Path to SPICE3 RAW output (passed via --raw-out).")
    parser.add_argument("--binary", type=Path, help="Override KirimaSpice binary path.")
    parser.add_argument(
        "--verbose", action="store_true", help="Print shim diagnostics (command routing, paths, etc.)."
    )
    parser.add_argument(
        "netlists",
        nargs="+",
        type=Path,
        help="One or more SPICE decks (typically emitted by KiCad/Qucs-S).",
    )
    args, passthrough = parser.parse_known_args(argv)
    binary = args.binary or Path(os.environ.get("KIRIMASPICE_BIN", DEFAULT_BINARY))
    return ShimArgs(
        binary=binary,
        raw=args.raw,
        log=args.log,
        netlists=[path.resolve() for path in args.netlists],
        leftover=passthrough,
        verbose=args.verbose,
    )


def ensure_binary(path: Path) -> None:
    if not path.exists():
        raise FileNotFoundError(f"KirimaSpice binary not found: {path}")
    if not os.access(path, os.X_OK):
        raise PermissionError(f"KirimaSpice binary is not executable: {path}")


def prepare_raw_path(base: Path | None, deck: Path, multi: bool) -> Path:
    if base is None:
        return deck.with_suffix(".raw")
    if not multi:
        return base
    suffix = base.suffix or ".raw"
    stem = f"{base.stem}_{deck.stem}"
    return base.with_name(stem + suffix)


def write_log(log_path: Path, header: str, stdout: str, stderr: str) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as handle:
        handle.write(header + "\n")
        if stdout:
            handle.write(stdout)
            if not stdout.endswith("\n"):
                handle.write("\n")
        if stderr:
            handle.write(stderr)
            if not stderr.endswith("\n"):
                handle.write("\n")


def run_deck(binary: Path, deck: Path, raw: Path, verbose: bool) -> subprocess.CompletedProcess:
    raw.parent.mkdir(parents=True, exist_ok=True)
    cmd: List[str] = [str(binary), str(deck), "--raw-out", str(raw)]
    if verbose:
        print(f"[shim] running {' '.join(cmd)}", file=sys.stderr)
    return subprocess.run(cmd, capture_output=True, text=True)


def main(argv: Sequence[str]) -> int:
    try:
        cfg = parse_cli(argv)
        ensure_binary(cfg.binary)
    except Exception as exc:
        print(f"[shim] {exc}", file=sys.stderr)
        return 2

    if cfg.leftover:
        print(f"[shim] ignoring unsupported args: {' '.join(cfg.leftover)}", file=sys.stderr)

    rc = 0
    multi = len(cfg.netlists) > 1
    for deck in cfg.netlists:
        if not deck.exists():
            print(f"[shim] netlist not found: {deck}", file=sys.stderr)
            rc = 2
            continue
        raw = prepare_raw_path(cfg.raw, deck, multi)
        result = run_deck(cfg.binary, deck, raw, cfg.verbose)
        header = f"[shim] deck={deck.name} raw={raw} exit={result.returncode}"
        if cfg.log:
            write_log(cfg.log, header, result.stdout, result.stderr)
        else:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
        if result.returncode != 0:
            rc = result.returncode
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
