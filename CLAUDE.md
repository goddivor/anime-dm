# anime-dm — instructions projet

**Anime Download Manager** : gestionnaire de téléchargement type IDM pour sites d'animés. Stack : **Tauri 2** (backend Rust) + **React 19 + Vite + TS** (frontend). **Modèle d'addons façon Aniyomi** : toute la logique d'une source (scraping + extraction des lecteurs) vit dans un **module WASM séparé** ; l'app ne contient AUCUN code spécifique à une source. Voir `README.md` / `CONTRIBUTING.md`.

> Historique : v1 GUI egui → migration Tauri → extraction de la logique source dans des addons WASM (Extism). Voir-anime n'est plus dans ce repo.

## Deux repos (côte à côte sous `rust-project/`)
- **`anime-dm`** (ce repo) : l'app (downloader ffmpeg + runtime d'addons + UI + Addon Store). **Zéro logique de source.**
- **`anime-dm-addons`** (`github.com/goddivor/anime-dm-addons`) : les sources en WASM. `addons/<lang>/<nom>/` (1 crate = 1 `.wasm` : `lib.rs` scraping+extractors, `addon.json` métadonnées store, `icon.png`). `scripts/build-repo.sh` assemble `repo/` (index.min.json + wasm + icônes) → publié sur la **branche `repo`**. Index live : `https://raw.githubusercontent.com/goddivor/anime-dm-addons/repo/index.min.json`. Le contrat partagé `addon-api` vit dans **ce** repo (`src-tauri/addon-api`) et est référencé par chemin depuis les addons.

## Lancer / vérifier
- `npm run tauri dev` · `npm run dev` (front seul).
- Vérifs : `npm run build` (**0 erreur**) · `cd src-tauri && cargo build` (**0 warning**).
- Test addon générique (piloté par env, aucune source en dur) : `ADDON_WASM=<path.wasm> [ADDON_ANIME_URL=<url>] cargo test --lib addon_contract_smoke -- --ignored --nocapture`.
- **Prérequis** : ffmpeg dans le PATH ; Node ; libs webview Linux (`libwebkit2gtk-4.1-dev`, `libsoup-3.0-dev`, …). (Chrome/headless n'est PLUS requis — les addons décodent les lecteurs en pur HTTP, façon Aniyomi.)

## Architecture
- **`src-tauri/addon-api/`** : contrat partagé (crate). Modèles serde camelCase : `Metadata`, `Anime`, `Episode`, `AnimesPage`, `Hoster`, `Video{url,quality,headers,subtitles}`, `Preference{key,title,default,kind,options}`. Noms des exports plugin (`exports::*`) et host-fns.
- **`src-tauri/src/`** = backend :
  - `addons.rs` : `Addon` (loader Extism, `load_with_config` injecte la config via `Manifest::with_config_key`, `call_json`, `preferences`) + **registre disque** (`<app_data>/addons/<id>/` = `addon.wasm`+`meta.json`+`icon.png`+`config.json`) : `installed`, `install`, `remove`, `open`, `read/write_config`.
  - `db.rs` : **persistance SQLite** (`rusqlite` bundled, `<app_data>/anime-dm.db`) — tables `downloads` + `anime_groups`. Ouverte dans `.setup`, state managé `Db(Mutex<Connection>)`. Commands `state_load` (au démarrage), `download_save`, `downloads_delete`, `downloads_clear`, `group_save`. Au chargement, les statuts non-terminaux (en cours) sont normalisés en `stopped`. La progression % n'est PAS persistée (éphémère). NB : rusqlite pinné en **0.32** (les versions récentes tirent un `libsqlite3-sys` qui utilise une feature instable `cfg_select`).
  - `lib.rs` : commands **store** (`get_settings`/`set_repo_url`/`store_fetch`/`store_install`), **addons** (`addons_installed`/`addon_remove`/`addon_preferences`/`addon_get/set_config`), **source** (`load_anime(addonId,url)`, `start_download(addonId,…)`, `fetch_image(url,referer?)`). Résolution vidéo via `spawn_blocking` (les appels WASM sont bloquants). Events `download://progress|finished` inchangés.
  - `foldericon.rs` : **icônes de dossier** (style RCFI). Chaque animé → sous-dossier `<dest>/<Titre>/`. Compose l'icône depuis l'affiche via **ImageMagick** (`magick`/`convert`) avec des **recettes en données** (`TEMPLATES` single-pass + `MULTI_TEMPLATES` multi-passes via `{TMP}`, placeholders `{INPUT}`/`{ASSETS}`), puis la **pose** selon l'OS (portage de `seticon`) : Linux `gio metadata::custom-icon` + `.directory` ; Windows `desktop.ini`+`attrib` (.ico) ; macOS `osascript` NSWorkspace. Calques+polices embarqués (`resources/folder-templates/`, repli source en dev). Commands `list_folder_templates`/`apply_folder_icon`. Opt-in (`Settings.folder_icons`/`folder_template`) + override de modèle dans le dialogue d'ajout. 13/15 gabarits portés (Win11 Folderify et Kometa exclus : args conditionnels / dépendants de metadata `.nfo`).
  - `worker/` : seulement `downloader` (ffmpeg, prend `url`+`headers: BTreeMap`) + `net` (UA + client). Plus de scraper/extractors/headless/model.
- **`src/`** = frontend : `App.tsx` (vue `downloads`|`addons`, addons installés, flux routé par `addonId`), `api.ts`, `components/AddonsScreen` (URL dépôt + store + installés + **config par addon**), `AddDialog` (sélecteur de source), `Sidebar`/`DownloadsTable`/`Toolbar`/`MenuBar`/`Poster`.
- **Flux** : Addon Store → installe un `.wasm` ; `AddDialog` choisit une source → `load_anime(addonId,url)` (= `anime_details`+`episode_list` du plugin) → `start_download(addonId,…)` (= `hoster_list`+`video_list` du plugin → `Video{url,headers}` → ffmpeg). La config utilisateur (ex. URL du site) est réinjectée à chaque chargement de plugin.
- **Config par addon** (= « personnalisable depuis l'app ») : l'addon **déclare** ses réglages via l'export `preferences()` ; l'app les stocke (`config.json`) et les réinjecte (config Extism) ; l'addon les lit avec `config::get("base_url")`. Calque exact du `setupPreferenceScreen` d'Aniyomi.

## Règles de travail (IMPORTANT)
- **Git (PR-only)** : `master`+`dev` protégés (ruleset GitHub). Repo public `github.com/goddivor/anime-dm`, défaut=`dev`. Pour toute tâche : brancher `feature/* | fix/* | refactor/* | chore/* | docs/*` **depuis `dev`**, commiter en local, puis **S'ARRÊTER et DEMANDER avant `git push`/`gh pr create`**. PR ciblent `dev` ; `dev`→`master` pour release. La migration Tauri est sur `feature/tauri-migration`. Cf. mémoire `anime-dm-git-workflow`.
- **Commits** : conventional commits **en anglais**, impératif ; staging sélectif (jamais `git add .`) ; **aucune** signature `Co-Authored-By`/`Generated with` ; pas de push auto.
- **Pas d'emoji dans le code** : icônes = vraies icônes vectorielles (**lucide-react** côté front). Idem messages CLI : ASCII.
- **Commentaires** : code auto-documenté ; commentaire **uniquement sur une fonction**, court/précis/clair. Pas de `//` inline.
- **i18n** : TOUT texte UI passe par `t("cle")` (`translator(lang)`) + `src/locales/fr.json`/`en.json` (clés hiérarchiques `menu.*`, `dialog.*`, `table.*`, `toolbar.*`, `tooltip.*`, `sidebar.*`, `status.*`, `queue.*`…). **Garder fr/en synchrones**. Exceptions FR légitimes : `"Français"`, noms de lecteurs.
- **Captures d'écran = l'utilisateur**, jamais moi. Je vérifie seulement la compilation.
- **Aucun placeholder / donnée de démo** (liste vide par défaut).

## Faits / pièges à retenir
- **Installation = Addon Store UNIQUEMENT** : on colle l'URL d'un `index.min.json` dans les réglages, l'app liste/installe. Pas d'autre voie. (Import de fichier `.wasm` local = TODO éventuel.)
- **Aniyomi = PAS de headless** : les lecteurs JS-obfusqués (VOE…) se décodent en pur HTTP dans l'addon (ex. VOE : rot13→base64→shift→reverse→base64→JSON). C'est la réponse à « comment extraire n'importe quel lecteur ». Inspiration : `../../android-project/aniyomi-extension/voiranime/` + extractors `../../StudioProjects/yuzono/aniyomi-extensions/lib/`.
- **Détails source voiranime** (désormais dans l'addon `anime-dm-addons`, PAS dans l'app) : `voir-anime.to` ; poster `.summary_image img` ; lecteurs `thisChapterSources` ; myTV=vidmoly, Stape=streamtape, FHD1=mail.ru (cookie `video_key`), VOE décodé. Hotlink poster : 403 si pas de `Referer` = origine du site → `fetch_image(url, referer)` côté app passe l'origine de la page.
- **Pièges Extism** : `Plugin` non-Send → appels WASM dans `spawn_blocking` ; HTTP du plugin gated par `allowed_hosts` du `Manifest` (actuellement `*`) ; `with_config_key` = la config réinjectée.
- **TODO** : import d'un `.wasm` local ; « lecteur préféré » par addon ; files d'attente = visuel seulement ; choix dossier de sortie (pour l'instant `downloadDir()`) ; signature/vérification des addons du store.
