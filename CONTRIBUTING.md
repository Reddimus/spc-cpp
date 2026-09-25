# Contributing

Thanks for helping. Report security problems privately through
[SECURITY.md](SECURITY.md), not in a public issue.

## Set up

```bash
git clone https://github.com/Reddimus/spc-cpp.git
cd spc-cpp

# Ubuntu 24.04
sudo apt install build-essential cmake clang-format libcurl4-openssl-dev
# macOS
brew install cmake clang-format curl

make test
make install-hooks   # optional: format and lint before each commit
```

`make help` lists every target.

## Before you push

CI runs all of these, so running them first saves a round trip:

```bash
make test             # build and unit tests
make lint             # clang-format and the auto audit
make lint-md          # Markdown lint (needs Node)
make fixtures-check   # fixture checksums
make test-consumers   # installed and FetchContent consumers
```

CI also runs the tests with ASan and UBSan, with TSan, and with Clang 18 and
libc++, and it compiles the libraries with clang-tidy. To match those locally:

```bash
make test BUILD_DIR=build-asan CMAKE_ARGS=-DSPC_ENABLE_SANITIZERS=ON
make test BUILD_DIR=build-tsan CMAKE_ARGS=-DSPC_ENABLE_THREAD_SANITIZER=ON
make tidy
```

On macOS, `make tidy` needs Homebrew's LLVM and curl. Apple's SDK copy of
curl adds an include path that breaks non-Apple Clang:

```bash
brew install llvm@18 curl
llvm=$(brew --prefix llvm@18)
make tidy CMAKE_ARGS="-DCMAKE_CXX_COMPILER=$llvm/bin/clang++ \
  -DSPC_CLANG_TIDY_EXECUTABLE=$llvm/bin/clang-tidy -DCMAKE_PREFIX_PATH=$(brew --prefix curl)"
```

## Code style

- `.clang-format` decides layout: tabs, 100 columns. `make format` applies it.
- Spell out types. `auto` is allowed only for iterators, structured
  bindings, and lambdas, and `tools/cpp_auto_audit.py` enforces that.
  Mark a genuine exception with `// auto-ok`.
- Public failures return `spc::Result<T>`. Client methods never throw for
  network or data problems.
- Keep comments short and about why. History belongs in commit messages and
  the changelog.

## Rules that are easy to break

- **The Day 1-3 parser matches spc-data.** `parse_categorical`,
  `parse_probabilistic`, `severity_from_label`, `point_in_polygon`, and the
  helpers they call in `src/models/json.hpp` must give the same output as
  the internal spc-data service. Key-case fallbacks, numeric strings, and
  outer-rings-only geometry are part of that.
- **Each product has its own label mapping.** Fire weather, conditional
  intensity, and watches must not reuse `severity_from_label`.
- **Tests never use the network.** Test clients through `HttpTransport`, as
  `tests/test_client.cpp` does, or against the loopback server in
  `tests/support/`.
- **The ArcGIS layer table has a contract.** When NOAA renumbers layers,
  update `kLayers` in `src/api/client.cpp` and
  `tests/fixtures/arcgis_layers_2026-09-03.json` together, then run
  `python3 tools/verify_arcgis_metadata.py`, which checks the live services.

## Fixtures

`tests/fixtures/` holds captured NOAA and IEM responses. After adding or
changing one, update the checksums:

```bash
for f in $(ls tests/fixtures | grep -v -e README.md -e SHA256SUMS); do
  shasum -a 256 "tests/fixtures/$f"
done > tests/fixtures/SHA256SUMS
make fixtures-check
```

Say where the file came from in `tests/fixtures/README.md`.

## Pull requests

- Branch from `main` and open a pull request against it. Every CI job must
  pass before merging.
- Use a conventional-commit title, such as `fix(api): ...`.
- Add user-visible changes to `CHANGELOG.md` under `[Unreleased]`.
- Pull requests are squash-merged, so the title becomes the commit on `main`.

## Releasing

1. In a pull request, set `project(spc-cpp VERSION X.Y.Z)` in
   `CMakeLists.txt`, move `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD` in
   `CHANGELOG.md`, and update the `GIT_TAG` and `find_package` versions in
   `README.md`.
2. After it merges, tag `main` and push the tag:
   `git tag vX.Y.Z && git push origin vX.Y.Z`.
3. The release workflow checks that the tag matches the CMake version, runs
   the tests and consumer checks, then publishes the release with the
   changelog section as its notes.
