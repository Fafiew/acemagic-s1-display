#!/usr/bin/env bash
# run_host_tests.sh - build and run the portable core unit tests on any host.
# These tests do NOT require Windows or the S1 hardware.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# Prefer a cmake on PATH, else the one installed by pip.
CMAKE="${CMAKE:-$(command -v cmake || echo "$HOME/.local/bin/cmake")}"
if [ ! -x "$CMAKE" ]; then
  echo "cmake not found; install it (pip install cmake) or set \$CMAKE" >&2
  exit 2
fi

echo "== configuring =="
"$CMAKE" -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
echo "== building =="
"$CMAKE" --build build -j"$(nproc)"
echo "== testing =="
"$CMAKE" --build build --target test
echo "All host tests passed."
