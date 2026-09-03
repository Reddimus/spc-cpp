# spc-cpp development guide

## Commands

```bash
make build
make test
make lint
make test-consumers
python3 tools/verify_arcgis_metadata.py  # live, opt-in
```

Use `-DSPC_ENABLE_SANITIZERS=ON` for ASan and UBSan. Use
`-DSPC_ENABLE_CLANG_TIDY=ON` for the configured clang-tidy gate.

## Architecture

The static-library chain is `spc_core -> spc_http -> spc_models -> spc_api`.
Consumers normally link `spc::spc`.

All public failures return `Result<T>`, which aliases `std::expected<T,
Error>`. High-level clients use `HttpClient` by default and accept a shared
`HttpTransport` for custom networking and tests.

Glaze 8.3 parses loose SPC JSON. GoogleTest 1.18 runs the unit suite.

## Invariants

- Use C++23 and explicit types. `tools/cpp_auto_audit.py` enforces the narrow
  exceptions for `auto`.
- Follow `.clang-format`, use the `spc` namespace, and place project includes
  before system includes.
- Keep the convective parser aligned with the internal `spc-data` parser. Its
  key-case, number conversion, and Polygon or MultiPolygon behavior are
  compatibility requirements.
- Give each new product its own label mapper. Fire weather and watch labels do
  not use `severity_from_label`.
- Test client behavior through `HttpTransport`. Unit tests must not require
  NOAA or IEM access.
- Treat `tests/fixtures/arcgis_layers_2026-09-03.json` as the ArcGIS routing
  contract. Run the live metadata check when changing layer IDs.
- Resolve SPC watch boxes through `ArchiveClient::watches()`. NOAA WWA polygons
  have a different schema.

## Release

The tag must match `project(spc-cpp VERSION ...)`. A `vX.Y.Z` tag triggers the
release workflow, which reads the matching `CHANGELOG.md` section.
