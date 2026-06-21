#!/usr/bin/env bash
# Fetch a static ffmpeg binary for the current platform into
# src-tauri/resources/bin/, so `tauri build` bundles it and the packaged app
# needs no system ffmpeg. The binary is gitignored — run this before a release
# build. The app resolves the bundled binary first, then a system ffmpeg on PATH.
#
# Linux is intentionally NOT bundled: ffmpeg is a one-command package-manager
# install there, and the redistributable static builds are either unreliable on
# recent kernels or huge (~200 MB). Linux uses the system ffmpeg (PATH).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/src-tauri/resources/bin"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

os="$(uname -s)"
case "$os" in
  Linux)
    echo "Linux: ffmpeg is taken from the system PATH — install it with your"
    echo "package manager (e.g. 'sudo apt install ffmpeg'). Nothing to bundle."
    exit 0
    ;;
  Darwin)
    echo "Fetching ffmpeg (macOS)…"
    mkdir -p "$OUT"
    curl -fsSL -o "$tmp/f.zip" "https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip"
    unzip -o -q "$tmp/f.zip" -d "$tmp"
    cp "$tmp/ffmpeg" "$OUT/ffmpeg"
    chmod +x "$OUT/ffmpeg"
    ;;
  MINGW* | MSYS* | CYGWIN*)
    echo "Fetching ffmpeg (windows x64)…"
    mkdir -p "$OUT"
    curl -fsSL -o "$tmp/f.zip" "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download/ffmpeg-master-latest-win64-gpl.zip"
    unzip -o -q "$tmp/f.zip" -d "$tmp"
    cp "$tmp"/*/bin/ffmpeg.exe "$OUT/ffmpeg.exe"
    ;;
  *)
    echo "Unsupported OS: $os" >&2
    exit 1
    ;;
esac

echo "ffmpeg ready in $OUT"
