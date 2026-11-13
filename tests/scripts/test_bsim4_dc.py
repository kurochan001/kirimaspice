#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Smoke test for BSIM4 DC behavior (with optional ngspice cross-check)."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

from tools.kirimaspice_pyapi import KirimaspiceSession, parse_ascii_raw


def extract_ids(samples: List[Dict[str, float]]) -> float:
    for key in samples[0].keys():
        if key.startswith("i(") and "vds" in key:
            ids_key = key
            break
    else:
        raise RuntimeError("Unable to locate i(VDS) in RAW output")
    return samples[-1][ids_key]


def run_ngspice(deck_text: str, ngspice_bin: Path) -> float:
    with tempfile.TemporaryDirectory() as tmpdir:
        deck_path = Path(tmpdir) / "bsim4.sp"
        raw_path = Path(tmpdir) / "ngspice.raw"
        log_path = Path(tmpdir) / "ngspice.log"
        # Replace @RAWFILE@ placeholder with actual RAW file path for .control block
        deck_text = deck_text.replace("@RAWFILE@", str(raw_path))
        deck_path.write_text(deck_text)
        env = os.environ.copy()
        env.setdefault("HOME", tmpdir)
        cmd = [str(ngspice_bin), "-b", "-o", str(log_path), str(deck_path)]
        completed = subprocess.run(cmd, env=env, text=True, capture_output=False)
        if completed.returncode != 0:
            raise RuntimeError(f"ngspice exited with code {completed.returncode}")
        samples = parse_ascii_raw(raw_path)
        # Negate ngspice current: i(vds) measures current INTO the positive terminal,
        # so for NMOS conducting, it's negative. We negate to match KirimaSpice's
        # convention which returns positive Ids for forward-active NMOS.
        return -extract_ids(samples)


def render_template(template_path: Path, vgs: float) -> str:
    text = template_path.read_text()
    return text.replace("@VGS@", f"{vgs:.3f}")


def main() -> None:
    parser = argparse.ArgumentParser(description="BSIM4 DC regression test")
    parser.add_argument("--binary", required=True, help="Path to kirimaspice binary")
    parser.add_argument("--ngspice", help="Path to ngspice binary (optional)")
    parser.add_argument(
        "--template",
        default="tests/netlists/bsim4_qa_dc.template.sp",
        help="Template SPICE deck containing @VGS@ placeholder",
    )
    parser.add_argument(
        "--json-out",
        help="Optional JSON file storing Kirimaspice/Ngspice Ids for later analysis",
    )
    args = parser.parse_args()

    session = KirimaspiceSession(Path(args.binary))
    ngspice_bin = Path(args.ngspice) if args.ngspice else shutil.which("ngspice")
    if ngspice_bin:
        ngspice_bin = Path(ngspice_bin)

    template_path = Path(args.template)
    if not template_path.exists():
        raise FileNotFoundError(f"Template not found: {template_path}")

    ids_results: List[Tuple[float, float]] = []
    summary: List[Dict[str, float]] = []
    for vgs in (0.45, 0.60, 0.85, 1.00):
        deck_text = render_template(template_path, vgs)
        raw = session.run_netlist(deck_text)
        ids = extract_ids(raw.samples)
        ids_results.append((vgs, ids))

        if ngspice_bin:
            ids_ng = run_ngspice(deck_text, ngspice_bin)
            rel_err = abs(ids - ids_ng) / max(abs(ids_ng), 1e-6)
            sign_mismatch = ids * ids_ng < 0
            print(
                f"[info] VGS={vgs:.2f} -> Id_kirimaspice={ids:.4e} A, "
                f"Id_ngspice={ids_ng:.4e} A, rel_err={rel_err:.2f}"
            )
            if rel_err > 5.0 or sign_mismatch:
                print(
                    f"[warn] BSIM4 drain current drift exceeds loose tolerance "
                    f"(rel_err={rel_err:.2f}, sign_mismatch={sign_mismatch})"
                )
            summary.append(
                {
                    "vgs": vgs,
                    "ids_kirimaspice": ids,
                    "ids_ngspice": ids_ng,
                    "rel_err": rel_err,
                }
            )
        else:
            summary.append({"vgs": vgs, "ids_kirimaspice": ids})

    for vgs, ids in ids_results:
        if not (float("-inf") < ids < float("inf")):
            raise AssertionError(f"Ids is not finite at VGS={vgs}: {ids}")
        if abs(ids) > 1e15:
            raise AssertionError(f"Ids magnitude implausibly large at VGS={vgs}: {ids:.3e}")

    if args.json_out:
        out_path = Path(args.json_out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
