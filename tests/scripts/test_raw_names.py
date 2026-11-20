#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Both raw writers name the signals v(<node>) and i(<source>) in lower case
(include/kirimaspice/output/signal_names.hpp), as ngspice-47 writes them
(v(out), i(v1)), whatever the case of the netlist."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = None


def variables(path):
    names, in_vars = [], False
    for raw in path.read_bytes().split(b"\n"):
        line = raw.decode("latin-1")
        if line.startswith("Variables:"):
            in_vars = True
            continue
        if line.startswith(("Values:", "Binary:")):
            break
        if in_vars and line.strip():
            names.append(line.split()[1])
    return names


class RawNames(unittest.TestCase):
    def test_both_writers_write_lower_case_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory / "deck.cir").write_text("t\nV1 In 0 1\nR1 In Out 1k\nR2 Out 0 1k\n.op\n.end\n")
            result = subprocess.run([str(BINARY), "deck.cir", "--raw-out", "a.raw",
                                     "--raw-binary-out", "b.raw"], cwd=directory,
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            expected = ["v(in)", "v(out)", "i(v1)"]
            self.assertEqual(variables(directory / "a.raw"), expected)
            self.assertEqual(variables(directory / "b.raw"), expected)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args, rest = parser.parse_known_args()
    BINARY = Path(args.binary).resolve()
    unittest.main(argv=[sys.argv[0], *rest])
