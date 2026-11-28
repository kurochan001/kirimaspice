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

    def test_op_with_dc_runs_both(self):
        # ngspice-45.2 runs both and writes the DC transfer characteristic,
        # then the operating point, to its raw file, in either order of the
        # lines (run on 2026-11-28); before, .OP with .DC gave the sweep alone
        for lines in (".op\n.dc V1 0 2 1\n", ".dc V1 0 2 1\n.op\n"):
            with self.subTest(lines=lines):
                result, plots = self.run_deck("t\nV1 a 0 1\nR1 a b 1k\nR2 b 0 1k\n" + lines + ".end\n")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(plots, ["DC Sweep", "DC Operating Point"])
                self.assertIn("V(b) = 0.5 V", result.stdout)

    def table(self, stdout, heading):
        lines = stdout.split("=== " + heading + " ===\n", 1)[1].split("\n\n", 1)[0].splitlines()
        return lines[0].split("\t"), [[float(v) for v in line.split("\t")] for line in lines[1:] if line]

    def test_print_dc_and_ac_tables(self):
        # ngspice-45.2 prints these values for the same .PRINT lines (run on
        # 2026-11-28): v(b) and i(v1) at each value of V1, and in AC v(b) as
        # its real and imaginary parts, vm, vp in radians, vdb, vr and vi
        result, _ = self.run_deck(
            "t\nV1 a 0 DC 1 AC 1\nR1 a b 1k\nR2 b 0 1k\nC1 b 0 1u\n.dc V1 0 2 1\n.print dc v(b) i(v1)\n"
            ".ac dec 1 10 1k\n.print ac v(b) vm(b) vp(b) vdb(b) vr(b) vi(b)\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("is read, but this program does not run it", result.stderr)
        titles, rows = self.table(result.stdout, "DC Sweep Table")
        self.assertEqual(titles, ["V1", "v(b)", "i(v1)"])
        for row, expected in zip(rows, [[0, 0, 0], [1, 0.5, -5e-4], [2, 1, -1e-3]]):
            for value, reference in zip(row, expected):
                self.assertAlmostEqual(value, reference, places=9)
        titles, rows = self.table(result.stdout, "AC Analysis Table")
        self.assertEqual(titles, ["Frequency (Hz)", "v(b)_real", "v(b)_imag", "vm(b)", "vp(b)", "vdb(b)",
                                  "vr(b)", "vi(b)"])
        reference = [[10, 4.995070e-01, -1.56925e-02, 4.997534e-01, -3.14056e-02],
                     [100, 4.550849e-01, -1.42969e-01, 4.770141e-01, -3.04396e-01],
                     [1000, 4.599983e-02, -1.44513e-01, 1.516572e-01, -1.26263e+00]]
        for row, expected in zip(rows, reference):
            for value, ref in zip(row, expected):
                self.assertAlmostEqual(value / ref, 1.0, places=5)
            self.assertAlmostEqual(row[5], 20 * __import__("math").log10(expected[3]), places=4)
            self.assertAlmostEqual(row[6], expected[1], places=5)
            self.assertAlmostEqual(row[7], expected[2], places=5)

    def raw_variables(self, raw):
        plots, current, inside = [], [], False
        for line in raw.read_text(errors="replace").splitlines():
            if line.startswith("Variables:"):
                inside, current = True, []
            elif line.startswith("Values:") or line.startswith("Binary:"):
                inside = False
                plots.append(current)
            elif inside and line.strip():
                current.append(line.split()[1])
        return plots

    def test_save_keeps_its_signals_in_every_file(self):
        # ngspice-45.2 keeps the scale and the .save vectors of each analysis
        # in its raw file, the lines of several .save together, a node's name
        # as its voltage, and everything for .save all (run on 2026-11-28);
        # a name that is nothing is left out with a warning
        circuit = ("t\nV1 a 0 PULSE(0 1 0 1u 1u 1m 2m)\nR1 a b 1k\nC1 b 0 1n\nR2 b c 1k\nR3 c 0 1k\n"
                   ".op\n.dc V1 0 1 1\n.tran 1u 5u\n")
        cases = [(".save v(b) i(v1)\n", ["v(b)", "i(v1)"]), (".save v(b)\n.save C\n", ["v(b)", "v(c)"]),
                 (".save v(zz) b\n", ["v(b)"]), (".save all\n", ["v(a)", "v(b)", "v(c)", "i(v1)"]),
                 ("", ["v(a)", "v(b)", "v(c)", "i(v1)"])]
        for lines, kept in cases:
            with self.subTest(lines=lines):
                result, _ = self.run_deck(circuit + lines + ".end\n")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertNotIn(".SAVE is read", result.stderr)
                plots = self.raw_variables(self.directory / "out.raw")
                self.assertEqual(plots, [["V1"] + kept, kept, ["time"] + kept])
                self.assertEqual("no such node" in result.stderr, "zz" in lines)
        # The print command of a .control block keeps every signal, as in
        # ngspice-45.2
        result, _ = self.run_deck(circuit + ".control\nprint v(b)\n.endc\n.end\n")
        self.assertEqual(self.raw_variables(self.directory / "out.raw")[2],
                         ["time", "v(a)", "v(b)", "v(c)", "i(v1)"])
        deck = self.directory / "circuit.cir"
        deck.write_text(circuit + ".save v(b) i(v1)\n.end\n")
        binary = self.directory / "out_binary.raw"
        result = subprocess.run([str(BINARY), str(deck), "--raw-binary-out", str(binary)],
                                cwd=self.directory, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.raw_variables(binary), [["V1", "v(b)", "i(v1)"], ["v(b)", "i(v1)"],
                                                     ["time", "v(b)", "i(v1)"]])

    def test_step_runs_every_analysis_for_each_value(self):
        # .STEP param reads the netlist again with each value given and runs
        # its analyses (main.cpp); V(b) of the divider is rl/(1k + rl), the
        # analytic value. Each step adds its plots to the raw file and puts
        # _<name>_<value> in the SSF name; a second .STEP line is not run
        result, plots = self.run_deck(
            "t\n.param rl=1k\nV1 a 0 1\nR1 a b 1k\nR2 b 0 {rl}\n.step param rl list 1k 3k\n"
            ".step param x 1 2 1\n.op\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(".STEP is read", result.stderr)
        self.assertIn("only the first .STEP line is run", result.stderr)
        self.assertEqual(plots, ["DC Operating Point", "DC Operating Point"])
        steps = result.stdout.split("=== Step rl = ")[1:]
        self.assertEqual([step.split(" ===")[0] for step in steps], ["1000", "3000"])
        self.assertIn("V(b) = 0.5 V", steps[0])
        self.assertIn("V(b) = 0.75 V", steps[1])
        deck = self.directory / "circuit.cir"
        result = subprocess.run([str(BINARY), str(deck), "-o", str(self.directory / "out.ssf")],
                                cwd=self.directory, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.directory / "out_rl_1000.ssf").exists())
        self.assertTrue((self.directory / "out_rl_3000.ssf").exists())

    def test_ac_from_zero_hertz_is_run_and_fails(self):
        # A decade sweep from 0 Hz has no points (log 0); the .AC line is
        # still an analysis line, so the program reports the failure
        result, plots = self.run_deck(CIRCUIT + ".ac dec 10 0 1k\n.end\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertNotIn("No analysis specified", result.stdout)

    def test_directive_the_program_does_not_run_is_named(self):
        result, plots = self.run_deck("t\nV1 in 0 1\nR1 in 0 1k\n.OPTIMIZE COBYLA OBJECTIVE=v(in) MAX PARAM=R1.resistance:1k:100:10k\n"
                                      ".op\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(".OPTIMIZE is read, but this program does not run it", result.stdout + result.stderr)

    def test_transient_prints_the_signals_of_print_tran(self):
        # One row per time point of the fixed step (0, 1, 2 and 3 ns), with
        # the time and the two signals, in the order of the .PRINT line
        result, plots = self.run_deck(
            "t\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1p\n.tran 1n 3n UIC\n"
            ".print tran v(out) i(v1)\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = result.stdout.splitlines()
        header = lines.index("Time (s)\tv(out)\ti(v1)")
        rows = [line.split("\t") for line in lines[header + 1:] if line]
        self.assertEqual(len(rows), 4)
        self.assertTrue(all(len(row) == 3 for row in rows))

    def test_transient_without_print_prints_no_table(self):
        result, plots = self.run_deck(CIRCUIT + ".tran 10u 1m\n.end\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn("Transient Analysis Results", result.stdout)
        self.assertEqual(plots, ["Transient Analysis"])

    def test_tmax_limits_the_step(self):
        # TSTEP 10 us, TMAX 2 us over 1 ms: steps of 2 us, 501 points
        deck = self.directory / "circuit.cir"
        deck.write_text("t\nV1 in 0 1\nR1 in out 1k\nC1 out 0 1n\n.tran 10u 1m 0 2u\n.end\n")
        raw = self.directory / "out.raw"
        result = subprocess.run([str(BINARY), str(deck), "--raw-out", str(raw)],
                                cwd=self.directory, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        points = [line for line in raw.read_text().splitlines() if line.startswith("No. Points:")]
        self.assertEqual(points, ["No. Points: 501"])

    def test_dc_sweep_summary_names_the_files_that_hold_it(self):
        # -o writes no SSF file for a DC sweep; the summary names the raw files
        deck = self.directory / "circuit.cir"
        deck.write_text("t\nV1 in 0 1\nR1 in out 1k\nR2 out 0 1k\n.dc V1 0 1 0.5\n.end\n")
        result = subprocess.run([str(BINARY), str(deck), "-o", "sweep"],
                                cwd=self.directory, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("--raw-out", result.stdout)
        self.assertNotIn("SSF", result.stdout)
        self.assertEqual(list(self.directory.glob("sweep*")), [])

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
