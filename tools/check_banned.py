#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Checks the repository for content that must not be published.

  tools/check_banned.py                       # tracked files of the work tree
  tools/check_banned.py --history             # every commit: files, messages, authors
  tools/check_banned.py --private FILE        # also look for the patterns in FILE

Checks, each reported as <file>:<line>: <check>:
  home-path      absolute paths into a home directory (/home/<name>, /Users/<name>, C:\\Users\\<name>)
  email          e-mail addresses other than GitHub noreply and example.com/.org/.net;
                 THIRD_PARTY_NOTICES*.md and CODE_OF_CONDUCT*.md may name others
                 (third-party copyright lines, the contact of the code of conduct)
  foundry        names of semiconductor foundries (models must not carry them;
                 this file, which lists them, is exempt)
  binary         tracked files that contain NUL bytes
  large          tracked files over 5 MB
  author         (--history) commit authors or committers without a noreply address
  private        (--private) a line of FILE, matched without regard to case;
                 in CODE_OF_CONDUCT*.md, e-mail addresses are not matched (the
                 contact of the code of conduct is published on purpose)

Private patterns are names that must not appear and so cannot be written in
this repository. Keep them in an untracked file, one per line, and pass it
with --private or the KIRIMASPICE_PRIVATE_PATTERNS environment variable. The
output names a private pattern only by its line number in that file.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HOME_PATH = re.compile(r"(/home/|/Users/|[A-Za-z]:\\+Users\\+)(?!<)[A-Za-z0-9._-]+")
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
EMAIL_OK = re.compile(r"@users\.noreply\.github\.com$|@example\.(com|org|net)$", re.I)
EMAIL_FREE_FILES = re.compile(r"(^|[/ ])(THIRD_PARTY_NOTICES|CODE_OF_CONDUCT)(\.ja)?\.md$")
CONTACT_FILES = re.compile(r"(^|[/ ])CODE_OF_CONDUCT(\.ja)?\.md$")
FOUNDRY = re.compile(r"\b(TSMC|UMC|SMIC|GlobalFoundries|GLOBALFOUNDRIES|Samsung Foundry|TowerJazz|X-FAB)\b", re.I)
MAX_BYTES = 5 * 1024 * 1024


def git(root: Path, *args) -> bytes:
    return subprocess.run(["git", *args], cwd=root, check=True, capture_output=True).stdout


def load_private(path):
    if not path:
        return []
    pats = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        pats.append(line.lower() if line and not line.startswith("#") else None)
    return pats


def check_text(name: str, lineno: int, text: str, private, out):
    if HOME_PATH.search(text):
        out.append(f"{name}:{lineno}: home-path")
    if not EMAIL_FREE_FILES.search(name):
        for m in EMAIL.finditer(text):
            if not EMAIL_OK.search(m.group(0)):
                out.append(f"{name}:{lineno}: email")
                break
    if FOUNDRY.search(text) and not name.endswith("tools/check_banned.py"):
        out.append(f"{name}:{lineno}: foundry")
    low = text.lower()
    if CONTACT_FILES.search(name):
        low = EMAIL.sub("", low)
    for i, p in enumerate(private, 1):
        if p and p in low:
            out.append(f"{name}:{lineno}: private pattern {i}")


def check_file(name: str, path: str, data: bytes, private, out):
    check_text(name, 0, path, private, out)
    if len(data) > MAX_BYTES:
        out.append(f"{name}:0: large ({len(data)} bytes)")
    if b"\0" in data:
        out.append(f"{name}:0: binary")
        return
    for n, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
        check_text(name, n, line, private, out)


def scan_tree(root: Path, private, out):
    for path in git(root, "ls-files", "-z").decode().split("\0"):
        if not path or not (root / path).is_file():
            continue
        data = (root / path).read_bytes()
        check_file(path, path, data, private, out)


def scan_history(root: Path, private, out):
    seen_files = set()
    for sha in git(root, "rev-list", "--all").decode().splitlines():
        name = f"commit {sha[:12]}"
        metadata = git(root, "show", "-s", "--format=%ae%x00%ce%x00%an%x00%cn%x00%B", sha)
        ae, ce, author, committer, message = metadata.decode("utf-8", "replace").split("\0", 4)
        for e in (ae, ce):
            if not e.endswith("@users.noreply.github.com"):
                out.append(f"{name}:0: author")
        check_text(name, 0, author, private, out)
        check_text(name, 0, committer, private, out)
        for n, line in enumerate(message.splitlines(), 1):
            check_text(name, n, line, private, out)

        # Snapshot traversal includes binary blobs and changes made only by a
        # merge resolution. Patch output does not contain all of that data.
        for entry in git(root, "ls-tree", "-r", "-z", sha).split(b"\0"):
            if not entry:
                continue
            metadata, raw_path = entry.split(b"\t", 1)
            _, kind, oid = metadata.split()
            if kind != b"blob" or (raw_path, oid) in seen_files:
                continue
            seen_files.add((raw_path, oid))
            path = raw_path.decode("utf-8", "replace")
            data = git(root, "cat-file", "blob", oid.decode())
            check_file(f"{name} {path}", path, data, private, out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--history", action="store_true", help="scan every commit instead of the work tree")
    ap.add_argument("--private", default=os.environ.get("KIRIMASPICE_PRIVATE_PATTERNS"),
                    help="untracked file with private patterns, one per line")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args()
    private = load_private(args.private)
    out = []
    if args.history:
        scan_history(args.root, private, out)
    else:
        scan_tree(args.root, private, out)
    seen = []
    for item in out:
        if item not in seen:
            seen.append(item)
    for item in seen:
        print(item)
    note = "" if private else " (no private patterns given)"
    print(f"check_banned: {len(seen)} finding(s){note}", file=sys.stderr)
    return 1 if seen else 0


if __name__ == "__main__":
    sys.exit(main())
