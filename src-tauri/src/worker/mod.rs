pub mod downloader;
pub mod extractors;
pub mod headless;
pub mod net;
pub mod scraper;

use std::sync::Arc;
use std::time::Duration;

use tokio::sync::Mutex;

use self::headless::Headless;
use crate::model::VideoSource;

pub type HeadlessCell = Arc<Mutex<Option<Arc<Headless>>>>;

pub async fn get_headless(cell: &HeadlessCell) -> Result<Arc<Headless>, String> {
    let mut guard = cell.lock().await;
    if let Some(h) = guard.as_ref() {
        return Ok(h.clone());
    }
    let h = Arc::new(Headless::launch().await.map_err(|e| e.to_string())?);
    *guard = Some(h.clone());
    Ok(h)
}

pub async fn resolve_source(
    http: &reqwest::Client,
    headless: &HeadlessCell,
    episode_url: &str,
    player_name: &str,
) -> Result<VideoSource, String> {
    let players = scraper::fetch_players(http, episode_url)
        .await
        .map_err(|e| e.to_string())?;

    let player = players
        .iter()
        .find(|p| p.name.eq_ignore_ascii_case(player_name))
        .or_else(|| players.first())
        .ok_or_else(|| "aucun lecteur disponible sur cet épisode".to_string())?;

    if extractors::is_http_extractable(&player.iframe_url) {
        extractors::resolve(http, &player.iframe_url)
            .await
            .map_err(|e| e.to_string())?
            .into_iter()
            .next()
            .ok_or_else(|| "aucune source vidéo extraite".to_string())
    } else {
        let hl = get_headless(headless).await?;
        hl.capture(&player.iframe_url, Duration::from_secs(45))
            .await
            .map_err(|e| e.to_string())
    }
}
