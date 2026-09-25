#!/usr/bin/env bash
# Build and run a tiny consumer against an installed spc-cpp and against a
# FetchContent checkout. The installed consumer asks for the version the
# README's find_package() line documents.
set -euo pipefail

source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
scratch_dir=$(mktemp -d "${TMPDIR:-/tmp}/spc-consumer.XXXXXX")
trap 'rm -rf "$scratch_dir"' EXIT

documented_version=$(sed -nE 's/^find_package\(spc ([0-9.]+) REQUIRED\)$/\1/p' "$source_dir/README.md")
if [ -z "$documented_version" ]; then
  echo "README.md has no 'find_package(spc X.Y REQUIRED)' line" >&2
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
  -DSPC_SOURCE_DIR="$source_dir"
cmake --build "$scratch_dir/fetchcontent" --parallel
"$scratch_dir/fetchcontent/spc_fetchcontent_consumer"

echo "consumer checks passed (find_package(spc $documented_version))"
