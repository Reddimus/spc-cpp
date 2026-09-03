#!/usr/bin/env python3
"""Verify that every parser fixture matches the checked SHA-256 manifest."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures"
MANIFEST = FIXTURES / "SHA256SUMS"


def main() -> int:
    expected: dict[Path, str] = {}
    for line_number, line in enumerate(MANIFEST.read_text(encoding="utf-8").splitlines(), 1):
        digest, separator, relative_name = line.partition("  ")
        if not separator or len(digest) != 64:
            print(f"SHA256SUMS:{line_number}: malformed entry", file=sys.stderr)
            return 1
        expected[ROOT / relative_name] = digest

    actual = {
        path
        for path in FIXTURES.iterdir()
        if path.is_file() and path.name not in {"README.md", "SHA256SUMS"}
    }
    if actual != set(expected):
        missing = sorted(str(path.relative_to(ROOT)) for path in actual - set(expected))
        stale = sorted(str(path.relative_to(ROOT)) for path in set(expected) - actual)
        print(f"unlisted fixtures: {missing}", file=sys.stderr)
        print(f"missing fixtures: {stale}", file=sys.stderr)
        return 1

    failures: list[str] = []
    for path, wanted in expected.items():
        found = hashlib.sha256(path.read_bytes()).hexdigest()
        if found != wanted:
            failures.append(f"{path.relative_to(ROOT)}: {found} != {wanted}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"verified {len(expected)} fixture checksums")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
