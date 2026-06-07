# Anime Download Manager

Gestionnaire de téléchargement type **IDM**, en **Rust**, pour les sites d'animés.
**Premier site pris en charge : `voir-anime.to`** — l'architecture est pensée pour en
accueillir d'autres (chaque site = son module de scraping + ses extracteurs).

Interface graphique (egui) : on colle un lien d'animé, l'app liste les épisodes, on choisit
une plage (`1-20`, `1,5,8`…) et un lecteur, et les téléchargements partent **en parallèle**.

## État actuel (itération 1 — tranche verticale)

✅ Coller une URL d'animé → liste des épisodes (titre + nombre)
✅ Sélection d'épisodes par plage/liste (`1-20`, `1,5,8`, vide = tous)
✅ Choix du lecteur (menu déroulant)
✅ Extraction de la vraie source vidéo et téléchargement **simultané** via ffmpeg
✅ Table de téléchargements avec statut, barre de progression et vitesse (lues sur ffmpeg)

### Hébergeurs

Constaté en juin 2026 : la plupart des hébergeurs ont migré vers des sources **générées
en JavaScript au runtime** pour casser les scrapers HTTP. Deux voies d'extraction :
**HTTP direct** (rapide) pour ceux qui exposent encore la source dans le HTML, et un
**backend Chrome headless** (`headless.rs`, via `chromiumoxide`) qui charge l'embed,
déclenche la lecture et intercepte le manifeste réseau pour tous les autres.

| Lecteur | Hébergeur | Voie | Statut (vérifié bout-en-bout) |
|---------|-----------|------|-------------------------------|
| `LECTEUR myTV`  | vidmoly        | HTTP     | ✅ HLS `.m3u8` |
| `LECTEUR Stape` | streamtape     | HTTP     | ✅ MP4 direct (`get_video`) |
| `LECTEUR FHD1`  | my.mail.ru     | HTTP     | ✅ MP4 **1080p** (endpoint `/+/video/meta/` + cookie `video_key`) |
| `LECTEUR VOE`   | voe.sx         | Headless | ✅ HLS `.m3u8` intercepté |
| `LECTEUR MOON`  | sb*.org « Byse » | Headless | ⚠️ SPA récalcitrante (détection) |
| `LECTEUR SB`    | streamhide     | Headless | ⚠️ même famille « Byse » |
| `LECTEUR YU`    | yourupload     | —        | ⚠️ jwplayer (souvent DMCA) |

**4 lecteurs opérationnels** (myTV, Stape, FHD1, VOE) — confortable en pratique. Les ⚠️
« Byse » résistent à l'automatisation headless ; piste : **mode headful** (Xvfb pour rester
invisible). Le CDN mail.ru renvoyant parfois une I/O error au 1ᵉʳ accès, le downloader
réessaie une fois automatiquement.

## Prérequis

- **Rust** (édition 2021) et Cargo
- **ffmpeg** dans le `PATH` (récupère/remuxe MP4, HLS et DASH)
- **Google Chrome / Chromium** dans le `PATH` (backend headless pour les hébergeurs JS :
  VOE, mail.ru, …). Non requis pour myTV/Stape qui sont en HTTP pur.

## Lancer

```bash
cargo run --release
```

Diagnostic de bout en bout sans interface (scrape → extract HTTP + headless → ffmpeg).
Une URL d'animé optionnelle permet de tester sur des liens récents (plus fiables) :

```bash
cargo run --release -- --selftest
cargo run --release -- --selftest "https://voir-anime.to/anime/<slug>/"
```

## Architecture

Pipeline découplé, un module par responsabilité :

| Module            | Rôle |
|-------------------|------|
| `net.rs`          | Client HTTP partagé, User-Agent navigateur, constantes (`BASE`, `UA`) |
| `scraper.rs`      | `fetch_anime` (liste épisodes via `li.wp-manga-chapter`) et `fetch_players` (JSON `thisChapterSources`) |
| `extractors.rs`   | Extraction HTTP directe (vidmoly, streamtape) ; `is_http_extractable()` route le reste vers le headless |
| `headless.rs`     | Chrome headless (chromiumoxide) : charge l'embed, déclenche la lecture, **intercepte** le manifeste réseau (`.m3u8`/`.mpd`/`.mp4`) |
| `downloader.rs`   | ffmpeg piloté en async, progression réelle lue sur stderr (`Duration:` / `time=` / `speed=`) |
| `selection.rs`    | Parse `1-20` / `1,5,8` → liste de numéros (testé unitairement) |
| `worker.rs`       | Runtime tokio + canal `mpsc` ; orchestre `load_anime` / `start_download` sans bloquer l'UI |
| `model.rs`        | Types du domaine (`Anime`, `Episode`, `Player`, `VideoSource`, `DownloadItem`) |
| `app.rs`          | Interface egui (barre d'outils, table, dialogue d'ajout) |

### Chaîne d'extraction (rappel technique)

1. **Page animé** `/anime/{slug}/` → `li.wp-manga-chapter a` → liste d'épisodes (remise en ordre croissant)
2. **Page épisode** → variable JS `thisChapterSources` = `{ "LECTEUR X": "<iframe src=…>", … }` (parsée en JSON)
3. **iframe hébergeur** → extracteur dédié → `.mp4` direct ou playlist `.m3u8`
4. **ffmpeg** `-c copy -bsf:a aac_adtstoasc` → fichier MP4 final

### Concurrence / UI

egui est *immediate-mode* (synchrone). Tout le réseau/téléchargement tourne sur un runtime
**tokio multi-thread** ; les tâches renvoient leur état par un canal `mpsc` que l'UI draine à
chaque frame (`WorkerMsg`). Aucun appel bloquant dans la boucle de rendu.

## Notes sur le site

- Domaine actif : **`voir-anime.to`** (l'ancien `v6.voiranime.com` est hors service).
- Cloudflare est présent mais **ne challenge pas** avec un User-Agent Chrome → de simples
  requêtes HTTP suffisent (pas de navigateur headless nécessaire pour l'instant).

## Suite envisagée

- Extracteurs supplémentaires (VOE = déchiffrement 6 étapes, Stape, Filemoon, etc.)
- Sélecteur de qualité (le `master.m3u8` expose plusieurs résolutions)
- Pause/reprise, file d'attente avec limite de concurrence, renommage automatique
