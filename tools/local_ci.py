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
     the list can be shortened. A test that CTest could not start, a missing
     or unreadable result file, and a run without tests also fail the run;
     tests that skip themselves and disabled tests do not
  3. tools/check_banned.py (and --history with --history); private patterns
     come from KIRIMASPICE_PRIVATE_PATTERNS if it is set, and the summary
     says for each scan whether it had any ("private patterns: yes" or "no")
  4. tools/check_spdx.py

Logs go to build/local-ci/. This script builds in build/; the tests
themselves read their data from the source tree wherever the build is.
"""

import argparse
import os
import platform
import re
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


def note_private_patterns(results):
    """Adds to the detail of the check_banned step just run whether it had
    private patterns, as check_banned reported it on its last line: a scan
    without them passes without having looked for the private names."""
    name, passed, detail = results[-1]
    try:
        text = (LOGS / f"{name}.log").read_text(encoding="utf-8", errors="replace")
    except OSError:
        text = ""
    found = re.findall(r"private patterns: (yes|no)", text)
    given = found[-1] if found else "not reported"
    results[-1] = (name, passed, f"{detail}, private patterns: {given}")


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


def classify_tests(junit: Path):
    """Sorts the test cases of CTest's JUnit file by what happened to them.

    CTest writes status="notrun" both for a test that skipped itself (its
    <skipped> message starts with SKIP_, from SKIP_REGULAR_EXPRESSION or
    SKIP_RETURN_CODE) and for one it could not start (a missing executable
    or required file). Only the first is a skip; the second checked nothing.
    """
    root = ET.parse(junit).getroot()
    suite = root if root.tag == "testsuite" else root.find("testsuite")
    if suite is None:
        raise ValueError("no testsuite element")
    groups = {"passed": set(), "failed": set(), "skipped": set(), "disabled": set(),
              "unexplained": {}}
    for case in suite.findall("testcase"):
        name, status = case.get("name"), case.get("status")
        skipped = case.find("skipped")
        message = (skipped.get("message") or "") if skipped is not None else ""
        if status == "run":
            groups["passed"].add(name)
        elif status == "fail":
            groups["failed"].add(name)
        elif status == "disabled":
            groups["disabled"].add(name)
        elif status == "notrun" and message.startswith("SKIP_"):
            groups["skipped"].add(name)
        else:
            groups["unexplained"][name] = f"{status}: {message}" if message else str(status)
    return groups


def judge_tests(junit: Path, known):
    """Returns (ok, detail) for the result file that this run's ctest wrote."""
    if not junit.exists():
        return False, f"ctest wrote no result file ({junit.name})"
    try:
        groups = classify_tests(junit)
    except (ET.ParseError, ValueError) as err:
        return False, f"cannot read {junit.name}: {err}"
    failed, passed, unexplained = groups["failed"], groups["passed"], groups["unexplained"]
    if not (failed or passed or groups["skipped"] or groups["disabled"] or unexplained):
        return False, "no tests ran"
    new = sorted(failed - known)
    fixed = sorted(known & passed)
    detail = (f"{len(passed)} passed, {len(failed)} failed, {len(failed & known)} of them known, "
              f"{len(groups['skipped'])} skipped, {len(groups['disabled'])} disabled")
    if new:
        detail += "; new: " + ", ".join(new)
    if unexplained:
        detail += "; did not run: " + ", ".join(
            f"{name} ({why})" for name, why in sorted(unexplained.items()))
    if fixed:
        detail += "; listed but passing: " + ", ".join(fixed)
    return not new and not unexplained, detail


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
        junit.unlink(missing_ok=True)  # a result left by an earlier run must not be judged
        run("ctest", ["ctest", "--test-dir", str(BUILD), "-j", args.jobs, "--output-on-failure",
                      "--output-junit", str(junit)], results)
        results.pop()  # judged against the known failures instead of the exit code
        passed, detail = judge_tests(junit, known_failures())
        results.append(("tests", passed, detail))

    run("check_banned", [sys.executable, "tools/check_banned.py"], results)
    note_private_patterns(results)
    if args.history:
        run("check_banned_history", [sys.executable, "tools/check_banned.py", "--history"], results)
        note_private_patterns(results)
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
