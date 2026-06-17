mod addons;
mod db;
mod foldericon;
mod worker;

use std::collections::{BTreeMap, HashMap};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use addon_api::{Episode, Hoster, Preference, UrlInput, Video};
use addons::{InstalledAddon, StoreEntry, StoreIndex};
use db::{DownloadRecord, GroupRecord};
use rusqlite::Connection;
use serde::{Deserialize, Serialize};
use tauri::async_runtime::JoinHandle;
use tauri::{AppHandle, Emitter, Manager, State};
use tauri_plugin_opener::OpenerExt;

struct Engine {
    http: reqwest::Client,
    tasks: Arc<Mutex<HashMap<u64, JoinHandle<()>>>>,
    pids: Arc<Mutex<HashMap<u64, u32>>>,
}

struct Db(Mutex<Connection>);

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct PersistedState {
    downloads: Vec<DownloadRecord>,
    groups: Vec<GroupRecord>,
}

#[tauri::command]
fn state_load(db: State<'_, Db>) -> Result<PersistedState, String> {
    let conn = db.0.lock().unwrap();
    Ok(PersistedState {
        downloads: db::load_downloads(&conn).map_err(|e| e.to_string())?,
        groups: db::load_groups(&conn).map_err(|e| e.to_string())?,
    })
}

#[tauri::command]
fn download_save(db: State<'_, Db>, record: DownloadRecord) -> Result<(), String> {
    db::upsert_download(&db.0.lock().unwrap(), &record).map_err(|e| e.to_string())
}

#[tauri::command]
fn downloads_delete(db: State<'_, Db>, ids: Vec<u64>) -> Result<(), String> {
    let conn = db.0.lock().unwrap();
    db::delete_downloads(&conn, &ids).map_err(|e| e.to_string())?;
    db::prune_groups(&conn).map_err(|e| e.to_string())
}

#[tauri::command]
fn downloads_clear(db: State<'_, Db>) -> Result<(), String> {
    db::clear_downloads(&db.0.lock().unwrap()).map_err(|e| e.to_string())
}

#[tauri::command]
fn group_save(db: State<'_, Db>, record: GroupRecord) -> Result<(), String> {
    db::upsert_group(&db.0.lock().unwrap(), &record).map_err(|e| e.to_string())
}

#[tauri::command]
fn group_delete(db: State<'_, Db>, id: i64) -> Result<(), String> {
    db::delete_group(&db.0.lock().unwrap(), id).map_err(|e| e.to_string())
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct ProgressEvent {
    id: u64,
    status: String,
    progress: Option<f32>,
    speed: Option<String>,
    size_bytes: Option<u64>,
    address: Option<String>,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
struct FinishedEvent {
    id: u64,
    ok: bool,
    error: Option<String>,
    path: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct AnimeResult {
    title: String,
    url: String,
    poster_url: Option<String>,
    episodes: Vec<Episode>,
}

#[derive(Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
struct Settings {
    #[serde(default)]
    repos: Vec<String>,
    #[serde(default)]
    disabled_repos: Vec<String>,
    #[serde(default)]
    lang: String,
    #[serde(default)]
    folder_icons: bool,
    #[serde(default)]
    folder_template: String,
    #[serde(default)]
    last_dir: String,
    #[serde(default)]
    skip_delete_confirm: bool,
    #[serde(default)]
    theme: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct RepoInfo {
    url: String,
    name: String,
    website: Option<String>,
    icon_url: Option<String>,
    disabled: bool,
}

#[derive(Deserialize)]
struct RepoMetaFile {
    meta: RepoMeta,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct RepoMeta {
    name: Option<String>,
    website: Option<String>,
    icon: Option<String>,
}

fn data_dir(app: &AppHandle) -> Result<PathBuf, String> {
    app.path().app_data_dir().map_err(|e| e.to_string())
}

fn addons_dir(app: &AppHandle) -> Result<PathBuf, String> {
    let base = data_dir(app)?;
    addons::addons_dir(&base).map_err(|e| e.to_string())
}

fn settings_path(app: &AppHandle) -> Result<PathBuf, String> {
    Ok(data_dir(app)?.join("settings.json"))
}

/// Resolve the ffmpeg binary: the copy bundled under `resources/bin/` first
/// (so a packaged app needs no system install), then the system `PATH`.
fn ffmpeg_bin(app: &AppHandle) -> String {
    let name = if cfg!(target_os = "windows") {
        "ffmpeg.exe"
    } else {
        "ffmpeg"
    };
    let bases = [
        app.path()
            .resource_dir()
            .ok()
            .map(|b| b.join("resources").join("bin")),
        Some(
            PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("resources")
                .join("bin"),
        ),
    ];
    for base in bases.into_iter().flatten() {
        let p = base.join(name);
        if p.is_file() {
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let exec = std::fs::metadata(&p)
                    .map(|m| m.permissions().mode() & 0o111 != 0)
                    .unwrap_or(false);
                if !exec {
                    let _ = std::fs::set_permissions(&p, std::fs::Permissions::from_mode(0o755));
                }
            }
            return p.to_string_lossy().into_owned();
        }
    }
    "ffmpeg".to_string()
}

fn read_settings(app: &AppHandle) -> Settings {
    settings_path(app)
        .ok()
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default()
}

fn write_settings(app: &AppHandle, settings: &Settings) -> Result<(), String> {
    let base = data_dir(app)?;
    std::fs::create_dir_all(&base).map_err(|e| e.to_string())?;
    let path = settings_path(app)?;
    std::fs::write(path, serde_json::to_vec_pretty(settings).map_err(|e| e.to_string())?)
        .map_err(|e| e.to_string())
}

#[tauri::command]
fn get_settings(app: AppHandle) -> Settings {
    read_settings(&app)
}

#[tauri::command]
fn set_lang(app: AppHandle, lang: String) -> Result<(), String> {
    let mut settings = read_settings(&app);
    settings.lang = lang;
    write_settings(&app, &settings)
}

#[tauri::command]
fn set_last_dir(app: AppHandle, dir: String) -> Result<(), String> {
    let mut settings = read_settings(&app);
    settings.last_dir = dir;
    write_settings(&app, &settings)
}

#[tauri::command]
fn set_theme(app: AppHandle, theme: String) -> Result<(), String> {
    let mut settings = read_settings(&app);
    settings.theme = theme;
    write_settings(&app, &settings)
}

// Create a destination folder ahead of time (used when scheduling downloads
// that are queued paused, so the folder exists before any download starts).
#[tauri::command]
fn create_dir(path: String) -> Result<(), String> {
    std::fs::create_dir_all(&path).map_err(|e| e.to_string())
}

#[tauri::command]
fn set_skip_delete_confirm(app: AppHandle, skip: bool) -> Result<(), String> {
    let mut settings = read_settings(&app);
    settings.skip_delete_confirm = skip;
    write_settings(&app, &settings)
}

// Delete downloaded files from disk (best-effort, silent on missing files).
// Empty containing folders are cleaned up afterwards.
#[tauri::command]
fn delete_files(paths: Vec<String>) -> Result<(), String> {
    for p in &paths {
        let path = std::path::Path::new(p);
        let _ = std::fs::remove_file(path);
        if let Some(dir) = path.parent() {
            if dir.read_dir().map(|mut d| d.next().is_none()).unwrap_or(false) {
                let _ = std::fs::remove_dir(dir);
            }
        }
    }
    Ok(())
}

// Folder-icon helper files left behind by foldericon.rs, safe to clear when an
// anime's dedicated folder is removed.
fn is_icon_helper(name: &str) -> bool {
    name.starts_with(".folder-icon-")
        || name == ".directory"
        || name == ".folder.png"
        || name == "desktop.ini"
        || name == "Icon\r"
}

// Delete every file of an anime from disk, then remove its folder if only
// icon-helper files remain (protects a shared download directory).
#[tauri::command]
fn delete_anime_files(paths: Vec<String>) -> Result<(), String> {
    let mut dirs: std::collections::BTreeSet<PathBuf> = std::collections::BTreeSet::new();
    for p in &paths {
        let path = std::path::Path::new(p);
        let _ = std::fs::remove_file(path);
        if let Some(dir) = path.parent() {
            dirs.insert(dir.to_path_buf());
        }
    }
    for dir in dirs {
        let Ok(entries) = std::fs::read_dir(&dir) else {
            continue;
        };
        let remaining: Vec<_> = entries.flatten().collect();
        let only_helpers = remaining.iter().all(|e| {
            e.file_name()
                .to_str()
                .map(is_icon_helper)
                .unwrap_or(false)
        });
        if only_helpers {
            for e in &remaining {
                let _ = std::fs::remove_file(e.path());
            }
            let _ = std::fs::remove_dir(&dir);
        }
    }
    Ok(())
}

// Open all of an anime's episodes at once with the default video player, as a
// playlist when possible (Linux). Falls back to opening each file.
#[tauri::command]
fn open_episodes(app: AppHandle, paths: Vec<String>) -> Result<(), String> {
    let files: Vec<String> = paths
        .into_iter()
        .filter(|p| std::path::Path::new(p).is_file())
        .collect();
    if files.is_empty() {
        return Err("file_missing".to_string());
    }
    #[cfg(target_os = "linux")]
    {
        if let Some(def) = linux_default_app(&files[0]) {
            if linux_launch_desktop(&def, &files).is_ok() {
                return Ok(());
            }
        }
    }
    for f in &files {
        let _ = app.opener().open_path(f.clone(), None::<&str>);
    }
    Ok(())
}

// Open the downloaded file with the OS default application. Errors with
// "file_missing" when the file was moved or deleted so the UI can warn.
#[tauri::command]
fn open_file(app: AppHandle, path: String) -> Result<(), String> {
    if !std::path::Path::new(&path).is_file() {
        return Err("file_missing".to_string());
    }
    app.opener()
        .open_path(path, None::<&str>)
        .map_err(|e| e.to_string())
}

// Open the folder containing the file. Errors with "folder_missing" when the
// containing folder was moved, renamed or deleted.
#[tauri::command]
fn open_folder(app: AppHandle, path: String) -> Result<(), String> {
    let dir = std::path::Path::new(&path)
        .parent()
        .ok_or("folder_missing")?
        .to_path_buf();
    if !dir.is_dir() {
        return Err("folder_missing".to_string());
    }
    app.opener()
        .open_path(dir.to_string_lossy().to_string(), None::<&str>)
        .map_err(|e| e.to_string())
}

// An application able to open a file, derived from a freedesktop .desktop entry.
#[derive(Serialize)]
struct AppEntry {
    name: String,
    id: String,
}

// List installed applications that can open the file. Linux only: parses
// .desktop entries and ranks the MIME default first. Returns an empty list on
// other platforms so the UI falls back to the native picker.
#[tauri::command]
fn list_apps(path: String) -> Result<Vec<AppEntry>, String> {
    if !std::path::Path::new(&path).is_file() {
        return Err("file_missing".to_string());
    }
    #[cfg(target_os = "linux")]
    {
        Ok(linux_apps_for(&path))
    }
    #[cfg(not(target_os = "linux"))]
    {
        Ok(Vec::new())
    }
}

// Open the file with a chosen application. `with` is a .desktop path (from
// list_apps) or a plain executable (file picker). Without `with`, shows the
// native picker on Windows; elsewhere asks the caller for an app ("need_app").
#[tauri::command]
fn open_with(path: String, with: Option<String>) -> Result<(), String> {
    if !std::path::Path::new(&path).is_file() {
        return Err("file_missing".to_string());
    }
    if let Some(app) = with {
        #[cfg(target_os = "linux")]
        if app.ends_with(".desktop") {
            return linux_launch_desktop(&app, std::slice::from_ref(&path));
        }
        #[cfg(target_os = "macos")]
        let mut cmd = {
            let mut c = std::process::Command::new("open");
            c.arg("-a").arg(&app).arg(&path);
            c
        };
        #[cfg(not(target_os = "macos"))]
        let mut cmd = {
            let mut c = std::process::Command::new(&app);
            c.arg(&path);
            c
        };
        cmd.spawn().map_err(|e| e.to_string())?;
        return Ok(());
    }
    #[cfg(target_os = "windows")]
    {
        std::process::Command::new("rundll32.exe")
            .arg("shell32.dll,OpenAs_RunDLL")
            .arg(&path)
            .spawn()
            .map_err(|e| e.to_string())?;
        return Ok(());
    }
    #[cfg(not(target_os = "windows"))]
    Err("need_app".to_string())
}

// Directories where freedesktop .desktop entries live, most-specific first.
#[cfg(target_os = "linux")]
fn linux_app_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    let home = std::env::var("HOME").unwrap_or_default();
    let data_home = std::env::var("XDG_DATA_HOME")
        .ok()
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| format!("{home}/.local/share"));
    dirs.push(PathBuf::from(data_home).join("applications"));
    let data_dirs = std::env::var("XDG_DATA_DIRS")
        .ok()
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "/usr/local/share:/usr/share".to_string());
    for d in data_dirs.split(':') {
        dirs.push(PathBuf::from(d).join("applications"));
    }
    dirs
}

// Resolve a .desktop id (e.g. "vlc.desktop") to its absolute path.
#[cfg(target_os = "linux")]
fn linux_desktop_path(id: &str) -> Option<PathBuf> {
    linux_app_dirs()
        .into_iter()
        .map(|d| d.join(id))
        .find(|p| p.is_file())
}

// Read a single key from a .desktop entry's [Desktop Entry] group.
#[cfg(target_os = "linux")]
fn desktop_key(content: &str, key: &str) -> Option<String> {
    let mut in_entry = false;
    for line in content.lines() {
        let line = line.trim();
        if line.starts_with('[') {
            in_entry = line == "[Desktop Entry]";
            continue;
        }
        if in_entry {
            if let Some(v) = line.strip_prefix(key).and_then(|r| r.strip_prefix('=')) {
                return Some(v.trim().to_string());
            }
        }
    }
    None
}

// Build the applications list for a file, MIME default ranked first.
#[cfg(target_os = "linux")]
fn linux_apps_for(path: &str) -> Vec<AppEntry> {
    let mime = std::process::Command::new("xdg-mime")
        .args(["query", "filetype", path])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|s| !s.is_empty());

    let default_id = mime.as_ref().and_then(|m| {
        std::process::Command::new("xdg-mime")
            .args(["query", "default", m])
            .output()
            .ok()
            .filter(|o| o.status.success())
            .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
            .filter(|s| !s.is_empty())
    });

    let mut seen = std::collections::HashSet::new();
    let mut apps: Vec<AppEntry> = Vec::new();
    for dir in linux_app_dirs() {
        let Ok(entries) = std::fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let p = entry.path();
            if p.extension().and_then(|e| e.to_str()) != Some("desktop") {
                continue;
            }
            let id = match p.file_name().and_then(|n| n.to_str()) {
                Some(n) => n.to_string(),
                None => continue,
            };
            if !seen.insert(id.clone()) {
                continue;
            }
            let Ok(content) = std::fs::read_to_string(&p) else {
                continue;
            };
            if desktop_key(&content, "Type").as_deref() != Some("Application") {
                continue;
            }
            if desktop_key(&content, "NoDisplay").as_deref() == Some("true") {
                continue;
            }
            let handles = mime.as_ref().is_some_and(|m| {
                desktop_key(&content, "MimeType")
                    .map(|list| list.split(';').any(|x| x == m))
                    .unwrap_or(false)
            });
            if !handles {
                continue;
            }
            let Some(name) = desktop_key(&content, "Name") else {
                continue;
            };
            apps.push(AppEntry { name, id });
        }
    }

    apps.sort_by(|a, b| a.name.to_lowercase().cmp(&b.name.to_lowercase()));
    if let Some(def) = default_id {
        if let Some(pos) = apps.iter().position(|a| a.id == def) {
            let d = apps.remove(pos);
            apps.insert(0, d);
        }
    }
    apps
}

// The .desktop id of the default application registered for a file's MIME type.
#[cfg(target_os = "linux")]
fn linux_default_app(file: &str) -> Option<String> {
    let mime = std::process::Command::new("xdg-mime")
        .args(["query", "filetype", file])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|s| !s.is_empty())?;
    std::process::Command::new("xdg-mime")
        .args(["query", "default", &mime])
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|s| !s.is_empty())
}

// Launch a .desktop entry with one or more files, expanding Exec field codes.
#[cfg(target_os = "linux")]
fn linux_launch_desktop(desktop: &str, files: &[String]) -> Result<(), String> {
    let resolved = if desktop.ends_with(".desktop") && !desktop.contains('/') {
        linux_desktop_path(desktop).ok_or("app introuvable")?
    } else {
        PathBuf::from(desktop)
    };
    let content = std::fs::read_to_string(&resolved).map_err(|e| e.to_string())?;
    let exec = desktop_key(&content, "Exec").ok_or("Exec manquant")?;

    let mut tokens: Vec<String> = Vec::new();
    let mut had_file = false;
    for tok in exec.split_whitespace() {
        match tok {
            "%f" | "%F" | "%u" | "%U" => {
                tokens.extend(files.iter().cloned());
                had_file = true;
            }
            "%i" | "%c" | "%k" | "%d" | "%D" | "%n" | "%N" | "%v" | "%m" => {}
            other => tokens.push(other.replace("%%", "%")),
        }
    }
    if tokens.is_empty() {
        return Err("Exec vide".to_string());
    }
    if !had_file {
        tokens.extend(files.iter().cloned());
    }
    std::process::Command::new(&tokens[0])
        .args(&tokens[1..])
        .spawn()
        .map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
fn set_folder_icons(app: AppHandle, enabled: bool, template: String) -> Result<(), String> {
    let mut settings = read_settings(&app);
    settings.folder_icons = enabled;
    settings.folder_template = template;
    write_settings(&app, &settings)
}

#[tauri::command]
fn add_repo(app: AppHandle, url: String) -> Result<(), String> {
    let url = url.trim().to_string();
    if url.is_empty() {
        return Err("URL vide".to_string());
    }
    let mut settings = read_settings(&app);
    if !settings.repos.iter().any(|r| r == &url) {
        settings.repos.push(url);
    }
    write_settings(&app, &settings)
}

#[tauri::command]
fn remove_repo(app: AppHandle, url: String) -> Result<(), String> {
    let mut settings = read_settings(&app);
    settings.repos.retain(|r| r != &url);
    settings.disabled_repos.retain(|r| r != &url);
    write_settings(&app, &settings)
}

#[tauri::command]
fn set_repo_disabled(app: AppHandle, url: String, disabled: bool) -> Result<(), String> {
    let mut settings = read_settings(&app);
    settings.disabled_repos.retain(|r| r != &url);
    if disabled {
        settings.disabled_repos.push(url);
    }
    write_settings(&app, &settings)
}

fn url_host(url: &str) -> String {
    url.split("://")
        .nth(1)
        .and_then(|s| s.split('/').next())
        .unwrap_or(url)
        .to_string()
}

fn mime_from_url(url: &str) -> &'static str {
    let u = url.to_lowercase();
    if u.ends_with(".jpg") || u.ends_with(".jpeg") {
        "image/jpeg"
    } else if u.ends_with(".webp") {
        "image/webp"
    } else if u.ends_with(".svg") {
        "image/svg+xml"
    } else {
        "image/png"
    }
}

/// Fetch (and cache to disk) a repo icon, returned as a base64 data URL for offline use.
async fn repo_icon_data(
    http: &reqwest::Client,
    cache_dir: &std::path::Path,
    url: &str,
) -> Option<String> {
    use base64::Engine as _;
    use std::hash::{Hash, Hasher};
    let mut h = std::collections::hash_map::DefaultHasher::new();
    url.hash(&mut h);
    let path = cache_dir.join(format!("{:016x}", h.finish()));
    let bytes = if let Ok(b) = std::fs::read(&path) {
        b
    } else {
        let resp = http.get(url).send().await.ok()?.error_for_status().ok()?;
        let b = resp.bytes().await.ok()?.to_vec();
        let _ = std::fs::create_dir_all(cache_dir);
        let _ = std::fs::write(&path, &b);
        b
    };
    let b64 = base64::engine::general_purpose::STANDARD.encode(&bytes);
    Some(format!("data:{};base64,{b64}", mime_from_url(url)))
}

/// Configured repos with their metadata (name/icon/website from `repo.json`).
/// The icon is cached locally and returned as a data URL (works offline).
#[tauri::command]
async fn list_repos(app: AppHandle, engine: State<'_, Engine>) -> Result<Vec<RepoInfo>, String> {
    let settings = read_settings(&app);
    let cache_dir = data_dir(&app)?.join("repo-icons");
    let mut out = Vec::new();
    for url in &settings.repos {
        let disabled = settings.disabled_repos.iter().any(|r| r == url);
        let meta_url = resolve_asset(url, "repo.json");
        let meta = engine
            .http
            .get(&meta_url)
            .send()
            .await
            .ok()
            .and_then(|r| r.error_for_status().ok());
        let (name, website, icon_remote) = match meta {
            Some(resp) => match resp.text().await.ok().and_then(|t| {
                serde_json::from_str::<RepoMetaFile>(&t).ok()
            }) {
                Some(m) => (
                    m.meta.name.filter(|s| !s.is_empty()).unwrap_or_else(|| url_host(url)),
                    m.meta.website,
                    m.meta.icon.map(|i| resolve_asset(url, &i)),
                ),
                None => (url_host(url), None, None),
            },
            None => (url_host(url), None, None),
        };
        let icon_url = match icon_remote {
            Some(iu) => repo_icon_data(&engine.http, &cache_dir, &iu).await,
            None => None,
        };
        out.push(RepoInfo {
            url: url.clone(),
            name,
            website,
            icon_url,
            disabled,
        });
    }
    Ok(out)
}

/// Resolve a relative store asset (wasm/icon) against the index URL.
fn resolve_asset(index_url: &str, rel: &str) -> String {
    if rel.starts_with("http://") || rel.starts_with("https://") {
        return rel.to_string();
    }
    match index_url.rfind('/') {
        Some(i) => format!("{}/{}", &index_url[..i], rel),
        None => rel.to_string(),
    }
}

async fn fetch_index(http: &reqwest::Client, url: &str) -> Result<StoreIndex, String> {
    let text = http
        .get(url)
        .send()
        .await
        .map_err(|e| e.to_string())?
        .error_for_status()
        .map_err(|e| e.to_string())?
        .text()
        .await
        .map_err(|e| e.to_string())?;
    serde_json::from_str(&text).map_err(|e| format!("index JSON invalide : {e}"))
}

#[tauri::command]
async fn store_fetch(app: AppHandle, engine: State<'_, Engine>) -> Result<Vec<StoreEntry>, String> {
    let settings = read_settings(&app);
    let dir = addons_dir(&app)?;
    let installed: Vec<String> = addons::installed(&dir).into_iter().map(|a| a.id).collect();

    let mut out: Vec<StoreEntry> = Vec::new();
    for url in &settings.repos {
        if settings.disabled_repos.iter().any(|r| r == url) {
            continue;
        }
        let Ok(index) = fetch_index(&engine.http, url).await else {
            continue;
        };
        for mut e in index.addons {
            if out.iter().any(|x| x.id == e.id) {
                continue;
            }
            e.installed = installed.contains(&e.id);
            e.icon_url = e.icon.as_ref().map(|rel| resolve_asset(url, rel));
            e.repo_url = url.clone();
            out.push(e);
        }
    }
    Ok(out)
}

#[tauri::command]
async fn store_install(
    app: AppHandle,
    engine: State<'_, Engine>,
    repo_url: String,
    id: String,
) -> Result<InstalledAddon, String> {
    let url = repo_url;
    let index = fetch_index(&engine.http, &url).await?;
    let entry = index
        .addons
        .into_iter()
        .find(|e| e.id == id)
        .ok_or_else(|| format!("addon `{id}` absent du dépôt"))?;

    let wasm = engine
        .http
        .get(resolve_asset(&url, &entry.wasm))
        .send()
        .await
        .map_err(|e| e.to_string())?
        .error_for_status()
        .map_err(|e| e.to_string())?
        .bytes()
        .await
        .map_err(|e| e.to_string())?;

    let icon = match &entry.icon {
        Some(rel) => engine
            .http
            .get(resolve_asset(&url, rel))
            .send()
            .await
            .ok()
            .and_then(|r| r.error_for_status().ok())
            .map(|r| async move { r.bytes().await.ok() }),
        None => None,
    };
    let icon_bytes = match icon {
        Some(fut) => fut.await,
        None => None,
    };

    let dir = addons_dir(&app)?;
    let meta = addons::install(&dir, &entry, &wasm, icon_bytes.as_deref()).map_err(|e| e.to_string())?;

    let verify_dir = dir.clone();
    let verify_id = entry.id.clone();
    let real_id = tauri::async_runtime::spawn_blocking(move || {
        addons::open(&verify_dir, &verify_id).map(|a| a.metadata.id)
    })
    .await
    .map_err(|e| e.to_string())?;
    match real_id {
        Ok(rid) if rid == entry.id => Ok(meta),
        Ok(rid) => {
            let _ = addons::remove(&dir, &entry.id);
            Err(format!("identité de l'addon incohérente ({rid} ≠ {})", entry.id))
        }
        Err(e) => {
            let _ = addons::remove(&dir, &entry.id);
            Err(format!("addon invalide : {e}"))
        }
    }
}

#[tauri::command]
fn addons_installed(app: AppHandle) -> Result<Vec<InstalledAddon>, String> {
    Ok(addons::installed(&addons_dir(&app)?))
}

#[tauri::command]
fn addon_remove(app: AppHandle, id: String) -> Result<(), String> {
    addons::remove(&addons_dir(&app)?, &id).map_err(|e| e.to_string())
}

#[tauri::command]
async fn addon_preferences(app: AppHandle, id: String) -> Result<Vec<Preference>, String> {
    let dir = addons_dir(&app)?;
    // Loading/instantiating the WASM plugin is slow; keep it off the UI thread.
    tauri::async_runtime::spawn_blocking(move || {
        let mut addon = addons::open(&dir, &id).map_err(|e| e.to_string())?;
        Ok(addon.preferences())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn addon_get_config(app: AppHandle, id: String) -> Result<BTreeMap<String, String>, String> {
    Ok(addons::read_config(&addons_dir(&app)?, &id))
}

#[tauri::command]
fn addon_set_config(
    app: AppHandle,
    id: String,
    config: BTreeMap<String, String>,
) -> Result<(), String> {
    addons::write_config(&addons_dir(&app)?, &id, &config).map_err(|e| e.to_string())
}

#[tauri::command]
async fn load_anime(app: AppHandle, addon_id: String, url: String) -> Result<AnimeResult, String> {
    let dir = addons_dir(&app)?;
    tauri::async_runtime::spawn_blocking(move || {
        let mut addon = addons::open(&dir, &addon_id).map_err(|e| e.to_string())?;
        let anime: addon_api::Anime = addon
            .call_json(addon_api::exports::ANIME_DETAILS, &UrlInput { url: url.clone() })
            .map_err(|e| e.to_string())?;
        let episodes: Vec<Episode> = addon
            .call_json(addon_api::exports::EPISODE_LIST, &UrlInput { url })
            .map_err(|e| e.to_string())?;
        Ok(AnimeResult {
            title: anime.title,
            url: anime.url,
            poster_url: anime.poster_url,
            episodes,
        })
    })
    .await
    .map_err(|e| e.to_string())?
}

/// List the playable hosters the addon extracts for an episode (preferred first).
#[tauri::command]
async fn list_hosters(app: AppHandle, addon_id: String, url: String) -> Result<Vec<Hoster>, String> {
    let dir = addons_dir(&app)?;
    tauri::async_runtime::spawn_blocking(move || {
        let mut addon = addons::open(&dir, &addon_id).map_err(|e| e.to_string())?;
        addon
            .call_json::<_, Vec<Hoster>>(addon_api::exports::HOSTER_LIST, &UrlInput { url })
            .map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn addon_icon(app: AppHandle, id: String) -> Result<Option<String>, String> {
    use base64::Engine as _;
    let dir = addons_dir(&app)?;
    let path = addons::icon_path(&dir, &id);
    match std::fs::read(&path) {
        Ok(bytes) => {
            let b64 = base64::engine::general_purpose::STANDARD.encode(&bytes);
            Ok(Some(format!("data:image/png;base64,{b64}")))
        }
        Err(_) => Ok(None),
    }
}

#[tauri::command]
async fn fetch_image(
    engine: State<'_, Engine>,
    url: String,
    referer: Option<String>,
) -> Result<String, String> {
    use base64::Engine as _;
    let mut req = engine.http.get(&url);
    if let Some(r) = referer {
        req = req.header("Referer", r);
    }
    let resp = req.send().await.map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("HTTP {}", resp.status()));
    }
    let content_type = resp
        .headers()
        .get(reqwest::header::CONTENT_TYPE)
        .and_then(|v| v.to_str().ok())
        .unwrap_or("image/jpeg")
        .to_string();
    let bytes = resp.bytes().await.map_err(|e| e.to_string())?;
    let b64 = base64::engine::general_purpose::STANDARD.encode(&bytes);
    Ok(format!("data:{content_type};base64,{b64}"))
}

#[tauri::command]
fn list_folder_templates() -> Vec<foldericon::TemplateInfo> {
    foldericon::template_list()
}

/// Generate a styled folder icon from the stored anime poster (base64 data URL or
/// raw base64) and apply it to `folder`. No network — the poster lives in the DB.
#[tauri::command]
async fn apply_folder_icon(
    app: AppHandle,
    folder: String,
    poster_data: String,
    template: String,
) -> Result<String, String> {
    use base64::Engine as _;
    let b64 = poster_data.rsplit(',').next().unwrap_or(&poster_data);
    let bytes = base64::engine::general_purpose::STANDARD
        .decode(b64.trim())
        .map_err(|e| format!("affiche illisible : {e}"))?;
    let assets = foldericon::assets_dir(&app)?;
    let cache = data_dir(&app)?.join("icon-cache");
    let folder = PathBuf::from(folder);
    tauri::async_runtime::spawn_blocking(move || {
        foldericon::generate_and_apply(&assets, &cache, &folder, &bytes, &template)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn start_download(
    app: AppHandle,
    engine: State<'_, Engine>,
    addon_id: String,
    id: u64,
    episode_url: String,
    player_name: String,
    out_path: String,
) -> Result<(), String> {
    let dir = addons_dir(&app)?;
    let out = PathBuf::from(&out_path);
    let tasks = engine.tasks.clone();
    let tasks_body = tasks.clone();
    let pids_body = engine.pids.clone();

    let handle = tauri::async_runtime::spawn(async move {
        emit_progress(&app, id, "resolving", None, None, None, None);

        let resolved = resolve_video(dir, addon_id, episode_url, player_name).await;
        let video = match resolved {
            Ok(Some(v)) => v,
            Ok(None) => {
                emit_finished(&app, id, false, Some("aucune source vidéo".into()), None);
                tasks_body.lock().unwrap().remove(&id);
                return;
            }
            Err(e) => {
                emit_finished(&app, id, false, Some(e), None);
                tasks_body.lock().unwrap().remove(&id);
                return;
            }
        };

        emit_progress(&app, id, "downloading", None, None, None, Some(video.url.clone()));

        let app_cb = app.clone();
        let out_cb = out.clone();
        let pids = pids_body.clone();
        let ffmpeg = ffmpeg_bin(&app);
        let result = worker::downloader::download(
            video.url,
            video.headers,
            out.clone(),
            ffmpeg,
            move |p, speed| {
                let size = std::fs::metadata(&out_cb).ok().map(|m| m.len());
                emit_progress(&app_cb, id, "downloading", p, speed, size, None);
            },
            move |pid| match pid {
                Some(p) => {
                    pids.lock().unwrap().insert(id, p);
                }
                None => {
                    pids.lock().unwrap().remove(&id);
                }
            },
        )
        .await;

        match result {
            Ok(()) => {
                let size = std::fs::metadata(&out).ok().map(|m| m.len());
                emit_progress(&app, id, "downloading", Some(1.0), None, size, None);
                emit_finished(&app, id, true, None, Some(out.to_string_lossy().into_owned()));
            }
            Err(e) => emit_finished(&app, id, false, Some(e), None),
        }
        tasks_body.lock().unwrap().remove(&id);
        pids_body.lock().unwrap().remove(&id);
    });

    tasks.lock().unwrap().insert(id, handle);
    Ok(())
}

/// Send a POSIX signal (STOP/CONT/KILL) to a running ffmpeg process.
fn signal_pid(pid: u32, sig: &str) {
    let _ = std::process::Command::new("kill")
        .arg(format!("-{sig}"))
        .arg(pid.to_string())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status();
}

/// Pause: suspend the ffmpeg process so the download freezes in place (resumable).
/// If it hasn't started downloading yet (still resolving), abort the task instead.
#[tauri::command]
fn pause_download(engine: State<'_, Engine>, id: u64) {
    let pid = engine.pids.lock().unwrap().get(&id).copied();
    match pid {
        Some(p) => signal_pid(p, "STOP"),
        None => {
            if let Some(h) = engine.tasks.lock().unwrap().remove(&id) {
                h.abort();
            }
        }
    }
}

#[tauri::command]
fn pause_all(engine: State<'_, Engine>) {
    let pids: Vec<u32> = engine.pids.lock().unwrap().values().copied().collect();
    for p in pids {
        signal_pid(p, "STOP");
    }
}

/// Resume a paused download in place. Returns true if it was paused and continued,
/// false if there is nothing to continue (caller may restart it).
#[tauri::command]
fn resume_download(engine: State<'_, Engine>, id: u64) -> bool {
    let pid = engine.pids.lock().unwrap().get(&id).copied();
    match pid {
        Some(p) => {
            signal_pid(p, "CONT");
            true
        }
        None => false,
    }
}

#[tauri::command]
fn cancel_download(engine: State<'_, Engine>, id: u64) {
    if let Some(h) = engine.tasks.lock().unwrap().remove(&id) {
        h.abort();
    }
    engine.pids.lock().unwrap().remove(&id);
}

#[tauri::command]
fn cancel_all(engine: State<'_, Engine>) {
    for (_, h) in engine.tasks.lock().unwrap().drain() {
        h.abort();
    }
    engine.pids.lock().unwrap().clear();
}

/// Resolve an episode to a downloadable video through the addon (blocking WASM calls).
/// Tries the preferred player first, then falls back through every hoster until one yields a video.
async fn resolve_video(
    dir: PathBuf,
    addon_id: String,
    episode_url: String,
    player_name: String,
) -> Result<Option<Video>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let mut addon = addons::open(&dir, &addon_id).map_err(|e| e.to_string())?;
        let hosters: Vec<Hoster> = addon
            .call_json(addon_api::exports::HOSTER_LIST, &UrlInput { url: episode_url })
            .map_err(|e| e.to_string())?;
        if hosters.is_empty() {
            return Err("aucun lecteur sur cet épisode".to_string());
        }

        let mut order: Vec<usize> = (0..hosters.len()).collect();
        if !player_name.is_empty() {
            if let Some(p) = hosters.iter().position(|h| h.name.eq_ignore_ascii_case(&player_name)) {
                order.retain(|&i| i != p);
                order.insert(0, p);
            }
        }

        for i in order {
            if let Ok(videos) = addon.call_json::<_, Vec<Video>>(
                addon_api::exports::VIDEO_LIST,
                &hosters[i],
            ) {
                if let Some(v) = videos.into_iter().next() {
                    return Ok(Some(v));
                }
            }
        }
        Ok(None)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[allow(clippy::too_many_arguments)]
fn emit_progress(
    app: &AppHandle,
    id: u64,
    status: &str,
    progress: Option<f32>,
    speed: Option<String>,
    size_bytes: Option<u64>,
    address: Option<String>,
) {
    let _ = app.emit(
        "download://progress",
        ProgressEvent {
            id,
            status: status.to_string(),
            progress,
            speed,
            size_bytes,
            address,
        },
    );
}

fn emit_finished(app: &AppHandle, id: u64, ok: bool, error: Option<String>, path: Option<String>) {
    let _ = app.emit(
        "download://finished",
        FinishedEvent {
            id,
            ok,
            error,
            path,
        },
    );
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let http = worker::net::client().expect("HTTP client init");
    let engine = Engine {
        http,
        tasks: Arc::new(Mutex::new(HashMap::new())),
        pids: Arc::new(Mutex::new(HashMap::new())),
    };

    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(engine)
        .setup(|app| {
            let dir = app.path().app_data_dir()?;
            std::fs::create_dir_all(&dir)?;
            let conn = db::open(&dir.join("anime-dm.db"))?;
            app.manage(Db(Mutex::new(conn)));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_settings,
            set_lang,
            set_last_dir,
            set_theme,
            set_skip_delete_confirm,
            create_dir,
            delete_files,
            delete_anime_files,
            open_file,
            open_folder,
            open_with,
            open_episodes,
            list_apps,
            set_folder_icons,
            add_repo,
            remove_repo,
            set_repo_disabled,
            list_repos,
            store_fetch,
            store_install,
            addons_installed,
            addon_remove,
            addon_icon,
            addon_preferences,
            addon_get_config,
            addon_set_config,
            load_anime,
            list_hosters,
            fetch_image,
            list_folder_templates,
            apply_folder_icon,
            start_download,
            pause_download,
            pause_all,
            resume_download,
            cancel_download,
            cancel_all,
            state_load,
            download_save,
            downloads_delete,
            downloads_clear,
            group_save,
            group_delete
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
