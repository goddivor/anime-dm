# Anime Download Manager

An **IDM-style** download manager for anime sites, built with **Tauri 2** (Rust backend)
and **React 19 + Vite + TypeScript** (frontend).

The app itself contains **zero site-specific code**. Every source — how to list an anime's
episodes and how to extract a playable video from a host — lives in a **sandboxed WebAssembly
addon**, in the spirit of Aniyomi extensions. You install the sources you want from an
**Addon Store**, and the app stays small, generic and easy to ship.

## How it works

1. **Install a source** from the Addon Store (paste a repo index URL, pick an addon, install).
2. **Add a download**: choose the source, paste an anime link — the app resolves the title,
   poster and episode list through the addon.
3. **Pick episodes** (a `1-20` / `1,5,8` text range, or a visual grid) and, optionally, the
   **player** and **destination folder**.
4. Downloads run **concurrently** (with a queue limit) and survive restarts.

## Sources (addons)

Sources are distributed as a **repo**: an `index.min.json` index plus one `.wasm` module per
source. You point the app at a repo URL in the Addon Store and install from there.

- **Official source repo:** https://github.com/goddivor/anime-dm-addons
- Live index served from its `repo` branch:
  `https://raw.githubusercontent.com/goddivor/anime-dm-addons/repo/index.min.json`

Each addon **declares its own settings** (site URL, preferred player, quality…) which the app
renders and stores; the app re-injects them when it loads the module. Addons are **versioned**,
so the Store shows when an update is available.

Want to write your own source? See the addons repo — the shared contract lives in this repo
under `src-tauri/addon-api`.

## Features

- Source-agnostic **WASM addon** runtime (Aniyomi-style), with an in-app **Addon Store**
- Per-addon settings + **addon update detection** (installed vs repo version)
- Episode selection by **text range** or **visual grid**, with a per-episode **player override**
  and a global player picker
- **Destination folder** picker (native dialog)
- **Concurrent** downloads with a queue limit, **pause / resume** (survives app restart)
- **SQLite** persistence of downloads and anime groups
- Downloads table with status, progress and throughput; multi-select, keyboard navigation,
  context menu
- Bilingual UI (FR / EN), persisted language

## Requirements

- **Rust** (edition 2021) and Cargo, **Node.js**
- **ffmpeg** on `PATH` (fetch / remux of MP4 and HLS)
- Linux webview libraries: `libwebkit2gtk-4.1-dev`, `libsoup-3.0-dev`, …

No headless browser is required — addons decode the players over plain HTTP.

## Run

```bash
npm install
npm run tauri dev      # full app (Rust + frontend)
npm run dev            # frontend only
```

Build checks:

```bash
npm run build                       # frontend, 0 error
cd src-tauri && cargo build         # backend, 0 warning
```

## Architecture

- **`src-tauri/addon-api/`** — the shared contract crate (serde models: `Anime`, `Episode`,
  `Hoster`, `Video`, `Preference`…) referenced by both the app and the addons.
- **`src-tauri/src/`** — Rust backend:
  - `addons.rs` — Extism loader + on-disk registry (install / remove / open / config).
  - `db.rs` — SQLite persistence (`downloads` + `anime_groups`), restored on startup.
  - `worker/downloader.rs` — fetches the resolved video (MP4 / HLS) via ffmpeg with real progress.
  - `lib.rs` — Tauri commands: Addon Store, source operations (`load_anime`, `list_hosters`,
    `start_download`), settings, persistence; emits `download://progress|finished` events.
- **`src/`** — React frontend: downloads view, Addon Store, add-download dialog, components.

### Flow

Addon Store installs a `.wasm` → the add dialog calls `load_anime(addonId, url)` (the addon's
`anime_details` + `episode_list`) → `start_download(...)` resolves a host via the addon's
`hoster_list` + `video_list`, then downloads the resulting `Video`. User settings (site URL,
preferred player, quality) are re-injected into the module on every load.

## License

See repository.
