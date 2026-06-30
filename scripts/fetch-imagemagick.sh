#!/usr/bin/env bash
# Fetch a portable ImageMagick (magick.exe) for Windows into
# src-tauri/resources/folder-templates/bin/, so folder-icon generation works on
# a clean machine with nothing installed. Gitignored; run before a release build.
#
# Linux ships a committed `magick` binary already. macOS has no static portable
# build, so it falls back to a system ImageMagick (brew).
set -euo pipefail

IM_VERSION="7.1.2-26"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/src-tauri/resources/folder-templates/bin"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

os="$(uname -s)"
case "$os" in
  Linux)
    echo "Linux: the bundled 'magick' binary is committed in the repo. Nothing to fetch."
    exit 0
    ;;
  Darwin)
    echo "macOS: install ImageMagick with 'brew install imagemagick' (no portable build to bundle)."
    exit 0
    ;;
  MINGW* | MSYS* | CYGWIN*)
    echo "Fetching ImageMagick ${IM_VERSION} (windows x64)…"
    mkdir -p "$OUT"
    url="https://github.com/ImageMagick/ImageMagick/releases/download/${IM_VERSION}/ImageMagick-${IM_VERSION}-portable-Q16-HDRI-x64.7z"
    curl -fsSL -o "$tmp/im.7z" "$url"
    7z x -y -o"$tmp/im" "$tmp/im.7z" >/dev/null
    cp "$(find "$tmp/im" -name magick.exe | head -1)" "$OUT/magick.exe"
    echo "magick.exe ready in $OUT"
    ;;
  *)
    echo "Unsupported OS: $os" >&2
    exit 1
    ;;
esac
