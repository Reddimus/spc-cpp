# Changelog

All notable changes to **spc-cpp** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- **HTTP 404 now says which kind of 404 it was.** `ErrorCode::FeedUnavailable`
  ("no active outlook — clear the rows") is produced only by
  `StaticFeedClient`, the one feed where SPC uses 404 that way. A 404 from the
  ArcGIS MapServer or from IEM — a retired product, a renamed service path, a
  wrong base URL — is now `ErrorCode::NotFound`, which was previously
  unreachable. A logical ArcGIS `{"error":{"code":404}}` envelope, which is how
  a renamed MapServer path is actually reported (over HTTP 200), maps to
  `NotFound` as well. Consumers that branch on `is_feed_unavailable()` for
  `ArcGISClient` or `ArchiveClient` results should treat `NotFound` as a fault
  and alert on it instead of clearing rows.
- `Error::from_response` takes a trailing `Feed404` argument stating what a 404
  means for the feed that answered. It defaults to `Feed404::NotFound`, so
  existing calls keep compiling and get the safe reading.

### Fixed

- **ArcGIS paging advanced `resultOffset` by the requested page size, not by
  the records the server returned.** ArcGIS clamps `resultRecordCount` to the
  layer's own `maxRecordCount`, so a truncated page can be shorter than the
  2000 requested; the offset then skipped the gap and the caller got a
  successful result with a silent hole. `ArcGISPager::advance()` now takes the
  returned record count.
- ArcGIS paging is bounded. A page that reports truncation while carrying no
  records, and a server that never stops reporting truncation, now fail with
  `ErrorCode::ServerError` after at most `ArcGISPager::max_pages()` (100)
  requests instead of looping forever and growing memory without bound.
  `ArcGISPager::offset()` is a `std::int64_t`, so the arithmetic cannot
  overflow.

### Removed

- The two-argument `parse_fire_weather(body, day)` overload. It silently
  assumed `FireWeatherLayer::Outlook`, so a dry-thunderstorm body decoded
  `dn=5` as `"ELEV"` (severity 1) instead of `"IDRT"` (severity 0) — the label
  confusion 0.2.0 fixed, still reachable through the public API. The captured
  day-1 and day-2 payloads carry no LABEL at all, only the shared numeric
  `dn`, so nothing in a body says which layer produced it. Pass the layer
  explicitly: `parse_fire_weather(body, day, FireWeatherLayer::Outlook)`
  restores the old behaviour where that was in fact the right layer.

### Added

- `Error::from_arcgis`, for ArcGIS logical failure envelopes. It keeps the
  ArcGIS code in `Error::http_status` only while that code is HTTP-shaped
  (100..599) and records it in `Error::detail`, so a code such as 1000 can no
  longer masquerade as an HTTP status.

## [0.2.0] - 2026-09-03

### Added

- ArcGIS access for conditional intensity and day 4 through 8 probability.
- An injectable `HttpTransport` for deterministic client tests and custom
  networking.
- Installed-package and FetchContent consumer checks, ASan, UBSan, TSan, and
  clang-tidy CI.
- A checked NOAA ArcGIS 11.3 layer contract and opt-in live metadata check.

### Changed

- Fire-weather queries now merge both feature layers for every day from 1
  through 8. Each feature records its source layer. Days 1 and 2 expose the
  categorical severity; days 3 through 8 expose a normalized probability.
  Group layers are never used as feature endpoints.
- Static probabilistic feeds now use NOAA's published `day{N}otlk_*` filenames.
- Glaze is now 8.3.0 and GoogleTest is now 1.18.0.
- libcurl global state now initializes once per process instead of once per
  client.

### Fixed

- Corrected the Day 2 tornado and hail probability layers, Day 3 probability,
  and every Day 3 through 8 fire-weather layer.
- Decode NOAA's numeric Day 1 and 2 fire-weather `dn` categories without
  confusing outlook and dry-thunderstorm labels.
- Percent-encoded ArcGIS query values and parse ArcGIS error and paging fields
  as JSON.
- Invalid product combinations now fail before network access.

### Deprecated

- `ArcGISClient::query_active_watches()`. NOAA's WWA polygons do not contain
  the SPC fields in `WatchPayload`; use `ArchiveClient::watches()`.

## [0.1.1] - 2026-06-06

### Fixed

- **`parse_probabilistic` probability scale (100x bug).** The day-1/2/3
  probabilistic-outlook parser divided every isopleth value by 100
  unconditionally, assuming `LABEL`/`dn` is an integer percent (`"2"`,
  `"5"`). The live `www.spc.noaa.gov` and ArcGIS GeoJSON feeds actually
  ship the value as an already-normalized fraction (`"0.02"`, `"0.30"`),
  so a 2% tornado risk parsed to `0.0002` instead of `0.02`. Now
  normalized like the day4-8 path (`pct > 1.0 ? pct / 100.0 : pct`):
  integer percents are divided, fractions pass through. Both forms are
  pinned by regression tests (`Parser.ProbabilisticFractionalLabel`,
  `Corpus.ProbabilisticParsesAndScales`). Consumers that persist
  probabilities (e.g. `spc-data`'s `spc.prob_outlooks`) get correct
  `[0,1]` values after upgrading.

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
