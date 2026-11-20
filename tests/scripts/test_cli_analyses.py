#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""The program runs the analyses the parser read, and the operating point,
as for .OP, when the netlist has no analysis line (main.cpp's run_analyses).
ngspice-47 runs the transient of the continued .TRAN below (run on
2026-11-20)."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = None
CIRCUIT = "t\nV1 in 0 PULSE(0 1 0 1u 1u 1m 2m)\nR1 in out 1k\nC1 out 0 1n\n"


class CliAnalyses(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)

    def run_deck(self, text):
        deck = self.directory / "circuit.cir"
        deck.write_text(text)
        raw = self.directory / "out.raw"
        result = subprocess.run([str(BINARY), str(deck), "--raw-out", str(raw)],
                                cwd=self.directory, capture_output=True, text=True, timeout=60)
        plots = []
        if raw.exists():
            plots = [line.split(":", 1)[1].strip() for line in raw.read_text().splitlines()
                     if line.startswith("Plotname:")]
        return result, plots

    def test_continued_tran_runs_the_transient(self):
        result, plots = self.run_deck(CIRCUIT + ".tran\n+ 10u 1m\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(plots, ["Transient Analysis"])

    def test_tran_in_a_comment_runs_nothing(self):
        result, plots = self.run_deck(CIRCUIT + "* it ran .tran 1n 1u once\n.op\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(plots, ["DC Operating Point"])

    def test_no_analysis_line_runs_the_operating_point_as_op(self):
        # The output files are written, and the currents printed, as for .OP
        result, plots = self.run_deck("t\nV1 in 0 1\nR1 in out 1k\nR2 out 0 1k\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(plots, ["DC Operating Point"])
        self.assertIn("I(V1) = -0.0005 A", result.stdout)

    def test_ac_from_zero_hertz_is_run_and_fails(self):
        # A decade sweep from 0 Hz has no points (log 0); the .AC line is
        # still an analysis line, so the program reports the failure
        result, plots = self.run_deck(CIRCUIT + ".ac dec 10 0 1k\n.end\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertNotIn("No analysis specified", result.stdout)

    def test_directive_the_program_does_not_run_is_named(self):
        result, plots = self.run_deck("t\nV1 in 0 1\nR1 in 0 1k\n.tf v(in) V1\n.op\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(".TF is read, but this program does not run it", result.stdout + result.stderr)

    def test_options_alone_is_no_analysis_line(self):
        result, plots = self.run_deck("t\nV1 in 0 1\nR1 in 0 1k\n.options reltol=1e-4\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(plots, ["DC Operating Point"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args, rest = parser.parse_known_args()
    BINARY = Path(args.binary).resolve()
    unittest.main(argv=[sys.argv[0], *rest])
