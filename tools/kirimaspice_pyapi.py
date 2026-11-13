#!/usr/bin/env python3
"""Lightweight Python API for running KirimaSpice and accessing waveforms."""
from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional

try:
    import pandas as pd  # type: ignore
    HAS_PANDAS = True
except Exception:  # pragma: no cover
    HAS_PANDAS = False


def parse_ascii_raw(path: Path) -> List[Dict[str, float]]:
    # Try to read as text first, handle binary RAW files gracefully
    try:
        text = path.read_text(encoding='utf-8', errors='strict').splitlines()
    except UnicodeDecodeError:
        # File is binary, try to read header in binary mode
        try:
            with open(path, 'rb') as f:
                header_bytes = f.read(4096)  # Read first 4KB for header
                header = header_bytes.decode('utf-8', errors='ignore')
                if 'Binary:' in header or 'binary' in header.lower():
                    raise ValueError(
                        f"Binary RAW file detected: {path}\n"
                        f"ngspice produced binary format. Please use 'set wr_singlescale' and "
                        f"'set wr_vecnames' options to force ASCII output, or implement binary RAW parser."
                    )
                else:
                    raise ValueError(f"Unknown binary file format: {path}")
        except Exception as e:
            raise ValueError(f"Failed to read RAW file {path}: {e}")

    names: List[str] = []
    values: List[float] = []
    nvars = None
    npts = 1
    in_vars = False
    in_values = False
    for line in text:
        stripped = line.strip()
        lower = line.lower()
        if lower.startswith("no. variables"):
            nvars = int(line.split(":")[1])
        elif lower.startswith("no. points"):
            npts = int(line.split(":")[1])
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
    if not names:
        raise ValueError("RAW file contains no variable metadata")
    step = len(names)
    rows: List[Dict[str, float]] = []
    for idx in range(0, len(values), step):
        row = {names[offset]: values[idx + offset] for offset in range(step)}
        rows.append(row)
    return rows


@dataclass
class RawResult:
    """Structured access to RAW samples."""

    samples: List[Dict[str, float]]

    def signals(self) -> Iterable[str]:
        if not self.samples:
            return []
        return self.samples[0].keys()

    def series(self, name: str) -> List[float]:
        if not self.samples or name not in self.samples[0]:
            raise KeyError(f"signal '{name}' not present in RAW")
        return [row[name] for row in self.samples]

    def to_dataframe(self):  # pragma: no cover - optional dependency
        if not HAS_PANDAS:
            raise RuntimeError("pandas is required for DataFrame conversion")
        return pd.DataFrame(self.samples)


class KirimaspiceSession:
    """Helper to run KirimaSpice from Python and capture RAW output."""

    def __init__(self, binary: Optional[Path] = None):
        if binary is None:
            binary = Path(__file__).resolve().parents[1] / "build" / "kirimaspice"
        self.binary = Path(binary)
        if not self.binary.exists():
            raise FileNotFoundError(f"kirimaspice binary not found at {self.binary}")

    def run_netlist(self, netlist_text: str, extra_args: Optional[List[str]] = None) -> RawResult:
        extra_args = extra_args or []
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            deck = tmp_path / "circuit.sp"
            deck.write_text(netlist_text)
            raw_path = tmp_path / "out.raw"
            cmd = [str(self.binary), str(deck), "--raw-out", str(raw_path)] + extra_args
            env = os.environ.copy()
            env.setdefault("OMP_NUM_THREADS", "1")
            completed = subprocess.run(cmd, env=env, text=True)
            if completed.returncode != 0:
                raise RuntimeError(f"kirimaspice exited with code {completed.returncode}")
            if not raw_path.exists():
                raise FileNotFoundError("RAW output was not generated")
            samples = parse_ascii_raw(raw_path)
            return RawResult(samples)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run KirimaSpice from Python and dump RAW as CSV")
    parser.add_argument("netlist", help="path to SPICE deck")
    parser.add_argument("--binary", default=str((Path(__file__).resolve().parents[1] / "build" / "kirimaspice")))
    parser.add_argument("--csv", help="optional CSV export path" )
    args = parser.parse_args()

    session = KirimaspiceSession(Path(args.binary))
    data = session.run_netlist(Path(args.netlist).read_text())
    if args.csv:
        if not HAS_PANDAS:
            raise RuntimeError("pandas is required for CSV export")
        df = data.to_dataframe()
        df.to_csv(args.csv, index=False)
        print(f"Wrote {args.csv} with columns: {', '.join(df.columns)}")
    else:
        print("Signals:", ", ".join(data.signals()))
        time_key = next((k for k in data.signals() if k in {"time", "frequency", "v(v-sweep)"}), None)
        if not time_key:
            time_key = next(iter(data.signals()))
        first_sig = next(iter(data.signals()))
        times = data.series(time_key)
        vals = data.series(first_sig)
        print("First 10 samples:")
        for t, v in list(zip(times, vals))[:10]:
            print(f"  {time_key}={t:.6g}\t{first_sig}={v:.6g}")


if __name__ == "__main__":
    main()
