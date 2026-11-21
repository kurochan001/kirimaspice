#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""The command line (src/main.cpp): options anywhere, each value as
"<option> <value>" or "<option>=<value>", one netlist; an unknown option,
an option without its value and a second netlist are errors (exit 1), not
ignored; --version prints the version."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = None
DECK = "t\nV1 in 0 1\nR1 in 0 1k\n.op\n.end\n"


class CliOptions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        (self.directory / "deck.cir").write_text(DECK)

    def run_cli(self, *args):
        return subprocess.run([str(BINARY), *args], cwd=self.directory,
                              capture_output=True, text=True, timeout=60)

    def assert_rejected(self, result, text):
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(text, result.stdout + result.stderr)

    def test_unknown_option_is_an_error(self):
        self.assert_rejected(self.run_cli("deck.cir", "--bogus"), "unknown option --bogus")

    def test_option_without_value_is_an_error(self):
        self.assert_rejected(self.run_cli("deck.cir", "-o"), "-o requires a value")
        self.assert_rejected(self.run_cli("deck.cir", "--raw-out="), "--raw-out requires a value")

    def test_second_netlist_is_an_error(self):
        self.assert_rejected(self.run_cli("deck.cir", "deck.cir"), "more than one netlist")

    def test_no_netlist_is_an_error(self):
        self.assert_rejected(self.run_cli("--raw-out", "x.raw"), "no netlist given")

    def test_options_before_the_netlist_and_with_equals(self):
        result = self.run_cli("--raw-out=a.raw", "--raw-binary-out", "b.raw", "deck.cir")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.directory / "a.raw").exists())
        self.assertTrue((self.directory / "b.raw").exists())

    def test_backend_options_are_gone(self):
        # KLU is the one working solver; the program has no option for another
        for option in ["--solver-backend=KLU", "--strict-backend", "--superlu-dist-grid=2x2"]:
            self.assert_rejected(self.run_cli("deck.cir", option), "unknown option " + option)

    def test_environment_does_not_choose_the_solver(self):
        env = dict(os.environ, KIRIMASPICE_SOLVER_BACKEND="SUPERLU_DIST", KIRIMASPICE_SOLVER_STRICT="1")
        result = subprocess.run([str(BINARY), "deck.cir"], cwd=self.directory, env=env,
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_results_on_stdout_and_messages_on_stderr(self):
        result = self.run_cli("deck.cir")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("V(in) = 1 V", result.stdout)
        self.assertNotIn("kirimaspice: ", result.stdout)
        self.assertIn("kirimaspice: info: ", result.stderr)
        self.assertNotIn("V(in) = 1 V", result.stderr)

    def test_log_level_leaves_out_lower_levels(self):
        result = self.run_cli("--log-level", "warning", "deck.cir")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("kirimaspice: info: ", result.stderr)
        self.assertIn("V(in) = 1 V", result.stdout)
        self.assert_rejected(self.run_cli("--log-level=loud", "deck.cir"), "--log-level must be")

    def test_exit_codes_are_those_of_kirimasim(self):
        # 0 finished, 1 an analysis failed, 2 an error of the command line
        # or the input, 3 an internal error (kirimasim's docs/spec/CLI.md 5)
        (self.directory / "parse.cir").write_text("t\nV1 a 0 1\nW1 a 0 1k\n.end\n")
        (self.directory / "fails.cir").write_text("t\nV1 in 0 1\nV2 in 0 2\nR1 in 0 1k\n.end\n")
        cases = [
            ((), 0),
            (("--bogus",), 2),
            (("missing.cir",), 2),
            (("parse.cir",), 2),
            (("--raw-out", str(self.directory / "no" / "such" / "dir.raw")), 2),
            (("fails.cir",), 1),
        ]
        for args, code in cases:
            with self.subTest(args=args):
                cli = [*args] if args and args[0].endswith(".cir") else ["deck.cir", *args]
                result = self.run_cli(*cli)
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)

    def test_version(self):
        result = self.run_cli("--version")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertRegex(result.stdout, r"^kirimaspice \d+\.\d+\.\d+\n$")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args, rest = parser.parse_known_args()
    BINARY = Path(args.binary).resolve()
    unittest.main(argv=[sys.argv[0], *rest])
