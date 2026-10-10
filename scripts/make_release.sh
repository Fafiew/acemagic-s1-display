#!/usr/bin/env bash
# make_release.sh - build and package the portable core for a release.
#
# Produces, in dist/:
#   acemagic-s1-display-<version>-src.tar.gz          full source
#   acemagic-s1-display-<version>-linux-x64.tar.gz    portable binaries + lib + tests
#   SHA256SUMS.txt                                     checksums of the above
#
# The Windows driver and control app are NOT built here: they require the WDK
# and the .NET SDK. See docs/build.md. This script packages the components that
# build on a plain C++ toolchain, so a release is reproducible from source on
# any machine.
#
# Usage: scripts/make_release.sh [version]
set -euo pipefail

VERSION="${1:-1.0.0}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# CMake may be pip-installed.
export PATH="$HOME/.local/bin:$PATH"
CMAKE="${CMAKE:-cmake}"
CTEST="${CTEST:-ctest}"

DIST="$ROOT/dist"
BUILD="$ROOT/build-release"
NAME="acemagic-s1-display-$VERSION"
STAGE="$DIST/$NAME"

echo "==> Cleaning"
rm -rf "$DIST" "$BUILD"
mkdir -p "$DIST"

echo "==> Configuring (Release)"
"$CMAKE" -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release

echo "==> Building"
"$CMAKE" --build "$BUILD" -j

echo "==> Running tests"
"$CTEST" --test-dir "$BUILD" --output-on-failure

echo "==> Staging portable binaries"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/docs"
cp "$BUILD/renderer" "$STAGE/bin/"
cp "$BUILD/s1test"   "$STAGE/bin/"
cp "$BUILD/libs1_core.a" "$STAGE/lib/"
cp docs/*.md "$STAGE/docs/"
cp README.md LICENSE AGENTS.md "$STAGE/"

cat > "$STAGE/README.txt" <<EOF
ACEMAGIC S1 Extended Display $VERSION - portable core (linux-x64)

This package contains the OS-independent core and the portable executables that
build on any C++17 toolchain:

  bin/renderer   the renderer process (use --selftest or --mock for testing)
  bin/s1test     standalone LCD/USB test utility (--mock for no hardware)
  lib/libs1_core.a  static library (protocol, IPC layout, device engine, pipeline)
  docs/          research, architecture, protocol, build, install, testing, ...

The Windows IddCx driver and the WPF control app are NOT in this package: they
require the Windows Driver Kit and the .NET 8 SDK. Build them on Windows with
docs/build.md and docs/installation.md.

Quick self-test:
  bin/renderer --selftest --fps 10 --frames 5
  bin/s1test --mock pattern testsrc
EOF

echo "==> Packaging source"
git archive --format=tar.gz --prefix="$NAME/" \
    -o "$DIST/$NAME-src.tar.gz" HEAD

echo "==> Packaging binaries"
tar -C "$DIST" -czf "$DIST/$NAME-linux-x64.tar.gz" "$NAME"

echo "==> Checksums"
( cd "$DIST" && sha256sum "$NAME-src.tar.gz" "$NAME-linux-x64.tar.gz" > SHA256SUMS.txt )

echo "==> Done"
ls -la "$DIST"
cat "$DIST/SHA256SUMS.txt"
