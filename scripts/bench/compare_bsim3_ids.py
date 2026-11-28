#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Generate Id-Vds comparison tables for BSIM3 NMOS using KirimaSpice and ngspice.

It expects:
  - build/kirimaspice   : KirimaSpice binary
  - ngspice             : ngspice in PATH
  - KIRIMASPICE_MODEL_BSIM3  : env var or default path to KirimaSpice BSIM3 model lib
  - NGSPICE_MODEL_BSIM3 : env var or default path to ngspice BSIM3 model lib

Both model libraries should define a common NMOS model name (default: BSIM3N_REF)
that is valid for both simulators.
"""

import csv
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KIRIMASPICE = ROOT / "build" / "kirimaspice"
NGSPICE_BIN = shutil.which("ngspice")

# Model libraries (can be overridden via env)
KIRIMASPICE_MODEL = Path(
    os.environ.get(
        "KIRIMASPICE_BSIM3_MODEL",
        ROOT / "examples" / "semiconductors" / "models" / "bsim3_ngspice_ref.lib",
    )
)
NGSPICE_MODEL = Path(
    os.environ.get(
        "NGSPICE_BSIM3_MODEL",
        ROOT / "examples" / "semiconductors" / "models" / "bsim3_ngspice_ref.lib",
    )
)

BENCH_DIR = ROOT / "bench_out"
BENCH_DIR.mkdir(exist_ok=True)
CSV_PATH = BENCH_DIR / "bsim3_nmos_id_vds_compare.csv"

ROUND_DIGITS = 6

# Sweep configuration (can be tuned later)
VGS_VALUES = [0.5, 1.0, 1.5, 2.0, 2.5]
VDS_START = 0.0
VDS_STOP = 5.0
VDS_STEP = 0.5

# Common model name used in both libraries
MODEL_NAME = "BSIM3N_REF"

KIRIMASPICE_TEMPLATE = """* BSIM3 NMOS Id-Vds sweep (KirimaSpice)
.option method=gear2 reltol=1e-4 gmin=1e-12 temp=27
.include "{model}"

VGS gate 0 {vgs}
VDS drain 0 0
M1 drain gate 0 0 {model_name} W=10u L=1u

.dc VDS {vds_start} {vds_stop} {vds_step}
.save dc v(drain) i(VDS)
.end
"""

NG_TEMPLATE = """* BSIM3 NMOS Id-Vds sweep (ngspice)
.include "{model}"

VGS gate 0 {vgs}
VDS drain 0 0
M1 drain gate 0 0 {model_name} W=10u L=1u

.dc VDS {vds_start} {vds_stop} {vds_step}
.end

.control
set filetype=ascii
run
write {raw} v(drain) i(vds)
quit
.endc
"""


def parse_ascii_raw(path: Path):
    names = []
    values = []
    nvars = None
    npts = 1
    in_vars = False
    in_values = False
    text = path.read_text().splitlines()
    for line in text:
        stripped = line.strip()
        lower = line.lower()
        if lower.startswith("no. variables"):
            try:
                nvars = int(line.split(":")[1])
            except (IndexError, ValueError):
                pass
        elif lower.startswith("no. points"):
            try:
                npts = int(line.split(":")[1])
            except (IndexError, ValueError):
                pass
        elif line.startswith("Variables:"):
            in_vars = True
            continue
        elif line.startswith("Values:"):
            in_vars = False
            in_values = True
            continue

        if in_vars:
            if not stripped:
                continue
            parts = stripped.split()
            if len(parts) >= 2:
                names.append(parts[1].lower())
        elif in_values:
            if not stripped:
                continue
            parts = stripped.replace("\t", " ").split()
            if not parts:
                continue
            token = parts[-1]
            if "," in token:
                token = token.split(",")[0]
            try:
                values.append(float(token))
            except ValueError:
                continue
            if nvars and npts and len(values) >= nvars * npts:
                break

    if nvars is None:
        nvars = len(names)
    if len(values) < nvars * npts:
        raise ValueError("Insufficient data in raw file")

    points = []
    idx = 0
    for _ in range(npts):
        point = {}
        for name in names:
            point[name] = values[idx]
            idx += 1
        points.append(point)
    return points


def extract(point, candidates):
    for key in candidates:
        if key in point:
            return point[key]
    raise KeyError(f"None of {candidates} found in raw point")


def quantize(value: float) -> float:
    return round(value, ROUND_DIGITS)


def invoke_kirimaspice_dc(vgs: float, vds_start: float, vds_stop: float, vds_step: float):
    if not KIRIMASPICE.exists():
        raise FileNotFoundError("Build kirimaspice before running this script.")
    if not KIRIMASPICE_MODEL.exists():
        raise FileNotFoundError(f"Kirimaspice BSIM3 model not found: {KIRIMASPICE_MODEL}")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir) / "kirimaspice_bsim3.sp"
        raw_path = Path(tmpdir) / "kirimaspice_bsim3.raw"
        tmp_path.write_text(
            KIRIMASPICE_TEMPLATE.format(
                model=KIRIMASPICE_MODEL,
                model_name=MODEL_NAME,
                vgs=vgs,
                vds_start=vds_start,
                vds_stop=vds_stop,
                vds_step=vds_step,
            )
        )
        result = subprocess.run(
            [str(KIRIMASPICE), str(tmp_path), "--raw-out", str(raw_path)],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        return parse_ascii_raw(raw_path), result.stdout.decode()


def invoke_ngspice_dc(vgs: float, vds_start: float, vds_stop: float, vds_step: float):
    if NGSPICE_BIN is None:
        raise FileNotFoundError("ngspice binary not found in PATH.")
    if not NGSPICE_MODEL.exists():
        raise FileNotFoundError(f"ngspice BSIM3 model not found: {NGSPICE_MODEL}")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_path = Path(tmpdir) / "ng_bsim3.sp"
        raw_path = Path(tmpdir) / "ng_bsim3.raw"
        tmp_path.write_text(
            NG_TEMPLATE.format(
                model=NGSPICE_MODEL,
                model_name=MODEL_NAME,
                vgs=vgs,
                vds_start=vds_start,
                vds_stop=vds_stop,
                vds_step=vds_step,
                raw=raw_path,
            )
        )
        log_path = Path(tmpdir) / "ng_bsim3.log"
        subprocess.run(
            [NGSPICE_BIN, "-b", str(tmp_path)],
            cwd=ROOT,
            check=True,
            stdout=log_path.open("w"),
            stderr=subprocess.STDOUT,
        )
        return parse_ascii_raw(raw_path)


def main():
    rows = []

    for vgs in VGS_VALUES:
        kirimaspice_pts, _ = invoke_kirimaspice_dc(vgs, VDS_START, VDS_STOP, VDS_STEP)
        ng_pts = invoke_ngspice_dc(vgs, VDS_START, VDS_STOP, VDS_STEP)

        if len(kirimaspice_pts) != len(ng_pts):
            raise RuntimeError(f"Point count mismatch at VGS={vgs}")

        for sp, ng in zip(kirimaspice_pts, ng_pts):
            vds_kirimaspice = extract(sp, ["vds", "v(v-sweep)", "v(vds)", "v(drain)"])
            vds_ng = extract(ng, ["v(v-sweep)", "vds", "v(vds)", "v(drain)"])
            if abs(vds_kirimaspice - vds_ng) > 1e-6:
                raise RuntimeError(f"Sweep mismatch at VGS={vgs}")

            ids_kirimaspice = extract(sp, ["i(vds)"])
            ids_ng = -extract(ng, ["i(vds)"])  # ngspice uses opposite current sign

            rows.append(
                {
                    "vgs": vgs,
                    "vds": vds_kirimaspice,
                    "ids_kirimaspice": ids_kirimaspice,
                    "ids_ngspice": ids_ng,
                    "delta_ids": ids_kirimaspice - ids_ng,
                }
            )

    with CSV_PATH.open("w", newline="") as fh:
        writer = csv.DictWriter(
            fh, fieldnames=["vgs", "vds", "ids_kirimaspice", "ids_ngspice", "delta_ids"]
        )
        writer.writeheader()
        writer.writerows(rows)

    worst_row = max(rows, key=lambda row: abs(row["delta_ids"]))
    max_abs = abs(worst_row["delta_ids"])
    print(f"Wrote {CSV_PATH}")
    print(
        f"Max |ΔIds| = {max_abs:.4e} A at "
        f"VGS={worst_row['vgs']:.2f} V, VDS={worst_row['vds']:.2f} V"
    )


if __name__ == "__main__":
    main()

