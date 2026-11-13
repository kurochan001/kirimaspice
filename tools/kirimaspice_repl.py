#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Interactive shell for KirimaSpice with lightweight plotting support."""
from __future__ import annotations

import argparse
import os
import readline
import shlex
import subprocess
import sys
import tempfile
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional

try:
    import matplotlib.pyplot as plt  # type: ignore
    HAS_MATPLOTLIB = True
except Exception:  # pragma: no cover - optional dependency
    HAS_MATPLOTLIB = False

try:
    import numpy as np  # type: ignore
    HAS_NUMPY = True
except Exception:  # pragma: no cover
    HAS_NUMPY = False

HISTORY_FILE = Path.home() / ".kirimaspice_history"
DEFAULT_SESSION_DIR = Path("bench_out") / "repl"
DEFAULT_RAW = DEFAULT_SESSION_DIR / "session.raw"
PROMPT = "kirimaspice> "


def load_history() -> None:
    try:
        readline.read_history_file(HISTORY_FILE)
    except FileNotFoundError:
        HISTORY_FILE.touch(exist_ok=True)


def save_history() -> None:
    try:
        readline.write_history_file(HISTORY_FILE)
    except Exception:
        pass


def parse_ascii_raw(path: Path) -> List[Dict[str, float]]:
    """Parse NGspice ASCII RAW file into list of dicts."""
    text = path.read_text().splitlines()
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
        raise ValueError("RAW file contains no variable metadata (Variables section missing).")
    step = len(names)
    if len(values) % step != 0:
        raise ValueError("RAW value count does not match variable list length.")
    rows: List[Dict[str, float]] = []
    for idx in range(0, len(values), step):
        point: Dict[str, float] = OrderedDict()
        for offset, name in enumerate(names):
            point[name] = values[idx + offset]
        rows.append(point)
    return rows


@dataclass
class SessionState:
    netlist: Optional[Path] = None
    raw_path: Path = DEFAULT_RAW
    last_results: Optional[List[Dict[str, float]]] = None
    derived: Dict[str, List[float]] = None  # type: ignore

    def __post_init__(self) -> None:
        if self.derived is None:
            self.derived = {}


class KirimaspiceRepl:
    def __init__(self, binary: Path):
        self.binary = binary
        self.state = SessionState()
        self.vars: Dict[str, str] = {}
        DEFAULT_SESSION_DIR.mkdir(parents=True, exist_ok=True)

    # --------------------- Helpers ---------------------
    def substitute(self, text: str) -> str:
        for key, value in self.vars.items():
            text = text.replace(f"{{{key}}}", value)
        return text

    def run_command(self, extra: str) -> None:
        if not self.state.netlist:
            print("[warn] load a netlist first with: load <path>")
            return
        cmd = [str(self.binary), str(self.state.netlist)]
        extra = self.substitute(extra)
        raw_specified = any(tok.startswith("--raw-out") for tok in shlex.split(extra))
        if not raw_specified:
            cmd.extend(["--raw-out", str(self.state.raw_path)])
        if extra:
            cmd.extend(shlex.split(extra))
        print(f"[info] running: {' '.join(cmd)}")
        try:
            completed = subprocess.run(cmd, text=True)
            if completed.returncode != 0:
                print(f"[error] kirimaspice exited with code {completed.returncode}")
                return
        except FileNotFoundError:
            print(f"[error] simulator binary not found: {self.binary}")
            return
        raw = self.state.raw_path
        if not raw.exists():
            print(f"[warn] RAW file not found at {raw}; cannot plot.")
            return
        try:
            data = parse_ascii_raw(raw)
            self.state.last_results = data
            self.state.derived.clear()
            print(f"[info] loaded {len(data)} points from {raw}")
        except Exception as exc:
            print(f"[warn] failed to parse RAW file: {exc}")

    def ensure_results(self) -> bool:
        if not self.state.last_results:
            print("[warn] no simulation results loaded yet; run a simulation first")
            return False
        return True

    def available_signals(self) -> List[str]:
        if not self.state.last_results:
            return []
        keys = list(self.state.last_results[0].keys())
        keys.extend(sorted(self.state.derived.keys()))
        return keys

    def get_series(self, name: str) -> Optional[List[float]]:
        if self.state.derived and name in self.state.derived:
            return self.state.derived[name]
        if not self.state.last_results:
            return None
        if name not in self.state.last_results[0]:
            return None
        return [point[name] for point in self.state.last_results]

    # --------------------- Commands ---------------------
    def cmd_load(self, arg: str) -> None:
        if not arg:
            print("usage: load <netlist.cir>")
            return
        path = Path(arg).expanduser().resolve()
        if not path.exists():
            print(f"[error] netlist not found: {path}")
            return
        self.state.netlist = path
        print(f"[info] netlist set to {path}")

    def cmd_run(self, arg: str) -> None:
        self.run_command(arg)

    def cmd_plot(self, arg: str) -> None:
        if not self.ensure_results():
            return
        tokens = shlex.split(arg)
        if not tokens:
            print("usage: plot <signal1> [signal2 ...] [--x axis]")
            return
        x_name = None
        signals: List[str] = []
        i = 0
        while i < len(tokens):
            if tokens[i] == "--x" and i + 1 < len(tokens):
                x_name = tokens[i + 1]
                i += 2
            else:
                signals.append(tokens[i])
                i += 1
        if not signals:
            print("[warn] specify at least one signal to plot")
            return
        if not x_name:
            candidates = ["time", "frequency", "v(v-sweep)"]
            for cand in candidates:
                if cand in self.state.last_results[0]:
                    x_name = cand
                    break
            if not x_name:
                x_name = next(iter(self.state.last_results[0].keys()))
        x_series = self.get_series(x_name)
        if x_series is None:
            print(f"[warn] x-axis '{x_name}' not found in dataset")
            return
        plotted = False
        if HAS_MATPLOTLIB:
            for sig in signals:
                y = self.get_series(sig)
                if y is None:
                    print(f"[warn] signal '{sig}' not found")
                    continue
                plt.plot(x_series, y, label=sig)
                plotted = True
            if plotted:
                plt.xlabel(x_name)
                plt.ylabel("value")
                plt.legend()
                plt.grid(True, linestyle='--', alpha=0.4)
                plt.show()
                return
        # fallback ASCII summary
        print("[info] matplotlib unavailable; showing first 10 samples")
        for sig in signals:
            y = self.get_series(sig)
            if y is None:
                print(f"  - missing signal: {sig}")
                continue
            print(f"  {sig} (first 10 points):")
            for xs, ys in list(zip(x_series, y))[:10]:
                print(f"    {x_name}={xs:.6g}\t{sig}={ys:.6g}")

    def cmd_list(self, _arg: str) -> None:
        signals = self.available_signals()
        if not signals:
            print("[info] no signals available yet")
            return
        print("Available signals:")
        for name in signals:
            print(f"  - {name}")

    def cmd_let(self, arg: str) -> None:
        if not arg:
            print("usage: let <name> <value>")
            return
        parts = arg.split(None, 1)
        if len(parts) != 2:
            print("usage: let <name> <value>")
            return
        key, value = parts
        self.vars[key] = value
        print(f"[info] set {key} = {value}")

    def cmd_vars(self, _arg: str) -> None:
        if not self.vars:
            print("[info] no variables defined")
            return
        for key, value in self.vars.items():
            print(f"  {key} = {value}")

    def cmd_shell(self, arg: str) -> None:
        if not arg:
            print("usage: shell <command>")
            return
        subprocess.run(arg, shell=True)

    def cmd_open(self, _arg: str) -> None:
        if not self.state.netlist:
            print("[warn] load a netlist first")
            return
        editor = os.environ.get("EDITOR", "nano")
        subprocess.run([editor, str(self.state.netlist)])

    def cmd_recent(self, _arg: str) -> None:
        print(f"netlist: {self.state.netlist or '-'}")
        print(f"raw: {self.state.raw_path}")

    def cmd_expr(self, arg: str) -> None:
        if not HAS_NUMPY:
            print("[warn] numpy is required for expr command")
            return
        if not self.ensure_results():
            return
        if "=" not in arg:
            print("usage: expr <name> = <python expression>")
            return
        name, expr = arg.split("=", 1)
        name = name.strip()
        expr = expr.strip()
        context: Dict[str, np.ndarray] = {}
        for key in self.available_signals():
            series = self.get_series(key)
            if series is not None:
                context[key.replace("(", "_").replace(")", "")] = np.array(series)
        try:
            result = eval(expr, {"np": np, "__builtins__": {}}, context)
        except Exception as exc:
            print(f"[error] failed to evaluate expression: {exc}")
            return
        if isinstance(result, np.ndarray):
            self.state.derived[name] = result.tolist()
            print(f"[info] stored derived signal '{name}' with {len(result)} samples")
        else:
            print(f"[info] expression value: {result}")

    def cmd_help(self, _arg: str) -> None:
        print("""
Commands:
  load <file>          load a SPICE netlist
  run [args]           run kirimaspice (args appended, variables {var} expanded)
  plot [--x name] s..  plot one or more signals from last RAW
  list                 list available signals
  expr name = EXPR     define derived waveform using numpy syntax
  let k v              set scalar variable for substitution
  vars                 list variables
  shell <cmd>          run external shell command
  open                 open current netlist in $EDITOR (default nano)
  recent               show current netlist/raw paths
  help                 show this message
  quit / exit          leave the shell
""")

    # --------------------- Loop ---------------------
    def loop(self) -> None:
        commands = {
            "load": self.cmd_load,
            "run": self.cmd_run,
            "plot": self.cmd_plot,
            "list": self.cmd_list,
            "let": self.cmd_let,
            "vars": self.cmd_vars,
            "shell": self.cmd_shell,
            "open": self.cmd_open,
            "recent": self.cmd_recent,
            "expr": self.cmd_expr,
            "help": self.cmd_help,
            "?": self.cmd_help,
        }
        load_history()
        print("Type 'help' for available commands. Use {var} inside commands for substitution.")
        try:
            while True:
                try:
                    line = input(PROMPT)
                except EOFError:
                    print()
                    break
                text = line.strip()
                if not text:
                    continue
                if text in {"quit", "exit"}:
                    break
                tokens = shlex.split(text)
                cmd = tokens[0]
                arg = text[len(cmd):].strip()
                handler = commands.get(cmd)
                if handler:
                    handler(arg)
                else:
                    print(f"[warn] unknown command: {cmd}")
        finally:
            save_history()


def main() -> None:
    parser = argparse.ArgumentParser(description="Interactive KirimaSpice shell")
    parser.add_argument("netlist", nargs="?", help="optional netlist to load at startup")
    parser.add_argument("--binary", default=str((Path(__file__).resolve().parents[1] / "build" / "kirimaspice")),
                        help="path to kirimaspice executable")
    args = parser.parse_args()
    repl = KirimaspiceRepl(Path(args.binary).resolve())
    if args.netlist:
        repl.cmd_load(args.netlist)
    repl.loop()


if __name__ == "__main__":
    main()
