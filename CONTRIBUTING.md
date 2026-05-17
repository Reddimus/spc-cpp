# Contributing to spc-cpp

Thanks for your interest in contributing. This guide covers the local
build flow, code style expectations, and PR conventions used in this
repo. For security-vulnerability reports, see [SECURITY.md](SECURITY.md)
— do **not** open a public issue for those.

## Getting started

```bash
git clone https://github.com/Reddimus/spc-cpp.git
cd spc-cpp

# One-time: install build deps (Ubuntu 24.04 example)
sudo apt install -y build-essential cmake clang-format \
    libcurl4-openssl-dev

make build      # CMake configure + Release build
make test       # Run unit tests (ctest)
make lint       # clang-format --dry-run + cpp_auto_audit
```

## Code style

- `.clang-format` (LLVM base, **tabs**, width 4, 100-col). `make
  format` rewrites in place; `make lint` is the gate.
- **No `auto`** except iterators, structured bindings, and range-for
  loop variables. `tools/cpp_auto_audit.py` enforces this with an
  empty allowlist; mark genuine one-offs with a trailing `// auto-ok`.
- Namespace `spc`. Include order: `"spc/..."` first, then system
  headers (clang-format regroups).
- All fallible APIs return `Result<T> = std::expected<T, Error>`. No
  exceptions across the public surface.

## Parser-divergence rule (important)

The **convective** path (`severity_from_label`, `parse_categorical`,
`parse_probabilistic`, `parse_rings`, `geometry.*`, `types.hpp`) is
extracted *verbatim* from the internal `spc-data` service and is
load-bearing for a downstream byte-identity guarantee. Do not change
its behavior. Net-new product models (fire weather, watches, mesoscale
discussions, storm reports, Day4-8 / conditional intensity) are
additive and must not reuse `severity_from_label` blindly — they carry
their own product-specific severity mappers.

## PRs

- Branch, push, open a PR against `main`. CI (linux + macos +
  markdown-lint) must be green.
- Conventional-commit subject lines.
- Update `CHANGELOG.md` under `[Unreleased]`.
- A maintainer merges with a **merge commit** (history reachability
  matters for the consumers that vendor this via FetchContent).
