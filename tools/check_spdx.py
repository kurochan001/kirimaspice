#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Checks that each file of kirimaspice's own code starts with its copyright
line and its SPDX line.

  tools/check_spdx.py          # exit 1 if a file lacks them
  tools/check_spdx.py --add    # put them at the top of each file that lacks them

The files are those git tracks with these names: C++ (.cpp, .hpp, .h and
.h.in), CMake (CMakeLists.txt, .cmake), Python, shell, and the workflows of
.github/workflows. The two lines come first, after a #! line, as comments of
the file's language:

  Copyright (c) <year> The kirimaspice Authors
  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

Files that are based on BSIM4 4.8.3 code carry a different SPDX expression
(see ECL_FILES). Test data (netlists, reference waveforms and the like) has no
such lines. --add writes the year of today, the year a new file is made.
"""

import argparse
import datetime
import re
import subprocess
import sys
from pathlib import Path

LICENSE = "Apache-2.0 WITH LLVM-exception"
ECL_LICENSE = "Apache-2.0 WITH LLVM-exception AND ECL-2.0"
# Files with parts based on the BSIM4 4.8.3 model code (ECL-2.0); see the notice in each.
ECL_FILES = {
    "include/kirimaspice/devices/bsim4_core.hpp",
    "include/kirimaspice/devices/mosfet_bsim4.hpp",
    "include/kirimaspice/devices/mosfet_bsim4_params.hpp",
    "src/devices/bsim4_core.cpp",
    "src/devices/mosfet_bsim4.cpp",
    "src/devices/mosfet_bsim4_params.cpp",
}
COPYRIGHT = re.compile(r"Copyright \(c\) \d{4} The kirimaspice Authors$")


def comment_prefix(path: str):
    name = Path(path).name
    if name.endswith((".cpp", ".hpp", ".h", ".h.in")):
        return "//"
    if name == "CMakeLists.txt" or name.endswith((".cmake", ".py", ".sh")):
        return "#"
    if path.startswith(".github/workflows/") and name.endswith((".yml", ".yaml")):
        return "#"
    return None


def tracked_files(root: Path):
    out = subprocess.run(["git", "ls-files", "-z"], cwd=root, check=True,
                         capture_output=True).stdout.decode()
    return [p for p in out.split("\0") if p]


def check(root: Path, path: str, prefix: str):
    lines = (root / path).read_text(encoding="utf-8").splitlines()
    start = 1 if lines and lines[0].startswith("#!") else 0
    head = lines[start:start + 2]
    want_spdx = f"{prefix} SPDX-License-Identifier: {ECL_LICENSE if path in ECL_FILES else LICENSE}"
    ok = (len(head) == 2 and head[0].startswith(prefix + " ")
          and COPYRIGHT.fullmatch(head[0][len(prefix) + 1:]) is not None
          and head[1] == want_spdx)
    return ok, start, want_spdx


def add(root: Path, path: str, prefix: str, start: int, spdx: str):
    p = root / path
    text = p.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    year = datetime.date.today().year
    header = [f"{prefix} Copyright (c) {year} The kirimaspice Authors\n", spdx + "\n"]
    rest = lines[start:]
    if rest and rest[0].strip():
        header.append("\n")
    p.write_text("".join(lines[:start] + header + rest), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--add", action="store_true", help="add the lines where they are missing")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args()
    missing = []
    for path in tracked_files(args.root):
        prefix = comment_prefix(path)
        if prefix is None or not (args.root / path).is_file():
            continue
        ok, start, spdx = check(args.root, path, prefix)
        if ok:
            continue
        if args.add:
            add(args.root, path, prefix, start, spdx)
        else:
            missing.append(path)
    for path in missing:
        print(f"{path}: missing copyright and SPDX lines")
    if missing:
        print(f"{len(missing)} file(s) lack the lines; run tools/check_spdx.py --add", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
