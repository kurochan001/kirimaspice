#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Exercise command failures, result freshness, and repository scanning."""
import argparse
import contextlib
import io
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools import check_banned, kirimaspice_pyapi, kirimaspice_repl

RAW = "No. Variables: 2\nNo. Points: 1\nVariables:\n0 time time\n1 v(out) voltage\nValues:\n0 0\n2\n"
BINARY = None


class ToolEdgeCases(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)

    def deck(self, analysis=".op"):
        path = self.directory / "circuit.cir"
        path.write_text("Boundary conditions\nV1 in 0 DC 1 AC 1\nR1 in 0 1k\n" + analysis + "\n.end\n")
        return path

    def run_cli(self, analysis=".op", *args):
        env = os.environ.copy()
        env["OMP_NUM_THREADS"] = "2"
        return subprocess.run([str(BINARY), str(self.deck(analysis)), *args],
                              cwd=self.directory, env=env, capture_output=True, text=True, timeout=30)

    def test_invalid_wav_options_return_an_error_without_aborting(self):
        for args in [("--wav-sample-rate", "bad"), ("--wav-sample-rate=0",),
                     ("--wav-sample-rate=123junk",), ("--wav-sample-rate=4294967296",),
                     ("--wav-bits=65552",), ("--wav-bits=12",),
                     ("--wav-bits",), ("--wav-sample-rate",)]:
            with self.subTest(args=args):
                result = self.run_cli(".op", *args)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("Invalid command-line option", result.stdout + result.stderr)

    def test_failed_analyses_return_failure_without_sweep_output(self):
        for analysis in [".options itl1=0", ".dc V1 0 1 0.5\n.options itl1=0",
                         ".op\n.options source_stepping=1 source_steps=0",
                         ".op\n.options gmin_stepping=1 gmin_steps=0"]:
            with self.subTest(analysis=analysis):
                result = self.run_cli(analysis)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertFalse(list(self.directory.glob("*_dc.csv")))

    def test_single_ac_frequency_and_metadata(self):
        result = self.run_cli(".ac lin 1 10 100")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        csv = next(self.directory.glob("*_ac.csv")).read_text()
        data = [line for line in csv.splitlines() if not line.startswith("#")]
        self.assertEqual(float(data[-1].split(",")[0]), 10.0)
        simulator = next(line.removeprefix("# Simulator: ") for line in csv.splitlines()
                         if line.startswith("# Simulator: "))
        self.assertIn(simulator + " - SPICE circuit simulator", result.stdout)

    def test_failed_default_operating_point_returns_failure(self):
        # Contradictory ideal voltage sources have no operating point.
        deck = self.directory / "inconsistent.cir"
        deck.write_text("Default analysis\nV1 in 0 1\nV2 in 0 2\nR1 in 0 1k\n.end\n")
        result = subprocess.run([str(BINARY), str(deck)], cwd=self.directory,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("No analysis specified", result.stdout)

    def test_raw_parser_rejects_truncated_or_complex_data(self):
        raw = self.directory / "data.raw"
        for content in [RAW.replace("Points: 1", "Points: 2"), RAW.rsplit("2\n", 1)[0],
                        RAW.replace("Variables: 2", "Variables: 3"), RAW.replace("0 0\n2", "0 0\n2,3")]:
            raw.write_text(content)
            for parser in [kirimaspice_pyapi.parse_ascii_raw, kirimaspice_repl.parse_ascii_raw]:
                with self.subTest(content=content, parser=parser):
                    with self.assertRaises(ValueError):
                        parser(raw)

    def repl(self):
        with patch.object(kirimaspice_repl, "DEFAULT_SESSION_DIR", self.directory):
            repl = kirimaspice_repl.KirimaspiceRepl(Path("simulator"))
        repl.state.netlist = self.deck()
        repl.state.raw_path = self.directory / "old.raw"
        repl.state.raw_path.write_text(RAW.replace("\n2\n", "\n99\n"))
        repl.state.last_results = [{"v(out)": 99.0}]
        repl.state.derived = {"old": [99.0]}
        return repl

    def test_repl_reads_the_requested_raw_path(self):
        for equals_form in [False, True]:
            repl = self.repl()
            raw = self.directory / "requested data.raw"
            def simulate(*args, **kwargs):
                raw.write_text(RAW)
                return subprocess.CompletedProcess(args[0], 0)
            extra = "--raw-out" + ("=" if equals_form else " ") + shlex.quote(str(raw))
            with patch.object(kirimaspice_repl.subprocess, "run", side_effect=simulate):
                with contextlib.redirect_stdout(io.StringIO()):
                    repl.run_command(extra)
            self.assertEqual(repl.get_series("v(out)"), [2.0])
            self.assertEqual(repl.state.derived, {})

    def test_repl_drops_previous_results_after_failure_or_unchanged_output(self):
        for exit_code in [0, 1]:
            repl = self.repl()
            with patch.object(kirimaspice_repl.subprocess, "run", return_value=subprocess.CompletedProcess([], exit_code)):
                with contextlib.redirect_stdout(io.StringIO()):
                    repl.run_command("")
            self.assertIsNone(repl.state.last_results)
            self.assertEqual(repl.state.derived, {})

    def test_loading_a_deck_clears_previous_results(self):
        repl = self.repl()
        with contextlib.redirect_stdout(io.StringIO()):
            repl.cmd_load(str(self.deck()))
        self.assertIsNone(repl.state.last_results)
        self.assertEqual(repl.state.derived, {})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
