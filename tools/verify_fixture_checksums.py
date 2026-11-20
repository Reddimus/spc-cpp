#!/usr/bin/env python3
"""Check tests/fixtures against tests/fixtures/SHA256SUMS, in both directions."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures"
MANIFEST = FIXTURES / "SHA256SUMS"
PREFIX = "tests/fixtures/"  # manifest paths are repository-relative, as shasum prints them
NOT_FIXTURES = {"README.md", "SHA256SUMS"}


def fixture_files() -> set[str]:
    """Paths relative to tests/fixtures of every fixture, in any subdirectory."""
    found: set[str] = set()
    for directory, subdirectories, names in os.walk(FIXTURES):
        # Dotfiles such as a Finder .DS_Store are not fixtures.
        subdirectories[:] = [name for name in subdirectories if not name.startswith(".")]
        for name in names:
            relative = (Path(directory) / name).relative_to(FIXTURES).as_posix()
            if not name.startswith(".") and relative not in NOT_FIXTURES:
                found.add(relative)
    return found


def main() -> int:
    expected: dict[str, str] = {}
    first_lines: dict[str, int] = {}
    for line_number, line in enumerate(MANIFEST.read_text(encoding="utf-8").splitlines(), 1):
        digest, separator, name = line.partition("  ")
        if not separator or len(digest) != 64 or not name.startswith(PREFIX):
            print(f"SHA256SUMS:{line_number}: malformed entry", file=sys.stderr)
            return 1
        relative = name[len(PREFIX):]
        # A later duplicate would otherwise hide a stale earlier digest.
        if relative in expected:
            print(
                f"SHA256SUMS:{line_number}: duplicate entry for {name}"
                f" (first on line {first_lines[relative]})",
                file=sys.stderr,
            )
            return 1
        expected[relative] = digest
        first_lines[relative] = line_number

    actual = fixture_files()
    if actual != set(expected):
        unlisted = sorted(PREFIX + path for path in actual - set(expected))
        missing = sorted(PREFIX + path for path in set(expected) - actual)
        print(f"unlisted fixtures: {unlisted}", file=sys.stderr)
        print(f"missing fixtures: {missing}", file=sys.stderr)
        return 1

    failures: list[str] = []
    for relative, wanted in expected.items():
        found = hashlib.sha256((FIXTURES / relative).read_bytes()).hexdigest()
        if found != wanted:
            failures.append(f"{PREFIX}{relative}: {found} != {wanted}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"verified {len(expected)} fixture checksums")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
