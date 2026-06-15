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
    lang: String,
    #[serde(default)]
    folder_icons: bool,
    #[serde(default)]
    folder_template: String,
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
    write_settings(&app, &settings)
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
    let repos = read_settings(&app).repos;
    let dir = addons_dir(&app)?;
    let installed: Vec<String> = addons::installed(&dir).into_iter().map(|a| a.id).collect();

    let mut out: Vec<StoreEntry> = Vec::new();
    for url in &repos {
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
fn addon_preferences(app: AppHandle, id: String) -> Result<Vec<Preference>, String> {
    let dir = addons_dir(&app)?;
    let mut addon = addons::open(&dir, &id).map_err(|e| e.to_string())?;
    Ok(addon.preferences())
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

/// Generate a styled folder icon from the anime poster and apply it to `folder`.
#[tauri::command]
async fn apply_folder_icon(
    app: AppHandle,
    engine: State<'_, Engine>,
    folder: String,
    poster_url: String,
    referer: Option<String>,
    template: String,
) -> Result<String, String> {
    let mut req = engine.http.get(&poster_url);
    if let Some(r) = referer {
        req = req.header("Referer", r);
    }
    let resp = req.send().await.map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("HTTP {}", resp.status()));
    }
    let bytes = resp.bytes().await.map_err(|e| e.to_string())?;
    let assets = foldericon::assets_dir(&app)?;
    let folder = PathBuf::from(folder);
    tauri::async_runtime::spawn_blocking(move || {
        foldericon::generate_and_apply(&assets, &folder, &bytes, &template)
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
        let result = worker::downloader::download(
            video.url,
            video.headers,
            out.clone(),
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
            set_folder_icons,
            add_repo,
            remove_repo,
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
            group_save
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
