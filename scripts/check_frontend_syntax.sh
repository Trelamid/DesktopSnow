#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsyntax-only \
  -I"$ROOT/tests/glfw_stub" -I"$ROOT/include" \
  "$ROOT/src/main.cpp" "$ROOT/src/renderer.cpp"
echo "Common GLFW-facing desktop C++ sources: syntax OK (stub declarations; native link not verified)."
