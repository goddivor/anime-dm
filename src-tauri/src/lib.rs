mod addons;
mod model;
mod worker;

use std::path::PathBuf;
use std::sync::Arc;

use serde::Serialize;
use tauri::{AppHandle, Emitter, State};
use tokio::sync::Mutex;

use worker::HeadlessCell;

struct Engine {
    http: reqwest::Client,
    headless: HeadlessCell,
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

#[tauri::command]
fn addon_metadata(path: String) -> Result<addon_api::Metadata, String> {
    addons::Addon::load(&path)
        .map(|a| a.metadata)
        .map_err(|e| e.to_string())
}

#[tauri::command]
fn addon_preferences(path: String) -> Result<Vec<addon_api::Preference>, String> {
    let mut addon = addons::Addon::load(&path).map_err(|e| e.to_string())?;
    Ok(addon.preferences())
}

#[tauri::command]
fn addon_episode_list(
    path: String,
    url: String,
    config: Option<std::collections::BTreeMap<String, String>>,
) -> Result<Vec<addon_api::Episode>, String> {
    let mut addon = addons::Addon::load_with_config(&path, &config.unwrap_or_default())
        .map_err(|e| e.to_string())?;
    addon
        .call_json(addon_api::exports::EPISODE_LIST, &addon_api::UrlInput { url })
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn load_anime(url: String, engine: State<'_, Engine>) -> Result<model::Anime, String> {
    let http = engine.http.clone();
    worker::scraper::fetch_anime(&http, &url)
        .await
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn fetch_image(url: String, engine: State<'_, Engine>) -> Result<String, String> {
    use base64::Engine as _;
    let http = engine.http.clone();
    let resp = http
        .get(&url)
        .header("Referer", "https://voir-anime.to/")
        .send()
        .await
        .map_err(|e| e.to_string())?;
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
    id: u64,
    episode_url: String,
    player_name: String,
    out_path: String,
) -> Result<(), String> {
    let http = engine.http.clone();
    let headless = engine.headless.clone();
    let out = PathBuf::from(&out_path);

    tauri::async_runtime::spawn(async move {
        emit_progress(&app, id, "resolving", None, None, None, None);

        let source =
            match worker::resolve_source(&http, &headless, &episode_url, &player_name).await {
                Ok(s) => s,
                Err(e) => {
                    emit_finished(&app, id, false, Some(e), None);
                    return;
                }
            };

        let address = source.url.clone();
        emit_progress(&app, id, "downloading", None, None, None, Some(address));

        let app_cb = app.clone();
        let out_cb = out.clone();
        let result = worker::downloader::download(source, out.clone(), move |progress, speed| {
            let size = std::fs::metadata(&out_cb).ok().map(|m| m.len());
            emit_progress(&app_cb, id, "downloading", progress, speed, size, None);
        })
        .await;

        match result {
            Ok(()) => {
                let size = std::fs::metadata(&out).ok().map(|m| m.len());
                emit_progress(&app, id, "downloading", Some(1.0), None, size, None);
                emit_finished(
                    &app,
                    id,
                    true,
                    None,
                    Some(out.to_string_lossy().into_owned()),
                );
            }
            Err(e) => emit_finished(&app, id, false, Some(e), None),
        }
    });

    Ok(())
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
        headless: Arc::new(Mutex::new(None)),
    };

    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(engine)
        .invoke_handler(tauri::generate_handler![
            addon_metadata,
            addon_preferences,
            addon_episode_list,
            load_anime,
            fetch_image,
            start_download
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
