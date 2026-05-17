# Changelog

All notable changes to **spc-cpp** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-05-17

Initial public release. Open-source C++23 SDK for NOAA Storm Prediction
Center (SPC) severe-weather products, extracted from the internal
`spc-data` ingestion service so its battle-tested NOAA-GeoJSON parser
(case-variant keys, numeric-as-string labels, Polygon/MultiPolygon
geometry) can be reused instead of re-implemented per consumer.

### Added

- **`spc_core`**: `Error`/`Result<T>` (`std::expected`-based; adds
  `ErrorCode::FeedUnavailable` for SPC's HTTP-404 "no active outlook"),
  token-bucket `RateLimiter`, exponential-backoff `retry`, GeoJSON
  pagination helpers incl. `ArcGISPager` (2000-record offset paging,
  stops on `exceededTransferLimit == false`). `geometry.*` (ray-cast
  point-in-polygon) and `types.hpp` (contract-stable outlook shapes)
  **extracted verbatim** from `spc-data`.
- **`spc_http`**: pimpl GET-only `HttpClient`, behavior-parity with
  `spc-data`'s libcurl fetcher (FOLLOWLOCATION / NOSIGNAL /
  ACCEPT_ENCODING / UA), absolute-URL pass-through so one client serves
  `spc.noaa.gov`, the ArcGIS MapServer, and the IEM archive.
- **`spc_models`**: null-safe `glz::generic` helpers + the verbatim
  `severity_from_label` / `parse_categorical` / `parse_probabilistic`
  AST walkers (lifted from `spc-data/src/parser.cpp`); GeoJSON +
  Esri-FeatureSet wrappers.
- Layered CMake static libs `spc_core → spc_http → spc_models →
  spc_api → spc` with `install(EXPORT)` + `spc::` namespace and a
  `find_package(spc)` config.
- House-standard tooling: `Makefile` (build/test/lint/format/coverage),
  `.clang-format`, `.editorconfig`, `.markdownlint-cli2.yaml`,
  `tools/cpp_auto_audit.py` (empty allowlist), GitHub Actions CI
  (linux + macos + markdown-lint) and tag-driven release workflow.

### JSON-library divergence (intentional)

`spc-cpp` parses JSON with **[Glaze](https://github.com/stephenberry/glaze)
v7.6.0**, *not* `nlohmann/json` like the sibling `nws-cpp` / `ncei-cpp`
SDKs. SPC's `properties` block is shape-loose (case-variant `LABEL` vs
`label` vs `dn`; a probabilistic `LABEL` that is sometimes the string
`"5"` and sometimes the number `5`) and its geometry is polymorphic
(`Polygon` vs `MultiPolygon`). The extracted parser walks a
`glz::generic` AST rather than a static `glz::meta` schema; this is the
exact, proven `spc-data` code path and is preserved verbatim so the
downstream byte-identity guarantee holds. See the README
"JSON library: Glaze (divergence note)" section.
