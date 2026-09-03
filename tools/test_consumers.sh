#!/usr/bin/env bash
set -euo pipefail

spc_source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
spc_scratch_dir=$(mktemp -d "${TMPDIR:-/tmp}/spc-consumer.XXXXXX")
trap 'rm -rf "$spc_scratch_dir"' EXIT

cmake -S "$spc_source_dir" -B "$spc_scratch_dir/sdk" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$spc_scratch_dir/prefix" \
  -DSPC_BUILD_TESTS=OFF \
  -DSPC_BUILD_EXAMPLES=OFF
cmake --build "$spc_scratch_dir/sdk" --parallel
cmake --install "$spc_scratch_dir/sdk"

cmake -S "$spc_source_dir/tests/consumer/installed" \
  -B "$spc_scratch_dir/installed" \
  -DCMAKE_PREFIX_PATH="$spc_scratch_dir/prefix"
cmake --build "$spc_scratch_dir/installed" --parallel
"$spc_scratch_dir/installed/spc_installed_consumer"

cmake -S "$spc_source_dir/tests/consumer/fetchcontent" \
  -B "$spc_scratch_dir/fetchcontent" \
  -DSPC_SOURCE_DIR="$spc_source_dir"
cmake --build "$spc_scratch_dir/fetchcontent" --parallel
"$spc_scratch_dir/fetchcontent/spc_fetchcontent_consumer"
