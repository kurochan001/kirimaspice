#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Runs the checks that must pass before a push, and prints a summary to attach.

  tools/local_ci.py                                  # build, test, repository checks
  tools/local_ci.py --cmake-arg=-DCMAKE_PREFIX_PATH=/opt/suitesparse
  tools/local_ci.py --history                        # also scan every commit

Steps, in order:
  1. configure, build and test in build/ (Release, Ninja); the tests run with
     the working directory the test definitions set
  2. compare the failed tests with tests/known_failures.txt: a failure not on
     the list fails the run; a listed test that passes is reported, so that
     the list can be shortened
  3. tools/check_banned.py (and --history with --history); private patterns
     come from KIRIMASPICE_PRIVATE_PATTERNS if it is set
  4. tools/check_spdx.py

Logs go to build/local-ci/. The build directory must be build/: some tests
find their data relative to it.
"""

import argparse
import os
import platform
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
LOGS = BUILD / "local-ci"


def run(name, cmd, results):
    LOGS.mkdir(parents=True, exist_ok=True)
    log = LOGS / f"{name}.log"
    with open(log, "w", encoding="utf-8") as fh:
        rc = subprocess.run(cmd, cwd=ROOT, stdout=fh, stderr=subprocess.STDOUT).returncode
    results.append((name, rc == 0, f"exit {rc}, log {log.relative_to(ROOT)}"))
    return rc


def tool_version(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()
        return out[0] if out else "unknown"
    except OSError:
        return "not found"


def known_failures():
    path = ROOT / "tests" / "known_failures.txt"
    names = set()
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            names.add(line)
    return names


def failed_tests(junit: Path):
    root = ET.parse(junit).getroot()
    suite = root if root.tag == "testsuite" else root.find("testsuite")
    failed, passed = set(), set()
    for case in suite.findall("testcase"):
        status = case.get("status")
        if status == "fail":
            failed.add(case.get("name"))
        elif status == "run":
            passed.add(case.get("name"))
    return failed, passed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cmake-arg", action="append", default=[], help="extra argument for cmake")
    ap.add_argument("--jobs", default=str(os.cpu_count() or 4))
    ap.add_argument("--history", action="store_true", help="also scan every commit")
    args = ap.parse_args()
    results = []

    ok = run("configure", ["cmake", "-S", ".", "-B", str(BUILD), "-G", "Ninja",
                           "-DCMAKE_BUILD_TYPE=Release", *args.cmake_arg], results) == 0
    ok = ok and run("build", ["cmake", "--build", str(BUILD), "-j", args.jobs], results) == 0
    if ok:
        junit = LOGS / "ctest.xml"
        run("ctest", ["ctest", "--test-dir", str(BUILD), "-j", args.jobs, "--output-on-failure",
                      "--output-junit", str(junit)], results)
        results.pop()  # judged against the known failures instead of the exit code
        failed, passed = failed_tests(junit)
        known = known_failures()
        new = sorted(failed - known)
        fixed = sorted(known & passed)
        detail = f"{len(failed)} failed, {len(failed & known)} of them known"
        if new:
            detail += "; new: " + ", ".join(new)
        if fixed:
            detail += "; listed but passing: " + ", ".join(fixed)
        results.append(("tests", not new, detail))

    run("check_banned", [sys.executable, "tools/check_banned.py"], results)
    if args.history:
        run("check_banned_history", [sys.executable, "tools/check_banned.py", "--history"], results)
    run("check_spdx", [sys.executable, "tools/check_spdx.py"], results)

    head = subprocess.run(["git", "rev-parse", "--short=12", "HEAD"], cwd=ROOT,
                          capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                           capture_output=True, text=True).stdout.strip()
    print("local CI summary")
    print(f"  commit:   {head}{' (uncommitted changes present)' if dirty else ''}")
    print(f"  system:   {platform.system()} {platform.release()} {platform.machine()}")
    cache = BUILD / "CMakeCache.txt"
    cxx = "c++"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_CXX_COMPILER:"):
                cxx = line.split("=", 1)[1]
    print(f"  compiler: {tool_version([cxx, '--version'])}")
    print(f"  cmake:    {tool_version(['cmake', '--version'])}")
    failed_any = False
    for name, passed, detail in results:
        failed_any |= not passed
        print(f"  {'ok  ' if passed else 'FAIL'}  {name:22s} {detail}")
    print("result: " + ("FAILED" if failed_any else "all checks passed"))
    return 1 if failed_any else 0


if __name__ == "__main__":
    sys.exit(main())
