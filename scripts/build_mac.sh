#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$ROOT" -B "$ROOT/build-native" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build "$ROOT/build-native" --parallel 4
ctest --test-dir "$ROOT/build-native" --output-on-failure
printf '\nApp: %s/build-native/desktop_snow.app\n' "$ROOT"
