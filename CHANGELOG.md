# Changelog

All notable changes to **spc-cpp** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- `parse_storm_reports` sizes its list of reports once instead of growing it
  one report at a time. On a 624-report day the result holds about 30% less
  memory, since the list no longer keeps the spare room that growth leaves,
  and parsing allocates about 12% fewer bytes.
- The typed `ArcGISClient` queries parse each page once instead of twice,
  so they do about half the CPU work and make about half the allocations.
  Every client also frees a response's text before building the result, so
  a large response such as a day of storm reports peaks lower in memory.

### Fixed

- Builds without floating-point `std::from_chars` (libc++ before 20, and
  macOS targets before 26) parsed numeric strings such as probability labels
  with a string stream, which disagreed with every other build. It read
  `0x10` as 16 and rejected `12abc`, `inf`, `nan`, subnormals such as
  `1e-310`, and a bare trailing exponent such as `5e`, so those fields became
  0 on those builds. Every platform now uses the fast_float code that Glaze
  already bundles, which also parses a number in about a tenth of the
  stream's instructions.
- The `parse_*` functions could read past the end of the `std::string_view`
  they were given. A view over the start of a larger buffer could parse as
  if it were the whole buffer, and a view that ran to the end of its buffer
  was read one byte past it. Clients are unaffected, since they parse the
  strings they own.
- The `ErrorCode` comments in `error.hpp` now agree with the README: a bad
  ArcGIS layer id is `InvalidRequest`, HTTP 503 is `RateLimited`, and
  `ParseError` means the body was not JSON.

## [0.4.1] - 2026-09-25

### Added

- `make bench` runs a Google Benchmark suite over every parser and the
  ArcGIS and static-feed clients. Each benchmark also reports the
  allocations and peak and retained heap of one call, and on macOS the
  instructions per call. The `SPC_BUILD_BENCHMARKS` option that builds it is
  off by default and installs nothing.

### Changed

- CI's macOS job builds the way the README does, with Apple Clang and the
  system libcurl, as a universal arm64 and x86_64 binary for macOS 13.4. It
  tests both slices and the consumer checks, then runs the same binary on
  macOS 15 and on an Intel Mac.
- The README leads with a copy-paste quick start and a table of which call
  fetches each product.

### Fixed

- On macOS, any `CMAKE_OSX_DEPLOYMENT_TARGET` below 26.0 failed to compile.
  Floating-point `std::from_chars` was used whenever libc++ was version 20 or
  newer, but Apple's libc++ provides it only from macOS 26. Older targets now
  use the stream parser, so the earliest supported macOS is 13.4.
- The README's error table was wrong about `NotFound`, `ServerError`, and
  `ParseError`, omitted `Unknown`, and overstated Clang 18 support.

## [0.4.0] - 2026-09-25

### Added

- `Watch::year`. Watch numbers restart every year, so `number` alone is not
  unique. `StormReport::wfo` names the issuing NWS office.
- `spc/version.hpp` with `SPC_VERSION_MAJOR`, `SPC_VERSION_MINOR`,
  `SPC_VERSION_PATCH`, `SPC_VERSION_STRING`, and `spc::version()`. It is
  the single source of the version; CMake reads it.
- `point_in_feature` accepts any feature with `rings`: fire weather, Day 4-8,
  conditional intensity, watches, and mesoscale discussions.
- `QueryParams::order_by_fields`. The typed `ArcGISClient` queries order by
  `objectid` so pages stay stable while paging.
- The `SPC_INSTALL` CMake option.
- Examples for fire weather and watches.

### Changed

- **Breaking:** Glaze is now private. No installed header includes it, the
  install no longer ships its headers, and `spc/models/common.hpp`,
  `spc::Json`, and `spc::detail` are gone from the public API.
- **Breaking:** `find_package(spc X.Y)` accepts only the same minor version
  while the major version is 0, because minor releases may break the API.
- **Breaking:** building needs CMake 3.31, which Glaze 8.3 already required,
  and libcurl 7.85. Both are checked at configure time.
- `HttpClient` is safe to share between threads. Concurrent calls use a pool
  of libcurl handles, which keeps connections open between requests. Client
  methods are `const`, and client string parameters take `std::string_view`.
- `HttpResponse::headers` holds only the final response's headers, not those
  of redirect hops.
- `ArcGISClient` parses each page as it arrives and moves its features into
  the result, so peak memory is one page plus the result.
- `RateLimiter::acquire()` sleeps until the next token is due instead of
  polling every 10 ms, and a bounded wait returns false at once when no token
  can arrive in time.
- `ArcGISClient::query_layer` rejects a `QueryParams::f` other than `json`,
  `pjson`, or `geojson` before sending anything.
- **Breaking:** tests, examples, and install rules are on by default only in
  a top-level build, so FetchContent consumers no longer download
  GoogleTest. A consumer that installs its own target linking `spc::spc`
  must now set `SPC_INSTALL=ON`.
- A response over `ClientConfig::max_response_bytes` is `InvalidRequest`
  instead of `NetworkError`, so it is no longer downloaded again on every
  retry.
- `SPC_ENABLE_LTO` is off by default. LTO put compiler-specific bitcode in the
  installed static libraries, which another compiler or compiler version
  could not link.
- CI runs the tests with Clang 18 and libc++, the only toolchain that uses the
  locale-independent number parser's fallback. The release workflow now runs
  the tests and consumer checks before publishing.

### Removed

- **Breaking:** `ArcGISClient::query_active_watches()` and
  `ArcGISClient::query_storm_reports()`, deprecated since 0.2.0 and 0.3.0.
  They always failed. Use `ArchiveClient::watches()` and
  `ArchiveClient::storm_reports()`.
- **Breaking:** `ArcGISClient::query_layer(layer_id, params)`. Pass
  `ArcGISService::Outlooks` to the three-argument overload.
- `spc/geo.hpp` and `RetryResult`, which nothing used.
- The `SPC_NATIVE_ARCH` and `SPC_TUNE_X86_64_V3` options and the forced
  Release flags. Set `CMAKE_CXX_FLAGS` to tune a build.

### Fixed

- `ArchiveClient::storm_reports(start, end, wfo)` returned reports from every
  office. IEM ignores `wfo=` on `lsr.geojson` and filters on `wfos=`.
- `ArcGISClient::query_active_md()` reported NOAA's `NoArea` placeholder as a
  discussion when none was active.
- If the runtime libcurl rejected `CURLOPT_PROTOCOLS_STR`, `HttpClient` sent the
  request anyway and could read `file://` URLs. It now fails the request.
- An allocation failure in the libcurl write callback threw through C code.
- A `Retry-After` header on a redirect hop could set the retry delay.
- libcurl's global cleanup could run before a client created during static
  initialization freed its handle.
- Percent-encoding used `std::isalnum`, which accepts bytes above 0x7F in
  macOS UTF-8 locales, so those bytes reached the URL unencoded.
- A watch number or ArcGIS error code of `inf`, `nan`, or out of `int32`
  range was cast to an integer, which is undefined behavior.
- `RateLimiter` dropped the partial interval on every refill, so tokens
  arrived more slowly than configured. A `max_tokens` of 0 made `acquire()`
  wait forever; it is now treated as 1.
- A default-constructed `Error` left `code` uninitialized.
- ArcGIS error `details`, sent as an array, were dropped from `Error::detail`.
- `RateLimiter::available_tokens()` ignored tokens earned since the last
  acquire, and `daily_requests_remaining()` ignored a new UTC day.
- A `Retry-After` value too large for milliseconds overflowed.
- `HttpClient::config()` on a moved-from client dereferenced null.

## [0.3.0] - 2026-09-04

### Added

- `ArcGISClient::query_fire_weather()` documents its all-or-nothing contract:
  it merges two layers, and a failure on either discards both.
- `Error::from_arcgis`, for ArcGIS logical failure envelopes. It keeps the
  ArcGIS code in `Error::http_status` only while that code is HTTP-shaped
  (100..599) and records it in `Error::detail`, so a code such as 1000 can no
  longer masquerade as an HTTP status.

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
- Release builds no longer default to `-march=x86-64-v3`. The compiler check
  proved only that the compiler accepted the flag, not that the machine
  running the code has AVX2. Set `SPC_TUNE_X86_64_V3=ON` to opt in.
- The Esri-vs-GeoJSON parity test covers every captured fixture pair, not
  just day 1 categorical.
- CI generates `de_DE.UTF-8` so the locale tests run instead of skipping,
  runs with a read-only token, and pins actions to commit SHAs.
- The docs list every CI gate and `make` target.

### Deprecated

- `ArcGISClient::query_storm_reports()`. The SPC MapServer has no Local Storm
  Report layer, so the method always failed without touching the network — it
  now carries the attribute and doc comment its sibling
  `query_active_watches()` already had. Use `ArchiveClient::storm_reports()`.

### Removed

- The two-argument `parse_fire_weather(body, day)` overload. It silently
  assumed `FireWeatherLayer::Outlook`, so a dry-thunderstorm body decoded
  `dn=5` as `"ELEV"` (severity 1) instead of `"IDRT"` (severity 0) — the label
  confusion 0.2.0 fixed, still reachable through the public API. The captured
  day-1 and day-2 payloads carry no LABEL at all, only the shared numeric
  `dn`, so nothing in a body says which layer produced it. Pass the layer
  explicitly: `parse_fire_weather(body, day, FireWeatherLayer::Outlook)`
  restores the old behaviour where that was in fact the right layer.

### Fixed

- **ArcGIS paging advanced `resultOffset` by the requested page size, not by
  the records the server returned.** ArcGIS clamps `resultRecordCount` to the
  layer's own `maxRecordCount`, so a truncated page can be shorter than the
  2000 requested; the offset then skipped the gap and the caller got a
  successful result with a silent hole. `ArcGISPager::advance()` now takes the
  returned record count.
- **`HttpClient` reached the local filesystem.** `is_absolute_url()` only
  recognises `http://` and `https://`, so `file:///etc/hosts` was classified as
  relative, appended to the empty default `base_url`, and handed to libcurl,
  which read the file and returned it as the response body. The transport now
  restricts libcurl to `http` and `https` on the request and on redirects, and
  caps redirects at 10.
- `ClientConfig::max_response_bytes` (64 MB default) bounds a single response
  body. An IEM archive window is caller-chosen and unbounded, and the body was
  buffered whole, then parsed into a full JSON AST, then into the payload.
- Retry honours a `Retry-After` header on 429/503 (delta-seconds form, clamped
  to `max_delay`) instead of retrying a server that asked for 60 s after
  200 ms. The header was already captured and then discarded.
- Retry jitter is applied before the `max_delay` clamp, not after, so a delay
  can no longer exceed the documented ceiling by `jitter_factor`. A
  `max_attempts` of 0 now performs the request once instead of returning a
  fabricated "Max retry attempts exceeded" for a request never made.
- The default `User-Agent` is generated from `PROJECT_VERSION` instead of a
  hard-coded literal, and a test fails if the two ever disagree.
- **Numeric-as-string probabilities were locale-dependent.** `std::stod`
  delegates to `strtod`, which honours the process `LC_NUMERIC`. On a
  comma-decimal host — any application that calls `setlocale(LC_ALL, "")` on a
  de_DE / fr_FR / pt_BR desktop, as Qt and GTK apps do — `strtod("0.15")`
  consumed only `"0"` and returned 0 without throwing. The live Day 4-8 static
  feed carries its probability only as the string `"LABEL": "0.15"`, so the
  feature was dropped by the `probability > 0.0` gate and
  `StaticFeedClient::day4_8()` returned a successful, silently empty payload.
  The same parse gated fire weather's no-risk sentinel filter, so a
  `"Probability Too Low"` polygon shipped as a real band. Both now go through
  a locale-independent `spc::detail::parse_double`, which uses
  `std::from_chars` where the standard library provides it for `double` and a
  classic-locale stream elsewhere (libc++ only implements floating-point
  `from_chars` from version 20; the project's clang-tidy job builds against
  libc++ 18). Output is unchanged in the C locale for every value in the
  fixture corpus; the parse is narrower than `std::stod` only in rejecting
  leading whitespace and a leading `+`, neither of which any SPC payload uses.
- **`RateLimiter` crashed on a zero `refill_interval` (SIGFPE).**
  `RateLimiter::Config` is a public aggregate with no validation, so
  `RateLimiter{{.refill_interval = 0ms}}` reached an integer division by zero
  in `refill()` on the first `try_acquire()`. The constructor now clamps a
  non-positive interval to the 1000 ms default, and clamps `initial_tokens` to
  `max_tokens`.
- **`RateLimiter::acquire()` hung forever once a `daily_limit` was spent.**
  With no `Config::max_wait` it polls `try_acquire()` every 10 ms, and
  `try_acquire()` returns false permanently until the next UTC-midnight reset,
  so the caller's thread spun until then. It now returns false immediately
  when the daily quota is exhausted, since waiting cannot help.
- `ArchiveClient` interpolated `ts`, `sts`, `ets` and `wfo` into IEM query URLs
  with no percent-encoding, while the ArcGIS path in the same file encoded
  every value. `api.hpp` documents the timestamps as ISO 8601, which permits a
  `+HH:MM` offset, and a raw `+` decodes server-side as a space — so an
  offset-bearing timestamp silently queried a different window, and an `&` in
  any of the four injected extra query parameters. All four are now encoded.
- `ArchiveClient` now bounds its rate-limit wait (5 s) instead of blocking the
  caller's thread indefinitely, which also makes the documented
  `ErrorCode::RateLimited` result reachable, and acquires a token per retry
  attempt rather than per call — `with_retry` re-issues up to 4 requests, and
  retries precisely on 429/503, so one token was buying up to four requests
  exactly when IEM was asking for less traffic.
- ArcGIS paging is bounded. A page that reports truncation while carrying no
  records, and a server that never stops reporting truncation, now fail with
  `ErrorCode::ServerError` after at most `ArcGISPager::max_pages()` (100)
  requests instead of looping forever and growing memory without bound.
  `ArcGISPager::offset()` is a `std::int64_t`, so the arithmetic cannot
  overflow.

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

### Deprecated

- `ArcGISClient::query_active_watches()`. NOAA's WWA polygons do not contain
  the SPC fields in `WatchPayload`; use `ArchiveClient::watches()`.

### Fixed

- Corrected the Day 2 tornado and hail probability layers, Day 3 probability,
  and every Day 3 through 8 fire-weather layer.
- Decode NOAA's numeric Day 1 and 2 fire-weather `dn` categories without
  confusing outlook and dry-thunderstorm labels.
- Percent-encoded ArcGIS query values and parse ArcGIS error and paging fields
  as JSON.
- Invalid product combinations now fail before network access.

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
downstream byte-identity guarantee holds.
