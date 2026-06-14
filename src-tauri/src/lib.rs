mod addons;
mod worker;

use std::collections::{BTreeMap, HashMap};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

use addon_api::{Episode, Hoster, Preference, UrlInput, Video};
use addons::{InstalledAddon, StoreEntry, StoreIndex};
use serde::{Deserialize, Serialize};
use tauri::async_runtime::JoinHandle;
use tauri::{AppHandle, Emitter, Manager, State};

struct Engine {
    http: reqwest::Client,
    tasks: Arc<Mutex<HashMap<u64, JoinHandle<()>>>>,
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
    repo_url: String,
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

#[tauri::command]
fn get_settings(app: AppHandle) -> Settings {
    read_settings(&app)
}

#[tauri::command]
fn set_repo_url(app: AppHandle, url: String) -> Result<(), String> {
    let base = data_dir(&app)?;
    std::fs::create_dir_all(&base).map_err(|e| e.to_string())?;
    let settings = Settings { repo_url: url };
    let path = settings_path(&app)?;
    std::fs::write(path, serde_json::to_vec_pretty(&settings).map_err(|e| e.to_string())?)
        .map_err(|e| e.to_string())
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
    let url = read_settings(&app).repo_url;
    if url.is_empty() {
        return Err("aucune URL de dépôt configurée".to_string());
    }
    let dir = addons_dir(&app)?;
    let installed: Vec<String> = addons::installed(&dir).into_iter().map(|a| a.id).collect();
    let index = fetch_index(&engine.http, &url).await?;
    Ok(index
        .addons
        .into_iter()
        .map(|mut e| {
            e.installed = installed.contains(&e.id);
            e
        })
        .collect())
}

#[tauri::command]
async fn store_install(
    app: AppHandle,
    engine: State<'_, Engine>,
    id: String,
) -> Result<InstalledAddon, String> {
    let url = read_settings(&app).repo_url;
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
        let result =
            worker::downloader::download(video.url, video.headers, out.clone(), move |p, speed| {
                let size = std::fs::metadata(&out_cb).ok().map(|m| m.len());
                emit_progress(&app_cb, id, "downloading", p, speed, size, None);
            })
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
    });

    tasks.lock().unwrap().insert(id, handle);
    Ok(())
}

#[tauri::command]
fn stop_download(engine: State<'_, Engine>, id: u64) {
    if let Some(h) = engine.tasks.lock().unwrap().remove(&id) {
        h.abort();
    }
}

#[tauri::command]
fn stop_all(engine: State<'_, Engine>) {
    let mut tasks = engine.tasks.lock().unwrap();
    for (_, h) in tasks.drain() {
        h.abort();
    }
}

/// Resolve an episode to a downloadable video through the addon (blocking WASM calls).
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
        let hoster = hosters
            .iter()
            .find(|h| h.name.eq_ignore_ascii_case(&player_name))
            .or_else(|| hosters.first())
            .ok_or_else(|| "aucun lecteur sur cet épisode".to_string())?
            .clone();
        let videos: Vec<Video> = addon
            .call_json(addon_api::exports::VIDEO_LIST, &hoster)
            .map_err(|e| e.to_string())?;
        Ok(videos.into_iter().next())
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
    };

    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(engine)
        .invoke_handler(tauri::generate_handler![
            get_settings,
            set_repo_url,
            store_fetch,
            store_install,
            addons_installed,
            addon_remove,
            addon_preferences,
            addon_get_config,
            addon_set_config,
            load_anime,
            fetch_image,
            start_download,
            stop_download,
            stop_all
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
