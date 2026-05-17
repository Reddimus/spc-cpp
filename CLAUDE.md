# spc-cpp Development Guide

## Build & Test

```bash
make build          # Release build (CMake + make)
make debug          # Debug build
make test           # Run unit tests (ctest)
make lint           # Check formatting (clang-format --dry-run) + cpp_auto_audit
make format         # Format in place
make coverage       # lcov coverage report
make clean          # Remove build/
make run-<example>  # Build + run an example (see examples/)
```

## Architecture

- **Layered static libraries**: spc_core -> spc_http -> spc_models ->
  spc_api -> spc (INTERFACE). `install(EXPORT)` + `spc::` namespace.
- **C++23**: `std::expected<T, Error>` for all returns, no exceptions.
- **Patterns**: Pimpl (HttpClient, the spc_api clients), non-copyable /
  movable clients, `[[nodiscard]]`.
- **JSON**: [Glaze](https://github.com/stephenberry/glaze) v7.6.0 via
  FetchContent. This **diverges** from the sibling `nws-cpp` /
  `ncei-cpp` SDKs (nlohmann/json) on purpose — SPC's properties block
  is shape-loose (case-variant `LABEL`/`label`/`dn`, numeric-as-string)
  and geometry is polymorphic (`Polygon`/`MultiPolygon`), so the parser
  walks a `glz::generic` AST rather than a static `glz::meta`. The
  convective path is lifted verbatim from the internal `spc-data`
  service and must stay parity-exact (a downstream byte-identity gate
  depends on it). Documented in README + CHANGELOG.
- **Tests**: GoogleTest via FetchContent. Fixture files in
  `tests/fixtures/` (live-captured SPC payloads, shared corpus with
  `spc-data`).

## Conventions

- Code style: `.clang-format` (LLVM base, tabs, 100 cols).
- Namespace: `spc`.
- **No `auto`**: explicit types except iterators, structured bindings,
  range-for. `tools/cpp_auto_audit.py`, empty allowlist.
- Verbatim convective parser is frozen behavior. Net-new product
  models carry their own severity mappers — never reuse
  `severity_from_label` for fire weather / watches / etc.
- Include order: project headers first, then system (clang-format
  enforced).

## Data sources

- **ArcGIS MapServer** (`mapservices.weather.noaa.gov`) — primary,
  contract-stable; `ArcGISClient` + `ArcGISPager` (2000-rec paging).
- **Static `www.spc.noaa.gov` `.nolyr.geojson`** — fallback;
  `StaticFeedClient` (the 7 feeds `spc-data` ships + day4-8 / fire).
- **IEM** (`mesonet.agron.iastate.edu`) — best-effort historical
  backfill; `ArchiveClient` (conservative rate/retry, off any hot path).

## CI

GitHub Actions `.github/workflows/ci.yml`: build + test + lint on
Ubuntu 24.04 and macos-latest, markdown-lint via DavidAnson. Release
workflow auto-creates a GitHub Release on `vX.Y.Z` tag push (notes
from `CHANGELOG.md`; the tag must equal `project(spc-cpp VERSION ...)`).
