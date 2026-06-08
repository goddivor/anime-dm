# anime-dm — instructions projet

**Anime Download Manager** : gestionnaire de téléchargement type IDM pour sites d'animés. 1er site : `voir-anime.to`. Stack : **Tauri 2** (backend Rust) + **React 19 + Vite + TypeScript** (frontend). Voir `README.md` / `CONTRIBUTING.md` — ne pas dupliquer ici.

> Historique : une 1ʳᵉ version GUI en egui a été abandonnée (rendu non natif) → migration Tauri. Le moteur Rust (`worker/`) a été conservé tel quel.

## Lancer / vérifier
- `npm run tauri dev` (app native, fenêtre webview) · `npm run dev` (frontend seul dans le navigateur, sans backend).
- Vérifs : `npm run build` (tsc + vite, **0 erreur**) côté front · `cd src-tauri && cargo build` (**0 warning**) côté Rust.
- **Prérequis** : ffmpeg + Google Chrome dans le PATH ; Node ; libs webview Linux (`libwebkit2gtk-4.1-dev`, `libsoup-3.0-dev`, `libjavascriptcoregtk-4.1-dev`, `librsvg2-dev`, …).

## Architecture (Tauri)
- **`src-tauri/src/`** = backend Rust :
  - `lib.rs` : `Engine` (state managé : http client + cell headless) ; **commands** `load_anime`, `start_download` ; **events** `download://progress` / `download://finished` (forwardés depuis le moteur).
  - `worker/` (agnostique du framework) : `mod` (`resolve_source`, `get_headless`), `net`, `scraper` (+ extraction poster `.summary_image img`), `extractors/` (vidmoly, streamtape, mailru), `headless` (chromiumoxide), `downloader` (ffmpeg, prend un **callback** de progression — plus d'egui).
  - `model.rs` : `Anime`/`Episode` (`Serialize`, camelCase), `Player`, `VideoSource`.
- **`src/`** = frontend React :
  - `App.tsx` (état + events temps réel + actions), `api.ts` (bridge `invoke`/`listen` + `defaultOutPath`), `types.ts`, `i18n.ts` (charge `src/locales/*.json`, `translator(lang)`), `format.ts` (tailles/durées/dates + `parseSelection`).
  - `components/` : `MenuBar`, `Toolbar`, `Sidebar` (arbre Catégories+files, posters via `<img>`), `DownloadsTable` (**TanStack Table** : 11 colonnes redimensionnables/triables, grille CSS), `StatusBar`, `AddDialog`.
- Flux : frontend `invoke("load_anime"/"start_download")` → `lib.rs` spawn la tâche → `worker` émet la progression via callback → `app.emit(...)` → `listen` côté React met à jour les lignes (ETA/ taille calculés côté JS). Posters : `<img src={posterUrl}>` (le webview charge l'URL, pas de fetch Rust).
- Args invoke : camelCase JS → snake_case Rust (conversion auto Tauri 2). Permissions dans `src-tauri/capabilities/default.json`.

## Règles de travail (IMPORTANT)
- **Git (PR-only)** : `master`+`dev` protégés (ruleset GitHub). Repo public `github.com/goddivor/anime-dm`, défaut=`dev`. Pour toute tâche : brancher `feature/* | fix/* | refactor/* | chore/* | docs/*` **depuis `dev`**, commiter en local, puis **S'ARRÊTER et DEMANDER avant `git push`/`gh pr create`**. PR ciblent `dev` ; `dev`→`master` pour release. La migration Tauri est sur `feature/tauri-migration`. Cf. mémoire `anime-dm-git-workflow`.
- **Commits** : conventional commits **en anglais**, impératif ; staging sélectif (jamais `git add .`) ; **aucune** signature `Co-Authored-By`/`Generated with` ; pas de push auto.
- **Pas d'emoji dans le code** : icônes = vraies icônes vectorielles (**lucide-react** côté front). Idem messages CLI : ASCII.
- **Commentaires** : code auto-documenté ; commentaire **uniquement sur une fonction**, court/précis/clair. Pas de `//` inline.
- **i18n** : TOUT texte UI passe par `t("cle")` (`translator(lang)`) + `src/locales/fr.json`/`en.json` (clés hiérarchiques `menu.*`, `dialog.*`, `table.*`, `toolbar.*`, `tooltip.*`, `sidebar.*`, `status.*`, `queue.*`…). **Garder fr/en synchrones**. Exceptions FR légitimes : `"Français"`, noms de lecteurs.
- **Captures d'écran = l'utilisateur**, jamais moi. Je vérifie seulement la compilation.
- **Aucun placeholder / donnée de démo** (liste vide par défaut).

## Faits / pièges à retenir
- **Site** : `voir-anime.to` actif (ancien `v6.voiranime.com` mort). Pas de challenge Cloudflare avec un User-Agent Chrome → HTTP simple suffit.
- **Hôtes vidéo (vérifiés)** : myTV=vidmoly (HLS, HTTP) · Stape=streamtape (MP4, HTTP) · FHD1=mail.ru (MP4 1080p via `/+/video/meta/?ajax_call=1&ext=1` + **cookie `video_key`**, HTTP) · VOE (HLS via **headless**, NE PAS mettre `Referer: voir-anime.to` → `ERR_BLOCKED_BY_CLIENT`). MOON/SB = SPA « Byse » résistent au headless · YU souvent DMCA. Routage : `extractors::is_http_extractable` sinon headless.
- **TODO** : i18n des erreurs worker (`anyhow!` FR, remontées via les events `error`) ; files d'attente = regroupement visuel seulement (pas de planificateur) ; MOON/SB via headful+Xvfb ; choix dossier de sortie dans `AddDialog` (pour l'instant `downloadDir()`).
