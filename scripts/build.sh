#!/usr/bin/env bash
# Configure, build, and (optionally) package StegoNinja CLI/TUI.
#
# Usage:
#   scripts/build.sh              # configure + build into ./build
#   scripts/build.sh --package    # also produce a dist tarball via CPack
#   scripts/build.sh --install <prefix>   # also install into <prefix>
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"

cmake -S "$ROOT" -B "$BUILD"
cmake --build "$BUILD" -j "$(nproc 2>/dev/null || echo 2)"

while [ $# -gt 0 ]; do
  case "$1" in
    --package)
      ( cd "$BUILD" && cpack )
      shift ;;
    --install)
      prefix="${2:?--install needs a prefix}"
      cmake --install "$BUILD" --prefix "$prefix"
      shift 2 ;;
    *)
      echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

echo "Built binaries in $BUILD:"
ls -1 "$BUILD" | grep -E '^(audio|imgBPCS(Embed|Extract)|SteganoImgLsb|SteganoVid)$' || true
