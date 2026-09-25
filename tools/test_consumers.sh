#!/usr/bin/env bash
# Build and run a tiny consumer against an installed spc-cpp and against a
# FetchContent checkout. The installed consumer asks for the version the
# README's find_package() line documents, and the FetchContent build also
# compiles the README's quick start. The README must pin the current version.
set -euo pipefail

source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
scratch_dir=$(mktemp -d "${TMPDIR:-/tmp}/spc-consumer.XXXXXX")
trap 'rm -rf "$scratch_dir"' EXIT
readme="$source_dir/README.md"

documented_version=$(sed -nE 's/^find_package\(spc ([0-9.]+) REQUIRED\)$/\1/p' "$readme")
if [ -z "$documented_version" ]; then
  echo "README.md has no 'find_package(spc X.Y REQUIRED)' line" >&2
  exit 1
fi

version=$(sed -nE 's/^#define SPC_VERSION_(MAJOR|MINOR|PATCH) ([0-9]+)$/\2/p' \
  "$source_dir/include/spc/version.hpp" | paste -sd . -)
if ! grep -qx "    GIT_TAG v$version" "$readme"; then
  echo "README.md's FetchContent snippet must pin GIT_TAG v$version" >&2
  exit 1
fi

# The first C++ block, the quick start. It needs the network, so it only
# compiles.
quick_start="$scratch_dir/quick_start.cpp"
awk '/^```cpp$/ { inside = 1; next } inside && /^```$/ { exit } inside' "$readme" >"$quick_start"
if ! grep -q 'int main' "$quick_start"; then
  echo "README.md's first C++ block is not a whole program" >&2
  exit 1
fi

cmake -S "$source_dir" -B "$scratch_dir/sdk" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$scratch_dir/prefix" -DSPC_BUILD_TESTS=OFF -DSPC_BUILD_EXAMPLES=OFF
cmake --build "$scratch_dir/sdk" --parallel
cmake --install "$scratch_dir/sdk"

if [ -e "$scratch_dir/prefix/include/glaze" ]; then
  echo "the install must not ship Glaze headers" >&2
  exit 1
fi

cmake -S "$source_dir/tests/consumer/installed" -B "$scratch_dir/installed" \
  -DCMAKE_PREFIX_PATH="$scratch_dir/prefix" -DSPC_EXPECTED_VERSION="$documented_version"
cmake --build "$scratch_dir/installed" --parallel
"$scratch_dir/installed/spc_installed_consumer"

cmake -S "$source_dir/tests/consumer/fetchcontent" -B "$scratch_dir/fetchcontent" \
  -DSPC_SOURCE_DIR="$source_dir" -DSPC_QUICK_START="$quick_start"
cmake --build "$scratch_dir/fetchcontent" --parallel
"$scratch_dir/fetchcontent/spc_fetchcontent_consumer"

echo "consumer checks passed (v$version, find_package(spc $documented_version))"
