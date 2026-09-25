<div align="center">

<img src="assets/logo.png" alt="Anime Download Manager" width="140" />

# Anime Download Manager

**An IDM-style download manager for anime sites** — a native Windows application in C++ and Win32.
Every source lives in an add-on, a native library loaded at run time, so the app stays small, generic and fast.

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

## Features

- **Fast, IDM-style downloads** — each video is split into segments fetched over up to 16 connections, resumed where it stopped after a pause, a crash or a reboot. HLS playlists are handled too (AES-128 included), with no ffmpeg.
- **Built around animes** — the categories panel groups the episodes under their anime and its poster; pick the episodes and the player of each, in windows of their own, as many at once as you like.
- **Batch from the clipboard** (Ctrl+Shift+V) — copy a list of pages, get one add window per anime, source already chosen.
- **Browser extension** for Chromium browsers and Firefox — a button on anime and episode pages, and over the links that lead to one.
- **Scheduler** — two queues with timed start and stop, and followed animes: new episodes are queued on release day, the ones already out are offered when you start following.
- **Folder icons** made from the poster, after the recipes of RightClickFolderIconTools, and `cover.jpg` for Aniyomi.
- **Export and import** — the ADM file, plain text, JSON, CSV, Excel and OpenDocument.
- **Beside the clock** — the close box hides the window; downloads, schedules and the extension go on, and a notification tells when an episode is done.
- **Dark and light themes**, French and English, toolbar skins (IDM `.tbi` skins, animated sprite packs, Neon).
- **Updates itself** — a new version is offered as soon as it is published.

## Install

Download `AnimeDownloadManager-<version>-setup.exe` from the [releases](https://github.com/goddivor/anime-dm/releases) tagged `win-v…` and run it. It needs **Windows 10 or 11, 64-bit**, and brings everything the application uses, ImageMagick included.

The application goes to `Program Files`; your list and settings live in `%APPDATA%\anime-dm`. Uninstalling removes them but keeps your add-ons, unless you ask otherwise; your downloaded videos are never touched.

## Sources

The app ships with **no source built in** — to actually download anything, install the sites you want from the in-app **Addon Store** (the *Addons* button). The store lists the reviewed add-ons of the official repository; to propose one, open a pull request there:

**→ [anime-dm-addons](https://github.com/goddivor/anime-dm-addons)**

## Browser extension

The extension lives in [`extension/`](extension/). Until it is published in the stores, load it as an unpacked extension (see [`extension/README.md`](extension/README.md)), then tick your browsers in *Options › General*.

## Build from source

Requirements: Windows 10/11, [MinGW-w64](https://www.mingw-w64.org/) (GCC, UCRT) and [CMake](https://cmake.org/) 3.20 or later.

```bash
cmake -S . -B build
cmake --build build --target anime-dm   # no error, no warning
```

The installer is built with [Inno Setup](https://jrsoftware.org/isinfo.php) 6 (see [`installer/README.md`](installer/README.md)); a merge into `feature/win32-cpp` builds and publishes it automatically.

## License

See repository.
