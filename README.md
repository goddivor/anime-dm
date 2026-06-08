# Anime Download Manager

An **IDM-style** download manager, written in **Rust**, for anime sites.
**First supported site: `voir-anime.to`** — the architecture is designed to host more
(each site = its own scraping module + extractors).

GUI (egui): paste an anime link, the app lists the episodes, you pick a range
(`1-20`, `1,5,8`…) and a player, and downloads run **in parallel**.

## Current status

✅ Paste an anime URL → episode list (title + number)
✅ Episode selection by range/list (`1-20`, `1,5,8`, empty = all)
✅ Player selection (dropdown)
✅ Real video-source extraction and **concurrent** download via ffmpeg
✅ Downloads table with status, progress bar and speed (parsed from ffmpeg)

### Hosts

As of June 2026, most hosts moved their source to **runtime-generated JavaScript** to
defeat HTTP scrapers. Two extraction paths: **direct HTTP** (fast) for hosts that still
expose the source in the HTML, and a **headless Chrome backend** (`worker/headless.rs`, via
`chromiumoxide`) that loads the embed, triggers playback and intercepts the network
manifest for the rest.

| Player | Host | Path | Status (verified end-to-end) |
|--------|------|------|------------------------------|
| `LECTEUR myTV`  | vidmoly          | HTTP     | ✅ HLS `.m3u8` |
| `LECTEUR Stape` | streamtape       | HTTP     | ✅ direct MP4 (`get_video`) |
| `LECTEUR FHD1`  | my.mail.ru       | HTTP     | ✅ **1080p** MP4 (`/+/video/meta/` endpoint + `video_key` cookie) |
| `LECTEUR VOE`   | voe.sx           | Headless | ✅ HLS `.m3u8` intercepted |
| `LECTEUR MOON`  | sb*.org "Byse"   | Headless | ⚠️ stubborn SPA (detection) |
| `LECTEUR SB`    | streamhide       | Headless | ⚠️ same "Byse" family |
| `LECTEUR YU`    | yourupload       | —        | ⚠️ jwplayer (often DMCA'd) |

**4 working players** (myTV, Stape, FHD1, VOE) — comfortable in practice. The ⚠️ "Byse"
hosts resist headless automation; possible next step: **headful mode** (Xvfb to stay
invisible). The mail.ru CDN sometimes returns an I/O error on the first hit, so the
downloader retries once automatically.

## Requirements

- **Rust** (edition 2021) and Cargo
- **ffmpeg** on `PATH` (fetches/remuxes MP4, HLS and DASH)
- **Google Chrome / Chromium** on `PATH` (headless backend for JS hosts: VOE, mail.ru, …).
  Not needed for myTV/Stape, which are pure HTTP.

## Run

```bash
cargo run --release
```

End-to-end diagnostics without the UI (scrape → HTTP + headless extract → ffmpeg).
An optional anime URL lets you test against fresh links (more reliable):

```bash
cargo run --release -- --selftest
cargo run --release -- --selftest "https://voir-anime.to/anime/<slug>/"
```

## Architecture

Two layers: `gui/` (UI) and `worker/` (backend logic), with shared types at the crate root.

| Module | Role |
|--------|------|
| `worker/net.rs`          | Shared HTTP client, browser User-Agent, constants (`BASE`, `UA`) |
| `worker/scraper.rs`      | `fetch_anime` (episodes via `li.wp-manga-chapter`) and `fetch_players` (`thisChapterSources` JSON) |
| `worker/extractors/`     | One module per host (vidmoly, streamtape, mailru); `is_http_extractable()` routes the rest to headless |
| `worker/headless.rs`     | Headless Chrome (chromiumoxide): loads the embed, triggers playback, **intercepts** the network manifest (`.m3u8`/`.mpd`/`.mp4`) |
| `worker/downloader.rs`   | Async-driven ffmpeg, real progress parsed from stderr (`Duration:` / `time=` / `speed=`) |
| `worker/mod.rs`          | tokio runtime + `mpsc` channel; orchestrates `load_anime` / `start_download` without blocking the UI |
| `selection.rs`           | Parses `1-20` / `1,5,8` → list of numbers (unit-tested) |
| `model.rs`               | Domain types (`Anime`, `Episode`, `Player`, `VideoSource`, `DownloadItem`) |
| `gui/`                   | egui UI: `app` (state + loop), `menu`, `view`, `dialogs`, `i18n` (FR/EN) |

### Extraction chain

1. **Anime page** `/anime/{slug}/` → `li.wp-manga-chapter a` → episode list (sorted ascending)
2. **Episode page** → JS variable `thisChapterSources` = `{ "LECTEUR X": "<iframe src=…>", … }` (parsed as JSON)
3. **Host iframe** → dedicated extractor → direct `.mp4` or `.m3u8` playlist
4. **ffmpeg** `-c copy` (+ `-bsf:a aac_adtstoasc` for HLS) → final MP4

### Concurrency / UI

egui is *immediate-mode* (synchronous). All networking/downloading runs on a
**multi-thread tokio runtime**; tasks report their state through an `mpsc` channel that the
UI drains every frame (`WorkerMsg`). No blocking call in the render loop.

## Site notes

- Active domain: **`voir-anime.to`** (the old `v6.voiranime.com` is dead).
- Cloudflare is present but **does not challenge** a Chrome User-Agent → plain HTTP requests
  are enough (no headless browser needed to scrape the site itself).

## Roadmap

- Headful (Xvfb) headless backend to unlock the "Byse" hosts (MOON/SB)
- Quality selector (the `master.m3u8` exposes several resolutions)
- Pause/resume, download queue with a concurrency limit, automatic renaming
