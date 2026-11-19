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
from tools import check_banned, kirimaspice_pyapi, kirimaspice_repl, local_ci

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


class HistoryScannerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Test Author")
        self.git("config", "user.email", "test@users.noreply.github.com")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "core.hooksPath", str(self.root / "no-hooks"))

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True)

    def commit(self, message):
        self.git("add", "-A")
        self.git("commit", "--allow-empty", "-m", message)

    def test_history_finds_deleted_binary_and_large_files(self):
        (self.root / "deleted.bin").write_bytes(b"sample\0data")
        (self.root / "oversize.txt").write_bytes(b"x" * 33)
        self.commit("Add files")
        (self.root / "deleted.bin").unlink()
        (self.root / "oversize.txt").unlink()
        self.commit("Remove files")
        findings = []
        with patch.object(check_banned, "MAX_BYTES", 32):
            check_banned.scan_history(self.root, [], findings)
        self.assertTrue(any("deleted.bin:0: binary" in item for item in findings), findings)
        self.assertTrue(any("oversize.txt:0: large" in item for item in findings), findings)

    def test_history_finds_content_introduced_by_a_merge(self):
        (self.root / "file.txt").write_text("base\n")
        self.commit("Base")
        self.git("checkout", "-b", "side")
        (self.root / "file.txt").write_text("side\n")
        self.commit("Side")
        self.git("checkout", "main")
        (self.root / "file.txt").write_text("main\n")
        self.commit("Main")
        merge = subprocess.run(["git", "merge", "--no-commit", "side"], cwd=self.root, capture_output=True)
        self.assertEqual(merge.returncode, 1)
        (self.root / "file.txt").write_text("confidential-model\n")
        self.commit("Resolve merge")
        findings = []
        check_banned.scan_history(self.root, ["confidential-model"], findings)
        self.assertTrue(any("file.txt:1: private pattern 1" in item for item in findings), findings)

    def test_foundry_check_ignores_case(self):
        findings = []
        check_banned.check_text("model.txt", 1, "t" + "Sm" + "c", [], findings)
        self.assertEqual(findings, ["model.txt:1: foundry"])


class LocalCiClassification(unittest.TestCase):
    """The forms below are those CTest 4.2.1 writes with --output-junit."""

    CASES = {
        "ok": '<testcase name="ok" status="run"/>',
        "bad": '<testcase name="bad" status="fail"><failure message="Failed"/></testcase>',
        "listed": '<testcase name="listed" status="fail"><failure message="Failed"/></testcase>',
        "skip_output": '<testcase name="skip_output" status="notrun">'
                       '<skipped message="SKIP_REGULAR_EXPRESSION_MATCHED"/></testcase>',
        "skip_code": '<testcase name="skip_code" status="notrun">'
                     '<skipped message="SKIP_RETURN_CODE=77"/></testcase>',
        "off": '<testcase name="off" status="disabled"/>',
        "no_exe": '<testcase name="no_exe" status="notrun">'
                  '<skipped message="Unable to find executable"/></testcase>',
        "no_file": '<testcase name="no_file" status="notrun">'
                   '<skipped message="Required Files Missing"/></testcase>',
        "odd": '<testcase name="odd" status="other"/>',
    }

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.junit = Path(self.tmp.name) / "ctest.xml"

    def judge(self, *names, known=("listed",)):
        self.junit.write_text('<?xml version="1.0"?>\n<testsuite name="(empty)">'
                              + "".join(self.CASES[name] for name in names) + "</testsuite>\n")
        return local_ci.judge_tests(self.junit, set(known))

    def test_skipped_disabled_and_known_failures_do_not_fail_the_run(self):
        ok, detail = self.judge("ok", "listed", "skip_output", "skip_code", "off")
        self.assertTrue(ok, detail)
        self.assertIn("1 passed, 1 failed, 1 of them known, 2 skipped, 1 disabled", detail)

    def test_a_failure_that_is_not_listed_fails_the_run(self):
        ok, detail = self.judge("ok", "bad", "listed")
        self.assertFalse(ok)
        self.assertIn("new: bad", detail)

    def test_a_test_that_could_not_start_fails_the_run(self):
        for name in ("no_exe", "no_file", "odd"):
            with self.subTest(name=name):
                ok, detail = self.judge("ok", name)
                self.assertFalse(ok, detail)
                self.assertIn("did not run: " + name, detail)

    def test_a_listed_test_that_could_not_start_is_not_excused(self):
        ok, detail = self.judge("ok", "no_exe", known=("no_exe",))
        self.assertFalse(ok, detail)

    def test_a_listed_test_that_passes_is_reported(self):
        ok, detail = self.judge("ok", known=("ok",))
        self.assertTrue(ok, detail)
        self.assertIn("listed but passing: ok", detail)

    def test_a_missing_unreadable_or_empty_result_fails_the_run(self):
        self.assertFalse(local_ci.judge_tests(self.junit, set())[0])
        self.junit.write_text("<testsuite><testcase")
        self.assertFalse(local_ci.judge_tests(self.junit, set())[0])
        self.junit.write_text("<other/>")
        self.assertFalse(local_ci.judge_tests(self.junit, set())[0])
        self.assertEqual(self.judge(), (False, "no tests ran"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    BINARY = args.binary.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
