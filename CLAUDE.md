# spc-cpp

C++23 client for NOAA Storm Prediction Center data. `make help` lists the
targets; CONTRIBUTING.md has the full check list and the macOS clang-tidy recipe.

## Checks

Run before calling work done. CI runs the same gates:

```bash
make test && make lint && make lint-md && make fixtures-check && make test-consumers
```

`python3 tools/verify_arcgis_metadata.py` checks the live NOAA layer table. Run it
when you touch ArcGIS layer ids; it needs the network.

## Architecture

- Static libraries `spc_core -> spc_http -> spc_models -> spc_api`; consumers
  link `spc::spc`.
- Public failures return `Result<T>` (`std::expected<T, Error>`). The standalone
  `parse_*` functions throw `std::runtime_error` on malformed JSON; the clients
  turn that into `ErrorCode::ParseError`.
- Glaze is private. It lives behind `src/models/json.hpp`, and no header under
  `include/` may include it; `tools/test_consumers.sh` fails if an install
  ships Glaze headers.
- `HttpClient` is thread-safe through a pool of libcurl handles. Custom
  transports implement `HttpTransport::get`, which must be safe to call
  concurrently.
- The version comes from `project(spc-cpp VERSION ...)` through the generated
  `spc/version.hpp`. Bumping `CMakeLists.txt` is the whole version change.

## Invariants

- Spell out types; `tools/cpp_auto_audit.py` allows `auto` only for iterators,
  structured bindings, and lambdas.
- Keep the Day 1-3 parser output identical to the internal spc-data service:
  key-case fallbacks, numeric strings, and outer rings only.
- Give each product its own label mapper. Only categorical outlooks use
  `severity_from_label`.
- Test clients through `HttpTransport` or the loopback server in
  `tests/support/`. Unit tests stay offline.
- `tests/fixtures/arcgis_layers_2026-09-03.json` is the contract for `kLayers`
  in `src/api/client.cpp`. Change them together.
- Watches come from IEM (`ArchiveClient::watches()`); NOAA's WWA layer lacks
  SPC's watch fields.
- IEM's `lsr.geojson` filters by office with `wfos=`; it ignores `wfo=`.
- After changing a fixture, regenerate `tests/fixtures/SHA256SUMS` (recipe in
  CONTRIBUTING.md).
- Docs and comments stay short: say why, leave history to git and the
  changelog.

## Release

Squash-merge pull requests. A `vX.Y.Z` tag triggers `release.yml`, which checks
the tag against `project(spc-cpp VERSION ...)`, runs the tests and consumer
checks, and publishes the matching `CHANGELOG.md` section. Steps are in
CONTRIBUTING.md.
