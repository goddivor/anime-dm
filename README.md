<div align="center">

<img src="assets/logo.png" alt="Anime Download Manager" width="140" />

# Anime Download Manager

**An IDM-style download manager for anime sites** — built with Tauri 2 (Rust) and React 19.
Every source lives in a sandboxed WebAssembly addon, so the app stays small, generic and fast.

[![C++](https://img.shields.io/badge/C%2B%2B-00599C?logo=cplusplus&logoColor=white&style=flat)](https://isocpp.org/)
[![Win32](https://img.shields.io/badge/Win32-0078D6?logo=windows&logoColor=white&style=flat)](https://learn.microsoft.com/windows/win32/)
[![CMake](https://img.shields.io/badge/CMake-064F8C?logo=cmake&logoColor=white&style=flat)](https://cmake.org/)
[![Rust](https://img.shields.io/badge/Rust-000000?logo=rust&logoColor=white&style=flat)](https://www.rust-lang.org/)
[![JavaScript](https://img.shields.io/badge/JavaScript-F7DF1E?logo=javascript&logoColor=black&style=flat)](https://developer.mozilla.org/en-US/docs/Web/JavaScript)

</div>

## Gallery

<p align="center">
  <img src="assets/screenshot-app.png" alt="Downloads and categories" width="85%" />
</p>

<p align="center">
  <img src="assets/screenshot-folder-icons.png" alt="Per-anime folder icons" width="85%" />
</p>

## Sources

The app ships with **no source built in** — to actually download anything, you install the sites you want from the in-app **Addon Store**. Add a repo, pick the addons you need, and you're ready to go.

Start with the official addons repository:

**→ [anime-dm-addons](https://github.com/goddivor/anime-dm-addons)**

## Requirements

| Platform    | Requirements                                                                                                                                              |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **All**     | [Rust](https://www.rust-lang.org/) (edition 2021) + Cargo · [Node.js](https://nodejs.org/) · [ffmpeg](https://ffmpeg.org/) on `PATH` (MP4 / HLS fetch & remux) |
| **Linux**   | `libwebkit2gtk-4.1-dev`, `libgtk-3-dev`, `libsoup-3.0-dev`, `librsvg2-dev`, `libssl-dev`, `build-essential`, `patchelf`                                    |
| **Windows** | WebView2 runtime (preinstalled on Windows 10/11) · MSVC C++ Build Tools                                                                                    |
| **macOS**   | Xcode Command Line Tools (`xcode-select --install`)                                                                                                        |

On **Windows / macOS**, `ffmpeg` is **bundled into release builds** (no system install needed) — run `scripts/fetch-ffmpeg.sh` before `tauri build` to fetch it. On **Linux** it is taken from the system `PATH` (install it with your package manager). The app resolves a bundled `ffmpeg` first, then the `PATH`.

## Getting started

```bash
npm install
npm run tauri dev      # full app (Rust + frontend)
npm run dev            # frontend only
```

Build checks:

```bash
npm run build                    # frontend — 0 error
cd src-tauri && cargo build      # backend — 0 warning
```

## License

See repository.
