#!/usr/bin/env bash
# Fetch the aria2c binary for the current platform into src-tauri/resources/bin/,
# so `tauri build` bundles it (the "Accelerated download" option). Gitignored —
# run before a release build. The app resolves a bundled aria2c first, then PATH.
#
# Linux is not bundled: aria2 is a one-command package-manager install. macOS has
# no official static binary, so it relies on a system aria2 (brew) or falls back.
set -euo pipefail

ARIA2_VERSION="1.37.0"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/src-tauri/resources/bin"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

os="$(uname -s)"
case "$os" in
  Linux)
    echo "Linux: aria2 is taken from the system PATH (e.g. 'sudo apt install aria2')."
    exit 0
    ;;
  Darwin)
    echo "macOS: no official aria2 binary to bundle — install via 'brew install aria2'"
    echo "(the app falls back to ffmpeg if aria2 is missing)."
    exit 0
    ;;
  MINGW* | MSYS* | CYGWIN*)
    echo "Fetching aria2 ${ARIA2_VERSION} (windows x64)…"
    mkdir -p "$OUT"
    url="https://github.com/aria2/aria2/releases/download/release-${ARIA2_VERSION}/aria2-${ARIA2_VERSION}-win-64bit-build1.zip"
    curl -fsSL -o "$tmp/a.zip" "$url"
    unzip -o -q "$tmp/a.zip" -d "$tmp"
    cp "$tmp"/*/aria2c.exe "$OUT/aria2c.exe"
    echo "aria2c ready in $OUT"
    ;;
  *)
    echo "Unsupported OS: $os" >&2
    exit 1
    ;;
esac
